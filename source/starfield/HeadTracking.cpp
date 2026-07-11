#include "starfield/PseudoFPState.h"
#include "starfield/SAFIntegration.h"
#include "systems/PseudoFPConfig.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BSFixedString.h"
#include "RE/A/AIProcess.h"
#include "RE/T/TESObjectREFR.h"
#include <cmath>
#include <cstring>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <algorithm>
#include <Windows.h>

namespace Patch {

    // === Global variable definitions ===
    bool g_PseudoFPPActive = false;
    bool g_SAFAnimationPlaying = false;

    RE::NiAVObject* g_HeadBone = nullptr;
    RE::NiAVObject* g_HeadAnchorNode = nullptr;
    RE::NiAVObject* g_HeadMesh = nullptr;
    RE::NiAVObject* g_PrevSkeletonRoot = nullptr;
    RE::NiAVObject* g_HideHeadNode = nullptr;
    bool g_HideHeadNodeValid = false;
    RE::NiPoint3 g_HeadAnchorLocalOffset = {};
    RE::NiPoint3 g_LastValidHeadAnchorWorld = {};
    bool g_HasLastValidHeadAnchorWorld = false;
    RE::NiPoint3 g_LastRawUltraRigidAnchor = {};
    bool g_HasLastRawUltraRigidAnchor = false;
    RE::NiPoint3 g_SAFPinnedCameraLocalOffset = {};
    bool g_HasSAFPinnedCameraLocalOffset = false;
    float g_EyeHeight = 1.4f;
    bool g_HasEyeHeight = false;
    bool g_WasHeadAppCulled = false;
    bool g_HasSavedHeadCull = false;
    uint32_t g_HeadAnchorMissCount = 0;
    int g_FrameCount = 0;

    RE::NiCamera* g_NiCamera = nullptr;
    RE::NiAVObject* g_CameraRoot = nullptr;
    RE::NiPoint3 g_PrevRootLocal = {};
    RE::NiPoint3 g_PrevRootWorld = {};
    RE::NiMatrix3 g_PrevRootLocalRot = {};
    RE::NiMatrix3 g_PrevRootWorldRot = {};
    RE::NiMatrix3 g_PrevRootPrevWorldRot = {};
    bool g_RestoreRootRotation = false;
    bool g_FurnitureYawLocked = false;
    float g_FurnitureBaseYaw = 0.0f;
    float g_FurnitureSavedBodyYaw = 0.0f;
    bool g_FurnitureYawRestored = false;
    RE::NiPoint3 g_SAFSmoothedHeadPos = {};
    bool g_HasSAFSmoothedHeadPos = false;
    RE::NiPoint3 g_PrevSetLocal = {};
    RE::NiPoint3 g_PrevSetWorld = {};
    int g_PrevSetWorldFrame = 0;
    int g_FurnitureGraceFrames = 0;
    float g_AngleBlendZ = 0.0f;

    // === Logging ===
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

    void LogFormatted(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        VLog(fmt, args);
        va_end(args);
    }

    // === Math helpers ===
    RE::NiPoint3 TransformLocalToWorld(const RE::NiMatrix3& rotation, const RE::NiPoint3& localOffset)
    {
        return {
            rotation.entry[0][0] * localOffset.x + rotation.entry[0][1] * localOffset.y + rotation.entry[0][2] * localOffset.z,
            rotation.entry[1][0] * localOffset.x + rotation.entry[1][1] * localOffset.y + rotation.entry[1][2] * localOffset.z,
            rotation.entry[2][0] * localOffset.x + rotation.entry[2][1] * localOffset.y + rotation.entry[2][2] * localOffset.z
        };
    }

    RE::NiPoint3 TransformWorldToLocal(const RE::NiMatrix3& rotation, const RE::NiPoint3& worldOffset)
    {
        RE::NiMatrix3 invRot = rotation.Transpose();
        return {
            invRot.entry[0][0] * worldOffset.x + invRot.entry[0][1] * worldOffset.y + invRot.entry[0][2] * worldOffset.z,
            invRot.entry[1][0] * worldOffset.x + invRot.entry[1][1] * worldOffset.y + invRot.entry[1][2] * worldOffset.z,
            invRot.entry[2][0] * worldOffset.x + invRot.entry[2][1] * worldOffset.y + invRot.entry[2][2] * worldOffset.z
        };
    }

    RE::NiPoint3 ComputeNodeLocalFromWorld(RE::NiAVObject* node, const RE::NiPoint3& desiredWorldPos)
    {
        if (!node || !node->parent) {
            return desiredWorldPos;
        }
        RE::NiPoint3 diff = desiredWorldPos - node->parent->world.translate;
        return TransformWorldToLocal(node->parent->world.rotate, diff);
    }

    float ClampFloat(float value, float minValue, float maxValue)
    {
        return (std::max)(minValue, (std::min)(value, maxValue));
    }

    float WrapAnglePi(float angle)
    {
        while (angle > 3.14159265f) angle -= 6.28318531f;
        while (angle < -3.14159265f) angle += 6.28318531f;
        return angle;
    }

    RE::NiMatrix3 MakeYawMatrix(float yaw)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        return RE::NiMatrix3(c, -s, 0.0f, 0.0f, s, c, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    }

    float DistanceSquared(const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool IsFinitePoint(const RE::NiPoint3& point)
    {
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    }

    void VisitNodesByName(RE::NiAVObject* root, const char* nameSubstr, NodeVisitorFn callback)
    {
        if (!root) return;
        const char* nameStr = root->name.c_str();
        if (nameStr && std::strstr(nameStr, nameSubstr)) {
            callback(root);
        }
        auto* asNode = root->GetAsNiNode();
        if (asNode) {
            for (auto& child : asNode->children) {
                if (child) {
                    VisitNodesByName(child.get(), nameSubstr, callback);
                }
            }
        }
    }

    bool NormalizePlanar(RE::NiPoint3& vector)
    {
        vector.z = 0.0f;
        const float len = std::sqrt(vector.x * vector.x + vector.y * vector.y);
        if (len <= 0.0001f) {
            return false;
        }
        vector.x /= len;
        vector.y /= len;
        return true;
    }

    bool Normalize3D(RE::NiPoint3& vector)
    {
        const float len = std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
        if (len <= 0.0001f) {
            return false;
        }
        vector.x /= len;
        vector.y /= len;
        vector.z /= len;
        return true;
    }

    void GetYawAxes(float yaw, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        outForward = { s, c, 0.0f };
        outRight = { c, -s, 0.0f };
    }

    void GetPlayerWorldAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        RE::NiAVObject* worldOrientationNode = g_PrevSkeletonRoot ? g_PrevSkeletonRoot : g_HeadBone;
        if (worldOrientationNode) {
            outForward = {
                worldOrientationNode->world.rotate.entry[0][1],
                worldOrientationNode->world.rotate.entry[1][1],
                worldOrientationNode->world.rotate.entry[2][1]
            };
            outRight = {
                worldOrientationNode->world.rotate.entry[0][0],
                worldOrientationNode->world.rotate.entry[1][0],
                worldOrientationNode->world.rotate.entry[2][0]
            };
            if (NormalizePlanar(outForward) && NormalizePlanar(outRight)) {
                return;
            }
        }
        const float yaw = player ? player->GetAngleZ() : 0.0f;
        GetYawAxes(yaw, outForward, outRight);
    }

