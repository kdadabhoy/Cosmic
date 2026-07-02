// MainLayer.cpp — SF_Telem Main screen. See MainLayer.h.
//
// The live dashboard (weapon/drivetrain photos + readout boxes), the three
// stat-box panels and the ESC plot tabs are all shared with the Replay screen,
// so they live in DashboardView and are simply driven here from live serial.
// The telemetry drill-down / replay panel now lives on its own Replay screen.

#include "MainLayer.h"
#include "DashboardView.h"
#include "Telemetry.h"   // ESC_LEFT / ESC_RIGHT

#include <imgui.h>

namespace Workspace
{
    void MainLayer::OnAttach()
    {
        m_Camera.SetManualMovementEnabled(false);

        // Hardware photos for the live dashboard (assets are synced to the VFS by
        // the project's CMake POST_BUILD step; project:// resolves to that folder).
        m_WeaponTex     = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Weapon.PNG"));
        m_DrivetrainTex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Drivetrain.PNG"));
    }

    void MainLayer::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        // No 2D visual on this screen — render an empty scene to keep the
        // viewport cleanly cleared each frame.
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::EndScene();
    }

    void MainLayer::OnEvent(Cosmic::Event& e) { m_Camera.OnEvent(e); }

    void MainLayer::OnImGuiRender()
    {
        DashboardView::DrawDashboard(m_Hub, m_WeaponTex, m_DrivetrainTex, &m_DashFocusFrames);
        DashboardView::DrawWeaponReadouts(m_Hub);
        DashboardView::DrawDriveReadouts(m_Hub, ESC_LEFT,  "Left Drive");
        DashboardView::DrawDriveReadouts(m_Hub, ESC_RIGHT, "Right Drive");

        // Same "ESC Plots" window, two layouts: tabbed (Dashboard view) or three
        // equal R/L/W columns (Plots view). The dock layout positions it per view.
        if (m_PlotsView) DashboardView::DrawPlotsTriple(m_Hub);
        else             DashboardView::DrawPlots(m_Hub);
    }

} // namespace Workspace
