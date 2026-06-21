// SF_Telem_Weapon.cpp
//
// ESP32 / single weapon-motor ESC telemetry — see SF_Telem_Weapon.h for overview.

#include "SF_Telem_Weapon.h"
#include "layers/WorkspaceLayer.h"   // dock-port registration (DockWindow)

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_WeaponColor = { 1.00f, 0.55f, 0.20f, 1.0f }; // amber = weapon (measured)
        const ImVec4 k_PredColor   = { 0.30f, 1.00f, 0.45f, 1.0f }; // green = predicted
    }

    // =========================================================================
    // PlotRing
    // =========================================================================
    void SF_Telem_Weapon::PlotRing::Clear()
    {
        offset = 0;
        count  = 0;
        lastT  = -1.0f;
    }

    void SF_Telem_Weapon::PlotRing::Push(float t, const std::vector<float>& values)
    {
        const int writeIdx = (offset + count) % Cap;
        times[writeIdx] = t;
        for (int c = 0; c < WPN_CH_COUNT; ++c)
            ch[c][writeIdx] = (c < (int)values.size()) ? values[c] : 0.0f;

        if (count < Cap) ++count;
        else             offset = (offset + 1) % Cap;
        lastT = t;
    }

    // =========================================================================
    // Construction
    // =========================================================================
    SF_Telem_Weapon::SF_Telem_Weapon()
        : Cosmic::Layer("SF_Telem_Weapon")
    {
    }

    // =========================================================================
    // OnAttach
    // =========================================================================
    void SF_Telem_Weapon::OnAttach()
    {
        CS_INFO("SF_Telem_Weapon: Attaching — single weapon ESC.");

        Cosmic::FileSystem::SetActiveProject("SF_Telem_Weapon");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        m_RecordId = m_Recorder.Register(WeaponEntity(), WeaponTag(), WeaponChannelNames());
        m_Recorder.ReserveCapacity(k_RecordCapacity);

        m_Panel.SetRecorder(&m_Recorder);
        m_Panel.SetPlayer(&m_Player);

        m_Panel.RegisterTagInspector(WeaponTag(),
            [](const std::string& name, const Cosmic::TelemetryFrame& f)
            {
                ImGui::Text("Entity: %s", name.c_str());
                if (f.values.size() >= WPN_CH_COUNT)
                {
                    ImGui::Text("Temp        : %.1f C",   f.values[WPN_CH_TEMP]);
                    ImGui::Text("Voltage     : %.2f V",   f.values[WPN_CH_VOLTAGE]);
                    ImGui::Text("Current     : %.2f A",   f.values[WPN_CH_CURRENT]);
                    ImGui::Text("Consumption : %.0f mAh", f.values[WPN_CH_CONSUMPTION]);
                    ImGui::Text("eRPM        : %.0f",     f.values[WPN_CH_ERPM]);
                    ImGui::Text("Motor RPM   : %.0f",     f.values[WPN_CH_MOTOR_RPM]);
                    ImGui::Text("Weapon RPM  : %.0f",     f.values[WPN_CH_WEAPON_RPM]);
                    ImGui::Text("Tip speed   : %.1f mph", f.values[WPN_CH_TIP_SPEED]);
                    ImGui::Text("Power       : %.1f W",   f.values[WPN_CH_POWER]);
                }
            });

        // No scene is drawn — the viewport just clears. Disable manual camera
        // driving so stray WASD/scroll input does nothing surprising.
        m_Camera.SetManualMovementEnabled(false);

        m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
        Cosmic::EntitySelection::SetByName(WeaponEntity(), WeaponTag());

        m_Log.reserve(1 << 16);
        m_RxAccumulator.reserve(1 << 12);
        m_LastMode = m_Panel.GetMode();

        // Dock panels into the engine's predefined ports. Unused ports take no
        // space; Dashboard + Model share RightTop as tabs. User can still drag.
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
        {
            ws->DockWindow("Project Inspector Top",    Cosmic::DockPort::LeftTop);
            ws->DockWindow("Serial Link",              Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Weapon Dashboard",         Cosmic::DockPort::RightTop);
            ws->DockWindow("Weapon Model (Predicted)", Cosmic::DockPort::RightTop);    // tab
            ws->DockWindow("Telemetry (drill-down)",   Cosmic::DockPort::BottomCenter);
        }

        CS_INFO("SF_Telem_Weapon: OnAttach complete.");
    }

    // =========================================================================
    // OnDetach
    // =========================================================================
    void SF_Telem_Weapon::OnDetach()
    {
        m_Serial.Close();
        m_Recorder.WaitForFlush();
        Cosmic::EntitySelection::Clear();
        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("SF_Telem_Weapon: Detached.");
    }

    bool SF_Telem_Weapon::WeaponStale() const
    {
        return !m_HasData || (m_AppClock - m_LastSeen) > k_StaleTimeout;
    }

    // Tip radius (m) derived from the live decode weapon diameter (inches), so
    // the predictive model and the telemetry tip-speed share one geometry.
    float SF_Telem_Weapon::TipRadiusM() const
    {
        return (m_Config.WeaponDiameterIn * 0.0254f) * 0.5f;
    }

    // Voltage the prediction runs on: the live measured pack voltage when the
    // toggle is on and a fresh packet is in, otherwise the manual input.
    float SF_Telem_Weapon::ModelEffectiveVoltage() const
    {
        if (m_ModelUseLiveVoltage && m_HasData && !WeaponStale() && m_Sample.voltageV > 1.0f)
            return m_Sample.voltageV;
        return m_Model.BatteryVoltage;
    }

    void SF_Telem_Weapon::RecomputeModel()
    {
        WeaponModelConfig cfg = m_Model;
        cfg.BatteryVoltage = ModelEffectiveVoltage();
        m_ModelLastVoltage = cfg.BatteryVoltage;
        m_ModelResult = SimulateWeaponModel(cfg, m_Config.GearRatio, TipRadiusM());
        m_ModelDirty  = false;
    }

    // =========================================================================
    // OnUpdate
    // =========================================================================
    void SF_Telem_Weapon::OnUpdate(float ts)
    {
        m_AppClock += std::abs(ts);
        m_Camera.OnUpdate(ts);

        // When driving the prediction off live voltage, recompute as the pack
        // voltage drifts (or when it goes stale and reverts to the manual value).
        if (m_ModelUseLiveVoltage &&
            std::abs(ModelEffectiveVoltage() - m_ModelLastVoltage) > 0.05f)
            m_ModelDirty = true;

        if (m_ModelDirty) RecomputeModel();

        const bool flushing = m_Recorder.IsFlushing();
        if (m_WasFlushing && !flushing) m_RecordStatus = "Export complete.";
        m_WasFlushing = flushing;

        PumpSerial();
        m_Panel.OnUpdate(ts);

        // Clear rolling chart state when switching between Live and Replay.
        const auto mode = m_Panel.GetMode();
        if (mode != m_LastMode)
        {
            m_Ring.Clear();
            m_LastMode = mode;
        }

        SampleForDisplay();

        // Render an empty scene so the viewport is cleanly cleared each frame
        // (this app's value is the docked plots/data, not a 2D visual).
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::EndScene();
    }

    // =========================================================================
    // OnFixedUpdate — record at a fixed rate (continuous capture so live charts
    // scroll; Start Recording clears for a clean segment).
    // =========================================================================
    void SF_Telem_Weapon::OnFixedUpdate(float dt)
    {
        if (dt <= 0.0f) return;
        if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay) return;

        m_Recorder.Record(m_RecordId, m_Sample.ToChannels());
        m_Recorder.Tick(dt);
    }

    // =========================================================================
    // PumpSerial — drain the threaded reader, split lines, parse frames.
    // =========================================================================
    void SF_Telem_Weapon::PumpSerial()
    {
        if (!m_Serial.IsOpen()) return;

        std::string chunk = m_Serial.FlushBuffer();
        if (chunk.empty()) return;
        m_RxAccumulator += chunk;

        size_t nl;
        while ((nl = m_RxAccumulator.find('\n')) != std::string::npos)
        {
            std::string line = m_RxAccumulator.substr(0, nl);
            m_RxAccumulator.erase(0, nl + 1);
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
            if (line.empty()) continue;

            m_Log += line; m_Log += '\n';
            if (m_Log.size() > 100000) m_Log.erase(0, 40000);

            WeaponRawPacket pkt;
            if (ParseFrame(line, pkt))
            {
                ++m_GoodFrames;
                m_Sample   = WeaponSample::Decode(pkt, m_Config);
                m_HasData  = true;
                m_LastSeen = m_AppClock;
                ++m_PacketCount;
            }
            else
            {
                ++m_BadFrames; // '#' status / heartbeat / corruption — ignored
            }
        }

        if (m_RxAccumulator.size() > 4096) m_RxAccumulator.clear();
    }

    // =========================================================================
    // SampleForDisplay — push the latest values into the scrolling plot ring,
    // in either Live or Replay mode.
    // =========================================================================
    void SF_Telem_Weapon::SampleForDisplay()
    {
        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());

        if (replay)
        {
            const float pos = m_Player.GetPosition();
            Cosmic::TelemetryFrame frame;
            if (m_Player.GetFrame(WeaponEntity(), frame) && pos != m_Ring.lastT)
                m_Ring.Push(pos, frame.values);
        }
        else
        {
            Cosmic::TelemetryFrame frame;
            if (m_Recorder.GetCurrentFrame(WeaponEntity(), frame)
                && frame.timestamp != m_Ring.lastT)
                m_Ring.Push(frame.timestamp, frame.values);
        }
    }

    // =========================================================================
    // OnImGuiRender
    // =========================================================================
    void SF_Telem_Weapon::OnImGuiRender()
    {
        DrawControlsWindow();
        DrawSerialWindow();
        DrawDashboardWindow();
        DrawModelWindow();
        DrawTelemetryWindow();
    }

    // -------------------------------------------------------------------------
    // Controls — transport, recording, decode constants.
    // -------------------------------------------------------------------------
    void SF_Telem_Weapon::DrawControlsWindow()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 1.0f, 0.7f, 0.3f, 1.0f }, "SF Weapon Telemetry  |  Single ESC");
        ImGui::Separator();
        ImGui::Spacing();

        m_Panel.DrawTransportControls();

        ImGui::Spacing();
        ImGui::SeparatorText("Recording");
        {
            char buf[64] = {};
            strncpy_s(buf, sizeof(buf), m_SessionName.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Session##rec_name", buf, sizeof(buf)))
                m_SessionName = buf;
            ImGui::SameLine();
            ImGui::TextDisabled("(blank = timestamp)");
        }

        if (!m_Recording)
        {
            if (ImGui::Button("  Start Recording  ##rec_start"))
            {
                if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay)
                {
                    m_Player.Unload();
                    Cosmic::EntitySelection::SetByName(WeaponEntity(), WeaponTag());
                }
                m_Recorder.Clear();
                m_Recorder.ReserveCapacity(k_RecordCapacity);
                m_Ring.Clear(); // restart charts on the new timeline
                m_Recording    = true;
                m_RecordStatus = "Recording...";
                m_Panel.SetMode(Cosmic::TelemetryPanel::Mode::Live);
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("  Stop  ##rec_stop"))
            {
                m_Recording    = false;
                m_RecordStatus = "Stopped. Ready to export.";
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        const bool canExport = !m_Recording && !m_Recorder.IsFlushing()
                               && m_Recorder.GetTotalFrameCount() > 0;
        if (!canExport) ImGui::BeginDisabled();
        if (ImGui::Button("  Export CSV + bin  ##rec_export"))
        {
            m_Recorder.Flush("logs", m_SessionName, k_SampleRate);
            const std::string dest = m_SessionName.empty() ? "<timestamp>" : m_SessionName;
            m_RecordStatus = "Exporting -> logs/" + dest + "/";
            m_WasFlushing  = true;
        }
        if (!canExport) ImGui::EndDisabled();

        ImGui::Spacing();
        if (m_Recording)
            ImGui::TextColored({ 1.0f, 0.25f, 0.25f, 1.0f }, "  RECORDING");
        else
            ImGui::TextColored({ 0.6f, 0.6f, 0.6f, 1.0f }, "  Monitoring (rolling buffer)");
        if (m_Recorder.IsFlushing())
            ImGui::TextColored({ 1.0f, 0.75f, 0.1f, 1.0f }, "Status: %s", m_RecordStatus.c_str());
        else if (m_RecordStatus.rfind("Export complete", 0) == 0)
            ImGui::TextColored({ 0.2f, 1.0f, 0.35f, 1.0f }, "Status: %s", m_RecordStatus.c_str());
        else
            ImGui::Text("Status: %s", m_RecordStatus.c_str());
        ImGui::Text("Frames: %zu   Duration: %.2f s",
                    m_Recorder.GetTotalFrameCount(), m_Recorder.GetRecordedDuration());

        // ---- Decode constants ----
        ImGui::Spacing();
        ImGui::SeparatorText("Decode Constants");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputInt("Motor pole pairs", &m_Config.PolePairs);
        if (m_Config.PolePairs < 1) m_Config.PolePairs = 1;
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputFloat("Gear ratio (1=direct)", &m_Config.GearRatio, 0,0,"%.2f")) m_ModelDirty = true;
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::InputFloat("Weapon dia (in)", &m_Config.WeaponDiameterIn, 0,0,"%.2f")) m_ModelDirty = true;
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Volt scale", &m_Config.VoltageScale, 0,0,"%.4f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Curr scale", &m_Config.CurrentScale, 0,0,"%.4f");
        ImGui::TextDisabled("Tip speed = WeaponRPM * pi * dia / 1056  (mph)");

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Serial — port selection, connect, health, raw monitor.
    // -------------------------------------------------------------------------
    void SF_Telem_Weapon::DrawSerialWindow()
    {
        ImGui::Begin("Serial Link");

        if (ImGui::Button("Refresh Ports"))
        {
            m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
            if (m_SelectedPortIndex >= (int)m_AvailablePorts.size()) m_SelectedPortIndex = 0;
        }

        const char* curPort = m_AvailablePorts.empty() ? "No Ports Found"
            : (m_SelectedPortIndex < (int)m_AvailablePorts.size()
               ? m_AvailablePorts[m_SelectedPortIndex].c_str() : "Error");

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("COM Port", curPort))
        {
            for (int i = 0; i < (int)m_AvailablePorts.size(); ++i)
                if (ImGui::Selectable(m_AvailablePorts[i].c_str(), m_SelectedPortIndex == i))
                    m_SelectedPortIndex = i;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Baud", std::to_string(m_BaudRates[m_SelectedBaudIndex]).c_str()))
        {
            for (int i = 0; i < (int)m_BaudRates.size(); ++i)
                if (ImGui::Selectable(std::to_string(m_BaudRates[i]).c_str(), m_SelectedBaudIndex == i))
                    m_SelectedBaudIndex = i;
            ImGui::EndCombo();
        }

        ImGui::Separator();
        if (!m_Serial.IsOpen())
        {
            ImGui::BeginDisabled(m_AvailablePorts.empty());
            if (ImGui::Button("Connect", ImVec2(-1, 0)))
            {
                if (m_Serial.Open(m_AvailablePorts[m_SelectedPortIndex],
                                  (uint32_t)m_BaudRates[m_SelectedBaudIndex]))
                    m_RxAccumulator.clear();
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextColored({ 0.2f, 1.0f, 0.35f, 1.0f }, "CONNECTED");
            ImGui::SameLine();
            if (ImGui::Button("Disconnect", ImVec2(-1, 0))) m_Serial.Close();
        }

        ImGui::Spacing();
        ImGui::Text("Frames  good: %llu   bad: %llu",
                    (unsigned long long)m_GoodFrames, (unsigned long long)m_BadFrames);

        ImGui::Separator();
        if (ImGui::Button("Copy Log")) ImGui::SetClipboardText(m_Log.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Clear Log")) m_Log.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScrollLog);

        ImGui::BeginChild("##rawmon", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_Log.c_str());
        if (m_AutoScrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Dashboard — health banner + one chart per channel.
    // -------------------------------------------------------------------------
    void SF_Telem_Weapon::DrawDashboardWindow()
    {
        ImGui::Begin("Weapon Dashboard");

        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());

        // ---- Health banner ----
        ImGui::TextColored(k_WeaponColor, "Weapon");
        ImGui::SameLine();
        if (replay)
        {
            Cosmic::TelemetryFrame f;
            if (m_Player.GetFrame(WeaponEntity(), f) && f.values.size() >= WPN_CH_COUNT)
                ImGui::Text(": %.0f rpm   %.1f A   %.1f mph tip",
                            f.values[WPN_CH_WEAPON_RPM], f.values[WPN_CH_CURRENT],
                            f.values[WPN_CH_TIP_SPEED]);
            else
                ImGui::Text(": (no replay data)");
        }
        else if (WeaponStale())
        {
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f },
                               ": NO SIGNAL — check weapon ESC telem wire");
        }
        else
        {
            ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f },
                               ": LIVE  %.0f rpm   %.1f A   %.1f mph tip",
                               m_Sample.weaponRPM, m_Sample.currentA, m_Sample.tipSpeedMph);
        }

        // ---- Predicted vs measured headline ----
        if (m_ModelResult.MaxTipSpeedMph > 0.0f)
        {
            ImGui::TextColored(k_PredColor, "Predicted max: %.1f mph tip   %.0f rpm",
                               m_ModelResult.MaxTipSpeedMph, m_ModelResult.MaxWeaponRPM);
            if (!replay && !WeaponStale())
            {
                const float pct = 100.0f * m_Sample.tipSpeedMph / m_ModelResult.MaxTipSpeedMph;
                ImGui::SameLine();
                ImGui::Text("  |  live = %.0f%% of predicted", pct);
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Channels  (green = predicted max)");

        const auto names = WeaponChannelNames();

        // X range from the ring.
        float xMin = 0.0f, xMax = 1.0f;
        if (m_Ring.count > 0)
        {
            xMin = m_Ring.times[m_Ring.offset % PlotRing::Cap];
            xMax = m_Ring.times[(m_Ring.offset + m_Ring.count - 1) % PlotRing::Cap];
            if (xMax <= xMin) xMax = xMin + 1.0f;
        }

        for (int c = 0; c < WPN_CH_COUNT; ++c)
        {
            // Predicted steady-state reference for the channels the model covers.
            float predMax = 0.0f;
            if      (c == WPN_CH_TIP_SPEED)  predMax = m_ModelResult.MaxTipSpeedMph;
            else if (c == WPN_CH_WEAPON_RPM) predMax = m_ModelResult.MaxWeaponRPM;

            // Y range for this channel.
            float yMin = FLT_MAX, yMax = -FLT_MAX;
            for (int i = 0; i < m_Ring.count; ++i)
            {
                float v = m_Ring.ch[c][(m_Ring.offset + i) % PlotRing::Cap];
                yMin = std::min(yMin, v);
                yMax = std::max(yMax, v);
            }
            if (yMin > yMax)       { yMin = 0.0f; yMax = 1.0f; }
            else if (yMin == yMax) { yMin -= 0.5f; yMax += 0.5f; }
            if (predMax > 0.0f) { yMin = std::min(yMin, 0.0f); yMax = std::max(yMax, predMax); }
            const float pad = (yMax - yMin) * 0.1f;

            ImPlot::SetNextAxisLimits(ImAxis_X1, xMin, xMax, ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin - pad, yMax + pad, ImPlotCond_Always);

            if (ImPlot::BeginPlot(names[c].c_str(), ImVec2(-1.0f, 130.0f)))
            {
                if (m_Ring.count > 0)
                {
                    ImPlotSpec spec; spec.LineColor = k_WeaponColor;
                    spec.Offset = m_Ring.offset;
                    ImPlot::PlotLine(names[c].c_str(), m_Ring.times.data(),
                                     m_Ring.ch[c].data(), m_Ring.count, spec);
                }
                // Predicted steady-state line (flat reference across the window).
                if (predMax > 0.0f)
                {
                    const float px[2] = { xMin, xMax };
                    const float py[2] = { predMax, predMax };
                    ImPlotSpec ps; ps.LineColor = k_PredColor;
                    ImPlot::PlotLine("Predicted max", px, py, 2, ps);
                }
                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Weapon Model — predictive spin-up (ported "Weapon Speed Analysis" sheet):
    // editable inputs, derived parameters, headline KPIs, and the spin-up curve.
    // Reduction + tip diameter are shared with the live Decode Constants.
    // -------------------------------------------------------------------------
    void SF_Telem_Weapon::DrawModelWindow()
    {
        ImGui::Begin("Weapon Model (Predicted)");

        ImGui::TextColored(k_PredColor, "Full-throttle spin-up prediction");
        ImGui::SameLine(); ImGui::TextDisabled("(motor torque vs aero drag)");
        ImGui::Separator();

        ImGui::SeparatorText("Inputs");
        bool d = false;

        // Battery voltage: live measured (default) or a fixed manual value.
        if (ImGui::Checkbox("Use live battery voltage", &m_ModelUseLiveVoltage)) m_ModelDirty = true;
        ImGui::BeginDisabled(m_ModelUseLiveVoltage);
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Battery voltage (V)", &m_Model.BatteryVoltage, 0,0,"%.2f");
        ImGui::EndDisabled();
        if (m_ModelUseLiveVoltage)
        {
            ImGui::SameLine();
            if (m_HasData && !WeaponStale())
                ImGui::TextColored(k_WeaponColor, "live: %.2f V", m_ModelLastVoltage);
            else
                ImGui::TextDisabled("(no live data -> %.2f V)", m_ModelLastVoltage);
        }

        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Motor Kv (rpm/V)",      &m_Model.MotorKv, 0,0,"%.1f");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("No-load current (A)",   &m_Model.NoLoadCurrent, 0,0,"%.2f");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Max current (A)",       &m_Model.MaxCurrent, 0,0,"%.1f");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Inertia (kg m^2)",      &m_Model.Inertia, 0,0,"%.7f");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Drag coeff (Nm/rpm^2)", &m_Model.DragCoeff, 0,0,"%.3e");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Internal res (ohm)",    &m_Model.InternalRes, 0,0,"%.4f");
        ImGui::TextDisabled("(internal res shown for reference; not in the linear t-w line)");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputFloat("Sim duration (s)",      &m_Model.SimDuration, 0,0,"%.1f");
        ImGui::SetNextItemWidth(130.0f); d |= ImGui::InputInt  ("Sim steps",             &m_Model.SimSteps);
        if (m_Model.SimSteps < 2) m_Model.SimSteps = 2;
        ImGui::TextDisabled("Reduction = %.2f : 1 and tip dia = %.3f in come from Decode Constants.",
                            m_Config.GearRatio, m_Config.WeaponDiameterIn);
        if (d) m_ModelDirty = true;

        ImGui::SeparatorText("Derived");
        ImGui::Text("Kt             : %.6f Nm/A", m_ModelResult.Kt);
        ImGui::Text("Motor no-load  : %.0f rpm    stall %.3f Nm",
                    m_ModelResult.MotorNoLoadRPM, m_ModelResult.MotorStallTorque);
        ImGui::Text("Weapon no-load : %.0f rpm    stall %.3f Nm",
                    m_ModelResult.WeaponNoLoadRPM, m_ModelResult.WeaponStallTorque);
        ImGui::Text("t-w slope      : %.6f Nm/rpm", m_ModelResult.TWSlope);

        ImGui::SeparatorText("Predicted");
        ImGui::TextColored(k_PredColor, "Max tip speed  : %.1f mph", m_ModelResult.MaxTipSpeedMph);
        ImGui::TextColored(k_PredColor, "Max weapon RPM : %.0f rpm", m_ModelResult.MaxWeaponRPM);
        if (m_ModelResult.TimeTo90Pct >= 0.0f)
            ImGui::Text("Time to 90%%    : %.2f s", m_ModelResult.TimeTo90Pct);
        else
            ImGui::Text("Time to 90%%    : n/a (raise sim duration)");
        ImGui::Text("Steady-state   : %.0f rpm", m_ModelResult.SteadyStateRPM);

        // Compare against the live measurement when streaming.
        if (m_HasData && !WeaponStale() && m_ModelResult.MaxTipSpeedMph > 0.0f)
        {
            const float pct = 100.0f * m_Sample.tipSpeedMph / m_ModelResult.MaxTipSpeedMph;
            ImGui::Separator();
            ImGui::Text("Live: %.1f mph tip   %.0f rpm   (%.0f%% of predicted max)",
                        m_Sample.tipSpeedMph, m_Sample.weaponRPM, pct);
        }

        // Predicted spin-up curve.
        if (!m_ModelResult.t.empty())
        {
            const int n = (int)m_ModelResult.t.size();
            ImPlot::SetNextAxisLimits(ImAxis_X1, 0.0f, m_ModelResult.t.back(), ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, 0.0f,
                                      m_ModelResult.MaxTipSpeedMph * 1.1f + 0.001f, ImPlotCond_Always);
            if (ImPlot::BeginPlot("Predicted Tip Speed (mph) vs Time (s)", ImVec2(-1.0f, 220.0f)))
            {
                ImPlotSpec spec; spec.LineColor = k_PredColor;
                ImPlot::PlotLine("Tip speed", m_ModelResult.t.data(),
                                 m_ModelResult.tipMph.data(), n, spec);
                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Telemetry — engine panel: replay loader + drill-down charts + inspector.
    // -------------------------------------------------------------------------
    void SF_Telem_Weapon::DrawTelemetryWindow()
    {
        ImGui::Begin("Telemetry (drill-down)");
        m_Panel.OnImGuiRender();
        ImGui::End();
    }

    // =========================================================================
    // OnEvent — camera only (no entity picking; there is no rendered scene).
    // =========================================================================
    void SF_Telem_Weapon::OnEvent(Cosmic::Event& e)
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
        return new Workspace::SF_Telem_Weapon();
    }
}