    void GetPseudoFPAxes(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight)
    {
        GetPlayerWorldAxes(player, outForward, outRight);
    }

    void GetNodeAxes(RE::NiAVObject* node, RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        if (node) {
            outForward = { node->world.rotate.entry[0][1], node->world.rotate.entry[1][1], node->world.rotate.entry[2][1] };
            outRight = { node->world.rotate.entry[0][0], node->world.rotate.entry[1][0], node->world.rotate.entry[2][0] };
            outUp = { node->world.rotate.entry[0][2], node->world.rotate.entry[1][2], node->world.rotate.entry[2][2] };
        } else {
            const float yaw = player ? player->GetAngleZ() : 0.0f;
            GetYawAxes(yaw, outForward, outRight);
            outUp = { 0.0f, 0.0f, 1.0f };
        }
        if (!Normalize3D(outForward)) {
            const float yaw = player ? player->GetAngleZ() : 0.0f;
            GetYawAxes(yaw, outForward, outRight);
        }
        if (!Normalize3D(outRight)) {
            outRight = { outForward.y, -outForward.x, 0.0f };
            Normalize3D(outRight);
        }
        if (!Normalize3D(outUp)) {
            outUp = { 0.0f, 0.0f, 1.0f };
        }
    }

    void GetPlayerBodyAxes3D(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        RE::NiAVObject* bodyNode = g_PrevSkeletonRoot ? g_PrevSkeletonRoot : g_HeadBone;
        if (bodyNode) {
            GetNodeAxes(bodyNode, player, outForward, outRight, outUp);
            return;
        }
        const float yaw = player ? player->GetAngleZ() : 0.0f;
        GetYawAxes(yaw, outForward, outRight);
        outUp = { 0.0f, 0.0f, 1.0f };
    }

    RE::NiPoint3 CrossProduct(const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        return {
            (a.y * b.z) - (a.z * b.y),
            (a.z * b.x) - (a.x * b.z),
            (a.x * b.y) - (a.y * b.x)
        };
    }

    // === Head tracking ===
    RE::NiCamera* FindNiCamera(RE::TESCamera* tesCam)
    {
        if (!tesCam || !tesCam->cameraRoot) return nullptr;
        auto* rootNode = tesCam->cameraRoot.get();
        if (!rootNode) return nullptr;
        auto* rootNiNode = rootNode->GetAsNiNode();
        if (!rootNiNode) return nullptr;
        {
            uint32_t num = std::min(rootNiNode->children.size(), 256u);
            for (uint32_t i = 0; i < num; i++) {
                auto* childPtr = rootNiNode->children[i].get();
                if (!childPtr) continue;
                auto* cam = starfield_cast<RE::NiCamera*>(childPtr);
                if (cam) return cam;
            }
        }
        {
            uint32_t num = std::min(rootNiNode->children.size(), 256u);
            for (uint32_t i = 0; i < num; i++) {
                auto* childPtr = rootNiNode->children[i].get();
                if (!childPtr) continue;
                auto* childNode = childPtr->GetAsNiNode();
                if (!childNode) continue;
                uint32_t num2 = std::min(childNode->children.size(), 256u);
                for (uint32_t j = 0; j < num2; j++) {
                    auto* grandchild = childNode->children[j].get();
                    if (!grandchild) continue;
                    auto* cam = starfield_cast<RE::NiCamera*>(grandchild);
                    if (cam) return cam;
                }
            }
        }
        return nullptr;
    }

    RE::NiAVObject* GetCachedHeadBone()
    {
        return g_HeadAnchorNode ? g_HeadAnchorNode : g_HeadBone;
    }

    RE::NiPoint3 GetNodeCenter(RE::NiAVObject* node)
    {
        if (!node) return {};
        if (node->worldBound.radius > 0.001f) {
            return node->worldBound.center;
        }
        return node->world.translate;
    }

    RE::NiAVObject* FindPreferredEyeNode(RE::NiAVObject* root)
    {
        if (!root) return nullptr;
        static constexpr const char* kNames[] = { "Eye_Target", "faceBone_C_EyesFat" };
        for (auto* name : kNames) {
            auto* node = root->GetObjectByName(RE::BSFixedString(name));
            if (node) return node;
        }
        return nullptr;
    }

    RE::NiPoint3 GetHeadMeshCenter(RE::NiAVObject* headMesh)
    {
        if (!headMesh) return {};
        if (headMesh->worldBound.radius > 0.001f) {
            return headMesh->worldBound.center;
        }
        return headMesh->world.translate;
    }

    RE::NiPoint3 GetCurrentHeadAnchorWorldPosition()
    {
        if (g_HeadAnchorNode) {
            if (g_HeadAnchorNode == g_HeadBone) {
                return g_HeadBone->world.translate;
            }
            if (g_HeadMesh && g_HeadAnchorNode == g_HeadMesh) {
                return GetHeadMeshCenter(g_HeadMesh);
            }
            const RE::NiPoint3 directAnchor = g_HeadAnchorNode->world.translate;
            if (IsFinitePoint(directAnchor)) {
                return directAnchor;
            }
        }
        if (!g_HeadBone) return {};
        const RE::NiPoint3 localAnchor = TransformLocalToWorld(g_HeadBone->world.rotate, g_HeadAnchorLocalOffset);
        return g_HeadBone->world.translate + localAnchor;
    }

