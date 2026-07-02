#pragma once

// ReplayLayer.h — SF_Telem Replay screen
//
// Plays back a recorded session. Reuses the exact Main dashboard (weapon photo +
// drivetrain photo with readout boxes, the three stat panels and the ESC plot
// tabs) via DashboardView, but the values come from the DataPlayer instead of
// live serial — so scrubbing/playing the transport animates the whole diagram.
//
// The transport itself is the engine TelemetryPanel (load / play / pause / seek),
// hosted here in a "Replay" window. The root puts the panel into Replay mode when
// this screen is activated.

#include <Cosmic.h>

namespace Workspace
{
    class TelemHub;

    class ReplayLayer : public Cosmic::Layer
    {
    public:
        explicit ReplayLayer(TelemHub* hub) : Cosmic::Layer("ReplayLayer"), m_Hub(hub) {}
        virtual ~ReplayLayer() override = default;

        virtual void OnAttach()      override;
        virtual void OnImGuiRender() override;

        // Force the Live Dashboard tab active for a few frames after a (re)dock.
        void RequestDashboardFocus() { m_DashFocusFrames = 3; }

    private:
        TelemHub* m_Hub = nullptr;

        Cosmic::Ref<Cosmic::Texture2D> m_WeaponTex;
        Cosmic::Ref<Cosmic::Texture2D> m_DrivetrainTex;

        int m_DashFocusFrames = 3;
    };

} // namespace Workspace
