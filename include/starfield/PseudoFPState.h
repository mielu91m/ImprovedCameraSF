#pragma once

#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"
#include "RE/N/NiPoint.h"
#include "RE/T/TESCamera.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"

namespace Patch {

    // === Global flags ===
    extern bool g_PseudoFPPActive;
    extern bool g_SAFAnimationPlaying;

    // === Head bone tracking ===
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
    extern int g_FrameCount;

    // === Camera state ===
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

    // === ForceCameraToHead cache ===
    extern int g_LastForceFrame;
    extern RE::NiPoint3 g_CachedForcePosition;

    // === Math helpers ===
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset);
    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset);
    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& desiredWorldPos);
    float ClampFloat(float value, float minValue, float maxValue);
    float WrapAnglePi(float angle);
    RE::NiMatrix3 MakeYawMatrix(float yaw);
    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b);
    bool IsFinitePoint(const RE::NiPoint3& point);
    bool NormalizePlanar(RE::NiPoint3& vector);
    bool Normalize3D(RE::NiPoint3& vector);
    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetPlayerWorldAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetPseudoFPAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight);
    void GetNodeAxes(RE::NiAVObject* node, RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    void GetPlayerBodyAxes3D(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b);

    // === Head tracking ===
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam);
    RE::NiAVObject* GetCachedHeadBone();
    void InitEyeHeight();
    void RefreshHeadBone();
    void HideHead(bool a_hide);
    void RestoreCameraOrbit();
    void ResetPseudoFPPState(bool a_restoreOrbit = true);
    void ResetCameraNodesToPlayer();
    void ClearPseudoCameraPointers();
    RE::NiPoint3 GetCurrentHeadAnchorWorldPosition();
    void UpdateHeadAnchorData();
    RE::NiPoint3 GetSAFHeadLocalOffset();
    RE::NiPoint3 BuildSAFPinnedCameraLocalOffset();
    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp);
    bool ComputeSAFCameraModeWorldPosition(RE::PlayerCharacter* player, RE::NiPoint3& outAnchor, RE::NiPoint3& outWorldPos);
    RE::NiPoint3 ComputeSAFHeadAnchorWorldPosition(RE::PlayerCharacter* player);
    RE::NiPoint3 ComputeUltraRigidHeadAnchorWorldPosition(RE::PlayerCharacter* player);
    RE::NiPoint3 ComputeUltraRigidCameraWorldPosition(RE::PlayerCharacter* player, const RE::NiPoint3& headAnchor, RE::NiAVObject* cr = nullptr);
    RE::NiPoint3 ClampWorldPosToPlayerCapsule(RE::PlayerCharacter* player, const RE::NiPoint3& desiredWorldPos, const RE::NiPoint3& headAnchor);
    bool IsReasonableHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos);
    bool IsPlausibleHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos);
    RE::NiPoint3 GetNodeCenter(RE::NiAVObject* node);
    RE::NiAVObject* FindPreferredEyeNode(RE::NiAVObject* root);
    using NodeVisitorFn = void (*)(RE::NiAVObject*);
    void VisitNodesByName(RE::NiAVObject* root, const char* nameSubstr, NodeVisitorFn callback);

    // === Detection ===
    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player);
    bool IsPlayerInShipPilotSeat(RE::PlayerCharacter* player);
    bool IsInShipCameraState(RE::PlayerCamera* camera);
    bool IsInVehicleCameraState(RE::PlayerCamera* camera);
    bool IsInFreeCameraState(RE::PlayerCamera* camera);

    // === Pseudo camera ===
    bool ComputePseudoFPPWorldPosition(RE::TESCamera* tesCam, RE::NiPoint3& outWorldPos,
        RE::NiPoint3* outHeadAnchor = nullptr, bool* outUsingFallback = nullptr);
    bool ApplyPseudoFPPRig(RE::TESCamera* tesCam, void* tpsThis = nullptr);
    void ForceCameraToHead();
    void RestorePseudoRig(RE::NiAVObject* a_this, const char* stage);

    // === Logging ===
    void LogFormatted(const char* fmt, ...);
}