    void UpdateHeadAnchorData()
    {
        g_HeadAnchorNode = nullptr;
        g_HeadAnchorLocalOffset = {};
        if (!g_HeadBone) {
            g_HeadMesh = nullptr;
            return;
        }
        g_HeadMesh = g_HeadBone->parent ? static_cast<RE::NiAVObject*>(g_HeadBone->parent) : g_HeadBone;
        auto* preferred = FindPreferredEyeNode(g_PrevSkeletonRoot);
        if (preferred) {
            g_HeadAnchorNode = preferred;
        } else {
            g_HeadAnchorNode = g_HeadBone->parent ? static_cast<RE::NiAVObject*>(g_HeadBone->parent) : g_HeadBone;
        }
        if (!g_HeadAnchorNode) {
            g_HeadAnchorNode = g_HeadBone;
        }
        RE::NiPoint3 targetCenter = preferred ? preferred->world.translate : GetHeadMeshCenter(g_HeadMesh);
        RE::NiPoint3 headToCenter = targetCenter - g_HeadBone->world.translate;
        g_HeadAnchorLocalOffset = TransformWorldToLocal(g_HeadBone->world.rotate, headToCenter);
    }

    RE::NiPoint3 GetSAFHeadLocalOffset()
    {
        if (g_SAFAnimationPlaying) {
            return { 0.0f, 0.10f, 0.18f };
        }
        if (g_HeadAnchorLocalOffset.z > 0.05f && std::fabs(g_HeadAnchorLocalOffset.x) < 0.05f) {
            return g_HeadAnchorLocalOffset;
        }
        return { 0.0f, 0.10f, 0.18f };
    }

    RE::NiPoint3 BuildSAFPinnedCameraLocalOffset()
    {
        const RE::NiPoint3 headLocalOffset = GetSAFHeadLocalOffset();
        RE::NiPoint3 pinnedOffset = headLocalOffset;
        pinnedOffset.x += Systems::PseudoFPConfigManager::Get().GetSideOffset();
        pinnedOffset.y += Systems::PseudoFPConfigManager::Get().GetForwardOffset();
        pinnedOffset.z += Systems::PseudoFPConfigManager::Get().GetUpOffset();
        return pinnedOffset;
    }

    void GetSAFStableHeadFrame(RE::PlayerCharacter* player, RE::NiPoint3& outForward, RE::NiPoint3& outRight, RE::NiPoint3& outUp)
    {
        RE::NiPoint3 headForward, headRight, headUp;
        GetNodeAxes(g_HeadBone, player, headForward, headRight, headUp);
        const RE::NiPoint3 worldUp = { 0.0f, 0.0f, 1.0f };
        RE::NiPoint3 stableRight = CrossProduct(worldUp, headForward);
        if (!Normalize3D(stableRight)) {
            stableRight = CrossProduct(headUp, headForward);
        }
        if (!Normalize3D(stableRight)) {
            stableRight = headRight;
            Normalize3D(stableRight);
        }
        RE::NiPoint3 stableUp = CrossProduct(headForward, stableRight);
        if (!Normalize3D(stableUp)) {
            stableUp = worldUp;
        }
        outForward = headForward;
        outRight = stableRight;
        outUp = stableUp;
    }

    bool ComputeSAFCameraModeWorldPosition(RE::PlayerCharacter* player, RE::NiPoint3& outAnchor, RE::NiPoint3& outWorldPos)
    {
        if (!g_HeadBone || !IsFinitePoint(g_HeadBone->world.translate)) {
            return false;
        }
        if (!g_HasSAFPinnedCameraLocalOffset) {
            g_SAFPinnedCameraLocalOffset = BuildSAFPinnedCameraLocalOffset();
            g_HasSAFPinnedCameraLocalOffset = true;
        }
        const RE::NiPoint3 headPos = g_HeadBone->world.translate;
        const RE::NiMatrix3& headRot = g_HeadBone->world.rotate;
        const RE::NiPoint3 anchorLocalOffset = GetSAFHeadLocalOffset();
        const RE::NiPoint3 worldAnchorOffset = TransformLocalToWorld(headRot, anchorLocalOffset);
        outAnchor = headPos + worldAnchorOffset;

        const float userForward = Systems::PseudoFPConfigManager::Get().GetForwardOffset();
        const float userSide = Systems::PseudoFPConfigManager::Get().GetSideOffset();
        const float userUp = Systems::PseudoFPConfigManager::Get().GetUpOffset();

        RE::NiPoint3 stableForward, stableRight, stableUp;
        GetSAFStableHeadFrame(player, stableForward, stableRight, stableUp);

        outWorldPos = outAnchor;
        outWorldPos.x += (stableForward.x * userForward) + (stableRight.x * userSide) + (stableUp.x * userUp);
        outWorldPos.y += (stableForward.y * userForward) + (stableRight.y * userSide) + (stableUp.y * userUp);
        outWorldPos.z += (stableForward.z * userForward) + (stableRight.z * userSide) + (stableUp.z * userUp);
        return true;
    }

    RE::NiPoint3 ComputeSAFHeadAnchorWorldPosition(RE::PlayerCharacter* player)
    {
        if (!g_HeadBone || !IsFinitePoint(g_HeadBone->world.translate)) {
            return GetCurrentHeadAnchorWorldPosition();
        }
        RE::NiPoint3 anchor = {};
        RE::NiPoint3 worldPos = {};
        if (ComputeSAFCameraModeWorldPosition(player, anchor, worldPos)) {
            return anchor;
        }
        return g_HeadBone->world.translate;
    }

    RE::NiPoint3 ComputeUltraRigidHeadAnchorWorldPosition(RE::PlayerCharacter* player)
    {
        (void)player;
        if (g_HeadMesh) {
            RE::NiPoint3 meshCenter = GetHeadMeshCenter(g_HeadMesh);
            if (IsFinitePoint(meshCenter)) {
                return meshCenter;
            }
        }
        if (!g_HeadBone) return {};
        return g_HeadBone->world.translate;
    }

