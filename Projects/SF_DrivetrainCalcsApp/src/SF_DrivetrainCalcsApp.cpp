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
        DrawInputsWindow();

        if (m_AutoRun && m_Dirty)
            Recompute();

        DrawInspectorTop();
        DrawPlotsWindow();
        DrawResultsWindow();
        DrawSchematicLabels();
    }

    // -------------------------------------------------------------------------
    // Inspector Top (docked sidebar) — headline KPIs + recompute + sync banner.
    // -------------------------------------------------------------------------
    void SF_DrivetrainCalcsApp::DrawInspectorTop()
    {
        ImGui::Begin("Project Inspector Top");

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

        // --- Front/Rear velocity-sync check ---
        ImGui::Spacing();
        ImGui::SeparatorText("Front / Rear sync");
        if (m_Sim.velMismatch)
        {
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "  VELOCITY MISMATCH");
            ImGui::TextDisabled("  Axles would fight: K_front %.5f vs K_rear %.5f m/rad",
                                m_Sim.kFront, m_Sim.kRear);
            const double rf = (m_Sim.kRear != 0.0) ? (m_Sim.kFront / m_Sim.kRear) : 0.0;
            ImGui::TextDisabled("  Front runs %.1f%% %s than rear",
                                std::abs(rf - 1.0) * 100.0, rf > 1.0 ? "faster" : "slower");
        }
        else
        {
            ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f }, "  Axles synced (K matched)");
        }

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
