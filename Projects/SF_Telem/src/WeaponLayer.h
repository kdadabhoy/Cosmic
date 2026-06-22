#pragma once

// WeaponLayer.h — SF_Telem screen 3
//
// Weapon-only view: the same weapon data boxes as the Main screen plus the
// predicted spin-up model panel and the weapon plot stack. Drivetrain is absent.

#include <Cosmic.h>

namespace Workspace
{
    class TelemHub;

    class WeaponLayer : public Cosmic::Layer
    {
    public:
        explicit WeaponLayer(TelemHub* hub) : Cosmic::Layer("WeaponLayer"), m_Hub(hub) {}
        virtual ~WeaponLayer() override = default;

        virtual void OnAttach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        void DrawWeaponPanel();
        void DrawModelPanel();
        void DrawPlots();
        void DrawTelemetry();

        TelemHub* m_Hub = nullptr;
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };
    };

} // namespace Workspace
