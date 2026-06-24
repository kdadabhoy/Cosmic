// SF_Telem.cpp — root manager. See SF_Telem.h for the overview.

#include "SF_Telem.h"
#include "MainLayer.h"
#include "DrivetrainLayer.h"
#include "WeaponLayer.h"
#include "layers/WorkspaceLayer.h"   // dock-port registration

#include <imgui.h>
#include <implot.h>

namespace Workspace
{
    SF_Telem::SF_Telem() : Cosmic::Layer("SF_Telem") {}

    // =========================================================================
    void SF_Telem::OnAttach()
    {
        CS_INFO("SF_Telem: Attaching root manager (drive + weapon).");

        Cosmic::FileSystem::SetActiveProject("SF_Telem");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        m_Hub.Init();

        m_Modes.push_back(std::make_shared<MainLayer>(&m_Hub));       // MODE_MAIN
        m_Modes.push_back(std::make_shared<DrivetrainLayer>());        // MODE_DRIVE
        m_Modes.push_back(std::make_shared<WeaponLayer>(&m_Hub));     // MODE_WEAPON

        for (auto& m : m_Modes) m->OnAttach();

        ApplyDockLayout(m_ActiveMode);
        CS_INFO("SF_Telem: {} screens attached.", m_Modes.size());
    }

    // =========================================================================
    void SF_Telem::OnDetach()
    {
        for (auto& m : m_Modes) m->OnDetach();
        m_Modes.clear();
        m_Hub.Shutdown();
        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("SF_Telem: Detached.");
    }

    // =========================================================================
    void SF_Telem::OnUpdate(float ts)
    {
        m_Hub.OnUpdate(ts);                       // serial + recorder + model + rings
        if (!m_Modes.empty())
            m_Modes[m_ActiveMode]->OnUpdate(ts);  // active screen renders its scene
    }

    void SF_Telem::OnFixedUpdate(float dt)
    {
        m_Hub.RecordFixed(dt);                    // continuous capture (all screens)
        if (!m_Modes.empty())
            m_Modes[m_ActiveMode]->OnFixedUpdate(dt);
    }

    // =========================================================================
    void SF_Telem::OnImGuiRender()
    {
        if (m_AppliedDockMode != m_ActiveMode)
            ApplyDockLayout(m_ActiveMode);

        DrawTopPanel();

        if (IsTelemetryScreen())
            m_Hub.DrawSerialPanel();

        if (!m_Modes.empty())
            m_Modes[m_ActiveMode]->OnImGuiRender();
    }

    // =========================================================================
    void SF_Telem::OnEvent(Cosmic::Event& e)
    {
        if (m_Modes.empty()) return;

        // Application events (resize, etc.) broadcast to all screens so inactive
        // cameras keep correct projection.
        if (e.IsInCategory(Cosmic::EventCategoryApplication))
        {
            for (auto& m : m_Modes) m->OnEvent(e);
            return;
        }
        if (e.Handled) return;
        m_Modes[m_ActiveMode]->OnEvent(e);
    }

    // =========================================================================
    // Top panel — always visible: mode selector, recording (telemetry screens),
    // and live-editable decode constants for all three ESCs.
    // =========================================================================
    void SF_Telem::DrawTopPanel()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Shear Force Telemetry");
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Screen selector ----
        ImGui::TextDisabled("Screen");
        for (int i = 0; i < MODE_COUNT; ++i)
        {
            if (i > 0) ImGui::SameLine();
            const bool active = (m_ActiveMode == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.45f, 1.0f));
            if (ImGui::Button(m_ModeNames[i], ImVec2(90, 0)))
                m_ActiveMode = i;
            if (active) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Recording (telemetry screens only) ----
        if (IsTelemetryScreen())
        {
            m_Hub.DrawRecordingControls();
            ImGui::Spacing();
            ImGui::Separator();
        }

        // ---- Live stats (the avg/max shown on every readout box) ----
        ImGui::SeparatorText("Live Stats");
        ImGui::SetNextItemWidth(110);
        ImGui::DragFloat("Avg window (s)", &m_Hub.StatsWindowSec(), 0.1f, 0.0f, 120.0f, "%.1f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Auto-reset max/avg every N seconds (0 = accumulate until manual reset).");
        if (ImGui::Button("Reset All Stats")) m_Hub.ResetStats();
        ImGui::Spacing();
        ImGui::Separator();

