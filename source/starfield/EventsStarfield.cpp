#define _CRT_SECURE_NO_WARNINGS
#include "starfield/EventsStarfield.h"
#include "starfield/Hooks.h"
#include "starfield/Helper.h"
#include "starfield/SAFIntegration.h"
#include "SFSE/API.h"
#include "RE/P/PlayerCamera.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiNode.h"
#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/T/TESForm.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESFile.h"
#include "RE/T/TESObjectARMO.h"
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib")

#pragma push_macro("near")
#pragma push_macro("far")
#undef near
#undef far

namespace Patch {

    static void VLog(const char* fmt, va_list args)
    {
        char* userProfile = nullptr;
        size_t len = 0;
        _dupenv_s(&userProfile, &len, "USERPROFILE");
        std::string path = std::string(userProfile ? userProfile : "") + "\\Documents\\My Games\\Starfield\\SFSE\\ImprovedCameraSF_debug.log";
        free(userProfile);
        std::ofstream log(path, std::ios::app);
        if (log) {
            time_t t = time(nullptr);
            struct tm tm;
            localtime_s(&tm, &t);
            char buf[64];
            strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
            log << "[" << buf << "] ";
            char msg[512];
            vsprintf_s(msg, fmt, args);
            log << msg << std::endl;
        }
    }

    static void LogFormatted(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VLog(fmt, args);
        va_end(args);
    }

    static void Log(const char* msg) { LogFormatted("%s", msg); }

    // Left trigger analog aiming on gamepad (controller 0). GetAsyncKeyState
    // cannot see this - XInput has to be polled separately.
    static bool IsGamepadAiming(int threshold)
    {
        XINPUT_STATE state{};
        if (XInputGetState(0, &state) != ERROR_SUCCESS) {
            return false; // no controller connected
        }
        return state.Gamepad.bLeftTrigger > static_cast<BYTE>(threshold);
    }

    static std::string GetPluginDir()
    {
        char path[MAX_PATH];
        GetModuleFileNameA(GetModuleHandleA("ImprovedCameraSF.dll"), path, MAX_PATH);
        std::string full(path);
        size_t pos = full.find_last_of("\\/");
        return (pos != std::string::npos) ? full.substr(0, pos + 1) : "";
    }

    static float GetINIFloat(const char* section, const char* key, float defaultValue)
    {
        std::string iniPath = GetPluginDir() + "ImprovedCameraSF.ini";
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        return static_cast<float>(atof(buf));
    }

    static int GetINIInt(const char* section, const char* key, int defaultValue)
    {
        std::string iniPath = GetPluginDir() + "ImprovedCameraSF.ini";
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        return atoi(buf);
    }

