#include "starfield/PseudoFP.h"
#include "starfield/HeadTracking.h"
#include "starfield/SAFIntegration.h"
#include "systems/PseudoFPConfig.h"
#include "plugin.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiNode.h"
#include "RE/B/BGSKeywordForm.h"
#include "RE/B/BSFixedString.h"
#include "RE/IDs_VTABLE.h"
#include <MinHook.h>
#include <cmath>

namespace Patch {

    void (*origUpdate)(RE::TESCamera*) = nullptr;
    void (*origTPSUpdate)(void*) = nullptr;
    void* g_origFPSUpdate = nullptr;
    void* g_origSetCameraState = nullptr;

    void* g_origNiNodeUWD = nullptr;
    void* g_origNiNodeUTB = nullptr;
    void* g_origNiNodeUT = nullptr;
    bool g_NiNodeHooksInstalled = false;

    void* g_origUpdateWorldData = nullptr;
    void* g_origUpdateTransformAndBounds = nullptr;
    void* g_origUpdateTransforms = nullptr;

    int g_LastForceFrame = -1;
    RE::NiPoint3 g_CachedForcePosition = {};

    bool ApplyPseudoFPPRig(RE::TESCamera* tesCam, void* tpsThis)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (camera && IsInShipCameraState(camera)) {
            return true;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && IsPlayerInShipPilotSeat(player)) {
            return true;
        }
        if (camera && !camera->IsInThirdPerson() && !camera->QCameraEquals(RE::CameraState::kIronSights) && !IsInShipCameraState(camera) && !IsInVehicleCameraState(camera)) {
            auto* tcam = static_cast<RE::TESCamera*>(camera);
            uint32_t currentIdx = 0xFF;
            for (uint32_t i = 0; i < RE::CameraState::kTotal; i++) {
                if (tcam->currentState == camera->cameraStates[i]) {
                    currentIdx = i;
                    break;
                }
            }
            LogFormatted("PFPP_APPLY_NON_TPP: state=%u tpsThis=%p", currentIdx, tpsThis);
        }

        auto* niCam = FindNiCamera(tesCam);
        if (!player || !niCam || !tesCam) return false;
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return false;
        g_CameraRoot = cr;
        g_NiCamera = niCam;

        bool usingFallback = false;
        RE::NiPoint3 headAnchor;
        RE::NiPoint3 worldPos;
        if (!ComputePseudoFPPWorldPosition(tesCam, worldPos, &headAnchor, &usingFallback)) {
            return false;
        }

        constexpr bool kEnableBodyFollowsCameraYaw = true;
        constexpr float kMaxYawTurnPerFrameDeg = 6.0f;
        float camYaw = std::atan2(cr->world.rotate.entry[1][0], cr->world.rotate.entry[0][0]);
        float bodyYaw = player->GetAngleZ();
        const bool isFurniture = IsPlayerUsingFurniture(player) || (camera && camera->QCameraEquals(RE::CameraState::kFurniture));
        g_RestoreRootRotation = false;

