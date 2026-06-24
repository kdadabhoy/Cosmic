#pragma once

// SF_TelemTest.h
// ============================================================================
// SF_TelemTest — bench-test companion to SF_Telem (root manager).
// ============================================================================
//
// Four focused test screens, each backed by its own Arduino sketch, sharing one
// serial connection through TestHub:
//
//   * Single Drive  — one drive ESC streaming valid telemetry?
//   * Single Weapon — the weapon ESC streaming valid telemetry?
//   * Dual Drive    — both drive ESCs at once?
//   * Sniffer       — ANY bytes on the telemetry wires (valid or not)?
//
// Same wire protocol + decode as SF_Telem, same dock-port layout style.
// ============================================================================

#include <Cosmic.h>
#include "TestHub.h"

#include <memory>
#include <vector>

namespace Workspace
{
    class SF_TelemTest : public Cosmic::Layer
    {
    public:
        SF_TelemTest();
        virtual ~SF_TelemTest() override = default;

        virtual void OnAttach()      override;
        virtual void OnDetach()      override;
        virtual void OnUpdate(float ts) override;
        virtual void OnImGuiRender() override;

        enum Mode { MODE_DRIVE = 0, MODE_WEAPON = 1, MODE_DUAL = 2, MODE_SNIFF = 3, MODE_COUNT = 4 };

    private:
        void DrawTopPanel();
        void ApplyDockLayout(int mode);

        TestHub m_Hub;

        std::vector<std::shared_ptr<Cosmic::Layer>> m_Modes;
        int m_ActiveMode      = MODE_DRIVE;
        int m_AppliedDockMode = -1;

        const char* m_ModeNames[MODE_COUNT]   = { "Single Drive", "Single Weapon", "Dual Drive", "Sniffer" };
        const char* m_WindowNames[MODE_COUNT] = { "Drive ESC Test", "Weapon ESC Test", "Dual Drive Test", "Telem Sniffer" };
    };

} // namespace Workspace