    RE::NiPoint3 ComputeUltraRigidCameraWorldPosition(RE::PlayerCharacter* player, const RE::NiPoint3& headAnchor, RE::NiAVObject* cr)
    {
        RE::NiPoint3 forwardAxis = {};
        RE::NiPoint3 rightAxis = {};
        RE::NiPoint3 upAxis = { 0.0f, 0.0f, 1.0f };

        if (g_SAFAnimationPlaying && g_HeadBone) {
            RE::NiPoint3 fwd, right, up;
            GetSAFStableHeadFrame(player, fwd, right, up);
            const float fwdOff = Systems::PseudoFPConfigManager::Get().GetForwardOffset();
            const float sideOff = Systems::PseudoFPConfigManager::Get().GetSideOffset();
            const float upOff = Systems::PseudoFPConfigManager::Get().GetUpOffset();
            RE::NiPoint3 worldPos = headAnchor;
            worldPos.x += fwd.x * fwdOff + right.x * sideOff + up.x * upOff;
            worldPos.y += fwd.y * fwdOff + right.y * sideOff + up.y * upOff;
            worldPos.z += fwd.z * fwdOff + right.z * sideOff + up.z * upOff;

            // TEMPORARY DIAGNOSTIC - REMOVE AFTER TESTING.
            // Flip to true, rebuild, test one SAF animation, then flip back
            // to false (or delete this block) before normal use.
            // If the on-screen camera visibly jumps ~3m straight up the
            // instant SAF starts and stays there for the whole animation,
            // we ARE editing the camera object that's actually rendered -
            // meaning the "pulls back like TPP" look comes from somewhere
            // else, not from patching the wrong node. If the camera does
            // NOT visibly move at all, the rendered camera is a different
            // NiCamera object than the one cameraRoot resolves to here.
            constexpr bool kDiagTestSAFCameraOffset = false;
            if (kDiagTestSAFCameraOffset) {
                worldPos.z += 3.0f;
                LogFormatted("DIAG_TEST: applied +3.0 Z offset, worldPos=(%.3f,%.3f,%.3f)",
                    worldPos.x, worldPos.y, worldPos.z);
            }

            return worldPos;
        } else if (cr) {
            const float camYaw = std::atan2(cr->world.rotate.entry[1][0], cr->world.rotate.entry[0][0]);
            GetYawAxes(camYaw, forwardAxis, rightAxis);
        } else {
            GetPlayerWorldAxes(player, forwardAxis, rightAxis);
        }

        RE::NiPoint3 worldPos = headAnchor;
        worldPos.x += (forwardAxis.x * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.x * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.x * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        worldPos.y += (forwardAxis.y * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.y * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.y * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        worldPos.z += (forwardAxis.z * Systems::PseudoFPConfigManager::Get().GetForwardOffset()) + (rightAxis.z * Systems::PseudoFPConfigManager::Get().GetSideOffset()) + (upAxis.z * Systems::PseudoFPConfigManager::Get().GetUpOffset());
        return worldPos;
    }

    RE::NiPoint3 ClampWorldPosToPlayerCapsule(RE::PlayerCharacter* player, const RE::NiPoint3& desiredWorldPos, const RE::NiPoint3& headAnchor)
    {
        if (!player) return desiredWorldPos;
        const RE::NiPoint3 boundMin = player->GetBoundMin();
        const RE::NiPoint3 boundMax = player->GetBoundMax();
        const float playerX = player->GetPositionX();
        const float playerY = player->GetPositionY();
        const float playerZ = player->GetPositionZ();

        float radiusX = (std::max)(std::fabs(boundMin.x - playerX), std::fabs(boundMax.x - playerX));
        float radiusY = (std::max)(std::fabs(boundMin.y - playerY), std::fabs(boundMax.y - playerY));
        float capsuleRadius = (std::max)(radiusX, radiusY);
        if (capsuleRadius < 0.20f || capsuleRadius > 1.00f) {
            capsuleRadius = 0.35f;
        }

        RE::NiPoint3 clamped = desiredWorldPos;
        float offsetX = desiredWorldPos.x - playerX;
        float offsetY = desiredWorldPos.y - playerY;
        float planarLen = std::sqrt(offsetX * offsetX + offsetY * offsetY);
        if (planarLen > capsuleRadius && planarLen > 0.0001f) {
            float scale = capsuleRadius / planarLen;
            clamped.x = playerX + offsetX * scale;
            clamped.y = playerY + offsetY * scale;
        }

        float minZ = boundMin.z;
        float maxZ = boundMax.z;
        if ((maxZ - minZ) < 0.5f || (maxZ - minZ) > 3.5f) {
            minZ = playerZ + 0.20f;
            maxZ = playerZ + (std::max)(g_EyeHeight + 0.10f, 1.20f);
        }
        maxZ = (std::max)(maxZ, headAnchor.z);
        clamped.z = ClampFloat(clamped.z, minZ, maxZ);
        return clamped;
    }

    bool IsReasonableHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos)
    {
        if (!player) return false;
        const float dx = anchorPos.x - player->GetPositionX();
        const float dy = anchorPos.y - player->GetPositionY();
        const float planarDist = std::sqrt(dx * dx + dy * dy);
        if (planarDist > Systems::PseudoFPConfigManager::Get().GetUltraRigidMaxPlanarOffset()) return false;
        const float expectedHeight = g_EyeHeight;
        const float actualHeight = anchorPos.z - player->GetPositionZ();
        const float tolerance = Systems::PseudoFPConfigManager::Get().GetUltraRigidHeightTolerance();
        return actualHeight >= (expectedHeight - tolerance) && actualHeight <= (expectedHeight + tolerance);
    }

    bool IsPlausibleHeadAnchor(RE::PlayerCharacter* player, const RE::NiPoint3& anchorPos)
    {
        if (!player || !IsFinitePoint(anchorPos)) return false;
        const float dx = anchorPos.x - player->GetPositionX();
        const float dy = anchorPos.y - player->GetPositionY();
        const float planarDist = std::sqrt(dx * dx + dy * dy);
        if (planarDist > 3.50f) return false;
        const float actualHeight = anchorPos.z - player->GetPositionZ();
        return actualHeight >= 0.20f && actualHeight <= 3.50f;
    }

    void InitEyeHeight()
    {
        if (g_HasEyeHeight) return;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        g_PrevSkeletonRoot = loaded->data3D.get();
        g_HeadBone = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("C_Head"));
        if (g_HeadBone) {
            UpdateHeadAnchorData();
            RE::NiPoint3 eyeCenter = GetCurrentHeadAnchorWorldPosition();
            if (Systems::PseudoFPConfigManager::Get().IsUltraRigidEnabled()) {
                eyeCenter = ComputeUltraRigidCameraWorldPosition(player, ComputeUltraRigidHeadAnchorWorldPosition(player));
            }
            g_EyeHeight = eyeCenter.z - player->GetPositionZ();
            g_HasEyeHeight = true;
            LogFormatted("Init: C_Head eyeHeight=%.2f", g_EyeHeight);
        }
    }

