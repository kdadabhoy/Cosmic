#pragma once

// MainLayer.h — SF_Telem screen 1
//
// The landing screen: a weapon data-box panel and a drivetrain data-box panel
// (live value + running max + reset, nothing animated), plus per-ESC plot tabs
// (Right / Left / Weapon) and the telemetry drill-down. All data comes from the
// shared TelemHub, so it stays robust if any ESC is unplugged.

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

    private:
        void DrawWeaponPanel();
        void DrawDrivetrainPanel();
        void DrawPlots();
        void DrawTelemetry();

        TelemHub* m_Hub = nullptr;
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };
    };

} // namespace Workspace
