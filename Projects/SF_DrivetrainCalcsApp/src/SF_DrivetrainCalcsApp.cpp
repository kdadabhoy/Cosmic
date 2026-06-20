// SF_DrivetrainCalcsApp.cpp
//
// Interactive Shear Force drivetrain calculator. See the header for the
// overview; all physics lives in DrivetrainModel.h.

#include "SF_DrivetrainCalcsApp.h"

#include <imgui.h>
#include <implot.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace Workspace
{
    namespace
    {
        // Shared palette: front = orange, rear = teal. Used by both the charts
        // and the viewport schematic so the two axles are always recognisable.
        const ImVec4 k_FrontCol = { 1.00f, 0.62f, 0.20f, 1.0f };
        const ImVec4 k_RearCol  = { 0.30f, 0.80f, 0.85f, 1.0f };
        const ImVec4 k_AccentCol = { 0.45f, 1.00f, 0.55f, 1.0f };

        glm::vec4 ToVec4(const ImVec4& c) { return { c.x, c.y, c.z, c.w }; }

        // Helper: min/max over a column (returns false if empty).
        bool MinMax(const std::vector<double>& v, double& lo, double& hi)
        {
            if (v.empty()) return false;
            lo = hi = v.front();
            for (double x : v) { lo = std::min(lo, x); hi = std::max(hi, x); }
            return true;
        }
    }

    // =========================================================================
    SF_DrivetrainCalcsApp::SF_DrivetrainCalcsApp()
        : Cosmic::Layer("SF_DrivetrainCalcsApp")
    {
    }

    // =========================================================================
    void SF_DrivetrainCalcsApp::OnAttach()
    {
        CS_INFO("SF_DrivetrainCalcsApp: Attaching drivetrain calculator.");

        Cosmic::FileSystem::SetActiveProject("SF_DrivetrainCalcsApp");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        // The viewport is a small fixed schematic stage — no manual panning.
        m_Camera.SetManualMovementEnabled(false);
        m_Camera.SetZoomLimits(0.05f, 5.0f);
        m_Camera.SetZoomLevel(0.22f);

        Recompute();

        CS_INFO("SF_DrivetrainCalcsApp: OnAttach complete.");
    }

    // =========================================================================
    void SF_DrivetrainCalcsApp::OnDetach()
    {
        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("SF_DrivetrainCalcsApp: Detached.");
    }

    // =========================================================================
    void SF_DrivetrainCalcsApp::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        RenderSchematic();
    }

    // =========================================================================
    void SF_DrivetrainCalcsApp::Recompute()
    {
        m_Sim   = Simulate(m_Cfg);
        m_Dirty = false;
    }

    // =========================================================================
    // OnImGuiRender — inputs first (which may dirty the sim), recompute, then
    // draw everything that reads the result.
    // =========================================================================
    void SF_DrivetrainCalcsApp::OnImGuiRender()
    {
        // Recompute from last frame's edits before any panel reads the result.
        if (m_AutoRun && m_Dirty)
            Recompute();

        // Inspector first: it lives in the engine's left "Project Inspector Top"
        // slot, so after drawing it we know that dock node and can tab every
        // other panel into it (FirstUseEver = the user can drag them out later).
        DrawInspectorTop();

        auto dockLeft = [&]() { if (m_LeftDockId) ImGui::SetNextWindowDockID(m_LeftDockId, ImGuiCond_FirstUseEver); };

        dockLeft(); DrawInputsWindow();
        dockLeft(); DrawPlotsWindow();
        dockLeft(); DrawResultsWindow();
        dockLeft(); DrawExplorersWindow();

        DrawSchematicLabels();
    }

    // -------------------------------------------------------------------------
    // Inspector Top (docked sidebar) — headline KPIs + recompute + sync banner.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawInspectorTop()
    {
        ImGui::Begin("Project Inspector Top");

        // Remember the engine's left dock node so the other panels can tab in.
        m_LeftDockId = ImGui::GetWindowDockID();

        ImGui::TextColored(k_AccentCol, "Shear Force  |  Drivetrain Calculator");
        ImGui::TextDisabled("Rapid wheel / pulley / battery prototyping");
        ImGui::Separator();
        ImGui::Spacing();

        // --- Recompute controls ---
        ImGui::Checkbox("Auto-recompute", &m_AutoRun);
        ImGui::SameLine();
        if (ImGui::Button("Recompute now"))
            Recompute();

        if (m_Dirty && !m_AutoRun)
            ImGui::TextColored({ 1.0f, 0.75f, 0.1f, 1.0f }, "  Inputs changed — stale results.");

        ImGui::Spacing();
        ImGui::Separator();

        if (!m_Sim.valid)
        {
            ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "Invalid configuration:");
            ImGui::TextWrapped("%s", m_Sim.error.c_str());
            ImGui::End();
            return;
        }

        // --- Headline KPIs (front axle = primary, matching the original tool) ---
        const AxleResult& f = m_Sim.front;
        ImGui::SeparatorText("Front axle — key numbers");

        ImGui::TextColored(k_FrontCol, "Top speed");
        ImGui::SameLine(140); ImGui::Text("%.2f mph", f.topSpeedMph);

        ImGui::TextColored(k_FrontCol, "No-load ceiling");
        ImGui::SameLine(140); ImGui::Text("%.2f mph", f.noLoadTopSpeedMph);

        ImGui::TextColored(k_FrontCol, "Peak accel");
        ImGui::SameLine(140); ImGui::Text("%.2f g", f.peakAccelG);

        ImGui::TextColored(k_FrontCol, "Launch force");
        ImGui::SameLine(140); ImGui::Text("%.1f N", f.launchForceN);

        ImGui::TextColored(k_FrontCol, "Traction cap");
        ImGui::SameLine(140); ImGui::Text("%.1f N", f.tractionLimitN);

        ImGui::TextColored(k_FrontCol, "Distance");
        ImGui::SameLine(140); ImGui::Text("%.1f ft", f.distanceFt);

        ImGui::Spacing();
        if (f.launchTractionLimited)
        {
            ImGui::TextColored({ 1.0f, 0.55f, 0.2f, 1.0f }, "  Launch is TRACTION-LIMITED");
            if (f.timeToReleaseTraction >= 0.0)
                ImGui::TextDisabled("  grips until %.3f s, then motor-limited", f.timeToReleaseTraction);
            else
                ImGui::TextDisabled("  wheels at the traction cap the whole run");
        }
        else
        {
            ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "  Launch is MOTOR-LIMITED (no wheelspin)");
        }

        // --- Tangential-velocity feasibility: is the build even possible? ---
        ImGui::Spacing();
        ImGui::SeparatorText("Feasibility — wheel tangential velocity");
        const bool feasible = m_Sim.tangMismatchPct <= m_FeasTolPct;
        if (feasible)
            ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f },
                               "  POSSIBLE — wheels track (%.2f%% gap)", m_Sim.tangMismatchPct);
        else
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f },
                               "  IMPOSSIBLE — wheels slip/fight (%.2f%% gap)", m_Sim.tangMismatchPct);
        ImGui::TextDisabled("  Front surface %.2f mph  vs  rear %.2f mph",
                            m_Sim.vTangFrontMph, m_Sim.vTangRearMph);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputDouble("Tolerance (%)", &m_FeasTolPct, 0.1, 1.0, "%.2f") && m_FeasTolPct < 0.0)
            m_FeasTolPct = 0.0;
        ImGui::TextDisabled("  Fixes in: Drivetrain Explorers > Feasibility");

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.0f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Inputs — every editable parameter, grouped. Each edit dirties the sim.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawInputsWindow()
    {
        ImGui::Begin("Drivetrain Inputs");

        bool changed = false;
        const float w = 130.0f;
        auto Dbl = [&](const char* label, double* v, double step, const char* fmt)
        {
            ImGui::SetNextItemWidth(w);
            changed |= ImGui::InputDouble(label, v, step, step * 10.0, fmt);
        };

        ImGui::SeparatorText("Simulation");
        Dbl("Total time (s)", &m_Cfg.simTime, 0.5, "%.2f");
        Dbl("Time step (s)",  &m_Cfg.dt,      0.005, "%.4f");

        ImGui::SeparatorText("Reduction");
        Dbl("Gearbox reduction", &m_Cfg.gearboxReduction, 1.0, "%.2f");

        ImGui::Spacing();
        ImGui::TextColored(k_FrontCol, "Front axle");
        Dbl("Wheel dia (in)##f",     &m_Cfg.frontWheelInches, 0.25, "%.2f");
        Dbl("Wheel pulley teeth##f", &m_Cfg.frontPulleyTeeth, 1.0,  "%.0f");
        Dbl("Motor pulley teeth##f", &m_Cfg.motorPulleyFront, 1.0,  "%.0f");

        ImGui::Spacing();
        ImGui::TextColored(k_RearCol, "Rear axle");
        Dbl("Wheel dia (in)##r",     &m_Cfg.rearWheelInches, 0.25, "%.2f");
        Dbl("Wheel pulley teeth##r", &m_Cfg.rearPulleyTeeth, 1.0,  "%.0f");
        Dbl("Motor pulley teeth##r", &m_Cfg.motorPulleyRear, 1.0,  "%.0f");

        ImGui::SeparatorText("Efficiency");
        Dbl("Speed factor",          &m_Cfg.speedFactor,          0.01, "%.3f");
        Dbl("Drivetrain efficiency", &m_Cfg.drivetrainEfficiency, 0.01, "%.3f");

        ImGui::SeparatorText("Battery / Motor");
        Dbl("Motor Kv (rpm/V)",   &m_Cfg.kv,           50.0, "%.0f");
        Dbl("Current limit (A)",  &m_Cfg.currentLimit, 5.0,  "%.1f");
        Dbl("Battery voltage (V)",&m_Cfg.vBatt,        0.5,  "%.2f");

        ImGui::SeparatorText("Constants");
        Dbl("Coeff. of friction", &m_Cfg.mu,           0.05, "%.3f");
        Dbl("Total weight (lb)",  &m_Cfg.totalWeightLb,1.0,  "%.2f");

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset to template defaults"))
        {
            m_Cfg = DrivetrainConfig{};
            changed = true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Overlay rear", &m_ShowRear);
        ImGui::SameLine();
        ImGui::Checkbox("Shade area", &m_FillCurves);

        if (changed) m_Dirty = true;

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // One performance curve (front solid + optional rear overlay), auto-framed.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::PlotChannel(const char* title, const char* yLabel,
                                            const std::vector<double>& fX,
                                            const std::vector<double>& fY,
                                            const std::vector<double>& rX,
                                            const std::vector<double>& rY,
                                            float height)
    {
        // X across both, Y across both (when rear overlay is on).
        double xLo = 0.0, xHi = 1.0, yLo = 0.0, yHi = 1.0;
        double a, b;
        bool haveX = false, haveY = false;
        if (MinMax(fX, a, b)) { xLo = a; xHi = b; haveX = true; }
        if (MinMax(fY, a, b)) { yLo = a; yHi = b; haveY = true; }
        if (m_ShowRear)
        {
            if (MinMax(rX, a, b)) { xLo = haveX ? std::min(xLo, a) : a; xHi = haveX ? std::max(xHi, b) : b; haveX = true; }
            if (MinMax(rY, a, b)) { yLo = haveY ? std::min(yLo, a) : a; yHi = haveY ? std::max(yHi, b) : b; haveY = true; }
        }
        if (xHi <= xLo) xHi = xLo + 1.0;
        if (yHi <= yLo) { yLo -= 0.5; yHi += 0.5; }
        const double pad = (yHi - yLo) * 0.08;

        ImPlot::SetNextAxisLimits(ImAxis_X1, xLo, xHi, ImPlotCond_Always);
        ImPlot::SetNextAxisLimits(ImAxis_Y1, yLo - pad, yHi + pad, ImPlotCond_Always);

        if (ImPlot::BeginPlot(title, ImVec2(-1.0f, height)))
        {
            if (!fX.empty())
            {
                ImPlotSpec spec;
                spec.LineColor = k_FrontCol;
                spec.LineWeight = 2.0f;
                if (m_FillCurves)
                {
                    spec.Flags = ImPlotLineFlags_Shaded;
                    spec.FillColor = k_FrontCol;
                    spec.FillAlpha = 0.18f;
                }
                ImPlot::PlotLine("Front", fX.data(), fY.data(), (int)fX.size(), spec);
            }
            if (m_ShowRear && !rX.empty())
            {
                ImPlotSpec spec;
                spec.LineColor = k_RearCol;
                spec.LineWeight = 2.0f;
                ImPlot::PlotLine("Rear", rX.data(), rY.data(), (int)rX.size(), spec);
            }
            ImPlot::EndPlot();
        }
        (void)yLabel;
    }

    // -------------------------------------------------------------------------
    // Plots — Speed / Accel / Force / Torque / Distance vs time.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawPlotsWindow()
    {
        ImGui::Begin("Performance Curves");

        if (!m_Sim.valid)
        {
            ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "Fix the configuration to see curves.");
            ImGui::TextWrapped("%s", m_Sim.error.c_str());
            ImGui::End();
            return;
        }

        ImGui::TextColored(k_FrontCol, "Front");
        ImGui::SameLine();
        if (m_ShowRear) { ImGui::TextColored(k_RearCol, "Rear"); ImGui::SameLine(); }
        ImGui::TextDisabled("vs time (s)");

        const AxleResult& f = m_Sim.front;
        const AxleResult& r = m_Sim.rear;

        // Pick a chart height so all five share the window without scrolling too much.
        const float avail = ImGui::GetContentRegionAvail().y;
        const float h = std::max(150.0f, (avail - 40.0f) / 3.0f);

        PlotChannel("Speed (mph)",        "mph", f.time, f.speedMph, r.time, r.speedMph, h);
        PlotChannel("Acceleration (g)",   "g",   f.time, f.accelG,   r.time, r.accelG,   h);
        PlotChannel("Tractive force (N)", "N",   f.time, f.forceN,   r.time, r.forceN,   h);
        PlotChannel("Motor torque (Nm)",  "Nm",  f.time, f.torqueNm, r.time, r.torqueNm, h);
        PlotChannel("Distance (ft)",      "ft",  f.time, f.distFt,   r.time, r.distFt,   h);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Results & export — detailed metric table + CSV writer.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawResultsWindow()
    {
        ImGui::Begin("Results & Export");

        if (!m_Sim.valid)
        {
            ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "%s", m_Sim.error.c_str());
            ImGui::End();
            return;
        }

        const AxleResult& f = m_Sim.front;
        const AxleResult& r = m_Sim.rear;

        if (ImGui::BeginTable("metrics", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Metric");
            ImGui::TableSetupColumn("Front");
            ImGui::TableSetupColumn("Rear");
            ImGui::TableHeadersRow();

            auto Row = [&](const char* name, const char* fmt, double fv, double rv)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
                ImGui::TableNextColumn(); ImGui::Text(fmt, fv);
                ImGui::TableNextColumn(); ImGui::Text(fmt, rv);
            };

            Row("Top speed (mph)",     "%.2f", f.topSpeedMph,       r.topSpeedMph);
            Row("No-load top (mph)",   "%.2f", f.noLoadTopSpeedMph, r.noLoadTopSpeedMph);
            Row("Peak accel (g)",      "%.3f", f.peakAccelG,        r.peakAccelG);
            Row("Launch force (N)",    "%.1f", f.launchForceN,      r.launchForceN);
            Row("Traction cap (N)",    "%.1f", f.tractionLimitN,    r.tractionLimitN);
            Row("Distance (ft)",       "%.2f", f.distanceFt,        r.distanceFt);
            Row("K_sys (m/rad)",       "%.5f", f.K_sys,             r.K_sys);
            Row("Stall torque (Nm)",   "%.3f", f.torqueStall,       r.torqueStall);
            Row("No-load omega (rad/s)","%.1f", f.omegaNoLoad,      r.omegaNoLoad);
            Row("Reflected inertia",   "%.5f", f.inertiaEq,         r.inertiaEq);

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Export CSV");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("File name", m_ExportName, sizeof(m_ExportName));
        ImGui::SameLine();
        ImGui::TextDisabled(".csv");

        if (ImGui::Button("  Export front + rear CSV  "))
            ExportCSV(m_ExportName);

        if (!m_ExportStatus.empty())
            ImGui::TextWrapped("%s", m_ExportStatus.c_str());

        ImGui::Spacing();
        ImGui::TextDisabled("Rows: %zu (front) / %zu (rear)", f.time.size(), r.time.size());

        ImGui::End();
    }

    // =========================================================================
    // Explorers — a tabbed window of "what-if" sweeps. All use the lightweight
    // allocation-free metrics sim, so the tables are recomputed live each frame.
    // =========================================================================
    namespace
    {
        // Relative gap between two K_sys values, as a percent (sync = 0%).
        double TangGapPct(double k, double kOther)
        {
            const double d = std::max({ std::abs(k), std::abs(kOther), 1e-9 });
            return std::abs(k - kOther) / d * 100.0;
        }
    }

    void SF_DrivetrainCalcsApp::DrawExplorersWindow()
    {
        ImGui::Begin("Drivetrain Explorers");

        if (!m_Sim.valid)
        {
            ImGui::TextColored({ 1.0f, 0.35f, 0.35f, 1.0f }, "%s", m_Sim.error.c_str());
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##explorers"))
        {
            if (ImGui::BeginTabItem("Lift planner"))     { DrawLiftPlannerTab();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Same-pulley range")){ DrawSamePulleyRangeTab(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Feasibility"))      { DrawFeasibilityTab();     ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Wheel sweep"))      { DrawWheelSweepTab();      ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Pulley ratios"))    { DrawPulleyRatioTab();     ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Feasibility tab — explain the tangential-velocity rule, show the gap, and
    // offer concrete one-click fixes (sync wheel dia / sync pulley ratio).
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawFeasibilityTab()
    {
        ImGui::TextWrapped(
            "A rigid chassis forces both driven wheels to roll at the same ground "
            "speed. The tangential (surface) velocity of each wheel = motor speed x "
            "K_sys, so if the front and rear K_sys differ the wheels MUST slip or "
            "fight each other -- the build is mechanically impossible.");

        ImGui::Spacing();
        const bool feasible = m_Sim.tangMismatchPct <= m_FeasTolPct;
        if (feasible)
            ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f },
                               "POSSIBLE  -  front/rear surface speeds within %.2f%% (tol %.2f%%)",
                               m_Sim.tangMismatchPct, m_FeasTolPct);
        else
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f },
                               "IMPOSSIBLE  -  %.2f%% surface-speed gap exceeds the %.2f%% tolerance",
                               m_Sim.tangMismatchPct, m_FeasTolPct);

        ImGui::Spacing();
        if (ImGui::BeginTable("feas", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("");
            ImGui::TableSetupColumn("Front");
            ImGui::TableSetupColumn("Rear");
            ImGui::TableSetupColumn("Gap");
            ImGui::TableHeadersRow();

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("Tangential speed (mph)");
            ImGui::TableNextColumn(); ImGui::Text("%.3f", m_Sim.vTangFrontMph);
            ImGui::TableNextColumn(); ImGui::Text("%.3f", m_Sim.vTangRearMph);
            ImGui::TableNextColumn(); ImGui::Text("%.2f%%", m_Sim.tangMismatchPct);

            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted("K_sys (m/rad)");
            ImGui::TableNextColumn(); ImGui::Text("%.5f", m_Sim.kFront);
            ImGui::TableNextColumn(); ImGui::Text("%.5f", m_Sim.kRear);
            ImGui::TableNextColumn(); ImGui::TextDisabled("-");
            ImGui::EndTable();
        }

        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputDouble("Tolerance (%)##feas", &m_FeasTolPct, 0.1, 1.0, "%.2f") && m_FeasTolPct < 0.0)
            m_FeasTolPct = 0.0;

        ImGui::Spacing();
        ImGui::SeparatorText("One-click fixes (keep everything else fixed)");

        const double syncFrontDia = SyncWheelDiameterIn(m_Cfg, true);
        const double syncRearDia  = SyncWheelDiameterIn(m_Cfg, false);

        ImGui::Text("Front wheel dia -> %.3f in  (currently %.3f)", syncFrontDia, m_Cfg.frontWheelInches);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply##fdia")) { m_Cfg.frontWheelInches = syncFrontDia; m_Dirty = true; }

        ImGui::Text("Rear  wheel dia -> %.3f in  (currently %.3f)", syncRearDia, m_Cfg.rearWheelInches);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply##rdia")) { m_Cfg.rearWheelInches = syncRearDia; m_Dirty = true; }

        const double syncRfront = SyncPulleyRatio(m_Cfg, true);
        const double syncRrear  = SyncPulleyRatio(m_Cfg, false);
        ImGui::Spacing();
        ImGui::TextDisabled("Or match the pulley ratio (driven/driving):");
        ImGui::Text("Front ratio -> %.4f  => motor teeth %.1f (wheel %.0f fixed)",
                    syncRfront, (syncRfront > 1e-6 ? m_Cfg.frontPulleyTeeth / syncRfront : 0.0),
                    m_Cfg.frontPulleyTeeth);
        ImGui::Text("Rear  ratio -> %.4f  => motor teeth %.1f (wheel %.0f fixed)",
                    syncRrear, (syncRrear > 1e-6 ? m_Cfg.rearPulleyTeeth / syncRrear : 0.0),
                    m_Cfg.rearPulleyTeeth);
    }

    // -------------------------------------------------------------------------
    // Shared table renderer for a single sweep (rows already gathered).
    // -------------------------------------------------------------------------
    namespace
    {
        struct SweepRow
        {
            double value;        // wheel dia or teeth
            AxleMetrics m;
            double gapPct;
            bool   isCurrent;
        };

        void RenderSweepTable(const char* id, const char* valueHeader,
                              const char* valueFmt, const std::vector<SweepRow>& rows,
                              double tolPct, const ImVec4& frontCol)
        {
            if (ImGui::BeginTable(id, 7,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0, 260)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn(valueHeader);
                ImGui::TableSetupColumn("Ratio");
                ImGui::TableSetupColumn("K_sys");
                ImGui::TableSetupColumn("Top mph");
                ImGui::TableSetupColumn("Peak g");
                ImGui::TableSetupColumn("Gap %");
                ImGui::TableSetupColumn("Status");
                ImGui::TableHeadersRow();

                for (const SweepRow& r : rows)
                {
                    ImGui::TableNextRow();
                    if (r.isCurrent)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 90, 130, 110));
                    else if (r.gapPct <= tolPct)
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 110, 55, 90));

                    ImGui::TableNextColumn();
                    ImGui::TextColored(frontCol, valueFmt, r.value);
                    if (r.isCurrent) { ImGui::SameLine(); ImGui::TextDisabled("(now)"); }

                    if (!r.m.valid)
                    {
                        for (int c = 1; c < 7; ++c) { ImGui::TableNextColumn(); ImGui::TextDisabled("-"); }
                        continue;
                    }

                    ImGui::TableNextColumn(); ImGui::Text("%.3f", r.m.R);
                    ImGui::TableNextColumn(); ImGui::Text("%.5f", r.m.K_sys);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", r.m.topSpeedMph);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", r.m.peakAccelG);
                    ImGui::TableNextColumn();
                    if (r.gapPct <= tolPct) ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "%.2f", r.gapPct);
                    else                    ImGui::TextColored({ 1.0f, 0.55f, 0.3f, 1.0f }, "%.2f", r.gapPct);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.m.launchTractionLimited ? "LIMIT" : "MOTOR");
                }
                ImGui::EndTable();
            }
        }
    }

    // -------------------------------------------------------------------------
    // Wheel diameter sweep — vary one axle's wheel dia, hold everything else.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawWheelSweepTab()
    {
        ImGui::TextDisabled("All other parameters stay fixed; the rear/front axle "
                            "you are NOT sweeping is the sync reference.");

        ImGui::RadioButton("Front axle##swaxle", &m_SweepAxle, 0); ImGui::SameLine();
        ImGui::RadioButton("Rear axle##swaxle",  &m_SweepAxle, 1);

        const bool front = (m_SweepAxle == 0);
        const ImVec4 col = front ? k_FrontCol : k_RearCol;

        ImGui::SetNextItemWidth(90); ImGui::InputDouble("min (in)",  &m_SweepDiaMin,  0.25, 1.0, "%.2f"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputDouble("max (in)",  &m_SweepDiaMax,  0.25, 1.0, "%.2f"); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputDouble("step (in)", &m_SweepDiaStep, 0.05, 0.25, "%.3f");
        if (m_SweepDiaMin  < 0.1)  m_SweepDiaMin  = 0.1;
        if (m_SweepDiaStep < 0.01) m_SweepDiaStep = 0.01;
        if (m_SweepDiaMax  < m_SweepDiaMin) m_SweepDiaMax = m_SweepDiaMin;

        const double syncDia = SyncWheelDiameterIn(m_Cfg, front);
        ImGui::Text("Perfect-sync dia for the %s axle: ", front ? "front" : "rear");
        ImGui::SameLine();
        ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "%.3f in", syncDia);
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply##syncdia"))
        {
            if (front) m_Cfg.frontWheelInches = syncDia; else m_Cfg.rearWheelInches = syncDia;
            m_Dirty = true;
        }

        // Gather rows (cap to keep the table bounded).
        const double pulley = front ? m_Cfg.frontPulleyTeeth : m_Cfg.rearPulleyTeeth;
        const double motor  = front ? m_Cfg.motorPulleyFront  : m_Cfg.motorPulleyRear;
        const double kOther = front ? m_Sim.kRear : m_Sim.kFront;
        const double curDia = front ? m_Cfg.frontWheelInches : m_Cfg.rearWheelInches;

        std::vector<SweepRow> rows;
        const int maxRows = 240;
        int closest = -1; double closestDelta = 1e30;
        for (double d = m_SweepDiaMin; d <= m_SweepDiaMax + 1e-9 && (int)rows.size() < maxRows; d += m_SweepDiaStep)
        {
            SweepRow row;
            row.value = d;
            row.m     = SimulateAxleMetrics(m_Cfg, d, pulley, motor);
            row.gapPct = TangGapPct(row.m.K_sys, kOther);
            row.isCurrent = false;
            const double delta = std::abs(d - curDia);
            if (delta < closestDelta) { closestDelta = delta; closest = (int)rows.size(); }
            rows.push_back(row);
        }
        if (closest >= 0 && closestDelta <= m_SweepDiaStep * 0.5 + 1e-9)
            rows[closest].isCurrent = true;

        RenderSweepTable("##wheelsweep", "dia (in)", "%.2f", rows, m_FeasTolPct, col);
    }

    // -------------------------------------------------------------------------
    // Pulley ratio explorer — vary one pulley's teeth +/- span around current.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawPulleyRatioTab()
    {
        ImGui::TextDisabled("Walks the chosen pulley +/- a few teeth around the "
                            "current value; everything else stays fixed.");

        ImGui::RadioButton("Front axle##plaxle", &m_PulleyAxle, 0); ImGui::SameLine();
        ImGui::RadioButton("Rear axle##plaxle",  &m_PulleyAxle, 1);

        ImGui::RadioButton("Vary wheel pulley", &m_PulleyVary, 0); ImGui::SameLine();
        ImGui::RadioButton("Vary motor pulley", &m_PulleyVary, 1);

        ImGui::SetNextItemWidth(90);
        ImGui::InputInt("+/- teeth", &m_PulleySpan);
        m_PulleySpan = std::clamp(m_PulleySpan, 1, 30);

        const bool  front     = (m_PulleyAxle == 0);
        const bool  varyWheel = (m_PulleyVary == 0);
        const ImVec4 col      = front ? k_FrontCol : k_RearCol;

        const double wheelIn   = front ? m_Cfg.frontWheelInches : m_Cfg.rearWheelInches;
        const double wheelTeeth = front ? m_Cfg.frontPulleyTeeth : m_Cfg.rearPulleyTeeth;
        const double motorTeeth = front ? m_Cfg.motorPulleyFront  : m_Cfg.motorPulleyRear;
        const double kOther     = front ? m_Sim.kRear : m_Sim.kFront;

        const double center = varyWheel ? wheelTeeth : motorTeeth;
        const double syncR  = SyncPulleyRatio(m_Cfg, front);
        ImGui::Text("Perfect-sync ratio for the %s axle: ", front ? "front" : "rear");
        ImGui::SameLine();
        ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "%.4f", syncR);
        ImGui::SameLine();
        if (varyWheel)
            ImGui::TextDisabled("(=> %.1f wheel teeth at motor %.0f)",
                                syncR * motorTeeth, motorTeeth);
        else
            ImGui::TextDisabled("(=> %.1f motor teeth at wheel %.0f)",
                                (syncR > 1e-6 ? wheelTeeth / syncR : 0.0), wheelTeeth);

        std::vector<SweepRow> rows;
        for (int off = -m_PulleySpan; off <= m_PulleySpan; ++off)
        {
            const double teeth = center + off;
            if (teeth < 1.0) continue; // a pulley needs at least 1 tooth

            const double pulley = varyWheel ? teeth : wheelTeeth;
            const double motor  = varyWheel ? motorTeeth : teeth;

            SweepRow row;
            row.value     = teeth;
            row.m         = SimulateAxleMetrics(m_Cfg, wheelIn, pulley, motor);
            row.gapPct    = TangGapPct(row.m.K_sys, kOther);
            row.isCurrent = (off == 0);
            rows.push_back(row);
        }

        RenderSweepTable("##pulleysweep",
                         varyWheel ? "Wheel teeth" : "Motor teeth",
                         "%.0f", rows, m_FeasTolPct, col);
    }

    // -------------------------------------------------------------------------
    // Lift planner — enter a target ground-clearance increase; the tool lifts
    // both wheels equally (diameter += 2 x clearance, so the chassis stays
    // level) and lists WHOLE-TOOTH pulley changes that keep the drive perfectly
    // synced (0 error). Each integer tooth count maps to exactly one clearance,
    // so the menu is the clearances nearest your target that land on round teeth.
    // -------------------------------------------------------------------------
    namespace
    {
        // Sync ratio T = (WPf*MPr)/(MPf*WPr) with one pulley swapped to `teeth`.
        // Sync needs (dF0+2h)/(dR0+2h) = T.  fam: 0 rear-driven, 1 rear-motor,
        // 2 front-driven, 3 front-motor.
        double FamilyT(int fam, double teeth, double WPf, double MPf, double WPr, double MPr)
        {
            switch (fam)
            {
                case 0: return (WPf * MPr) / (MPf * teeth);
                case 1: return (WPf * teeth) / (MPf * WPr);
                case 2: return (teeth * MPr) / (MPf * WPr);
                case 3: return (WPf * MPr) / (teeth * WPr);
            }
            return 0.0;
        }
        const char* FamilyName(int fam)
        {
            switch (fam)
            {
                case 0: return "rear wheel pulley";
                case 1: return "rear motor pulley";
                case 2: return "front wheel pulley";
                case 3: return "front motor pulley";
            }
            return "?";
        }
    }

    void SF_DrivetrainCalcsApp::DrawLiftPlannerTab()
    {
        ImGui::TextWrapped(
            "Enter a target ground-clearance gain. Both wheels grow by 2x that "
            "(clearance = wheel radius), so the chassis stays level. The tool then "
            "lists whole-tooth pulley changes whose EXACT clearance (0 sync error) "
            "lands nearest your target.");

        ImGui::SetNextItemWidth(140);
        ImGui::InputDouble("Target clearance (in)", &m_LiftTarget, 0.05, 0.25, "%.3f");
        if (m_LiftTarget < 0.0) m_LiftTarget = 0.0;
        ImGui::SameLine();
        ImGui::Checkbox("Include motor-pulley swaps", &m_LiftIncludeMotor);
        ImGui::TextDisabled("At target: each wheel diameter +%.3f in (front %.2f, rear %.2f)",
                            2.0 * m_LiftTarget, m_Cfg.frontWheelInches + 2.0 * m_LiftTarget,
                            m_Cfg.rearWheelInches + 2.0 * m_LiftTarget);

        const double dF0 = m_Cfg.frontWheelInches;
        const double dR0 = m_Cfg.rearWheelInches;
        const double WPf = m_Cfg.frontPulleyTeeth, MPf = m_Cfg.motorPulleyFront;
        const double WPr = m_Cfg.rearPulleyTeeth,  MPr = m_Cfg.motorPulleyRear;

        // Even-lift clearance h that perfectly syncs for sync ratio T:
        //   (dF0+2h)/(dR0+2h) = T  =>  h = (T*dR0 - dF0) / (2(1-T))
        auto solveH = [&](double T) -> double
        {
            const double denom = 2.0 * (1.0 - T);
            return (std::abs(denom) < 1e-9) ? -1.0 : (T * dR0 - dF0) / denom;
        };

        struct LiftOpt { double clr, fDia, rDia, top; int fam, teeth, oldTeeth; };
        std::vector<LiftOpt> opts;

        const int    window = 14;
        const int    tMin   = 8;                 // smallest sensible pulley
        const double hLo    = 0.005;             // ignore ~zero lifts
        const double hHi    = m_LiftTarget + 1.5;

        const int famList[4] = { 0, 2, 1, 3 };   // wheel pulleys first
        const int famN = m_LiftIncludeMotor ? 4 : 2;
        for (int fi = 0; fi < famN; ++fi)
        {
            const int fam = famList[fi];
            const double cur = (fam == 0) ? WPr : (fam == 1) ? MPr : (fam == 2) ? WPf : MPf;
            const int c = (int)std::lround(cur);
            for (int t = c - window; t <= c + window; ++t)
            {
                if (t < tMin || t == c) continue;
                const double h = solveH(FamilyT(fam, (double)t, WPf, MPf, WPr, MPr));
                if (h <= hLo || h > hHi) continue;

                LiftOpt o;
                o.clr = h; o.fDia = dF0 + 2.0 * h; o.rDia = dR0 + 2.0 * h;
                o.fam = fam; o.teeth = t; o.oldTeeth = c;
                const double wpf = (fam == 2) ? (double)t : WPf;
                const double mpf = (fam == 3) ? (double)t : MPf;
                o.top = SimulateAxleMetrics(m_Cfg, o.fDia, wpf, mpf).topSpeedMph;
                opts.push_back(o);
            }
        }

        std::sort(opts.begin(), opts.end(), [&](const LiftOpt& a, const LiftOpt& b)
        {
            return std::abs(a.clr - m_LiftTarget) < std::abs(b.clr - m_LiftTarget);
        });
        if (opts.size() > 40) opts.resize(40);

        ImGui::Spacing();
        if (opts.empty())
        {
            ImGui::TextColored({ 1.0f, 0.55f, 0.3f, 1.0f },
                "No whole-tooth option near this target. Raise the target or tick motor-pulley swaps.");
            return;
        }

        ImGui::SeparatorText("Whole-tooth options (0 sync error), nearest target first");
        if (ImGui::BeginTable("liftopts", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 280)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Clearance");
            ImGui::TableSetupColumn("Front dia");
            ImGui::TableSetupColumn("Rear dia");
            ImGui::TableSetupColumn("Pulley change");
            ImGui::TableSetupColumn("Top mph");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)opts.size(); ++i)
            {
                const LiftOpt& o = opts[i];
                ImGui::TableNextRow();
                if (i == 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(40, 110, 55, 90));

                ImGui::TableNextColumn();
                ImGui::Text("+%.3f in", o.clr);
                if (i == 0) { ImGui::SameLine(); ImGui::TextDisabled("(closest)"); }
                ImGui::TableNextColumn(); ImGui::Text("%.3f", o.fDia);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", o.rDia);
                ImGui::TableNextColumn(); ImGui::Text("%s %d->%d", FamilyName(o.fam), o.oldTeeth, o.teeth);
                ImGui::TableNextColumn(); ImGui::Text("%.2f", o.top);

                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::SmallButton("Apply"))
                {
                    m_Cfg.frontWheelInches = o.fDia;
                    m_Cfg.rearWheelInches  = o.rDia;
                    switch (o.fam)
                    {
                        case 0: m_Cfg.rearPulleyTeeth  = o.teeth; break;
                        case 1: m_Cfg.motorPulleyRear  = o.teeth; break;
                        case 2: m_Cfg.frontPulleyTeeth = o.teeth; break;
                        case 3: m_Cfg.motorPulleyFront = o.teeth; break;
                    }
                    m_Dirty = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Wheel diameters come out exact (custom) so the teeth stay whole and sync is perfect.");
    }

    // -------------------------------------------------------------------------
    // Same-pulley range — how much clearance you can add or remove WITHOUT
    // touching any pulley, staying inside the feasibility tolerance. For a level
    // even lift with different-size wheels, perfect sync only holds at zero, so
    // this band is whatever your tolerance allows around the current setup.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawSamePulleyRangeTab()
    {
        ImGui::TextWrapped(
            "Keeps every pulley exactly as-is and reports how far ground clearance "
            "can move (up or down) before the front/rear sync error exceeds your "
            "tolerance. Both wheels move together so the chassis stays level.");

        ImGui::SetNextItemWidth(110);
        if (ImGui::InputDouble("Tolerance (%)##sprange", &m_FeasTolPct, 0.1, 1.0, "%.2f") && m_FeasTolPct < 0.0)
            m_FeasTolPct = 0.0;

        const double dF0 = m_Cfg.frontWheelInches;
        const double dR0 = m_Cfg.rearWheelInches;
        const double Rf  = m_Cfg.frontPulleyTeeth / m_Cfg.motorPulleyFront;
        const double Rr  = m_Cfg.rearPulleyTeeth  / m_Cfg.motorPulleyRear;
        const double G   = m_Cfg.gearboxReduction;

        auto gapAt = [&](double h) -> double
        {
            const double dF = dF0 + 2.0 * h, dR = dR0 + 2.0 * h;
            if (dF <= 0.05 || dR <= 0.05) return 1e9;
            const double kF = (dF * 0.5 * DT::INCHES_TO_METERS) / (G * Rf);
            const double kR = (dR * 0.5 * DT::INCHES_TO_METERS) / (G * Rr);
            return TangGapPct(kF, kR);
        };

        // Walk outward from 0 in both directions until the gap exceeds tolerance.
        const double step = 0.005;
        double hUp = 0.0, hDn = 0.0;
        for (double h = 0.0; h <= 12.0; h += step) { if (gapAt(h) <= m_FeasTolPct) hUp = h; else break; }
        for (double h = 0.0; h > -12.0; h -= step) { if (gapAt(h) <= m_FeasTolPct) hDn = h; else break; }

        ImGui::Spacing();
        ImGui::SeparatorText("Clearance band with pulleys unchanged");

        const bool any = (hUp > step * 0.5) || (hDn < -step * 0.5);
        if (!any)
        {
            ImGui::TextColored({ 1.0f, 0.55f, 0.3f, 1.0f },
                "Essentially none (only ~0).");
            ImGui::TextWrapped(
                "Your front and rear wheels are different sizes, so any level lift "
                "changes their ratio - a pulley must change. Use the Lift planner "
                "tab, or raise the tolerance above to allow a small band.");
            return;
        }

        ImGui::Text("Clearance you can apply:");
        ImGui::SameLine();
        ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "%+.3f in  to  %+.3f in", hDn, hUp);
        ImGui::TextDisabled("Front dia %.2f..%.2f in,  rear dia %.2f..%.2f in",
                            dF0 + 2.0 * hDn, dF0 + 2.0 * hUp, dR0 + 2.0 * hDn, dR0 + 2.0 * hUp);
        ImGui::TextDisabled("Sync error reaches your %.2f%% tolerance at the ends.", m_FeasTolPct);

        ImGui::Spacing();
        if (ImGui::Button("Apply max lift"))
        {
            m_Cfg.frontWheelInches = dF0 + 2.0 * hUp;
            m_Cfg.rearWheelInches  = dR0 + 2.0 * hUp;
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply max drop"))
        {
            m_Cfg.frontWheelInches = dF0 + 2.0 * hDn;
            m_Cfg.rearWheelInches  = dR0 + 2.0 * hDn;
            m_Dirty = true;
        }
    }

    // -------------------------------------------------------------------------
    // CSV export — same config-header layout as the original command-line tool,
    // with both axles' time series side by side.
    // -------------------------------------------------------------------------
    bool SF_DrivetrainCalcsApp::ExportCSV(std::string fileName)
    {
        if (!m_Sim.valid)
        {
            m_ExportStatus = "Cannot export: configuration is invalid.";
            return false;
        }

        // Sanitise + default the filename.
        fileName.erase(std::remove(fileName.begin(), fileName.end(), '\"'), fileName.end());
        while (!fileName.empty() && (fileName.front() == ' ')) fileName.erase(fileName.begin());
        while (!fileName.empty() && (fileName.back()  == ' ')) fileName.pop_back();
        if (fileName.empty())
        {
            char stamp[32];
            std::time_t t = std::time(nullptr);
            std::tm tmv{};
        #if defined(_WIN32)
            localtime_s(&tmv, &t);
        #else
            localtime_r(&t, &tmv);
        #endif
            std::strftime(stamp, sizeof(stamp), "run_%Y%m%d_%H%M%S", &tmv);
            fileName = stamp;
        }
        if (fileName.find(".csv") == std::string::npos) fileName += ".csv";

        const std::string dir = Cosmic::FileSystem::Resolve("project://logs");
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::filesystem::path outPath = std::filesystem::path(dir) / fileName;

        std::ofstream csv(outPath);
        if (!csv.is_open())
        {
            m_ExportStatus = "ERROR: could not open " + outPath.string();
            return false;
        }

        const DrivetrainConfig& c = m_Cfg;

        // --- Configuration header (mirrors the original tool) ---
        csv << "--- CONFIGURATION SPECS ---\n";
        csv << "Total Weight (lb),"           << c.totalWeightLb       << "\n";
        csv << "Battery Voltage (V),"         << c.vBatt               << "\n";
        csv << "Motor KV,"                    << c.kv                  << "\n";
        csv << "Gearbox Reduction,"           << c.gearboxReduction    << "\n";
        csv << "--------------------------,\n";
        csv << "Front Wheel Diameter (in),"   << c.frontWheelInches    << "\n";
        csv << "Front Pulley (Driven),"       << c.frontPulleyTeeth    << "\n";
        csv << "Motor Pulley (Front Driving)," << c.motorPulleyFront   << "\n";
        csv << "--------------------------,\n";
        csv << "Rear Wheel Diameter (in),"    << c.rearWheelInches     << "\n";
        csv << "Rear Pulley (Driven),"        << c.rearPulleyTeeth     << "\n";
        csv << "Motor Pulley (Rear Driving)," << c.motorPulleyRear     << "\n";
        csv << "--------------------------,\n";
        csv << "Current Limit (A),"           << c.currentLimit        << "\n";
        csv << "Coeff of Friction (mu),"      << c.mu                  << "\n";
        csv << "Drivetrain Efficiency,"       << c.drivetrainEfficiency << "\n";
        csv << "Speed Factor,"                << c.speedFactor         << "\n";
        csv << "Front K_sys (m/rad),"         << m_Sim.kFront          << "\n";
        csv << "Rear K_sys (m/rad),"          << m_Sim.kRear           << "\n";
        csv << "Velocity Sync,"               << (m_Sim.velMismatch ? "MISMATCH" : "OK") << "\n\n";

        // --- Time series (front then rear columns) ---
        csv << "Time(s),"
               "F_Speed(mph),F_Accel(g),F_Force(N),F_Torque(Nm),F_Dist(ft),F_Status,"
               "R_Speed(mph),R_Accel(g),R_Force(N),R_Torque(Nm),R_Dist(ft),R_Status\n";

        const AxleResult& f = m_Sim.front;
        const AxleResult& r = m_Sim.rear;
        const size_t n = std::max(f.time.size(), r.time.size());
        auto stat = [](int s) { return s == STATUS_LIMIT ? "LIMIT" : "MOTOR"; };

        for (size_t i = 0; i < n; ++i)
        {
            const double t = (i < f.time.size()) ? f.time[i]
                            : (i < r.time.size()) ? r.time[i] : 0.0;
            csv << t << ",";

            if (i < f.time.size())
                csv << f.speedMph[i] << "," << f.accelG[i] << "," << f.forceN[i] << ","
                    << f.torqueNm[i] << "," << f.distFt[i] << "," << stat(f.status[i]) << ",";
            else
                csv << ",,,,,,";

            if (i < r.time.size())
                csv << r.speedMph[i] << "," << r.accelG[i] << "," << r.forceN[i] << ","
                    << r.torqueNm[i] << "," << r.distFt[i] << "," << stat(r.status[i]);
            else
                csv << ",,,,,";

            csv << "\n";
        }

        csv.close();

        const std::string abs = std::filesystem::absolute(outPath, ec).string();
        m_ExportStatus = "Saved: " + (ec ? outPath.string() : abs);
        CS_INFO("SF_DrivetrainCalcsApp: exported CSV -> {}", m_ExportStatus);
        return true;
    }

    // =========================================================================
    // Viewport schematic — a to-scale side view of the two drive wheels with
    // their driven pulleys, the motor pulleys and the belts. Wheel circles are
    // drawn at true relative size so changing a diameter is visible instantly.
    // =========================================================================
    void SF_DrivetrainCalcsApp::RenderSchematic()
    {
        m_SchematicValid = m_Sim.valid;

        using namespace DT;
        const float rF = (float)((m_Cfg.frontWheelInches * 0.5) * INCHES_TO_METERS);
        const float rB = (float)((m_Cfg.rearWheelInches  * 0.5) * INCHES_TO_METERS);
        const float maxR = std::max({ rF, rB, 0.02f });

        // Pulley radii: scaled from tooth counts so the reduction is visible
        // without being physically exact.
        const float pScale = 0.0011f;
        const float rpFd = (float)m_Cfg.frontPulleyTeeth * pScale; // front driven
        const float rpFm = (float)m_Cfg.motorPulleyFront * pScale; // front motor
        const float rpRd = (float)m_Cfg.rearPulleyTeeth  * pScale;
        const float rpRm = (float)m_Cfg.motorPulleyRear  * pScale;

        const float spacing = maxR + 0.06f;
        const float cxF =  spacing;   // front on the right
        const float cxR = -spacing;   // rear on the left
        const float yMotor = 2.0f * maxR + 0.06f;

        m_FrontWheelWorld = { cxF, rF };
        m_RearWheelWorld  = { cxR, rB };
        m_FrontWheelDia   = (float)m_Cfg.frontWheelInches;
        m_RearWheelDia    = (float)m_Cfg.rearWheelInches;

        // Frame the stage: fit width + height with margin.
        const glm::vec2 vp = Cosmic::Application::Get().GetViewportSize();
        const float aspect = (vp.y > 0.0f) ? vp.x / vp.y : (16.0f / 9.0f);
        const float extentY = (yMotor + std::max(rpFm, rpRm) + 0.04f) * 0.5f;
        const float extentX = (spacing + maxR + 0.05f);
        const float zoom = std::max(extentY, extentX / std::max(aspect, 0.1f)) * 1.15f;
        m_Camera.SetPosition({ 0.0f, extentY, 0.0f });
        m_Camera.SetZoomLevel(std::max(zoom, 0.08f));

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

        // Ground line.
        const glm::vec4 ground = { 0.35f, 0.38f, 0.45f, 1.0f };
        Cosmic::Renderer2D::DrawLine({ cxR - maxR - 0.05f, 0.0f, -0.02f },
                                     { cxF + maxR + 0.05f, 0.0f, -0.02f }, ground);

        auto drawAxle = [&](float cx, float rWheel, float rDriven, float rMotor,
                            const ImVec4& colIm)
        {
            const glm::vec4 col   = ToVec4(colIm);
            const glm::vec4 faint = { col.r, col.g, col.b, 0.35f };
            const glm::vec4 belt  = { 0.7f, 0.7f, 0.75f, 1.0f };
            const glm::vec4 hub   = { 0.85f, 0.87f, 0.9f, 1.0f };

            const glm::vec3 wheelC = { cx, rWheel, 0.0f };
            const glm::vec3 motorC = { cx, yMotor, 0.0f };

            // Tyre (thick ring) + faint disc fill.
            Cosmic::Renderer2D::DrawCircle(wheelC, { 2 * rWheel, 2 * rWheel }, faint, 1.0f, 0.01f);
            Cosmic::Renderer2D::DrawCircle(wheelC, { 2 * rWheel, 2 * rWheel }, col, 0.10f, 0.005f);

            // Driven pulley at the hub.
            Cosmic::Renderer2D::DrawCircle(wheelC, { 2 * rDriven, 2 * rDriven }, col, 0.30f, 0.005f);
            Cosmic::Renderer2D::DrawCircle(wheelC, { 0.012f, 0.012f }, hub, 1.0f, 0.01f);

            // Motor block + motor pulley.
            Cosmic::Renderer2D::DrawQuad({ cx, yMotor, -0.01f },
                                         { rMotor * 1.4f, rMotor * 1.4f },
                                         { 0.30f, 0.32f, 0.38f, 1.0f });
            Cosmic::Renderer2D::DrawCircle(motorC, { 2 * rMotor, 2 * rMotor }, col, 0.30f, 0.005f);
            Cosmic::Renderer2D::DrawCircle(motorC, { 0.012f, 0.012f }, hub, 1.0f, 0.01f);

            // Belts (two near-vertical strands between the pulley edges).
            Cosmic::Renderer2D::DrawLine({ cx - rMotor, yMotor, -0.005f }, { cx - rDriven, rWheel, -0.005f }, belt);
            Cosmic::Renderer2D::DrawLine({ cx + rMotor, yMotor, -0.005f }, { cx + rDriven, rWheel, -0.005f }, belt);
        };

        drawAxle(cxR, rB, rpRd, rpRm, k_RearCol);
        drawAxle(cxF, rF, rpFd, rpFm, k_FrontCol);

        Cosmic::Renderer2D::EndScene();
    }

    // -------------------------------------------------------------------------
    // Schematic labels — project the cached world anchors to screen and draw
    // text with the ImGui foreground draw list (the renderer has no text).
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawSchematicLabels()
    {
        if (!m_SchematicValid) return;

        auto& app = Cosmic::Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x <= 1.0f || vpSize.y <= 1.0f) return;

        const glm::mat4 vp = m_Camera.GetCamera().GetViewProjectionMatrix();
        auto worldToScreen = [&](const glm::vec2& wp, ImVec2& out) -> bool
        {
            glm::vec4 c = vp * glm::vec4(wp.x, wp.y, 0.0f, 1.0f);
            if (c.w != 0.0f) c /= c.w;
            const float sx = vpPos.x + (c.x * 0.5f + 0.5f) * vpSize.x;
            const float sy = vpPos.y + (1.0f - (c.y * 0.5f + 0.5f)) * vpSize.y;
            out = ImVec2(sx, sy);
            return sx >= vpPos.x && sx <= vpPos.x + vpSize.x
                && sy >= vpPos.y && sy <= vpPos.y + vpSize.y;
        };

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        char buf[64];
        ImVec2 p;

        if (worldToScreen(m_FrontWheelWorld, p))
        {
            snprintf(buf, sizeof(buf), "Front  %.2f\"", m_FrontWheelDia);
            dl->AddText(ImVec2(p.x - 22.0f, p.y - 8.0f),
                        IM_COL32(255, 158, 51, 255), buf);
        }
        if (worldToScreen(m_RearWheelWorld, p))
        {
            snprintf(buf, sizeof(buf), "Rear  %.2f\"", m_RearWheelDia);
            dl->AddText(ImVec2(p.x - 22.0f, p.y - 8.0f),
                        IM_COL32(77, 204, 217, 255), buf);
        }
    }

    // =========================================================================
    void SF_DrivetrainCalcsApp::OnEvent(Cosmic::Event& e)
    {
        m_Camera.OnEvent(e);
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
        return new Workspace::SF_DrivetrainCalcsApp();
    }
}
