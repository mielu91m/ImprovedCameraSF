#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PlayerCamera.h"

namespace Patch {

    // Hook function pointers
    extern void (*origUpdate)(RE::TESCamera*);
    extern void (*origTPSUpdate)(void*);
    extern void* g_origFPSUpdate;
    extern void* g_origSetCameraState;

    // NiNode hook pointers
    extern void* g_origNiNodeUWD;
    extern void* g_origNiNodeUTB;
    extern void* g_origNiNodeUT;
    extern bool g_NiNodeHooksInstalled;

    // NiAVObject vtable hook pointers
    extern void* g_origUpdateWorldData;
    extern void* g_origUpdateTransformAndBounds;
    extern void* g_origUpdateTransforms;

    // Force camera cache
    extern int g_LastForceFrame;
    extern RE::NiPoint3 g_CachedForcePosition;

    // Functions
    bool ApplyPseudoFPPRig(RE::TESCamera* tesCam, void* tpsThis = nullptr);
    void ForceCameraToHead();
    void RestorePseudoRig(RE::NiAVObject* a_this, const char* stage);
    void InstallNiNodeHooks();

    // Detour functions
    void DetourUpdate(RE::TESCamera* a_this);
    void DetourTPSUpdate(void* a_this);
    void DetourFPSUpdate(void* a_this, float a_deltaTime);
    void DetourSetCameraState(RE::PlayerCamera* a_this, RE::CameraState a_newState);
    void* DetourUpdateWorldData(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourUpdateTransformAndBounds(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourUpdateTransforms(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUWD(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUTB(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
    void* DetourNiNodeUT(RE::NiAVObject* a_this, RE::NiUpdateData* a_data);
}
