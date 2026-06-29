#pragma once

// MainLayer.h — SF_Telem screen 1
//
// The Main telemetry screen: the live dashboard (weapon/drivetrain photos with
// readout boxes), the weapon + per-side drive stat panels, and per-ESC plot
// tabs (Right / Left / Weapon) — all shared with the Replay screen via
// DashboardView. All data comes from the shared TelemHub, so it stays robust if
// any ESC is unplugged. (The telemetry drill-down / replay UI is its own screen.)

#include <Cosmic.h>

namespace Workspace
{
    class TelemHub;

    class MainLayer : public Cosmic::Layer
    {
    public:
        explicit MainLayer(TelemHub* hub) : Cosmic::Layer("MainLayer"), m_Hub(hub) {}
        virtual ~MainLayer() override = default;

        virtual void OnAttach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

        // Make the Live Dashboard the active center tab for the next few frames
        // (the engine docks it tabbed with the Viewport; this selects it on load
        // and whenever we switch back to the Main screen).
        void RequestDashboardFocus() { m_DashFocusFrames = 3; }

    private:
        TelemHub* m_Hub = nullptr;
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };

        // Hardware photos overlaid with live readouts on the dashboard.
        Cosmic::Ref<Cosmic::Texture2D> m_WeaponTex;
        Cosmic::Ref<Cosmic::Texture2D> m_DrivetrainTex;

        // Frames remaining to force-focus the dashboard tab (see RequestDashboardFocus).
        int m_DashFocusFrames = 3;
    };

} // namespace Workspace
