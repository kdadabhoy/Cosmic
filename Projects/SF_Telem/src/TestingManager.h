#pragma once

// TestingManager.h — the "Testing" screen of SF_Telem.
//
// A sub-router that owns the four bench-test layers (single drive / single
// weapon / dual drive / sniffer) and their TestHub, switching between them with
// its own selector. It shares the root's single SerialLink, so the connection
// persists when moving between Main / Testing / Replay.
//
// The root drives it: DrawInspector() supplies the test-specific controls inside
// the shared top panel, DrawScreen() renders the Serial Link panel + the active
// test window, and OnUpdate() pumps the hub.

#include <Cosmic.h>
#include "TestHub.h"

#include <memory>
#include <vector>

namespace Workspace
{
    class TestingManager
    {
    public:
        enum Mode { MODE_DRIVE = 0, MODE_WEAPON = 1, MODE_DUAL = 2, MODE_SNIFF = 3, MODE_COUNT = 4 };

        void Init(Cosmic::SerialLink* link);  // create layers + hub + attach
        void Shutdown();
        void OnUpdate(float ts);               // serial pump + rate windows

        // Test-mode selector + reset + decode constants — drawn by the root inside
        // the shared "Project Inspector Top" window.
        void DrawInspector();

        // Serial Link panel (incl. firmware copy) + the active test window.
        void DrawScreen();

        int         ActiveMode()       const { return m_ActiveMode; }
        const char* ActiveWindowName() const { return m_WindowNames[m_ActiveMode]; }

    private:
        TestHub m_Hub;

        std::vector<std::shared_ptr<Cosmic::Layer>> m_Layers;
        int  m_ActiveMode  = MODE_DRIVE;
        bool m_PolesAsPairs = false;

        const char* m_ModeNames[MODE_COUNT]   = { "Single Drive", "Single Weapon", "Dual Drive", "Sniffer" };
        const char* m_WindowNames[MODE_COUNT] = { "Drive ESC Test", "Weapon ESC Test", "Dual Drive Test", "Telem Sniffer" };
    };

} // namespace Workspace
