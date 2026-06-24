// SF_TelemTest.cpp — root manager. See SF_TelemTest.h.

#include "SF_TelemTest.h"
#include "TestLayers.h"
#include "layers/WorkspaceLayer.h"   // dock-port registration

#include <imgui.h>
#include <implot.h>

namespace Workspace
{
    SF_TelemTest::SF_TelemTest() : Cosmic::Layer("SF_TelemTest") {}

    // =========================================================================
    void SF_TelemTest::OnAttach()
    {
        CS_INFO("SF_TelemTest: Attaching bench-test manager.");

        Cosmic::FileSystem::SetActiveProject("SF_TelemTest");
        m_Hub.Init();

        m_Modes.push_back(std::make_shared<DriveSingleLayer>(&m_Hub));   // MODE_DRIVE
        m_Modes.push_back(std::make_shared<WeaponSingleLayer>(&m_Hub));  // MODE_WEAPON
        m_Modes.push_back(std::make_shared<DualDriveLayer>(&m_Hub));     // MODE_DUAL
        m_Modes.push_back(std::make_shared<SnifferLayer>(&m_Hub));       // MODE_SNIFF

        for (auto& m : m_Modes) m->OnAttach();

        ApplyDockLayout(m_ActiveMode);
        CS_INFO("SF_TelemTest: {} test screens attached.", m_Modes.size());
    }

    // =========================================================================
    void SF_TelemTest::OnDetach()
    {
        for (auto& m : m_Modes) m->OnDetach();
        m_Modes.clear();
        m_Hub.Shutdown();
        CS_INFO("SF_TelemTest: Detached.");
    }

    // =========================================================================
    void SF_TelemTest::OnUpdate(float ts)
    {
        m_Hub.OnUpdate(ts);   // serial pump + rate windows (shared across screens)
    }

    // =========================================================================
    void SF_TelemTest::OnImGuiRender()
    {
        if (m_AppliedDockMode != m_ActiveMode)
            ApplyDockLayout(m_ActiveMode);

        DrawTopPanel();
        m_Hub.DrawSerialPanel();

        if (!m_Modes.empty())
            m_Modes[m_ActiveMode]->OnImGuiRender();
    }

    // =========================================================================
    // Top panel — test selector, reset, decode constants, FPS.
    // =========================================================================
    void SF_TelemTest::DrawTopPanel()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Shear Force Telemetry - TEST BENCH");
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Test selector (2 per row so it fits the narrow column) ----
        ImGui::TextDisabled("Test");
        const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        for (int i = 0; i < MODE_COUNT; ++i)
        {
            if (i % 2 != 0) ImGui::SameLine();
            const bool active = (m_ActiveMode == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.45f, 1.0f));
            if (ImGui::Button(m_ModeNames[i], ImVec2(bw, 0))) m_ActiveMode = i;
            if (active) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset Counts", ImVec2(-1, 0))) m_Hub.ResetCounts();

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Decode constants (so RPM/speed sanity-check correctly) ----
        if (ImGui::CollapsingHeader("Decode Constants"))
        {
            DriveConfig&  d = m_Hub.DriveCfg();
            WeaponConfig& w = m_Hub.WeaponCfg();

            ImGui::SeparatorText("Drive (Right + Left)");
            ImGui::SetNextItemWidth(110); ImGui::InputInt  ("Pole pairs##d", &d.PolePairs);
            if (d.PolePairs < 1) d.PolePairs = 1;
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##d",     &d.GearRatio,       0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Wheel dia (in)##d", &d.WheelDiameterIn, 0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Slip factor##d",    &d.SlipFactor,      0, 0, "%.3f");

            ImGui::SeparatorText("Weapon");
            ImGui::SetNextItemWidth(110); ImGui::InputInt  ("Pole pairs##w", &w.PolePairs);
            if (w.PolePairs < 1) w.PolePairs = 1;
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##w",      &w.GearRatio,       0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Weapon dia (in)##w", &w.WeaponDiameterIn, 0, 0, "%.2f");
        }

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // =========================================================================
    // Dock layout — same shell for every test: top + serial on the left, the
    // active test window owns the center (no 3D viewport).
    // =========================================================================
    void SF_TelemTest::ApplyDockLayout(int mode)
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws) { m_AppliedDockMode = mode; return; }

        ws->ClearDockWindows();
        ws->SetViewportVisible(false);  // bench app has no 3D scene

        ws->DockWindow("Project Inspector Top", Cosmic::DockPort::LeftTop);
        ws->DockWindow("Serial Link",           Cosmic::DockPort::LeftBottom);
        ws->DockWindow(m_WindowNames[mode],     Cosmic::DockPort::Center);

        m_AppliedDockMode = mode;
    }

} // namespace Workspace

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
// =============================================================================
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::SF_TelemTest();
    }
}