        if (camera && IsInVehicleCameraState(camera)) {
            if (!g_FurnitureYawLocked) {
                g_FurnitureBaseYaw = bodyYaw;
                g_FurnitureYawLocked = true;
            }
            player->data.angle.z = g_FurnitureBaseYaw;
        } else if (camera && IsInShipCameraState(camera)) {
            if (!g_FurnitureYawLocked) {
                g_FurnitureBaseYaw = bodyYaw;
                g_FurnitureYawLocked = true;
            }
            player->data.angle.z = g_FurnitureBaseYaw;
            worldPos = cr->world.translate;
            headAnchor = worldPos;
        } else if (isFurniture) {
            if (!g_FurnitureYawLocked) {
                g_FurnitureYawRestored = false;
                // Save the player's original body yaw before sitting,
                // so we can restore it when exiting the furniture.
                g_FurnitureSavedBodyYaw = bodyYaw;
                // Use the furniture's world rotation as the base yaw,
                // so the player sits facing the furniture's direction.
                float furnitureYaw = bodyYaw;
                auto* proc = player->currentProcess;
                if (proc && proc->middleHigh) {
                    auto& furnitureHandle = proc->middleHigh->occupiedFurniture;
                    if (!furnitureHandle) {
                        furnitureHandle = proc->middleHigh->currentFurniture;
                    }
                    if (furnitureHandle) {
                        auto furnitureRef = furnitureHandle.get();
                        if (furnitureRef) {
                            furnitureYaw = furnitureRef->GetAngleZ();
                        }
                    }
                }
                g_FurnitureBaseYaw = furnitureYaw;
                g_FurnitureYawLocked = true;
                g_FurnitureYawRestored = false;
            }
            player->data.angle.z = g_FurnitureBaseYaw;
            g_FurnitureGraceFrames = static_cast<int>(Systems::PseudoFPConfigManager::Get().GetFurnitureExitGraceFrames());
        } else {
            if (g_FurnitureGraceFrames > 0) {
                g_FurnitureGraceFrames--;
                player->data.angle.z = g_FurnitureBaseYaw;
            } else {
                g_FurnitureYawLocked = false;
                // Restore the player's original body yaw only ONCE when
                // exiting furniture, so the player exits facing the same
                // direction they were before sitting.
                if (!g_FurnitureYawRestored) {
                    player->data.angle.z = g_FurnitureSavedBodyYaw;
                    g_FurnitureYawRestored = true;
                }
                if (kEnableBodyFollowsCameraYaw && camera && camera->IsInThirdPerson()) {
                    float diff = WrapAnglePi(camYaw - bodyYaw);
                    float maxYaw = Systems::PseudoFPConfigManager::Get().GetMaxYawRad();
                    if (std::fabs(diff) > maxYaw) {
                        float clamped = ClampFloat(diff, -maxYaw, maxYaw);
                        float correction = diff - clamped;
                        float maxStep = kMaxYawTurnPerFrameDeg * (3.14159265f / 180.0f);
                        float step = ClampFloat(correction, -maxStep, maxStep);
                        if (std::fabs(step) > 0.0001f) {
                            player->data.angle.z = bodyYaw + step;
                        }
                    }
                }
            }
        }

        RE::NiPoint3 rootLocal = ComputeNodeLocalFromWorld(cr, worldPos);
        cr->local.translate = rootLocal;
        cr->previousWorld.translate = worldPos;
        cr->world.translate = worldPos;

        niCam->local.translate = { 0.0f, 0.0f, 0.0f };
        niCam->previousWorld.translate = worldPos;
        niCam->world.translate = worldPos;

        g_PrevRootLocal = rootLocal;
        g_PrevRootWorld = worldPos;
        g_PrevRootLocalRot = cr->local.rotate;
        g_PrevRootWorldRot = cr->world.rotate;
        g_PrevRootPrevWorldRot = cr->previousWorld.rotate;
        g_PrevSetLocal = {};
        g_PrevSetWorld = worldPos;
        g_PrevSetWorldFrame = g_FrameCount;