    void RefreshHeadBone()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        auto guard = player->loadedData.LockRead();
        auto* loaded = *guard;
        if (!loaded || !loaded->data3D.get()) return;
        auto* root = loaded->data3D.get();
        if (root != g_PrevSkeletonRoot) {
            LogFormatted("Skeleton root change: %p -> %p", (void*)g_PrevSkeletonRoot, (void*)root);
            if (g_HeadBone && IsFinitePoint(g_HeadBone->world.translate)) {
                g_LastValidHeadAnchorWorld = g_HeadBone->world.translate;
                g_HasLastValidHeadAnchorWorld = true;
            }
            g_PrevSkeletonRoot = root;
            g_HeadBone = nullptr;
            g_HeadAnchorNode = nullptr;
            g_HeadMesh = nullptr;
            g_HeadAnchorLocalOffset = {};
        }
        auto* newBone = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("C_Head"));
        if (newBone) {
            g_HeadBone = newBone;
            UpdateHeadAnchorData();
            g_HeadAnchorMissCount = 0;
            g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("ICSF_Hide_Head"));
            if (!g_HideHeadNode) {
                g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("HideHead"));
            }
            if (!g_HideHeadNode) {
                g_HideHeadNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("Hide_Head"));
            }
            g_HideHeadNodeValid = (g_HideHeadNode != nullptr);
        } else {
            RE::NiAVObject* altHead = nullptr;
            static constexpr const char* kAltHeadNames[] = {
                "NPC Head [Head]", "Head", "NPC Head", "C_Head"
            };
            for (auto* name : kAltHeadNames) {
                altHead = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString(name));
                if (altHead) break;
            }
            if (altHead) {
                g_HeadBone = altHead;
                UpdateHeadAnchorData();
                g_HeadAnchorMissCount = 0;
            } else {
                LogFormatted("C_HEAD NOT FOUND in new skeleton, using cached anchor grace=%u", g_HeadAnchorMissCount);
                g_HeadBone = nullptr;
                g_HeadAnchorNode = nullptr;
                g_HeadMesh = nullptr;
                g_HeadAnchorLocalOffset = {};
                g_HeadAnchorMissCount++;
            }
        }
    }

    void HideHead(bool a_hide)
    {
        if (!g_HeadMesh) return;
        if (a_hide) {
            if (!g_HasSavedHeadCull) {
                g_WasHeadAppCulled = (g_HeadMesh->flags & 1) != 0;
                g_HasSavedHeadCull = true;
            }
            g_HeadMesh->flags |= 1;
        } else {
            if (g_HasSavedHeadCull && !g_WasHeadAppCulled)
                g_HeadMesh->flags &= ~1ULL;
            g_HasSavedHeadCull = false;
        }
    }

    void RestoreCameraOrbit()
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera) return;
        auto* tesCam = static_cast<RE::TESCamera*>(camera);
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return;
        cr->local.translate = ComputeNodeLocalFromWorld(cr, cr->world.translate);
        cr->previousWorld.translate = cr->world.translate;
        auto* niCam = FindNiCamera(tesCam);
        if (niCam) {
            niCam->local.translate = { 0, 0, 0 };
            niCam->previousWorld.translate = cr->world.translate;
            niCam->world.translate = cr->world.translate;
        }
    }

    void ResetPseudoFPPState(bool a_restoreOrbit)
    {
        if (a_restoreOrbit) {
            RestoreCameraOrbit();
            HideHead(false);
        }
        g_CameraRoot = nullptr;
        g_NiCamera = nullptr;
        g_PrevRootLocal = {};
        g_PrevRootWorld = {};
        g_PrevRootLocalRot = {};
        g_PrevRootWorldRot = {};
        g_PrevRootPrevWorldRot = {};
        g_RestoreRootRotation = false;
        g_FurnitureYawLocked = false;
        g_FurnitureBaseYaw = 0.0f;
        g_FurnitureSavedBodyYaw = 0.0f;
        g_FurnitureYawRestored = false;
        g_FurnitureGraceFrames = 0;
        g_PrevSetLocal = {};
        g_PrevSetWorld = {};
        g_PrevSetWorldFrame = 0;
        g_HeadAnchorMissCount = 0;
        g_HasLastValidHeadAnchorWorld = false;
        g_LastValidHeadAnchorWorld = {};
        g_HasSAFPinnedCameraLocalOffset = false;
        g_SAFPinnedCameraLocalOffset = {};
        g_HasLastRawUltraRigidAnchor = false;
        g_LastRawUltraRigidAnchor = {};
    }

    void ResetCameraNodesToPlayer()
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!camera || !player) return;
        auto* tesCam = static_cast<RE::TESCamera*>(camera);
        auto* cr = tesCam->cameraRoot.get();
        if (!cr) return;
        RE::NiPoint3 playerPos = { player->GetPositionX(), player->GetPositionY(), player->GetPositionZ() };
        cr->local.translate = ComputeNodeLocalFromWorld(cr, playerPos);
        cr->previousWorld.translate = playerPos;
        cr->world.translate = playerPos;
        auto* niCam = FindNiCamera(tesCam);
        if (niCam) {
            niCam->local.translate = {};
            niCam->previousWorld.translate = playerPos;
            niCam->world.translate = playerPos;
        }
        HideHead(false);
    }

    void ClearPseudoCameraPointers()
    {
        g_NiCamera = nullptr;
        g_CameraRoot = nullptr;
    }

    // === Detection functions ===
    bool IsPlayerUsingFurniture(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        auto* proc = player->currentProcess;
        if (!proc || !proc->middleHigh) return false;
        return static_cast<bool>(proc->middleHigh->occupiedFurniture) || static_cast<bool>(proc->middleHigh->currentFurniture);
    }

    bool IsPlayerInShipPilotSeat(RE::PlayerCharacter* player)
    {
        if (!player) return false;
        auto* proc = player->currentProcess;
        if (!proc || !proc->middleHigh) return false;
        auto& furnitureHandle = proc->middleHigh->occupiedFurniture;
        if (!furnitureHandle) return false;
        auto furnitureRef = furnitureHandle.get();
        if (!furnitureRef) return false;

        auto* baseObj = furnitureRef->GetBaseObject().get();
        if (!baseObj) return false;

        // Confirmed via SHIP_SEAT_CHECK_FAIL log: the EditorID-based keyword
        // lookup below ("ShipPilotSeat") always resolves to null in the
        // shipped game (SHIP_SEAT_KEYWORD_LOOKUP: resolved=NO) - EditorID
        // strings simply aren't indexed at runtime in a retail build.
        // Confirmed FormID of the base game's universal pilot seat furniture
        // record (from Starfield.esm, load order 00): 0x00184717. This is
        // now the primary, verified detection.
        constexpr std::uint32_t kKnownShipPilotSeatFormID = 0x00184717;
        if (baseObj->GetFormID() == kKnownShipPilotSeatFormID) {
            return true;
        }

        // Kept as a secondary path in case a different ship/DLC uses a
        // differently-FormID'd seat that happens to share this keyword, or
        // in case EditorID data becomes available in some environments.
        static RE::BGSKeyword* shipPilotSeatKeyword = nullptr;
        if (!shipPilotSeatKeyword) {
            shipPilotSeatKeyword = RE::TESForm::LookupByEditorID<RE::BGSKeyword>(RE::BSFixedString("ShipPilotSeat"));
        }
        if (shipPilotSeatKeyword && furnitureRef->HasKeyword(shipPilotSeatKeyword)) {
            return true;
        }

        // DIAGNOSTIC: log any OTHER furniture's FormID once per second while
        // occupied but not recognized as the pilot seat, so additional ship
        // variants can be identified and added to the known list above.
        static int logThrottle = 0;
        logThrottle++;
        if ((logThrottle % 60) == 0) {
            LogFormatted("SHIP_SEAT_CHECK_MISS: furnitureFormID=%08X baseObjFormID=%08X (not the known pilot seat)",
                furnitureRef->GetFormID(), baseObj->GetFormID());
        }

        return false;
    }

    bool IsInShipCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kFlight) ||
               camera->QCameraEquals(RE::CameraState::kShipAction) ||
               camera->QCameraEquals(RE::CameraState::kShipTargeting) ||
               camera->QCameraEquals(RE::CameraState::kShipCombatOrbit) ||
               camera->QCameraEquals(RE::CameraState::kShipFarTravel);
    }

    bool IsInVehicleCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kVehicle);
    }

    bool IsInFreeCameraState(RE::PlayerCamera* camera)
    {
        if (!camera) return false;
        return camera->QCameraEquals(RE::CameraState::kFreeWalk) ||
               camera->QCameraEquals(RE::CameraState::kFreeAdvanced) ||
               camera->QCameraEquals(RE::CameraState::kFreeFly) ||
               camera->QCameraEquals(RE::CameraState::kFreeTethered) ||
               camera->QCameraEquals(RE::CameraState::kPhotoMode);
    }

    static void GetPseudoFPOffsets(float& outX, float& outY, float& outZ)
    {
        Systems::PseudoFPConfigManager::Get().GetOffsets(outX, outY, outZ);
    }

    bool ComputePseudoFPPWorldPosition(RE::TESCamera* tesCam, RE::NiPoint3& outWorldPos, RE::NiPoint3* outHeadAnchor, bool* outUsingFallback)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !tesCam) {
            return false;
        }

        InitEyeHeight();

        auto* cr = tesCam->cameraRoot.get();
        if (!cr) {
            return false;
        }

        RefreshHeadBone();

        if (!g_SAFAnimationPlaying && g_HeadBone && g_HeadBone->parent) {
            RE::NiPoint3 eyeWorld = GetCurrentHeadAnchorWorldPosition();
            if (IsFinitePoint(eyeWorld)) {
                RE::NiPoint3 headToEye = eyeWorld - g_HeadBone->world.translate;
                g_HeadAnchorLocalOffset = TransformWorldToLocal(g_HeadBone->world.rotate, headToEye);
            }
        }

        if (!g_SAFAnimationPlaying) {
            g_HasSAFPinnedCameraLocalOffset = false;
            g_HasLastRawUltraRigidAnchor = false;
            g_HasSAFSmoothedHeadPos = false;
        }

        bool usingFallback = false;
        RE::NiPoint3 anchorPos;
        const bool ultraRigidHeadAttach = Systems::PseudoFPConfigManager::Get().IsUltraRigidEnabled();
        const RE::NiPoint3 playerEyeFallback = {
            player->GetPositionX(),
            player->GetPositionY(),
            player->GetPositionZ() + g_EyeHeight
        };
        if (g_HeadBone) {
            if (ultraRigidHeadAttach) {
                RE::NiPoint3 directBoneAnchor = ComputeUltraRigidHeadAnchorWorldPosition(player);

                const bool ignoreUltraRigidSanity = Systems::PseudoFPConfigManager::Get().IsUltraRigidIgnoreSanityEnabled();
                bool acceptUltraRigidAnchor = IsFinitePoint(directBoneAnchor);
                if (acceptUltraRigidAnchor) {
                    const RE::NiPoint3 rawThisFrame = directBoneAnchor;

                    if (g_SAFAnimationPlaying && g_HasLastValidHeadAnchorWorld && !ignoreUltraRigidSanity) {
                        constexpr float kSmoothingAlpha = 0.25f;
                        RE::NiPoint3 smoothed;
                        smoothed.x = g_LastValidHeadAnchorWorld.x + (directBoneAnchor.x - g_LastValidHeadAnchorWorld.x) * kSmoothingAlpha;
                        smoothed.y = g_LastValidHeadAnchorWorld.y + (directBoneAnchor.y - g_LastValidHeadAnchorWorld.y) * kSmoothingAlpha;
                        smoothed.z = g_LastValidHeadAnchorWorld.z + (directBoneAnchor.z - g_LastValidHeadAnchorWorld.z) * kSmoothingAlpha;

                        const float jdx = directBoneAnchor.x - g_LastValidHeadAnchorWorld.x;
                        const float jdy = directBoneAnchor.y - g_LastValidHeadAnchorWorld.y;
                        const float jdz = directBoneAnchor.z - g_LastValidHeadAnchorWorld.z;
                        const float jumpDist = std::sqrt(jdx * jdx + jdy * jdy + jdz * jdz);
                        if (jumpDist > 0.02f) {
                            LogFormatted("ULTRA_RIGID_SMOOTH: raw=(%.3f,%.3f,%.3f) prev=(%.3f,%.3f,%.3f) smoothed=(%.3f,%.3f,%.3f) dist=%.3f",
                                directBoneAnchor.x, directBoneAnchor.y, directBoneAnchor.z,
                                g_LastValidHeadAnchorWorld.x, g_LastValidHeadAnchorWorld.y, g_LastValidHeadAnchorWorld.z,
                                smoothed.x, smoothed.y, smoothed.z, jumpDist);
                        }
                        directBoneAnchor = smoothed;
                    }

                    g_LastRawUltraRigidAnchor = rawThisFrame;
                    g_HasLastRawUltraRigidAnchor = true;
                }

                if (acceptUltraRigidAnchor) {
                    anchorPos = directBoneAnchor;
                    g_LastValidHeadAnchorWorld = anchorPos;
                    g_HasLastValidHeadAnchorWorld = true;

                    outWorldPos = ComputeUltraRigidCameraWorldPosition(player, anchorPos, cr);

                    if ((g_FrameCount % 60) == 0) LogFormatted("SAF_OUT: anchor=(%.3f,%.3f,%.3f) worldPos=(%.3f,%.3f,%.3f)",
                        anchorPos.x, anchorPos.y, anchorPos.z,
                        outWorldPos.x, outWorldPos.y, outWorldPos.z);

                    if (outHeadAnchor) {
                        *outHeadAnchor = anchorPos;
                    }
                    if (outUsingFallback) {
                        *outUsingFallback = false;
                    }
                    return true;
                }
            }

            RE::NiPoint3 headAnchorPos;
            if (g_SAFAnimationPlaying) {
                // During SAF, compute the camera position using head-relative
                // axes (via GetSAFStableHeadFrame) instead of body axes.
                // The head bone's orientation changes between poses (standing,
                // prone, on back) and body-relative axes push the camera in
                // the wrong direction. We then return early before the common
                // offset code (which uses body axes).
                if (g_HeadBone && IsFinitePoint(g_HeadBone->world.translate)) {
                    auto guard = player->loadedData.LockRead();
                    auto* loaded = *guard;
                    if (loaded && loaded->data3D.get()) {
                        auto* root = loaded->data3D.get();
                        // Use head bone position clamped to root node
                        RE::NiPoint3 rawHeadPos = g_HeadBone->world.translate;
                        RE::NiPoint3 rootPos = root->world.translate;
                        constexpr float kMaxDrift = 100.0f;
                        float dx = rawHeadPos.x - rootPos.x;
                        float dy = rawHeadPos.y - rootPos.y;
                        float dz = rawHeadPos.z - rootPos.z;
                        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                        if (dist > kMaxDrift) {
                            float scale = kMaxDrift / dist;
                            rawHeadPos.x = rootPos.x + dx * scale;
                            rawHeadPos.y = rootPos.y + dy * scale;
                            rawHeadPos.z = rootPos.z + dz * scale;
                        }
                        // Exponential smoothing for stable head tracking
                        constexpr float kSmoothing = 0.3f;
                        if (g_HasSAFSmoothedHeadPos) {
                            headAnchorPos.x = g_SAFSmoothedHeadPos.x + (rawHeadPos.x - g_SAFSmoothedHeadPos.x) * kSmoothing;
                            headAnchorPos.y = g_SAFSmoothedHeadPos.y + (rawHeadPos.y - g_SAFSmoothedHeadPos.y) * kSmoothing;
                            headAnchorPos.z = g_SAFSmoothedHeadPos.z + (rawHeadPos.z - g_SAFSmoothedHeadPos.z) * kSmoothing;
                        } else {
                            headAnchorPos = rawHeadPos;
                        }
                        g_SAFSmoothedHeadPos = headAnchorPos;
                        g_HasSAFSmoothedHeadPos = true;
                        // Apply INI offsets using head-relative axes, then return early
                        RE::NiPoint3 fwd, right, up;
                        GetSAFStableHeadFrame(player, fwd, right, up);
                        // Nose forward offset
                        const float noseFwd = Systems::PseudoFPConfigManager::Get().GetNoseForward();
                        headAnchorPos.x += fwd.x * noseFwd;
                        headAnchorPos.y += fwd.y * noseFwd;
                        headAnchorPos.z += fwd.z * noseFwd;
                        // INI offset values
                        float offX, offY, offZ;
                        Systems::PseudoFPConfigManager::Get().GetOffsets(offX, offY, offZ);
                        headAnchorPos.x += (-fwd.x * offX) + (right.x * offY) + (up.x * offZ);
                        headAnchorPos.y += (-fwd.y * offX) + (right.y * offY) + (up.y * offZ);
                        headAnchorPos.z += (-fwd.z * offX) + (right.z * offY) + (up.z * offZ);
                        // Debug offsets
                        float debugX, debugY, debugZ;
                        Systems::PseudoFPConfigManager::Get().GetDebugOffsets(debugX, debugY, debugZ);
                        headAnchorPos.x += debugX;
                        headAnchorPos.y += debugY;
                        headAnchorPos.z += debugZ;
                        anchorPos = headAnchorPos;
                        outWorldPos = anchorPos;
                        g_LastValidHeadAnchorWorld = anchorPos;
                        g_HasLastValidHeadAnchorWorld = true;
                        if (outHeadAnchor) *outHeadAnchor = anchorPos;
                        if (outUsingFallback) *outUsingFallback = false;
                        return true;
                    }
                }
                headAnchorPos = playerEyeFallback;
            } else {
                headAnchorPos = GetCurrentHeadAnchorWorldPosition();
            }

            RE::NiPoint3 directAnchorPos = headAnchorPos;
            const bool ignoreSanity = (ultraRigidHeadAttach && Systems::PseudoFPConfigManager::Get().IsUltraRigidIgnoreSanityEnabled()) || g_SAFAnimationPlaying;
            const bool acceptDirectAnchor = IsFinitePoint(directAnchorPos) &&
                (ignoreSanity || (ultraRigidHeadAttach ? IsPlausibleHeadAnchor(player, directAnchorPos) : IsReasonableHeadAnchor(player, directAnchorPos)));

            if (acceptDirectAnchor) {
                anchorPos = directAnchorPos;
                g_LastValidHeadAnchorWorld = anchorPos;
                g_HasLastValidHeadAnchorWorld = true;
            } else if (g_HasLastValidHeadAnchorWorld &&
                       (ignoreSanity ? IsFinitePoint(g_LastValidHeadAnchorWorld) :
                                       (ultraRigidHeadAttach ? IsPlausibleHeadAnchor(player, g_LastValidHeadAnchorWorld) :
                                                               IsReasonableHeadAnchor(player, g_LastValidHeadAnchorWorld)))) {
                anchorPos = g_LastValidHeadAnchorWorld;
                LogFormatted("PFPP_REJECT_BAD_HEAD: direct=(%.2f,%.2f,%.2f) cached=(%.2f,%.2f,%.2f) player=(%.2f,%.2f,%.2f)",
                    directAnchorPos.x, directAnchorPos.y, directAnchorPos.z,
                    g_LastValidHeadAnchorWorld.x, g_LastValidHeadAnchorWorld.y, g_LastValidHeadAnchorWorld.z,
                    player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            } else {
                usingFallback = true;
                anchorPos = playerEyeFallback;
                g_HasLastValidHeadAnchorWorld = false;
                LogFormatted("PFPP_REJECT_BAD_HEAD_FALLBACK: direct=(%.2f,%.2f,%.2f) player=(%.2f,%.2f,%.2f)",
                    directAnchorPos.x, directAnchorPos.y, directAnchorPos.z,
                    player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            }
        } else {
            if (g_SAFAnimationPlaying) {
                usingFallback = true;
                if (g_HasLastValidHeadAnchorWorld && IsFinitePoint(g_LastValidHeadAnchorWorld)) {
                    anchorPos = g_LastValidHeadAnchorWorld;
                } else if (g_PrevSkeletonRoot && IsFinitePoint(g_PrevSkeletonRoot->world.translate)) {
                    anchorPos = g_PrevSkeletonRoot->world.translate;
                    anchorPos.z += g_EyeHeight;
                    RE::NiAVObject* headNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("NPC Head [Head]"));
                    if (!headNode) {
                        headNode = g_PrevSkeletonRoot->GetObjectByName(RE::BSFixedString("Head"));
                    }
                    if (headNode && IsFinitePoint(headNode->world.translate)) {
                        anchorPos.z = headNode->world.translate.z;
                    }
                } else {
                    anchorPos = playerEyeFallback;
                }
            } else {
                const uint32_t kHeadAnchorGraceFrames = Systems::PseudoFPConfigManager::Get().GetHeadCacheGraceFrames();
                constexpr float kMaxCachedAnchorDistanceSq = 6.25f;
                const bool canUseCachedAnchor = ultraRigidHeadAttach ?
                    (g_HasLastValidHeadAnchorWorld && g_HeadAnchorMissCount <= kHeadAnchorGraceFrames && IsFinitePoint(g_LastValidHeadAnchorWorld)) :
                    (g_HasLastValidHeadAnchorWorld &&
                     g_HeadAnchorMissCount <= kHeadAnchorGraceFrames &&
                     DistanceSquared(g_LastValidHeadAnchorWorld, playerEyeFallback) <= kMaxCachedAnchorDistanceSq);

                if (canUseCachedAnchor) {
                    anchorPos = g_LastValidHeadAnchorWorld;
                    if (ultraRigidHeadAttach) {
                        outWorldPos = ComputeUltraRigidCameraWorldPosition(player, anchorPos, cr);
                        if (outHeadAnchor) {
                            *outHeadAnchor = anchorPos;
                        }
                        if (outUsingFallback) {
                            *outUsingFallback = false;
                        }
                        return true;
                    }
                } else {
                    usingFallback = true;
                    anchorPos = playerEyeFallback;
                    g_HasLastValidHeadAnchorWorld = false;
                }
            }
        }

        const float noseForward = Systems::PseudoFPConfigManager::Get().GetNoseForward();
        RE::NiPoint3 forwardAxis = {};
        RE::NiPoint3 rightAxis = {};
        RE::NiPoint3 upAxis = { 0.0f, 0.0f, 1.0f };
        GetPseudoFPAxes(player, forwardAxis, rightAxis);
        if (std::fabs(noseForward) > 0.0001f) {
            anchorPos.x += forwardAxis.x * noseForward;
            anchorPos.y += forwardAxis.y * noseForward;
            anchorPos.z += forwardAxis.z * noseForward;
        }

        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float offsetZ = 0.0f;
        Systems::PseudoFPConfigManager::Get().GetOffsets(offsetX, offsetY, offsetZ);

        RE::NiPoint3 worldPos = anchorPos;

        worldPos.x += (-forwardAxis.x * offsetX) + (rightAxis.x * offsetY) + (upAxis.x * offsetZ);
        worldPos.y += (-forwardAxis.y * offsetX) + (rightAxis.y * offsetY) + (upAxis.y * offsetZ);
        worldPos.z += (-forwardAxis.z * offsetX) + (rightAxis.z * offsetY) + (upAxis.z * offsetZ);

        float debugX, debugY, debugZ;
        Systems::PseudoFPConfigManager::Get().GetDebugOffsets(debugX, debugY, debugZ);
        worldPos.x += debugX;
        worldPos.y += debugY;
        worldPos.z += debugZ;

        RE::NiPoint3 unclampedWorldPos = worldPos;
        if (!ultraRigidHeadAttach) {
            if (usingFallback) {
                worldPos = ClampWorldPosToPlayerCapsule(player, worldPos, anchorPos);
            }
        }

        if (outHeadAnchor) {
            *outHeadAnchor = anchorPos;
        }
        if (outUsingFallback) {
            *outUsingFallback = usingFallback;
        }

        outWorldPos = worldPos;
        if (!usingFallback && g_HeadAnchorMissCount > 0) {
            LogFormatted("PFPP_HEAD_CACHE: missCount=%u anchor=(%.2f,%.2f,%.2f) world=(%.2f,%.2f,%.2f)",
                g_HeadAnchorMissCount,
                anchorPos.x, anchorPos.y, anchorPos.z,
                worldPos.x, worldPos.y, worldPos.z);
        }
        if (std::fabs(unclampedWorldPos.x - worldPos.x) > 0.001f ||
            std::fabs(unclampedWorldPos.y - worldPos.y) > 0.001f ||
            std::fabs(unclampedWorldPos.z - worldPos.z) > 0.001f) {
            LogFormatted("PFPP_CAPSULE_CLAMP: unclamped=(%.2f,%.2f,%.2f) clamped=(%.2f,%.2f,%.2f) anchor=(%.2f,%.2f,%.2f)",
                unclampedWorldPos.x, unclampedWorldPos.y, unclampedWorldPos.z,
                worldPos.x, worldPos.y, worldPos.z,
                anchorPos.x, anchorPos.y, anchorPos.z);
        }
        return true;
    }
}
