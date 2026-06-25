#pragma once

// SF_Telem.h
//
// ============================================================================
// SF_Telem — combined drive + weapon telemetry application (root manager)
// ============================================================================
//
// One ESP32 streams telemetry for THREE ESCs (2 drive + 1 weapon). This root
// layer owns a single shared TelemHub (serial + decode + recorder) and three
// screens, switching between them like the Template Project's mode manager:
//
//   * Main      — weapon + drivetrain data boxes, per-ESC plot tabs, recording.
//   * Drivetrain— the SF_DrivetrainCalcs calculator.
//   * Weapon    — weapon data boxes + predicted spin-up model.
//
// The hub is shared so the serial connection and recording persist across
// screen switches. Each screen registers its windows into the engine's dock
// ports when it becomes active.
// ============================================================================

#include <Cosmic.h>
#include "TelemHub.h"

#include <memory>
#include <vector>

namespace Workspace
{
    class SF_Telem : public Cosmic::Layer
    {
    public:
        SF_Telem();
        virtual ~SF_Telem() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnFixedUpdate(float dt)   override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

        enum Mode { MODE_MAIN = 0, MODE_DRIVE = 1, MODE_WEAPON = 2, MODE_COUNT = 3 };

    private:
        void DrawTopPanel();          // mode selector + recording + decode constants
        void ApplyDockLayout(int mode);
        bool IsTelemetryScreen() const { return m_ActiveMode == MODE_MAIN || m_ActiveMode == MODE_WEAPON; }

        TelemHub m_Hub;

        std::vector<std::shared_ptr<Cosmic::Layer>> m_Modes;
        int m_ActiveMode      = MODE_MAIN;
        int m_AppliedDockMode = -1;

        bool m_PolesAsPairs = false;  // Decode Constants: enter motor count as pole pairs vs. poles

        const char* m_ModeNames[MODE_COUNT] = { "Main", "Drivetrain", "Weapon" };
    };

} // namespace Workspace