        if (tpsThis) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)tpsThis + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        g_FrameCount++;
        if ((g_FrameCount % 300) == 0) {
            auto guard = player->loadedData.LockRead();
            auto* loaded = *guard;
            RE::NiPoint3 rootPos = {};
            if (loaded && loaded->data3D.get())
                rootPos = loaded->data3D->world.translate;
            RE::NiPoint3 headW = {};
            RE::NiPoint3 headNode = {};
            if (g_HeadMesh)
                headW = GetHeadMeshCenter(g_HeadMesh);
            if (g_HeadAnchorNode)
                headNode = g_HeadAnchorNode->world.translate;
            LogFormatted("PFPP: frame=%d eyeH=%.2f player=(%.2f,%.2f,%.2f) root=(%.2f,%.2f,%.2f) headMesh=(%.2f,%.2f,%.2f) headNode=(%.2f,%.2f,%.2f) anchor=(%.2f,%.2f,%.2f) cam=(%.2f,%.2f,%.2f) world=(%.2f,%.2f,%.2f) boneValid=%d fallback=%d",
                g_FrameCount, g_EyeHeight,
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(),
                rootPos.x, rootPos.y, rootPos.z,
                headW.x, headW.y, headW.z,
                headNode.x, headNode.y, headNode.z,
                headAnchor.x, headAnchor.y, headAnchor.z,
                cr->world.translate.x, cr->world.translate.y, cr->world.translate.z,
                worldPos.x, worldPos.y, worldPos.z,
                g_HeadBone ? 1 : 0, usingFallback ? 1 : 0);
            LogFormatted("FWDCHECK: cam2player=(%.2f,%.2f,%.2f)",
                player->GetPositionX() - cr->world.translate.x,
                player->GetPositionY() - cr->world.translate.y,
                player->GetPositionZ() - cr->world.translate.z);
        }
        if (usingFallback) {
            LogFormatted("FALLBACK: frame=%d bone=%p player=(%.2f,%.2f,%.2f)",
                g_FrameCount, (void*)g_HeadBone,
                player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
        }
        return true;
    }

    void ForceCameraToHead()
    {
        if (g_FrameCount == g_LastForceFrame) {
            auto* camera = RE::PlayerCamera::GetSingleton();
            auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
            if (tesCam) {
                auto* cr = tesCam->cameraRoot.get();
                auto* niCam = FindNiCamera(tesCam);
                if (cr) {
                    cr->local.translate = ComputeNodeLocalFromWorld(cr, g_CachedForcePosition);
                    cr->previousWorld.translate = g_CachedForcePosition;
                    cr->world.translate = g_CachedForcePosition;
                }
                if (niCam) {
                    niCam->local.translate = {};
                    niCam->previousWorld.translate = g_CachedForcePosition;
                    niCam->world.translate = g_CachedForcePosition;
                }
            }
            return;
        }
        g_LastForceFrame = g_FrameCount;

        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
        if (!tesCam) return;
        auto* cr = tesCam->cameraRoot.get();
        auto* niCam = FindNiCamera(tesCam);
        if (!cr && !niCam) {
            LogFormatted("FCTH: no cr or niCam (cr=%p niCam=%p)", (void*)cr, (void*)niCam);
            return;
        }

        RE::NiPoint3 freshWorld;
        bool usingFallback = false;
        if (!ComputePseudoFPPWorldPosition(tesCam, freshWorld, nullptr, &usingFallback)) {
            LogFormatted("FCTH: ComputePseudoFPPWorldPosition FAILED");
            return;
        }

        g_CachedForcePosition = freshWorld;

        if (cr) {
            cr->local.translate = ComputeNodeLocalFromWorld(cr, freshWorld);
            cr->previousWorld.translate = freshWorld;
            cr->world.translate = freshWorld;
        }
        if (niCam) {
            niCam->local.translate = {};
            niCam->previousWorld.translate = freshWorld;
            niCam->world.translate = freshWorld;
        }
        LogFormatted("FCTH: pos=(%.2f,%.2f,%.2f) cr=%p niCam=%p",
            freshWorld.x, freshWorld.y, freshWorld.z, (void*)cr, (void*)niCam);
    }

    void RestorePseudoRig(RE::NiAVObject* a_this, const char* stage)
    {
        if (!g_PseudoFPPActive) return;

        auto* camera = RE::PlayerCamera::GetSingleton();
        if (camera && camera->IsInFirstPerson() && !g_SAFAnimationPlaying) {
            return;
        }

        static int g_rprCount = 0;
        g_rprCount++;

        auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
        auto* currentNiCamera = tesCam ? FindNiCamera(tesCam) : nullptr;
        auto* currentCameraRoot = tesCam ? tesCam->cameraRoot.get() : nullptr;

        const bool isTrackedObject =
            (a_this == currentNiCamera && currentNiCamera) ||
            (a_this == currentCameraRoot && currentCameraRoot) ||
            (a_this == g_NiCamera && g_NiCamera) ||
            (a_this == g_CameraRoot && g_CameraRoot);

        if (!isTrackedObject) {
            if ((g_rprCount % 300) == 0 && g_SAFAnimationPlaying) {
                LogFormatted("RPR_MISS stage=%s count=%d a_this=%p cr=%p niCam=%p gcr=%p gnc=%p",
                    stage, g_rprCount, (void*)a_this, (void*)currentCameraRoot, (void*)currentNiCamera,
                    (void*)g_CameraRoot, (void*)g_NiCamera);
            }
            return;
        }

        if (currentNiCamera) g_NiCamera = currentNiCamera;
        if (currentCameraRoot) g_CameraRoot = currentCameraRoot;

        if ((g_FrameCount % 120) == 0) LogFormatted("RPR_HIT stage=%s count=%d isTracked=1", stage, g_rprCount);

        ForceCameraToHead();
    }

    void InstallNiNodeHooks()
    {
        if (g_NiNodeHooksInstalled) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        auto* skeletonRoot = loaded->data3D.get();
        auto* niNodeVtab = *reinterpret_cast<void**>(skeletonRoot);
        if (!niNodeVtab) return;
        auto avObjVtabAddr = RE::VTABLE::NiAVObject[0].address();
        auto uwAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 78 * 8);
        auto utbAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 79 * 8);
        auto utAddr = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(niNodeVtab) + 80 * 8);
        auto avUwAddr = *reinterpret_cast<void**>(avObjVtabAddr + 78 * 8);
        if (uwAddr && uwAddr != avUwAddr) {
            MH_CreateHook(uwAddr, (void*)DetourNiNodeUWD, (void**)&g_origNiNodeUWD);
            MH_EnableHook(uwAddr);
            LogFormatted("NiNode::UpdateWorldData hooked at %p (vtable %p)", uwAddr, niNodeVtab);
        } else if (uwAddr) {
            g_origNiNodeUWD = g_origUpdateWorldData;
        }
        auto avUtbAddr = *reinterpret_cast<void**>(avObjVtabAddr + 79 * 8);
        if (utbAddr && utbAddr != avUtbAddr) {
            MH_CreateHook(utbAddr, (void*)DetourNiNodeUTB, (void**)&g_origNiNodeUTB);
            MH_EnableHook(utbAddr);
            LogFormatted("NiNode::UpdateTransformAndBounds hooked at %p (vtable %p)", utbAddr, niNodeVtab);
        } else if (utbAddr) {
            g_origNiNodeUTB = g_origUpdateTransformAndBounds;
        }
        auto avUtAddr = *reinterpret_cast<void**>(avObjVtabAddr + 80 * 8);
        if (utAddr && utAddr != avUtAddr) {
            MH_CreateHook(utAddr, (void*)DetourNiNodeUT, (void**)&g_origNiNodeUT);
            MH_EnableHook(utAddr);
            LogFormatted("NiNode::UpdateTransforms hooked at %p (vtable %p)", utAddr, niNodeVtab);
        } else if (utAddr) {
            g_origNiNodeUT = g_origUpdateTransforms;
        }
        g_NiNodeHooksInstalled = true;
    }

    void DetourUpdate(RE::TESCamera* a_this)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        const bool isPlayerCam = (static_cast<RE::TESCamera*>(camera) == a_this);

        if (isPlayerCam) {
            InstallNiNodeHooks();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                static float g_LastPlayerAngleZ = 0.0f;
                float angleDelta = std::fabs(player->data.angle.z - g_LastPlayerAngleZ);
                g_LastPlayerAngleZ = player->data.angle.z;
                constexpr float kTurnThreshold = 0.0005f;
                constexpr float kTurnMax = 0.02f;
                if (angleDelta > kTurnThreshold) {
                    g_AngleBlendZ = std::min((angleDelta - kTurnThreshold) / (kTurnMax - kTurnThreshold), 1.0f);
                } else {
                    g_AngleBlendZ = 0.0f;
                }
            }
        }

        auto* playerBeforeUpdate = isPlayerCam ? RE::PlayerCharacter::GetSingleton() : nullptr;
        const bool safPlayingBeforeUpdate = playerBeforeUpdate && SAFIntegration::IsSAFAnimationPlaying(playerBeforeUpdate);

        if (isPlayerCam && g_PseudoFPPActive &&
            (camera->IsInThirdPerson() ||
             camera->QCameraEquals(RE::CameraState::kFurniture) ||
             camera->QCameraEquals(RE::CameraState::kIronSights) ||
             IsInVehicleCameraState(camera) ||
             safPlayingBeforeUpdate)) {
            auto* niCam = FindNiCamera(a_this);
            if (niCam) {
                g_NiCamera = niCam;
                g_CameraRoot = a_this->cameraRoot.get();
            }
        }

        origUpdate(a_this);

        if (!isPlayerCam) return;
        if (!g_PseudoFPPActive) return;

        if (!camera->IsInThirdPerson()) {
            const bool isIronSights = camera->QCameraEquals(RE::CameraState::kIronSights);
            const bool isShip = IsInShipCameraState(camera);
            const bool isVehicle = IsInVehicleCameraState(camera);
            if (!isIronSights && !isShip && !isVehicle) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                bool safPlaying = player && SAFIntegration::IsSAFAnimationPlaying(player);
                const bool isFurniture = camera->QCameraEquals(RE::CameraState::kFurniture);
                bool playerUsingFurniture = player ? IsPlayerUsingFurniture(player) : false;
                bool inShipPilotSeat = player ? IsPlayerInShipPilotSeat(player) : false;
                if ((isFurniture || playerUsingFurniture) && !inShipPilotSeat) {
                    auto* niCam = FindNiCamera(a_this);
                    if (niCam) {
                        g_NiCamera = niCam;
                        g_CameraRoot = a_this->cameraRoot.get();
                    }
                    InitEyeHeight();
                    HideHead(true);
                    ApplyPseudoFPPRig(a_this, nullptr);
                    ForceCameraToHead();
                    return;
                }
                if (!safPlaying) {
                    if (!camera->IsInFirstPerson() && !inShipPilotSeat) {
                        auto* niCam = FindNiCamera(a_this);
                        if (niCam) {
                            g_NiCamera = niCam;
                            g_CameraRoot = a_this->cameraRoot.get();
                            InitEyeHeight();
                            HideHead(true);
                            ApplyPseudoFPPRig(a_this, nullptr);
                            ForceCameraToHead();
                        }
                    }
                } else {
                    if (!inShipPilotSeat) {
                        auto* niCam = FindNiCamera(a_this);
                        if (niCam) {
                            g_NiCamera = niCam;
                            g_CameraRoot = a_this->cameraRoot.get();
                        }
                        InitEyeHeight();
                        HideHead(true);
                        ApplyPseudoFPPRig(a_this, nullptr);
                        ForceCameraToHead();
                    }
                }
                return;
            }
        }

        InitEyeHeight();
        if (!g_HasEyeHeight) return;

        auto* playerCam = RE::PlayerCamera::GetSingleton();
        if (playerCam && IsInShipCameraState(playerCam)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                float bodyYaw = player->GetAngleZ();
                if (!g_FurnitureYawLocked) {
                    g_FurnitureBaseYaw = bodyYaw;
                    g_FurnitureYawLocked = true;
                }
                player->data.angle.z = g_FurnitureBaseYaw;
            }
        } else {
            auto* niCam = FindNiCamera(a_this);
            if (niCam) {
                g_NiCamera = niCam;
                float dx = niCam->world.translate.x - g_PrevSetWorld.x;
                float dy = niCam->world.translate.y - g_PrevSetWorld.y;
                float dz = niCam->world.translate.z - g_PrevSetWorld.z;
                float dist = sqrt(dx*dx + dy*dy + dz*dz);
                if (dist > 0.01f) {
                    LogFormatted("TESCam_UPDATE_OVERWRITE: tpsSet=(%.2f,%.2f,%.2f) tesCamNow=(%.2f,%.2f,%.2f) dist=%.3f",
                        g_PrevSetWorld.x, g_PrevSetWorld.y, g_PrevSetWorld.z,
                        niCam->world.translate.x, niCam->world.translate.y, niCam->world.translate.z,
                        dist);
                }
                ApplyPseudoFPPRig(a_this, nullptr);
                ForceCameraToHead();
            }
        }
    }

    void DetourTPSUpdate(void* a_this)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* tesCam = camera ? static_cast<RE::TESCamera*>(camera) : nullptr;
        auto* niCam = tesCam ? FindNiCamera(tesCam) : nullptr;

        if (niCam && g_PseudoFPPActive && camera && (camera->IsInThirdPerson() || camera->QCameraEquals(RE::CameraState::kIronSights) || IsInVehicleCameraState(camera))) {
            g_NiCamera = niCam;
            g_CameraRoot = tesCam->cameraRoot.get();
        }

        if (a_this && g_PseudoFPPActive) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)a_this + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        origTPSUpdate(a_this);

        // During SAF, zero the orbit/zoom accumulator AFTER the original update
        // as well, because the game may have modified it during origTPSUpdate
        // (e.g. from WASD input). This prevents the player from moving the
        // camera while SAF animation is playing.
        if (a_this && g_PseudoFPPActive && g_SAFAnimationPlaying) {
            float* ptr = reinterpret_cast<float*>((uintptr_t)a_this + 0x1A8);
            ptr[0] = 0.0f;
            ptr[1] = 0.0f;
        }

        if (!camera || (!camera->IsInThirdPerson() && !camera->QCameraEquals(RE::CameraState::kIronSights) && !IsInShipCameraState(camera) && !IsInVehicleCameraState(camera))) return;

        if (g_PseudoFPPActive) {
            float dx = niCam->world.translate.x - g_PrevSetWorld.x;
            float dy = niCam->world.translate.y - g_PrevSetWorld.y;
            float dz = niCam->world.translate.z - g_PrevSetWorld.z;
            float dist = sqrt(dx*dx + dy*dy + dz*dz);
            if (dist > 0.01f) {
                LogFormatted("ENGINE_OVERWRITE: prevSet=(%.2f,%.2f,%.2f) engineNow=(%.2f,%.2f,%.2f) dist=%.3f",
                    g_PrevSetWorld.x, g_PrevSetWorld.y, g_PrevSetWorld.z,
                    niCam->world.translate.x, niCam->world.translate.y, niCam->world.translate.z,
                    dist);
            }
        }

        InitEyeHeight();
        if (!g_HasEyeHeight) return;

        if (!g_PseudoFPPActive) {
            HideHead(false);
            RestoreCameraOrbit();
            g_PrevRootLocal = {};
            g_PrevRootWorld = {};
            g_PrevSetLocal = {};
            g_PrevSetWorld = {};
            g_PrevSetWorldFrame = 0;
            return;
        }

        if (camera && IsInShipCameraState(camera)) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                float bodyYaw = player->GetAngleZ();
                if (!g_FurnitureYawLocked) {
                    g_FurnitureBaseYaw = bodyYaw;
                    g_FurnitureYawLocked = true;
                }
                player->data.angle.z = g_FurnitureBaseYaw;
            }
        } else {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && IsPlayerInShipPilotSeat(player)) {
                return;
            }
            HideHead(true);
            ApplyPseudoFPPRig(tesCam, a_this);
            ForceCameraToHead();
        }
    }

    void* DetourUpdateWorldData(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateWorldDataFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateWorldDataFunc)g_origUpdateWorldData)(a_this, a_data);
        RestorePseudoRig(a_this, "UWD_BLOCK");
        return result;
    }

    void* DetourUpdateTransformAndBounds(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformAndBoundsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformAndBoundsFunc)g_origUpdateTransformAndBounds)(a_this, a_data);
        RestorePseudoRig(a_this, "UTB_BLOCK");
        return result;
    }

    void* DetourUpdateTransforms(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformsFunc)g_origUpdateTransforms)(a_this, a_data);
        RestorePseudoRig(a_this, "UT_BLOCK");
        return result;
    }

    void* DetourNiNodeUWD(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateWorldDataFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateWorldDataFunc)g_origNiNodeUWD)(a_this, a_data);
        RestorePseudoRig(a_this, "UWD_NODE");
        return result;
    }

    void* DetourNiNodeUTB(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformAndBoundsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformAndBoundsFunc)g_origNiNodeUTB)(a_this, a_data);
        RestorePseudoRig(a_this, "UTB_NODE");
        return result;
    }

    void* DetourNiNodeUT(RE::NiAVObject* a_this, RE::NiUpdateData* a_data)
    {
        typedef void* (*UpdateTransformsFunc)(RE::NiAVObject*, RE::NiUpdateData*);
        auto result = ((UpdateTransformsFunc)g_origNiNodeUT)(a_this, a_data);
        RestorePseudoRig(a_this, "UT_NODE");
        return result;
    }

    void DetourFPSUpdate(void* a_this, float a_deltaTime)
    {
        if (g_origFPSUpdate) {
            ((void(*)(void*, float))g_origFPSUpdate)(a_this, a_deltaTime);
        }
        if (g_PseudoFPPActive && g_SAFAnimationPlaying) {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (camera && !IsInShipCameraState(camera)) {
                if (camera->IsInFirstPerson() || IsInFreeCameraState(camera)) {
                    camera->ForceThirdPerson();
                }
            }
            ForceCameraToHead();
        }
    }

    void DetourSetCameraState(RE::PlayerCamera* a_this, RE::CameraState a_newState)
    {
        if (g_PseudoFPPActive && g_SAFAnimationPlaying) {
            bool block = false;
            switch (a_newState) {
            case RE::CameraState::kFirstPerson:
            case RE::CameraState::kFreeWalk:
            case RE::CameraState::kFreeAdvanced:
            case RE::CameraState::kFreeFly:
            case RE::CameraState::kFreeTethered:
            case RE::CameraState::kDialogue:
            case RE::CameraState::kPhotoMode:
                block = true;
                break;
            default:
                break;
            }
            if (block) {
                LogFormatted("BLOCKED SetCameraState(%u) during SAF", (uint32_t)a_newState);
                return;
            }
        }
        if (g_origSetCameraState) {
            ((void(*)(RE::PlayerCamera*, RE::CameraState))g_origSetCameraState)(a_this, a_newState);
        }
    }
}