    static void EnsureINI()
    {
        std::string iniPath = GetPluginDir() + "ImprovedCameraSF.ini";
        if (GetFileAttributesA(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            WritePrivateProfileStringA("Settings", "fTPWorldFOV", "80.0", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "fFPWorldFOV", "85.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetX", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetY", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fOffsetZ", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fFOV", "85.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "bUltraRigidHeadAttach", "false", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fHeadAttachGraceFrames", "45", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUltraRigidMaxPlanarOffset", "0.60", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUltraRigidHeightTolerance", "0.50", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fMaxYawDegrees", "90", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fNoseForward", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fForwardOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fSideOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("PseudoFP", "fUpOffset", "0.0", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iToggleKey", "115", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iADSKey", "2", iniPath.c_str());
            WritePrivateProfileStringA("Settings", "iGamepadTriggerThreshold", "30", iniPath.c_str());
            Log("Created default ImprovedCameraSF.ini");
        }
    }

    static RE::TESBoundObject* LookupHideHeadgearForm(const char* editorID, RE::TESFormID objectIndex)
    {
        auto* form = RE::TESForm::LookupByEditorID<RE::TESObjectARMO>(RE::BSFixedString(editorID));
        if (form) {
            LogFormatted("LookupHideHeadgear: found '%s' via EditorID", editorID);
            return form;
        }
        LogFormatted("LookupHideHeadgear: EditorID '%s' not found, trying FormID scan", editorID);
        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            Log("LookupHideHeadgear: no TESDataHandler");
            return nullptr;
        }

        // One-time diagnostic dump of every currently loaded plugin, so we
        // can confirm from the log whether the expected plugin is actually
        // in the load order at all (this is the #1 cause of both the
        // EditorID lookup and the FormID scan failing at the same time).
        static bool dumpedFiles = false;
        if (!dumpedFiles) {
            dumpedFiles = true;
            LogFormatted("LookupHideHeadgear: dumping %u full-plugin files:", dh->compiledFileCollection.files.size());
            for (std::uint32_t i = 0; i < dh->compiledFileCollection.files.size(); i++) {
                auto* file = dh->compiledFileCollection.files[i];
                if (!file) continue;
                LogFormatted("  full[%u] file '%s' compileIndex=%02X",
                    i, file->fileName, file->compileIndex);
            }
            LogFormatted("LookupHideHeadgear: dumping %u small (light/ESL) files:", dh->compiledFileCollection.smallFiles.size());
            for (std::uint32_t i = 0; i < dh->compiledFileCollection.smallFiles.size(); i++) {
                auto* file = dh->compiledFileCollection.smallFiles[i];
                if (!file) continue;
                LogFormatted("  small[%u] file '%s'", i, file->fileName);
            }
            LogFormatted("LookupHideHeadgear: dumping %u medium files:", dh->compiledFileCollection.mediumFiles.size());
            for (std::uint32_t i = 0; i < dh->compiledFileCollection.mediumFiles.size(); i++) {
                auto* file = dh->compiledFileCollection.mediumFiles[i];
                if (!file) continue;
                LogFormatted("  medium[%u] file '%s'", i, file->fileName);
            }
        }

        // Use the position in the compiled full-plugin array as the
        // compile index (0x00-0xFD). Falling back to file->compileIndex
        // as well in case the array ordering ever doesn't match it.
        auto& fullFiles = dh->compiledFileCollection.files;
        for (std::uint32_t i = 0; i < fullFiles.size(); i++) {
            auto* file = fullFiles[i];
            if (!file) continue;
            RE::TESFormID fid = (static_cast<RE::TESFormID>(i) << 24) | objectIndex;
            auto* f = RE::TESForm::LookupByID<RE::TESObjectARMO>(fid);
            if (f) {
                LogFormatted("LookupHideHeadgear: found via FormID %08X (file '%s', full idx %u, full-plugin scheme)",
                    fid, file->fileName, i);
                return f;
            }
        }

        // Not found as a full plugin. Try the light (ESL) and medium
        // plugin FormID schemes: these use a 12-bit local form ID
        // (objectIndex & 0xFFF) combined with the plugin's position in the
        // respective compiled collection array, prefixed with 0xFE (light)
        // or 0xFD (medium).
        auto& smallFiles = dh->compiledFileCollection.smallFiles;
        for (std::uint32_t i = 0; i < smallFiles.size(); i++) {
            auto* file = smallFiles[i];
            if (!file) continue;
            RE::TESFormID fid = 0xFE000000u | (i << 12) | (objectIndex & 0xFFFu);
            auto* f = RE::TESForm::LookupByID<RE::TESObjectARMO>(fid);
            if (f) {
                LogFormatted("LookupHideHeadgear: found via FormID %08X (file '%s', light idx %u)",
                    fid, file->fileName, i);
                return f;
            }
        }

        auto& mediumFiles = dh->compiledFileCollection.mediumFiles;
        for (std::uint32_t i = 0; i < mediumFiles.size(); i++) {
            auto* file = mediumFiles[i];
            if (!file) continue;
            RE::TESFormID fid = 0xFD000000u | (i << 12) | (objectIndex & 0xFFFu);
            auto* f = RE::TESForm::LookupByID<RE::TESObjectARMO>(fid);
            if (f) {
                LogFormatted("LookupHideHeadgear: found via FormID %08X (file '%s', medium idx %u)",
                    fid, file->fileName, i);
                return f;
            }
        }

        LogFormatted("LookupHideHeadgear: '%s' not found by any method", editorID);
        return nullptr;
    }

    // Checks if the player is wearing an integrated spacesuit that occupies
    // both body (bit 2 = 0x04) and head (bit 0 = 0x01) slots as a single item.
    // Equipping the invisible hide-headgear would conflict and unequip such a suit.
    static bool HasIntegratedSpacesuit(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        bool found = false;
        player->ForEachEquippedItem([&](const RE::BGSInventoryItem& item) {
            auto* armor = item.object ? item.object->As<RE::TESObjectARMO>() : nullptr;
            if (armor) {
                const uint32_t slotMask = armor->bipedModelData.bipedObjectSlots;
                if ((slotMask & 0x05) == 0x05) {
                    found = true;
                    return RE::BSContainer::ForEachResult::kStop;
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return found;
    }

    // Cached form pointers shared between EquipHideHeadgear and UnequipHideHeadgear.
    // Lookup is retried until both forms are found (plugin data may not be ready
    // on the very first call).
    static RE::TESBoundObject* g_HideHead = nullptr;

    static void EnsureHideHeadgearLookup()
    {
        if (!g_HideHead) {
            g_HideHead = LookupHideHeadgearForm("ICSF_Hide_Head", 0x000800);
            LogFormatted("EnsureHideHeadgearLookup: hideHead=%p", (void*)g_HideHead);
        }
    }

    // Returns true if the hide-head item is currently equipped by the player.
    static bool AreHideItemsEquipped(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        EnsureHideHeadgearLookup();
        if (!g_HideHead) return false;
        bool hasHideHead = false;
        player->ForEachEquippedItem([&](const RE::BGSInventoryItem& item) {
            if (item.object == g_HideHead) {
                hasHideHead = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return hasHideHead;
    }

    static void EquipHideHeadgear()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            Log("EquipHideHeadgear: no player");
            return;
        }

        if (HasIntegratedSpacesuit(player)) {
            Log("EquipHideHeadgear: integrated spacesuit detected, skipping");
            return;
        }

        if (AreHideItemsEquipped(player)) {
            Log("EquipHideHeadgear: already equipped, skipping");
            return;
        }

        EnsureHideHeadgearLookup();

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            Log("EquipHideHeadgear: no ActorEquipManager");
            return;
        }

        if (g_HideHead) {
            // The item must actually be in the player's inventory for
            // EquipObject to succeed. Temporarily give it to the player
            // (it gets taken back again in UnequipHideHeadgear), so this
            // works even if the player never picked the item up themselves.
            player->AddObjectToContainer(g_HideHead, RE::BSTSmartPointer<RE::ExtraDataList>(), 1, nullptr, RE::ITEM_TRANSFER_REASON::kAddWornItem);
            RE::BGSObjectInstance inst(g_HideHead, nullptr);
            if (mgr->EquipObject(player, inst, nullptr, false, true, false, false, false)) {
                Log("EquipHideHeadgear: hideHead equipped via EquipObject");
            } else {
                Log("EquipHideHeadgear: EquipObject hideHead returned false");
            }
        } else {
            Log("EquipHideHeadgear: hideHead is null");
        }
    }

    static void UnequipHideHeadgear()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            Log("UnequipHideHeadgear: no player");
            return;
        }

        if (HasIntegratedSpacesuit(player)) {
            Log("UnequipHideHeadgear: integrated spacesuit detected, skipping");
            return;
        }

        EnsureHideHeadgearLookup();

        auto* mgr = RE::ActorEquipManager::GetSingleton();
        if (!mgr) {
            Log("UnequipHideHeadgear: no ActorEquipManager");
            return;
        }

        if (g_HideHead) {
            RE::BGSObjectInstance inst(g_HideHead, nullptr);
            if (mgr->UnequipObject(player, inst, nullptr, false, false, false, false, nullptr)) {
                Log("UnequipHideHeadgear: hideHead unequipped");
            } else {
                Log("UnequipHideHeadgear: UnequipObject hideHead returned false");
            }
            // Take back the copy we temporarily added in EquipHideHeadgear()
            // so it doesn't pile up in the player's inventory.
            std::uint32_t outHandle = 0;
            RE::RemoveItemRequest req;
            req.object = g_HideHead;
            req.count = 1;
            req.reason = RE::ITEM_TRANSFER_REASON::kScriptRemoveItem;
            player->RemoveItem(outHandle, req);
        } else {
            Log("UnequipHideHeadgear: hideHead is null");
        }
    }

    EventsStarfield::EventsStarfield() {}

    bool EventsStarfield::Setup()
    {
        if (m_Setup)
            return true;

        Log("Start");
        EnsureINI();

        float tppFOV = GetINIFloat("Settings", "fTPWorldFOV", 84.0f);
        float fpFOV = GetINIFloat("Settings", "fFPWorldFOV", 85.0f);
        float pseudoFOV = GetINIFloat("PseudoFP", "fFOV", 85.0f);

        auto task = SFSE::GetTaskInterface();
        if (task) {
            task->AddPermanentTask([tppFOV, fpFOV, pseudoFOV]() {
                auto camera = RE::PlayerCamera::GetSingleton();
                if (!camera) return;

                // === SAF integration ===
                // If SAF is playing an animation on the player while in a
                // non-TPP state (kFurniture), we keep pseudo active so the
                // camera stays at the player's head. This requires SAF to
                // export SAFAPI_IsPlayingAnimation.
                auto* player = RE::PlayerCharacter::GetSingleton();
                bool safPlayingRaw = false;
                static int safLogThrottle = 0;
                if (player) {
                    safPlayingRaw = SAFIntegration::IsSAFAnimationPlaying(player);
                }

                // Debounce: SAFAPI_IsPlayingAnimation can momentarily report
                // false for a frame or two during SAF's own internal pose
                // transitions (e.g. loop restarts, pose-to-pose blends).
                // A raw level-triggered read of that flag causes the SAF
                // guard in DetourSetCameraState to open for exactly those
                // frames, letting the engine slip into a Free* camera
                // state. That slip does NOT self-correct once it happens -
                // ForceCameraToHead() fighting the position every frame
                // afterward is what produces the "ghost you can nudge
                // before it snaps back" effect reported around animation
                // loop restarts. Require a short streak of consecutive
                // false reads before we actually drop the SAF flag.
                static int safFalseStreak = 0;
                constexpr int kSAFDropGraceFrames = 8;
                if (safPlayingRaw) {
                    safFalseStreak = 0;
                } else {
                    safFalseStreak++;
                }
                bool safPlaying = safPlayingRaw ||
                    (Patch::g_SAFAnimationPlaying && safFalseStreak <= kSAFDropGraceFrames);

                Patch::g_SAFAnimationPlaying = safPlaying;
                safLogThrottle++;
                if (safPlaying && (safLogThrottle % 60) == 0) {
                    LogFormatted("PseudoFP: SAF animation detected on player");
                }
                if (!safPlayingRaw && safFalseStreak > 0 && safFalseStreak <= kSAFDropGraceFrames) {
                    LogFormatted("PseudoFP: SAF raw=false, riding out grace frame %d/%d", safFalseStreak, kSAFDropGraceFrames);
                }

                // g_PseudoUserEnabled: user wants pseudo (set by F4)
                // g_PseudoFPPActive:  pseudo is actually running this frame
                static bool g_PseudoUserEnabled = false;

                // --- F4 toggle ---
    // Read toggle key from INI (default VK_F4 = 115).
    // Supports VK codes (e.g., 115=F4, 112=F1, 36=Home, 35=End, 33=PageUp, 34=PageDown)
    // or ASCII letter codes (65=A, 66=B, etc.).
    static int g_ToggleKey = 0;
    static int g_ToggleKeyCheckFrames = 0;
    g_ToggleKeyCheckFrames++;
    if (g_ToggleKey == 0 || (g_ToggleKeyCheckFrames % 120) == 0) {
        g_ToggleKey = static_cast<int>(GetINIInt("Settings", "iToggleKey", 115));
        if (g_ToggleKey < 1 || g_ToggleKey > 255) {
            g_ToggleKey = 115; // VK_F4
        }
    }

    static bool wasToggleDown = false;
    bool isToggleDown = (GetAsyncKeyState(g_ToggleKey) & 0x8000) != 0;
    if (isToggleDown && !wasToggleDown) {
        g_PseudoUserEnabled = !g_PseudoUserEnabled;
        LogFormatted("PseudoFP: user %s by key VK=%d", g_PseudoUserEnabled ? "ENABLED" : "DISABLED", g_ToggleKey);
        if (g_PseudoUserEnabled) {
            if (!IsInVehicleCameraState(camera)) {
                camera->ForceThirdPerson();
            }
            EquipHideHeadgear();
        } else {
            if (g_PseudoFPPActive) {
                g_PseudoFPPActive = false;
                ResetPseudoFPPState();
                UnequipHideHeadgear();
            }
        }
    }
    wasToggleDown = isToggleDown;

    // --- ADS key (default VK_RBUTTON = 2) ---
    // We poll the physical button instead of relying solely on the engine's
    // kIronSights camera state, because aiming while the game is in
    // third-person (which is what our pseudo camera actually is under the
    // hood) generally does NOT change CameraState to kIronSights - the
    // engine just zooms while staying in kThirdPerson. That meant the old
    // isIronSightsNow-only check never fired when aiming from pseudo.
    static int g_ADSKey = 0;
    static int g_ADSKeyCheckFrames = 0;
    g_ADSKeyCheckFrames++;
    if (g_ADSKey == 0 || (g_ADSKeyCheckFrames % 120) == 0) {
        g_ADSKey = static_cast<int>(GetINIInt("Settings", "iADSKey", 2));
        if (g_ADSKey < 1 || g_ADSKey > 255) {
            g_ADSKey = 2; // VK_RBUTTON
        }
    }
    const bool isADSKeyDown = (GetAsyncKeyState(g_ADSKey) & 0x8000) != 0;

    // --- Gamepad left trigger (default threshold 30/255) ---
    static int g_GamepadThreshold = -1;
    static int g_GamepadThresholdCheckFrames = 0;
    g_GamepadThresholdCheckFrames++;
    if (g_GamepadThreshold < 0 || (g_GamepadThresholdCheckFrames % 120) == 0) {
        g_GamepadThreshold = GetINIInt("Settings", "iGamepadTriggerThreshold", 30);
        if (g_GamepadThreshold < 0 || g_GamepadThreshold > 255) {
            g_GamepadThreshold = 30;
        }
    }
    const bool isGamepadAimingNow = IsGamepadAiming(g_GamepadThreshold);

                // --- Determine state ---
                const bool isTPP = camera->IsInThirdPerson();
                const bool isIronSightsNow = camera->QCameraEquals(RE::CameraState::kIronSights);
                // Ship = either the engine's own Flight/Ship* camera states
                // (only entered once actually flying/undocked), OR the
                // player is sitting in the ship's pilot seat furniture -
                // which happens FIRST, before takeoff. Sitting down is a
                // furniture interaction like any other chair, so it must be
                // checked separately from kFurniture: only THIS specific
                // seat should count as "ship", not furniture in general.
                //
                // NOTE: an earlier attempt also treated camera idx==6
                // (kUnk06, unlabeled in the SDK) as a ship signal, on the
                // observation that it co-occurred with ship seating in two
                // logs. That was wrong - it turned out to be a generic
                // transition state that also fires for ordinary furniture
                // sit/stand, which incorrectly kicked pseudo out to TPP for
                // any chair, not just the ship. Removed; IsPlayerInShipPilotSeat
                // (FormID-based, see HeadTracking.cpp) is now the sole signal
                // for "sitting, not yet flying".
                const bool isShip = IsInShipCameraState(camera) || (player && IsPlayerInShipPilotSeat(player));
                const bool isVehicle = IsInVehicleCameraState(camera);
                const bool isFPP = camera->IsInFirstPerson();
                const bool isFurniture = camera->QCameraEquals(RE::CameraState::kFurniture);
                // Aiming = real kIronSights state OR the ADS button/trigger
                // is physically held down (covers TPP/pseudo aiming, which
                // doesn't flip CameraState on its own, for both mouse and pad).
                const bool isAimingRaw = isIronSightsNow || ((isADSKeyDown || isGamepadAimingNow) && (isTPP || isFPP));
                // Hard-block aiming detection while SAF plays. If the engine
                // reports kIronSights (or the ADS button/trigger happens to
                // read as held) during a scripted SAF pose, isAimingNow used
                // to still go true and the ADS handler below would call
                // camera->ForceFirstPerson() - i.e. OUR OWN CODE actively
                // switching from the pseudo camera to the engine's real FPP
                // camera, which looks exactly like "the camera escapes the
                // moment SAF starts" from the outside. SAF sessions must
                // never be interpreted as aiming, full stop.
                if (isAimingRaw && safPlaying) {
                    LogFormatted("PseudoFP: aiming input detected DURING SAF (ironSights=%d adsKey=%d gamepad=%d) - suppressed, would have called ForceFirstPerson",
                        isIronSightsNow ? 1 : 0, isADSKeyDown ? 1 : 0, isGamepadAimingNow ? 1 : 0);
                }
                const bool isAimingNow = isAimingRaw && !safPlaying;
                const bool isBodyState = isTPP || isFPP || isFurniture || isAimingNow || isShip || isVehicle;

                // Track aiming transitions for ADS-in-FPP
                static bool g_WasAiming = false;

                bool shouldPseudo = false;
                if (g_PseudoUserEnabled) {
                    // Pseudo is ONLY toggled by the hotkey (F4). Camera state
                    // transitions (Free*, furniture, etc.) must NOT disable
                    // it — otherwise SAF animation startup briefly changes
                    // state before reporting the animation, killing pseudo.
                    // The only exceptions are aiming (ADS) and piloting a
                    // ship - both temporarily switch to a real engine
                    // camera. When the user releases the trigger / exits
                    // the ship, pseudo resumes automatically.
                    shouldPseudo = !isAimingNow && !isShip;
                    // Force FPP back to TPP - including during SAF. This
                    // used to skip while safPlaying was true (to avoid
                    // fighting a brief FPP flash right at SAF startup), but
                    // that let the engine settle into and STAY in real FPP
                    // for the entire SAF session once it flipped (confirmed
                    // by CAM_STATE logs: idx=0 TPP=0 FP=1 saf=1 persisting
                    // for the whole animation). While in real engine FPP,
                    // the player's normal camera-cycle input (TPP
                    // near/far/FPP) is processed by the real camera system
                    // instead of being masked by the pseudo rig - exactly
                    // "the pseudo camera leaves the head and lets you
                    // switch between the game's own cameras". Pseudo must
                    // mean pseudo, full stop, any time it's not a genuine
                    // aim or ship flight.
                    if (isFPP && !g_WasAiming && !isAimingNow && !isShip) {
                        camera->ForceThirdPerson();
                    }
                    if (safPlaying && Patch::IsInFreeCameraState(camera)) {
                        camera->ForceThirdPerson();
                    }
                }

                // ADS: when pseudo is active and player aims (PPM held), switch to real FPP
                // (isAimingNow is already forced false while safPlaying, so this
                // branch cannot fire during a SAF animation.)
                if (g_PseudoUserEnabled && isAimingNow) {
                    if (!isFPP) {
                        camera->ForceFirstPerson();
                        LogFormatted("PseudoFP: ADS -> FPP");
                    }
                } else if (g_PseudoUserEnabled && g_WasAiming && !isAimingNow) {
                    if (isFPP) {
                        camera->ForceThirdPerson();
                        LogFormatted("PseudoFP: ADS end -> TPP (restore pseudo)");
                    }
                }
                g_WasAiming = isAimingNow;

                // --- Active recovery from a Free* camera slip ---
                // Belt-and-braces: if SAF is (debounced) playing but the
                // engine is already sitting in a Free* camera state -
                // whether from a slip that predates the debounce above,
                // or a state change that didn't go through
                // DetourSetCameraState at all - force it straight back to
                // third person instead of only correcting world position
                // every frame. This is what actually returns input control
                // to the pseudo rig rather than leaving a movable "ghost".
                if (g_PseudoUserEnabled && safPlaying && Patch::IsInFreeCameraState(camera)) {
                    camera->ForceThirdPerson();
                    LogFormatted("PseudoFP: recovered from Free camera state during SAF (idx check)");
                }

                // --- Apply or disable pseudo ---
                if (shouldPseudo) {
                    if (!g_PseudoFPPActive) {
                        g_PseudoFPPActive = true;
                        LogFormatted("PseudoFP: activated (TPP or SAF)");
                    }
                    // Apply rig for both TPP and SAF non-TPP states
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    ApplyPseudoFPPRig(tesCam, nullptr);
                } else {
                    // Disable delay: when shouldPseudo becomes false (e.g.
                    // a brief camera-state transition during SAF startup
                    // before IsSAFAnimationPlaying reports true) but the
                    // user did NOT explicitly aim, wait up to 15 frames
                    // before actually disabling pseudo.  During ADS
                    // (isAimingNow) the user intentionally wants real FPP,
                    // so skip the delay and disable immediately.
                    static int g_DisableDelayFrames = 0;
                    constexpr int kDisableDelayMax = 15;
                    if (g_PseudoUserEnabled && g_PseudoFPPActive && !isAimingNow && !isShip && g_DisableDelayFrames < kDisableDelayMax) {
                        g_DisableDelayFrames++;
                        LogFormatted("PseudoFP: disable deferred %d/%d", g_DisableDelayFrames, kDisableDelayMax);
                    } else {
                        g_DisableDelayFrames = 0;
                        if (g_PseudoFPPActive) {
                            g_PseudoFPPActive = false;
                            ClearPseudoCameraPointers();
                            LogFormatted(isShip ? "PseudoFP: disabled (entered ship)" : "PseudoFP: disabled (non-TPP without SAF)");
                        }
                        if (!isBodyState) {
                            // Skip TPP-specific logic below
                            return;
                        }
                    }
                }

                // --- Log camera state ---
                static int stateLogCounter = 0;
                stateLogCounter++;
                if ((stateLogCounter % 60) == 0) {
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    uint32_t stateIdx = 0xFF;
                    for (uint32_t i = 0; i < RE::CameraState::kTotal; i++) {
                        if (tesCam->currentState == camera->cameraStates[i]) {
                            stateIdx = i;
                            break;
                        }
                    }
                    LogFormatted("CAM_STATE: idx=%u TPP=%d FP=%d pseudo=%d user=%d saf=%d",
                        stateIdx, isTPP ? 1 : 0, isFPP ? 1 : 0,
                        g_PseudoFPPActive ? 1 : 0, g_PseudoUserEnabled ? 1 : 0,
                        safPlaying ? 1 : 0);
                }

                // --- FOV switching ---
                static int lastState = -1;
                int state = isTPP ? 1 : 0;
                if (state != lastState && !g_PseudoFPPActive) {
                    lastState = state;
                    auto prefs = RE::INIPrefSettingCollection::GetSingleton();
                    if (!prefs) return;
                    if (isTPP) {
                        prefs->SetSetting("fTPWorldFOV:Camera", tppFOV);
                        LogFormatted("TPP FOV set to %.1f", tppFOV);
                    } else {
                        prefs->SetSetting("fFPWorldFOV:Camera", fpFOV);
                        LogFormatted("FP FOV set to %.1f", fpFOV);
                    }
                }

                // Pseudo-FPP FOV
                static float lastPseudoFOV = -1.0f;
                if (g_PseudoFPPActive && std::fabs(pseudoFOV - lastPseudoFOV) > 0.01f) {
                    lastPseudoFOV = pseudoFOV;
                    auto prefs = RE::INIPrefSettingCollection::GetSingleton();
                    if (prefs) {
                        prefs->SetSetting("fTPWorldFOV:Camera", pseudoFOV);
                        LogFormatted("PseudoFP FOV set to %.1f", pseudoFOV);
                    }
                } else if (!g_PseudoFPPActive) {
                    lastPseudoFOV = -1.0f;
                }

                // Aggressive camera pin: force head position at the VERY END
                // of the frame, after all mods including SAF have run.
                if (g_PseudoFPPActive) {
                    auto* tesCam = static_cast<RE::TESCamera*>(camera);
                    ApplyPseudoFPPRig(tesCam, nullptr);
                }

            });
            Log("Task registered");
        }

        m_Setup = true;
        Log("Setup done");
        return true;
    }

    bool EventsStarfield::Remove()
    {
        m_Setup = false;
        return true;
    }

}

#pragma pop_macro("near")
#pragma pop_macro("far")