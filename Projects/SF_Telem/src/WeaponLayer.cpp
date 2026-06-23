// WeaponLayer.cpp — SF_Telem screen 3. See WeaponLayer.h.

#include "WeaponLayer.h"
#include "TelemHub.h"
#include "StatBox.h"

#include <imgui.h>
#include <implot.h>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_WeaponColor = { 1.00f, 0.55f, 0.20f, 1.0f };
        const ImVec4 k_PredColor   = { 0.30f, 1.00f, 0.45f, 1.0f };
    }

    void WeaponLayer::OnAttach() { m_Camera.SetManualMovementEnabled(false); }

    void WeaponLayer::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::EndScene();
    }

    void WeaponLayer::OnEvent(Cosmic::Event& e) { m_Camera.OnEvent(e); }

    void WeaponLayer::OnImGuiRender()
    {
        DrawWeaponPanel();
        DrawModelPanel();
        DrawPlots();
        DrawTelemetry();
    }

    // -------------------------------------------------------------------------
    void WeaponLayer::DrawWeaponPanel()
    {
        ImGui::Begin("Weapon System");

        ImagePlaceholder("##wpnimg2", "[ weapon system — upload texture later ]",
                         ImVec2(-1.0f, 110.0f), k_WeaponColor);

        const int W = ESC_WEAPON;
        if (m_Hub->Present(W))
            ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f }, "LIVE");
        else if (m_Hub->HasData(W))
            ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, "STALE");
        else
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "NO SIGNAL — weapon ESC not detected");

        ImGui::Spacing();
        StatBox("##w2cur", "Current", m_Hub->Cur(W), "A", m_Hub->AvgCur(W), m_Hub->MaxCur(W), k_WeaponColor);
        ImGui::SameLine();
        StatBox("##w2volt", "Voltage", m_Hub->Volt(W), "V", m_Hub->AvgVolt(W), m_Hub->MaxVolt(W), k_WeaponColor);
        ImGui::SameLine();
        RpmBox("##w2rpm", "Weapon RPM", m_Hub->Rpm(W), m_Hub->PredictedRpm(W), m_Hub->AvgRpm(W), m_Hub->MaxRpm(W), k_WeaponColor);
        ImGui::SameLine();
        StatBox("##w2tip", "Tip speed", m_Hub->Tip(), "mph", m_Hub->AvgTip(), m_Hub->MaxTip(), k_WeaponColor);

        ImGui::Spacing();
        if (ImGui::Button("Reset Weapon Stats")) m_Hub->ResetMax(W);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Predicted spin-up model (ported from SF_Telem_Weapon), driven by the hub.
    // -------------------------------------------------------------------------
    void WeaponLayer::DrawModelPanel()
    {
        ImGui::Begin("Weapon Model (Predicted)");

        WeaponModelConfig& m = m_Hub->ModelCfg();
        const WeaponModelResult& r = m_Hub->ModelResult();
        WeaponConfig& w = m_Hub->WeaponCfg();

        ImGui::TextColored(k_PredColor, "Full-throttle spin-up prediction");
        ImGui::SameLine(); ImGui::TextDisabled("(motor torque vs aero drag)");
        ImGui::Separator();

        ImGui::SeparatorText("Inputs");
        bool d = false;

        bool live = m_Hub->ModelUsesLiveVoltage();
        if (ImGui::Checkbox("Use live battery voltage", &live)) m_Hub->SetModelUseLiveVoltage(live);
        ImGui::BeginDisabled(live);
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Battery voltage (V)", &m.BatteryVoltage, 0, 0, "%.2f");
        ImGui::EndDisabled();
        if (live)
        {
            ImGui::SameLine();
            if (m_Hub->Present(ESC_WEAPON)) ImGui::TextColored(k_WeaponColor, "live: %.2f V", m_Hub->ModelVoltageUsed());
            else                            ImGui::TextDisabled("(no live data -> %.2f V)", m_Hub->ModelVoltageUsed());
        }

        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Motor Kv (rpm/V)",      &m.MotorKv, 0, 0, "%.1f");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("No-load current (A)",   &m.NoLoadCurrent, 0, 0, "%.2f");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Max current (A)",       &m.MaxCurrent, 0, 0, "%.1f");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Inertia (kg m^2)",      &m.Inertia, 0, 0, "%.7f");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Drag coeff (Nm/rpm^2)", &m.DragCoeff, 0, 0, "%.3e");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputFloat("Sim duration (s)",      &m.SimDuration, 0, 0, "%.1f");
        ImGui::SetNextItemWidth(130); d |= ImGui::InputInt  ("Sim steps",             &m.SimSteps);
        if (m.SimSteps < 2) m.SimSteps = 2;
        ImGui::TextDisabled("Reduction = %.2f : 1 and tip dia = %.3f in come from Decode Constants.",
                            w.GearRatio, w.WeaponDiameterIn);
        if (d) m_Hub->MarkModelDirty();

        ImGui::SeparatorText("Derived");
        ImGui::Text("Weapon no-load : %.0f rpm    stall %.3f Nm", r.WeaponNoLoadRPM, r.WeaponStallTorque);
        ImGui::Text("t-w slope      : %.6f Nm/rpm", r.TWSlope);

        ImGui::SeparatorText("Predicted");
        ImGui::TextColored(k_PredColor, "Max tip speed  : %.1f mph", r.MaxTipSpeedMph);
        ImGui::TextColored(k_PredColor, "Max weapon RPM : %.0f rpm", r.MaxWeaponRPM);
        if (r.TimeTo90Pct >= 0.0f) ImGui::Text("Time to 90%%    : %.2f s", r.TimeTo90Pct);
        else                       ImGui::Text("Time to 90%%    : n/a (raise sim duration)");

        if (m_Hub->Present(ESC_WEAPON) && r.MaxTipSpeedMph > 0.0f)
        {
            const float pct = 100.0f * m_Hub->Tip() / r.MaxTipSpeedMph;
            ImGui::Separator();
            ImGui::Text("Live: %.1f mph tip   %.0f rpm   (%.0f%% of predicted)",
                        m_Hub->Tip(), m_Hub->Rpm(ESC_WEAPON), pct);
        }

        if (!r.t.empty())
        {
            const int n = (int)r.t.size();
            ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0f, r.t.back(), ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0f, r.MaxTipSpeedMph * 1.1f + 0.001f, ImPlotCond_Always);
            if (ImPlot::BeginPlot("Predicted Tip Speed (mph) vs Time (s)", ImVec2(-1.0f, 200.0f)))
            {
                ImPlotSpec spec; spec.LineColor = k_PredColor;
                ImPlot::PlotLine("Tip speed", r.t.data(), r.tipMph.data(), n, spec);
                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    void WeaponLayer::DrawPlots()
    {
        ImGui::Begin("Weapon Plots");
        m_Hub->DrawEscPlots(ESC_WEAPON);
        ImGui::End();
    }

    void WeaponLayer::DrawTelemetry()
    {
        ImGui::Begin("Telemetry (drill-down)");
        m_Hub->Panel().OnImGuiRender();
        ImGui::End();
    }

} // namespace Workspace