        // ---- Decode constants ----
        if (ImGui::CollapsingHeader("Decode Constants"))
        {
            DriveConfig&  d = m_Hub.DriveCfg();
            WeaponConfig& w = m_Hub.WeaponCfg();

            ImGui::SeparatorText("Drive (Right + Left)");
            ImGui::SetNextItemWidth(110); ImGui::InputInt  ("Poles##d", &d.Poles);
            if (d.Poles < 2) d.Poles = 2;
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##d",  &d.GearRatio, 0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Wheel dia (in)##d", &d.WheelDiameterIn, 0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Slip factor##d", &d.SlipFactor, 0, 0, "%.3f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Motor Kv##d",    &d.MotorKv, 0, 0, "%.0f");

            ImGui::SeparatorText("Weapon");
            ImGui::SetNextItemWidth(110); ImGui::InputInt  ("Poles##w", &w.Poles);
            if (w.Poles < 2) w.Poles = 2;
            bool wchg = false;
            ImGui::SetNextItemWidth(110); wchg |= ImGui::InputFloat("Gear ratio##w",  &w.GearRatio, 0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); wchg |= ImGui::InputFloat("Weapon dia (in)##w", &w.WeaponDiameterIn, 0, 0, "%.2f");
            if (wchg) m_Hub.MarkModelDirty();  // model uses weapon gear/dia
        }

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // =========================================================================
    // Dock layout per screen (re-applied when the screen changes).
    // =========================================================================
    void SF_Telem::ApplyDockLayout(int mode)
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws) { m_AppliedDockMode = mode; return; }

        ws->ClearDockWindows();
        ws->DockWindow("Project Inspector Top", Cosmic::DockPort::LeftTop);

        // Give the Main screen a taller bottom strip for the readout panels.
        ws->SetEdgeRatios(0.20f, 0.20f, 0.18f, mode == MODE_MAIN ? 0.30f : 0.22f);

        // The Main screen has no 3D scene — hide the empty Viewport tab so the
        // Live Dashboard owns the center outright. Other screens keep the viewport.
        ws->SetViewportVisible(mode != MODE_MAIN);

        switch (mode)
        {
        case MODE_MAIN:
            ws->DockWindow("Serial Link",            Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Live Dashboard",         Cosmic::DockPort::Center);       // CAD + readouts in the center
            ws->DockWindow("ESC Plots",              Cosmic::DockPort::RightTop);
            ws->DockWindow("Telemetry (drill-down)", Cosmic::DockPort::RightTop);     // tab with ESC Plots
            ws->DockWindow("Left Drive",             Cosmic::DockPort::BottomLeft);   // three equal readout panels
            ws->DockWindow("Weapon",                 Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Right Drive",            Cosmic::DockPort::BottomRight);
            break;

        case MODE_WEAPON:
            ws->DockWindow("Serial Link",              Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Weapon System",            Cosmic::DockPort::RightTop);
            ws->DockWindow("Weapon Model (Predicted)", Cosmic::DockPort::RightBottom);
            ws->DockWindow("Weapon Plots",             Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Telemetry (drill-down)",   Cosmic::DockPort::BottomRight);
            break;

        case MODE_DRIVE:
            ws->DockWindow("Drivetrain Inputs",    Cosmic::DockPort::LeftMiddle);
            ws->DockWindow("Drivetrain Results",   Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Performance Curves",   Cosmic::DockPort::RightTop);
            ws->DockWindow("Drivetrain KPIs",      Cosmic::DockPort::RightBottom);
            ws->DockWindow("Drivetrain Explorers", Cosmic::DockPort::BottomCenter);
            break;
        }

        // The dashboard is tabbed with the central Viewport — make it the active
        // tab once the Main layout is (re)applied.
        if (mode == MODE_MAIN && !m_Modes.empty())
            static_cast<MainLayer*>(m_Modes[MODE_MAIN].get())->RequestDashboardFocus();

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
        return new Workspace::SF_Telem();
    }
}
