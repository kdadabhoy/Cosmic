#pragma once

// SF_Telem.h
//
// ============================================================================
// SF_Telem — the combined Shear Force telemetry application (root manager).
// ============================================================================
//
// One ESP32 streams telemetry for THREE ESCs (2 drive + 1 weapon). This root
// layer presents a homescreen (Minecraft-style tile menu) that selects between
// four screens, all sharing ONE serial connection so it persists across switches:
//
//   * Main Telemetry — live dashboard (weapon/drivetrain photos + readout boxes),
//                      per-side stat panels, ESC plots, recording.
//   * Testing        — the four bench tests (single drive / single weapon /
//                      dual drive / sniffer), via TestingManager.
//   * Analysis       — the drivetrain spin-up calculator.
//   * Replay         — load a recording and scrub it; the dashboard diagram
//                      animates with the playback position.
//
// A "Home" button on the shared top bar returns to the homescreen from anywhere.
// ============================================================================

#include <Cosmic.h>

#include "TelemHub.h"
#include "TestingManager.h"

#include <memory>

namespace Workspace
{
    class MainLayer;
    class DrivetrainLayer;
    class ReplayLayer;

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

        enum Screen { SCREEN_HOME = 0, SCREEN_MAIN, SCREEN_TESTING, SCREEN_ANALYSIS, SCREEN_REPLAY, SCREEN_COUNT };

    private:
        void SetScreen(Screen s);
        void DrawHomescreen();
        void DrawTopPanel();          // Home + screen tabs + screen-specific controls
        void ApplyDockLayout();
        int  DockStateKey() const;    // screen (+ testing sub-mode) -> re-dock trigger

        bool UsesTelemHub() const { return m_Screen == SCREEN_MAIN || m_Screen == SCREEN_REPLAY; }

        // One shared serial connection for the whole app (root-owned).
        Cosmic::SerialLink m_Link;

        // Telemetry backbone (Main + Replay) and the testing sub-router.
        TelemHub       m_TelemHub;
        TestingManager m_Testing;

        std::shared_ptr<MainLayer>       m_Main;
        std::shared_ptr<DrivetrainLayer> m_Analysis;
        std::shared_ptr<ReplayLayer>     m_Replay;

        Screen m_Screen     = SCREEN_HOME;
        int    m_AppliedDock = -1;

        bool m_PolesAsPairs = false;  // Decode Constants: enter motor count as poles vs. pairs

        // Homescreen tile art (the existing hardware photos).
        Cosmic::Ref<Cosmic::Texture2D> m_WeaponTex;
        Cosmic::Ref<Cosmic::Texture2D> m_DrivetrainTex;
    };

} // namespace Workspace
