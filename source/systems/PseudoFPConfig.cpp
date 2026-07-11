#include "systems/PseudoFPConfig.h"
#include <Windows.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace Systems {

    static std::string GetPluginDirIniPath()
    {
        static std::string iniPath;
        static bool initialized = false;
        if (!initialized) {
            char path[MAX_PATH];
            GetModuleFileNameA(GetModuleHandleA("ImprovedCameraSF.dll"), path, MAX_PATH);
            std::string full(path);
            size_t pos = full.find_last_of("\\/");
            std::string dir = (pos != std::string::npos) ? full.substr(0, pos + 1) : "";
            iniPath = dir + "ImprovedCameraSF.ini";
            initialized = true;
        }
        return iniPath;
    }

    static float GetIniFloat(const char* section, const char* key, float defaultValue)
    {
        const std::string& iniPath = GetPluginDirIniPath();
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        return static_cast<float>(atof(buf));
    }

    static bool GetIniBool(const char* section, const char* key, bool defaultValue)
    {
        const std::string& iniPath = GetPluginDirIniPath();
        char buf[32];
        GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), iniPath.c_str());
        if (strlen(buf) == 0)
            return defaultValue;
        std::string value(buf);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value == "1" || value == "true" || value == "yes" || value == "on";
    }

    PseudoFPConfigManager& PseudoFPConfigManager::Get()
    {
        static PseudoFPConfigManager instance;
        return instance;
    }

    void PseudoFPConfigManager::Refresh()
    {
        m_Cache.forwardOffset = GetIniFloat("PseudoFP", "fForwardOffset",
            GetIniFloat("PseudoFP", "fNoseForward", 0.0f) - GetIniFloat("PseudoFP", "fOffsetX", 0.0f));
        m_Cache.sideOffset = GetIniFloat("PseudoFP", "fSideOffset",
            GetIniFloat("PseudoFP", "fOffsetY", 0.0f));
        m_Cache.upOffset = GetIniFloat("PseudoFP", "fUpOffset",
            GetIniFloat("PseudoFP", "fOffsetZ", 0.0f));
        m_Cache.noseForward = GetIniFloat("PseudoFP", "fNoseForward", 0.0f);
        m_Cache.offsetX = GetIniFloat("PseudoFP", "fOffsetX", 0.0f);
        m_Cache.offsetY = GetIniFloat("PseudoFP", "fOffsetY", 0.0f);
        m_Cache.offsetZ = GetIniFloat("PseudoFP", "fOffsetZ", 0.0f);
        m_Cache.debugOffsetX = GetIniFloat("PseudoFP", "fDebugOffsetX", 0.0f);
        m_Cache.debugOffsetY = GetIniFloat("PseudoFP", "fDebugOffsetY", 0.0f);
        m_Cache.debugOffsetZ = GetIniFloat("PseudoFP", "fDebugOffsetZ", 0.0f);
        m_Cache.ultraRigidHeadAttach = GetIniBool("PseudoFP", "bUltraRigidHeadAttach", false);
        m_Cache.ultraRigidIgnoreSanity = GetIniBool("PseudoFP", "bUltraRigidIgnoreSanityChecks", false);
        float configuredGrace = GetIniFloat("PseudoFP", "fHeadAttachGraceFrames", 45.0f);
        float clamped = (std::max)(0.0f, (std::min)(configuredGrace, 240.0f));
        m_Cache.headCacheGraceFrames = static_cast<uint32_t>(clamped + 0.5f);
        m_Cache.ultraRigidMaxPlanarOffset = (std::max)(0.20f, (std::min)(GetIniFloat("PseudoFP", "fUltraRigidMaxPlanarOffset", 0.60f), 1.50f));
        m_Cache.ultraRigidHeightTolerance = (std::max)(0.10f, (std::min)(GetIniFloat("PseudoFP", "fUltraRigidHeightTolerance", 0.50f), 3.50f));
        float maxYawDeg = GetIniFloat("PseudoFP", "fMaxYawDegrees", 90.0f);
        m_Cache.maxYawRad = (std::max)(30.0f, (std::min)(maxYawDeg, 180.0f)) * (3.14159265f / 180.0f);
        m_Cache.furnitureExitGraceFrames = GetIniFloat("PseudoFP", "fFurnitureExitGraceFrames", 90.0f);
        m_Cache.pseudoFPPFOV = GetIniFloat("PseudoFP", "fFOV", 85.0f);
    }

    void PseudoFPConfigManager::EnsureCached()
    {
        if (m_Cache.refreshCounter <= 0) {
            Refresh();
            m_Cache.refreshCounter = kRefreshInterval;
        }
        m_Cache.refreshCounter--;
    }

    float PseudoFPConfigManager::GetForwardOffset() { EnsureCached(); return m_Cache.forwardOffset; }
    float PseudoFPConfigManager::GetSideOffset() { EnsureCached(); return m_Cache.sideOffset; }
    float PseudoFPConfigManager::GetUpOffset() { EnsureCached(); return m_Cache.upOffset; }
    float PseudoFPConfigManager::GetNoseForward() { EnsureCached(); return m_Cache.noseForward; }
    float PseudoFPConfigManager::GetMaxYawRad() { EnsureCached(); return m_Cache.maxYawRad; }
    float PseudoFPConfigManager::GetUltraRigidMaxPlanarOffset() { EnsureCached(); return m_Cache.ultraRigidMaxPlanarOffset; }
    float PseudoFPConfigManager::GetUltraRigidHeightTolerance() { EnsureCached(); return m_Cache.ultraRigidHeightTolerance; }
    float PseudoFPConfigManager::GetFurnitureExitGraceFrames() { EnsureCached(); return m_Cache.furnitureExitGraceFrames; }
    float PseudoFPConfigManager::GetPseudoFOV() { EnsureCached(); return m_Cache.pseudoFPPFOV; }
    uint32_t PseudoFPConfigManager::GetHeadCacheGraceFrames() { EnsureCached(); return m_Cache.headCacheGraceFrames; }
    bool PseudoFPConfigManager::IsUltraRigidEnabled() { EnsureCached(); return m_Cache.ultraRigidHeadAttach; }
    bool PseudoFPConfigManager::IsUltraRigidIgnoreSanityEnabled() { EnsureCached(); return m_Cache.ultraRigidIgnoreSanity; }

    void PseudoFPConfigManager::GetOffsets(float& outX, float& outY, float& outZ)
    {
        EnsureCached();
        outX = m_Cache.offsetX;
        outY = m_Cache.offsetY;
        outZ = m_Cache.offsetZ;
    }

    void PseudoFPConfigManager::GetDebugOffsets(float& outX, float& outY, float& outZ)
    {
        EnsureCached();
        outX = m_Cache.debugOffsetX;
        outY = m_Cache.debugOffsetY;
        outZ = m_Cache.debugOffsetZ;
    }
}
