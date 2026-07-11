#include "starfield/StarfieldSF.h"
#include "systems/Config.h"
#include "plugin.h"
#include "starfield/Hooks.h"
#include <fstream>
#include <ctime>
#include <cstdarg>
#include <cstdlib>

namespace Patch {

    StarfieldSF::StarfieldSF() {}

    bool StarfieldSF::Load(Systems::Config* config)
    {
        if (m_Loaded)
            return true;

        LogFormatted("StarfieldSF::Load begin config=%p", static_cast<void*>(config));

        // Step 1: Addresses
        m_Addresses = std::make_unique<Addresses>();
        if (!m_Addresses->Load()) {
            LogFormatted("Addresses::Load failed");
            return false;
        }
        LogFormatted("Addresses::Load ok");

        // Step 2: Hooks (sets up TESCamera::Update hook from vtable)
        m_Hooks = std::make_unique<Hooks>();
        if (!m_Hooks->Setup()) {
            LogFormatted("Hooks::Setup failed");
            return false;
        }
        LogFormatted("Hooks::Setup ok");

        // Step 3: ImprovedCameraSF
        m_ImprovedCamera = std::make_unique<ImprovedCameraSF>();
        if (!m_ImprovedCamera->Load(config->General())) {
            LogFormatted("ImprovedCameraSF::Load failed");
            return false;
        }
        LogFormatted("ImprovedCameraSF::Load ok");

        // Step 5: EventsStarfield (permanent task - overwrites currentState + zoom)
        m_Events = std::make_unique<EventsStarfield>();
        if (!m_Events->Setup()) {
            LogFormatted("EventsStarfield::Setup failed");
            return false;
        }
        LogFormatted("EventsStarfield::Setup ok");

        m_Loaded = true;
        LogFormatted("StarfieldSF::Load success");
        return true;
    }
}
