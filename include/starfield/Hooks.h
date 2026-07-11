#pragma once

#include "starfield/HeadTracking.h"
#include "starfield/PseudoFP.h"
#include "systems/PseudoFPConfig.h"
#include "RE/T/TESCamera.h"
#include "RE/N/NiCamera.h"

namespace Patch {
    extern bool g_PseudoFPPActive;
    extern bool g_SAFAnimationPlaying;

    class Hooks {
    public:
        Hooks();
        ~Hooks() = default;

        Hooks(const Hooks&) = delete;
        Hooks& operator=(const Hooks&) = delete;
        Hooks(Hooks&&) = delete;
        Hooks& operator=(Hooks&&) = delete;

        bool Setup();
        bool Remove();

    private:
        bool m_Setup = false;
    };
}
