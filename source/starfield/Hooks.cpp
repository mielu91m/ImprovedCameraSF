#include "starfield/Hooks.h"
#include "starfield/PseudoFP.h"
#include "starfield/SAFIntegration.h"
#include "systems/PseudoFPConfig.h"
#include "plugin.h"
#include "REL/Relocation.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiNode.h"
#include "RE/IDs_VTABLE.h"
#include <MinHook.h>

namespace Patch {

    Hooks::Hooks() {}

    bool Hooks::Setup()
    {
        if (m_Setup)
            return true;
        if (MH_Initialize() != MH_OK)
            return false;

        auto vtabAddr = REL::ID(430192).address();
        auto funcAddr = *reinterpret_cast<void**>(vtabAddr + 3 * 8);
        if (funcAddr) {
            MH_CreateHook(funcAddr, (void*)DetourUpdate, (void**)&origUpdate);
            MH_EnableHook(funcAddr);
        }

        auto tpsVtabAddr = RE::VTABLE::ThirdPersonState[0].address();
        auto tpsFuncAddr = *reinterpret_cast<void**>(tpsVtabAddr + 12 * 8);
        if (tpsFuncAddr) {
            MH_CreateHook(tpsFuncAddr, (void*)DetourTPSUpdate, (void**)&origTPSUpdate);
            MH_EnableHook(tpsFuncAddr);
        }

        auto fpsVtabAddr = RE::VTABLE::FirstPersonState[0].address();
        auto fpsFuncAddr = *reinterpret_cast<void**>(fpsVtabAddr + 12 * 8);
        if (fpsFuncAddr) {
            MH_CreateHook(fpsFuncAddr, (void*)DetourFPSUpdate, (void**)&g_origFPSUpdate);
            MH_EnableHook(fpsFuncAddr);
            LogFormatted("FirstPersonState::Update hooked at %p (vtable %p, index 12)", fpsFuncAddr, (void*)fpsVtabAddr);
        } else {
            LogFormatted("ERROR: Could not find FirstPersonState::Update function address");
        }

        auto scsAddr = REL::ID(113375).address();
        if (scsAddr) {
            MH_CreateHook(reinterpret_cast<void*>(scsAddr), (void*)DetourSetCameraState, (void**)&g_origSetCameraState);
            MH_EnableHook(reinterpret_cast<void*>(scsAddr));
            LogFormatted("SetCameraState hooked at %p", reinterpret_cast<void*>(scsAddr));
        } else {
            LogFormatted("ERROR: Could not find SetCameraState address");
        }

        auto avObjVtabAddr = RE::VTABLE::NiAVObject[0].address();
        auto uwFuncAddr = *reinterpret_cast<void**>(avObjVtabAddr + 78 * 8);
        if (uwFuncAddr) {
            MH_CreateHook(uwFuncAddr, (void*)DetourUpdateWorldData, (void**)&g_origUpdateWorldData);
            MH_EnableHook(uwFuncAddr);
            LogFormatted("UpdateWorldData hooked at %p (vtable %p, index 78)", uwFuncAddr, (void*)avObjVtabAddr);
        } else {
            LogFormatted("ERROR: Could not find UpdateWorldData function address");
        }

        auto utbFuncAddr = *reinterpret_cast<void**>(avObjVtabAddr + 79 * 8);
        if (utbFuncAddr) {
            MH_CreateHook(utbFuncAddr, (void*)DetourUpdateTransformAndBounds, (void**)&g_origUpdateTransformAndBounds);
            MH_EnableHook(utbFuncAddr);
            LogFormatted("UpdateTransformAndBounds hooked at %p (vtable %p, index 79)", utbFuncAddr, (void*)avObjVtabAddr);
        } else {
            LogFormatted("ERROR: Could not find UpdateTransformAndBounds function address");
        }

        auto utFuncAddr = *reinterpret_cast<void**>(avObjVtabAddr + 80 * 8);
        if (utFuncAddr) {
            MH_CreateHook(utFuncAddr, (void*)DetourUpdateTransforms, (void**)&g_origUpdateTransforms);
            MH_EnableHook(utFuncAddr);
            LogFormatted("UpdateTransforms hooked at %p (vtable %p, index 80)", utFuncAddr, (void*)avObjVtabAddr);
        } else {
            LogFormatted("ERROR: Could not find UpdateTransforms function address");
        }

        m_Setup = true;
        return true;
    }

    bool Hooks::Remove()
    {
        if (!m_Setup)
            return true;
        MH_Uninitialize();
        m_Setup = false;
        return true;
    }
}
