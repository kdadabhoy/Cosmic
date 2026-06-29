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
        // The diagram + panels are identical to Main; they just read replayed
        // values because TelemHub is feeding them from the DataPlayer.
        DashboardView::DrawDashboard(m_Hub, m_WeaponTex, m_DrivetrainTex, &m_DashFocusFrames);
        DashboardView::DrawWeaponReadouts(m_Hub);
        DashboardView::DrawDriveReadouts(m_Hub, ESC_LEFT,  "Left Drive");
        DashboardView::DrawDriveReadouts(m_Hub, ESC_RIGHT, "Right Drive");
        DashboardView::DrawPlots(m_Hub);

        // Transport: load a recording, play / pause / seek. The hub watches the
        // player position and pushes frames into the dashboard each update.
        ImGui::Begin("Replay");
        m_Hub->Panel().OnImGuiRender();
        ImGui::End();
    }

} // namespace Workspace
