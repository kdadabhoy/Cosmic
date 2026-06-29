// ReplayLayer.cpp — see ReplayLayer.h.

#include "ReplayLayer.h"
#include "DashboardView.h"
#include "TelemHub.h"
#include "Telemetry.h"   // ESC_LEFT / ESC_RIGHT

#include <imgui.h>

namespace Workspace
{
    void ReplayLayer::OnAttach()
    {
        // Same hardware photos as the Main dashboard.
        m_WeaponTex     = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Weapon.PNG"));
        m_DrivetrainTex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Drivetrain.PNG"));
    }

    void ReplayLayer::OnImGuiRender()
    {
        // Layout (docked by SF_Telem::ApplyDockLayout):
        //   right  = Live Dashboard (the real-time visual, replayed)
        //   left   = Stats (min/max/avg panels)
        //   center = ESC Plots, three equal R/L/W columns
        //   bottom = Replay transport (load + play/pause + time scrubber + speed)
        // All read replayed values because TelemHub feeds them from the DataPlayer.
        DashboardView::DrawDashboard(m_Hub, m_WeaponTex, m_DrivetrainTex, &m_DashFocusFrames);
        DashboardView::DrawStatsPanel(m_Hub);
        DashboardView::DrawPlotsTriple(m_Hub);

        // Transport only (no duplicate plots) — load/browse, play/pause, time
        // scrubber, speed. Playback advances via TelemHub::OnUpdate -> Panel.OnUpdate.
        ImGui::Begin("Replay");
        m_Hub->Panel().DrawTransport();
        ImGui::End();
    }

} // namespace Workspace
