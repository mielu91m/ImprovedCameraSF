#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PlayerCamera.h"

namespace Patch {

    // Head bone globals
    extern RE::NiAVObject* g_HeadBone;
    extern RE::NiAVObject* g_HeadAnchorNode;
    extern RE::NiAVObject* g_HeadMesh;
    extern RE::NiAVObject* g_PrevSkeletonRoot;
    extern RE::NiAVObject* g_HideHeadNode;
    extern bool g_HideHeadNodeValid;
    extern RE::NiPoint3 g_HeadAnchorLocalOffset;
    extern RE::NiPoint3 g_LastValidHeadAnchorWorld;
    extern bool g_HasLastValidHeadAnchorWorld;
    extern RE::NiPoint3 g_LastRawUltraRigidAnchor;
    extern bool g_HasLastRawUltraRigidAnchor;
    extern RE::NiPoint3 g_SAFPinnedCameraLocalOffset;
    extern bool g_HasSAFPinnedCameraLocalOffset;
    extern float g_EyeHeight;
    extern bool g_HasEyeHeight;
    extern bool g_WasHeadAppCulled;
    extern bool g_HasSavedHeadCull;
    extern uint32_t g_HeadAnchorMissCount;

    // Camera globals
    extern RE::NiCamera* g_NiCamera;
    extern RE::NiAVObject* g_CameraRoot;
    extern RE::NiPoint3 g_PrevRootLocal;
    extern RE::NiPoint3 g_PrevRootWorld;
    extern RE::NiMatrix3 g_PrevRootLocalRot;
    extern RE::NiMatrix3 g_PrevRootWorldRot;
    extern RE::NiMatrix3 g_PrevRootPrevWorldRot;
    extern bool g_RestoreRootRotation;
    extern bool g_FurnitureYawLocked;
    extern float g_FurnitureBaseYaw;
    extern float g_FurnitureSavedBodyYaw;
    extern bool g_FurnitureYawRestored;
    extern RE::NiPoint3 g_PrevSetLocal;
    extern RE::NiPoint3 g_PrevSetWorld;
    extern int g_PrevSetWorldFrame;
    extern int g_FurnitureGraceFrames;
    extern float g_AngleBlendZ;
    extern int g_FrameCount;

    // Math helpers
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset);
    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset);
    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& worldPos);
    float ClampFloat(float value, float minValue, float maxValue);
    float WrapAnglePi(float angle);
    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b);
    bool IsFinitePoint(const RE::NiPoint3& point);
    bool NormalizePlanar(RE::NiPoint3& vector);
    bool Normalize3D(RE::NiPoint3& vector);
    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b);
    RE::NiPoint3 GetHeadMeshCenter(RE::NiAVObject* headMesh);
    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);

    // Logging
    void LogFormatted(const char* fmt, ...);

    // Functions
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam);
    RE::NiAVObject* GetCachedHeadBone();

    void InitEyeHeight();
    void RefreshHeadBone();
    void HideHead(bool a_hide);
    void RestoreCameraOrbit();
    void ResetPseudoFPPState(bool a_restoreOrbit = true);
    void ResetCameraNodesToPlayer();
    void ClearPseudoCameraPointers();

    bool ComputePseudoFPPWorldPosition(RE::TESCamera* tesCam, RE::NiPoint3& outWorldPos,
        RE::NiPoint3* outHeadAnchor = nullptr, bool* outUsingFallback = nullptr);

    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player);
    bool IsPlayerInShipPilotSeat(RE::PlayerCharacter* player);
    bool IsInShipCameraState(RE::PlayerCamera* camera);
    bool IsInVehicleCameraState(RE::PlayerCamera* camera);
    bool IsInFreeCameraState(RE::PlayerCamera* camera);
}
