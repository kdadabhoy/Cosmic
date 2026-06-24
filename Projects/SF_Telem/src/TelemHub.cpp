// TelemHub.cpp — see TelemHub.h for the overview.

#include "TelemHub.h"
#include "FirmwareTemplates.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_RightColor  = { 1.00f, 0.35f, 0.35f, 1.0f };
        const ImVec4 k_LeftColor   = { 0.35f, 0.70f, 1.00f, 1.0f };
        const ImVec4 k_WeaponColor = { 1.00f, 0.55f, 0.20f, 1.0f };
        const ImVec4 k_PredColor   = { 0.30f, 1.00f, 0.45f, 1.0f };
        const ImVec4 k_LiveColor   = { 0.30f, 1.00f, 0.40f, 1.0f };
        const ImVec4 k_DeadColor   = { 0.55f, 0.55f, 0.55f, 1.0f };

        ImVec4 ColorFor(int id)
        {
            return id == ESC_RIGHT ? k_RightColor : id == ESC_LEFT ? k_LeftColor : k_WeaponColor;
        }
        const char* TagFor(int id) { return IsDrive(id) ? "Drive" : "Weapon"; }

        // "(?)" hint with ESP32 pin guidance (consistent with the wiring diagram).
        void FwHelp()
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Enter GPIO numbers - NOT the 1-30 board positions.\n"
                    "  Right drive  -> GPIO16  (pad RX2,  UART1 RX)\n"
                    "  Left  drive  -> GPIO17  (pad TX2,  UART2 RX)\n"
                    "  Weapon       -> GPIO13  (pad D13,  UART0 RX; TX stays on GPIO1 for USB)\n"
                    "Avoid flash pins 6-11 and strapping pins 0/2/5/12/15.\n"
                    "Click Pinout for the board diagram.");
        }

        void ClampPin(int& p) { if (p < 0) p = 0; if (p > 39) p = 39; }

        // GPIO -> the silk pad label on a 30-pin ESP32-WROOM-32 dev module.
        std::string PadName(int gpio)
        {
            switch (gpio)
            {
            case 16: return "RX2"; case 17: return "TX2";
            case 1:  return "TX0"; case 3:  return "RX0";
            case 36: return "VP";  case 39: return "VN";
            default: return "D" + std::to_string(gpio);
            }
        }

        // GPIO number input that shows the live pad name beside it.
        void PinInput(const char* label, int& pin)
        {
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt(label, &pin);
            ClampPin(pin);
            ImGui::SameLine();
            ImGui::TextDisabled("= GPIO%d, pad %s", pin, PadName(pin).c_str());
        }

        // Legend shown under the pinout image (explains the board's naming scheme).
        const char* k_PinoutCaption =
            "Naming on this 30-pin ESP32 board:\n"
            "- The 1-30 numbers around the edge are PHYSICAL POSITIONS, not GPIOs - never used in code.\n"
            "- The inner labels are each pin's GPIO / function (this is what code uses):\n"
            "    'D<n>' pads  = GPIO<n>   (D13 = GPIO13, D34 = GPIO34, ...)\n"
            "    named pads   : RX2=GPIO16  TX2=GPIO17  RX0=GPIO3  TX0=GPIO1  VP=GPIO36  VN=GPIO39\n"
            "    3V3 / GND / VIN / EN are power/control pins, not GPIOs.\n"
            "- The pin fields here take GPIO numbers: 16 -> pad RX2, 17 -> pad TX2, 13 -> pad D13.\n"
            "- Avoid flash pins GPIO6-11 and strapping pins GPIO0/2/5/12/15; GPIO34-39 are input-only.";
    }

    // =========================================================================
    // Ring
    // =========================================================================
    void TelemHub::Ring::Clear() { offset = 0; count = 0; lastT = -1.0f; }

    void TelemHub::Ring::Push(float t, const std::vector<float>& values)
    {
        const int w = (offset + count) % Cap;
        times[w] = t;
        for (int c = 0; c < WCH_COUNT; ++c)
            ch[c][w] = (c < (int)values.size()) ? values[c] : 0.0f;
        if (count < Cap) ++count;
        else             offset = (offset + 1) % Cap;
        lastT = t;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================
    void TelemHub::Init()
    {
        for (int i = 0; i < ESC_COUNT; ++i)
        {
            const auto channels = IsDrive(i) ? DriveChannelNames() : WeaponChannelNames();
            m_RecordId[i] = m_Recorder.Register(IdEntity(i), TagFor(i), channels);
        }
        m_Recorder.ReserveCapacity(k_RecordCap);

        m_Panel.SetRecorder(&m_Recorder);
        m_Panel.SetPlayer(&m_Player);

        m_Panel.RegisterTagInspector("Drive",
            [](const std::string& name, const Cosmic::TelemetryFrame& f)
            {
                ImGui::Text("Entity: %s", name.c_str());
                if (f.values.size() >= DCH_COUNT)
                {
                    ImGui::Text("Temp        : %.1f C",   f.values[DCH_TEMP]);
                    ImGui::Text("Voltage     : %.2f V",   f.values[DCH_VOLT]);
                    ImGui::Text("Current     : %.2f A",   f.values[DCH_CURR]);
                    ImGui::Text("Consumption : %.0f mAh", f.values[DCH_CONS]);
                    ImGui::Text("eRPM        : %.0f",     f.values[DCH_ERPM]);
                    ImGui::Text("Motor RPM   : %.0f",     f.values[DCH_MOTRPM]);
                    ImGui::Text("Speed       : %.2f mph", f.values[DCH_SPEED]);
                    ImGui::Text("Power       : %.1f W",   f.values[DCH_POWER]);
                }
            });
        m_Panel.RegisterTagInspector("Weapon",
            [](const std::string& name, const Cosmic::TelemetryFrame& f)
            {
                ImGui::Text("Entity: %s", name.c_str());
                if (f.values.size() >= WCH_COUNT)
                {
                    ImGui::Text("Temp        : %.1f C",   f.values[WCH_TEMP]);
                    ImGui::Text("Voltage     : %.2f V",   f.values[WCH_VOLT]);
                    ImGui::Text("Current     : %.2f A",   f.values[WCH_CURR]);
                    ImGui::Text("Consumption : %.0f mAh", f.values[WCH_CONS]);
                    ImGui::Text("eRPM        : %.0f",     f.values[WCH_ERPM]);
                    ImGui::Text("Motor RPM   : %.0f",     f.values[WCH_MOTRPM]);
                    ImGui::Text("Weapon RPM  : %.0f",     f.values[WCH_WPNRPM]);
                    ImGui::Text("Tip speed   : %.1f mph", f.values[WCH_TIP]);
                    ImGui::Text("Power       : %.1f W",   f.values[WCH_POWER]);
                }
            });

        m_Ports = Cosmic::SerialPort::GetAvailablePorts();
        Cosmic::EntitySelection::SetByName(IdEntity(ESC_RIGHT), "Drive");

        m_Log.reserve(1 << 16);
        m_RxAccumulator.reserve(1 << 12);
        m_LastMode = m_Panel.GetMode();
        RecomputeModel();
    }

    void TelemHub::Shutdown()
    {
        m_Serial.Close();
        m_Recorder.WaitForFlush();
        Cosmic::EntitySelection::Clear();
    }

    // =========================================================================
    // Queries
    // =========================================================================
    bool TelemHub::Stale(int id) const
    {
        return !m_HasData[id] || (m_AppClock - m_LastSeen[id]) > k_StaleTimeout;
    }
    bool TelemHub::AnyPresent() const
    {
        for (int i = 0; i < ESC_COUNT; ++i) if (Present(i)) return true;
        return false;
    }
    bool TelemHub::Replaying() const
    {
        return m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay && m_Player.IsLoaded();
    }

    float TelemHub::Cur(int id)  const { return IsDrive(id) ? m_Drive[id].currentA : m_Weapon.currentA; }
    float TelemHub::Volt(int id) const { return IsDrive(id) ? m_Drive[id].voltageV : m_Weapon.voltageV; }
    float TelemHub::Rpm(int id)  const { return IsDrive(id) ? m_Drive[id].motorRPM : m_Weapon.weaponRPM; }
    float TelemHub::Speed(int id)const { return IsDrive(id) ? m_Drive[id].speedMph : 0.0f; }

    float TelemHub::PredictedRpm(int id) const
    {
        if (IsDrive(id)) return m_DriveCfg.MotorKv * m_Drive[id].voltageV; // no-load motor RPM
        return m_ModelResult.MaxWeaponRPM;                                 // weapon steady-state
    }

    void TelemHub::ResetMax(int id)
    {
        m_MaxCur[id] = m_MaxVolt[id] = m_MaxRpm[id] = m_MaxSpeed[id] = 0.0f;
        if (id == ESC_WEAPON) m_MaxTip = 0.0f;

        // Clear the running-average accumulators for this ESC too.
        m_SumCur[id] = m_SumVolt[id] = m_SumRpm[id] = m_SumSpeed[id] = 0.0;
        if (id == ESC_WEAPON) m_SumTip = 0.0;
        m_StatCount[id] = 0;
    }
    void TelemHub::ResetMaxAll() { for (int i = 0; i < ESC_COUNT; ++i) ResetMax(i); }

    // =========================================================================
    // Predicted weapon model
    // =========================================================================
    float TelemHub::ModelEffectiveVoltage() const
    {
        if (m_ModelUseLiveVoltage && m_HasData[ESC_WEAPON] && !Stale(ESC_WEAPON)
            && m_Weapon.voltageV > 1.0f)
            return m_Weapon.voltageV;
        return m_Model.BatteryVoltage;
    }
    void TelemHub::RecomputeModel()
    {
        WeaponModelConfig cfg = m_Model;
        cfg.BatteryVoltage = ModelEffectiveVoltage();
        m_ModelLastVoltage = cfg.BatteryVoltage;
        m_ModelResult = SimulateWeaponModel(cfg, m_WeaponCfg.GearRatio, TipRadiusM());
        m_ModelDirty  = false;
    }

    // =========================================================================
    // Update
    // =========================================================================
    void TelemHub::OnUpdate(float ts)
    {
        m_AppClock += std::abs(ts);

        // Windowed averaging: periodically clear max+average so the displayed
        // stats reflect the most recent window (0 = accumulate until manual reset).
        if (m_StatsWindowSec > 0.0f && (m_AppClock - m_LastStatsReset) >= m_StatsWindowSec)
        {
            ResetMaxAll();
            m_LastStatsReset = m_AppClock;
        }

        const bool flushing = m_Recorder.IsFlushing();
        if (m_WasFlushing && !flushing) m_RecordStatus = "Export complete.";
        m_WasFlushing = flushing;

        PumpSerial();
        m_Panel.OnUpdate(ts);

        const auto mode = m_Panel.GetMode();
        if (mode != m_LastMode)
        {
            for (auto& r : m_Ring) r.Clear();
            m_LastMode = mode;
        }

        if (m_ModelUseLiveVoltage &&
            std::abs(ModelEffectiveVoltage() - m_ModelLastVoltage) > 0.05f)
            m_ModelDirty = true;
        if (m_ModelDirty) RecomputeModel();

        SampleRings();
    }

    void TelemHub::RecordFixed(float dt)
    {
        if (dt <= 0.0f) return;
        if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay) return;

        // Record all three entities every tick (absent ESCs record zeros), so the
        // CSV columns stay consistent and the live charts always scroll.
        m_Recorder.Record(m_RecordId[ESC_RIGHT],  m_Drive[ESC_RIGHT].ToChannels());
        m_Recorder.Record(m_RecordId[ESC_LEFT],   m_Drive[ESC_LEFT].ToChannels());
        m_Recorder.Record(m_RecordId[ESC_WEAPON], m_Weapon.ToChannels());
        m_Recorder.Tick(dt);
    }

    // =========================================================================
    // Serial pump — drain, split, route by tag, decode, update stats.
    // =========================================================================
    void TelemHub::PumpSerial()
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

            RawPacket pkt;
            if (ParseFrame(line, pkt) && pkt.id >= 0 && pkt.id < ESC_COUNT)
            {
                ++m_GoodFrames;
                const int id = pkt.id;
                if (IsDrive(id))
                {
                    m_Drive[id] = DriveSample::Decode(pkt, m_DriveCfg);
                    m_MaxRpm[id]   = std::max(m_MaxRpm[id],   m_Drive[id].motorRPM);
                    m_MaxSpeed[id] = std::max(m_MaxSpeed[id], m_Drive[id].speedMph);
                }
                else
                {
                    m_Weapon = WeaponSample::Decode(pkt, m_WeaponCfg);
                    m_MaxRpm[id] = std::max(m_MaxRpm[id], m_Weapon.weaponRPM);
                    m_MaxTip     = std::max(m_MaxTip,     m_Weapon.tipSpeedMph);
                }
                m_MaxCur[id]  = std::max(m_MaxCur[id],  Cur(id));
                m_MaxVolt[id] = std::max(m_MaxVolt[id], Volt(id));

                // Accumulate for the running average (per received sample).
                m_SumCur[id]  += Cur(id);
                m_SumVolt[id] += Volt(id);
                if (IsDrive(id)) { m_SumRpm[id] += m_Drive[id].motorRPM; m_SumSpeed[id] += m_Drive[id].speedMph; }
                else             { m_SumRpm[id] += m_Weapon.weaponRPM;   m_SumTip       += m_Weapon.tipSpeedMph; }
                ++m_StatCount[id];

                m_HasData[id]  = true;
                m_LastSeen[id] = m_AppClock;
                ++m_PacketCount[id];
            }
            else
            {
                ++m_BadFrames; // '#' heartbeat / corruption — ignored
            }
        }

        if (m_RxAccumulator.size() > 4096) m_RxAccumulator.clear();
    }

    // =========================================================================
    // Ring sampling — live (recorder) or replay (player).
    // =========================================================================
    void TelemHub::SampleRings()
    {
        if (Replaying())
        {
            const float pos = m_Player.GetPosition();
            for (int i = 0; i < ESC_COUNT; ++i)
            {
                Cosmic::TelemetryFrame frame;
                if (m_Player.GetFrame(IdEntity(i), frame) && pos != m_Ring[i].lastT)
                    m_Ring[i].Push(pos, frame.values);
            }
        }
        else
        {
            for (int i = 0; i < ESC_COUNT; ++i)
            {
                Cosmic::TelemetryFrame frame;
                if (m_Recorder.GetCurrentFrame(IdEntity(i), frame)
                    && frame.timestamp != m_Ring[i].lastT)
                    m_Ring[i].Push(frame.timestamp, frame.values);
            }
        }
    }

    // =========================================================================
    // Shared UI — Serial Link
    // =========================================================================
    void TelemHub::DrawSerialPanel()
    {
        ImGui::Begin("Serial Link");

        if (ImGui::Button("Refresh Ports"))
        {
            m_Ports = Cosmic::SerialPort::GetAvailablePorts();
            if (m_PortIndex >= (int)m_Ports.size()) m_PortIndex = 0;
        }

        const char* curPort = m_Ports.empty() ? "No Ports Found"
            : (m_PortIndex < (int)m_Ports.size() ? m_Ports[m_PortIndex].c_str() : "Error");

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("COM Port", curPort))
        {
            for (int i = 0; i < (int)m_Ports.size(); ++i)
                if (ImGui::Selectable(m_Ports[i].c_str(), m_PortIndex == i)) m_PortIndex = i;
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Baud", std::to_string(m_BaudRates[m_BaudIndex]).c_str()))
        {
            for (int i = 0; i < (int)m_BaudRates.size(); ++i)
                if (ImGui::Selectable(std::to_string(m_BaudRates[i]).c_str(), m_BaudIndex == i)) m_BaudIndex = i;
            ImGui::EndCombo();
        }

        ImGui::Separator();
        if (!m_Serial.IsOpen())
        {
            ImGui::BeginDisabled(m_Ports.empty());
            if (ImGui::Button("Connect", ImVec2(-1, 0)))
                if (m_Serial.Open(m_Ports[m_PortIndex], (uint32_t)m_BaudRates[m_BaudIndex]))
                    m_RxAccumulator.clear();
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextColored(k_LiveColor, "CONNECTED");
            ImGui::SameLine();
            if (ImGui::Button("Disconnect", ImVec2(-1, 0))) m_Serial.Close();
        }

        // ---- Per-ESC presence (so a missing wire is obvious) ----
        ImGui::Spacing();
        ImGui::SeparatorText("ESC signals");
        for (int i = 0; i < ESC_COUNT; ++i)
        {
            ImGui::TextColored(ColorFor(i), "%-7s", IdLabel(i));
            ImGui::SameLine();
            if (Present(i))      ImGui::TextColored(k_LiveColor, ": LIVE  (%llu pkts)", (unsigned long long)m_PacketCount[i]);
            else if (m_HasData[i]) ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, ": STALE");
            else                 ImGui::TextColored(k_DeadColor, ": no signal");
        }

        ImGui::Spacing();
        ImGui::Text("Frames  good: %llu   bad: %llu",
                    (unsigned long long)m_GoodFrames, (unsigned long long)m_BadFrames);

        // ---- Arduino firmware: copy a ready-to-flash sketch with your pins ----
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Arduino Firmware"))
        {
            ImGui::TextDisabled("GPIO numbers (NOT the 1-30 board positions)");
            ImGui::SameLine(); FwHelp();

            PinInput("Right##fwr",  m_FwRightPin);
            PinInput("Left##fwl",   m_FwLeftPin);
            PinInput("Weapon##fww", m_FwWeaponPin);

            ImGui::Spacing();
            if (ImGui::Button("Copy firmware (.ino)"))
                ImGui::SetClipboardText(BuildMainFirmware(m_FwRightPin, m_FwLeftPin, m_FwWeaponPin).c_str());
            ImGui::SameLine();
            if (ImGui::Button("Copy simulator"))
                ImGui::SetClipboardText(BuildSimulatorFirmware().c_str());
            ImGui::SameLine();
            if (ImGui::Button("Pinout"))
            {
                m_ShowPinout = !m_ShowPinout;
                if (m_ShowPinout && !m_PinoutTex)
                    m_PinoutTex = Cosmic::Texture2D::Create(
                        Cosmic::FileSystem::Resolve("project://images/ESP32_Dev_Pin_Layout.png"));
            }
            ImGui::TextDisabled("Paste into the Arduino IDE and upload.");
        }

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

        // ESP32 pinout reference — its own floating window (toggled above).
        Cosmic::UI::ImageWindow("ESP32 Pinout", m_PinoutTex, &m_ShowPinout, k_PinoutCaption);
    }

    // =========================================================================
    // Shared UI — Recording (drawn inside the caller's window)
    // =========================================================================
    void TelemHub::DrawRecordingControls()
    {
        m_Panel.DrawTransportControls();

        ImGui::Spacing();
        ImGui::SeparatorText("Recording");
        {
            char buf[64] = {};
            strncpy_s(buf, sizeof(buf), m_SessionName.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Session##rec", buf, sizeof(buf))) m_SessionName = buf;
            ImGui::SameLine();
            ImGui::TextDisabled("(blank = timestamp)");
        }

        if (!m_Recording)
        {
            if (ImGui::Button("  Start Recording  ##recstart"))
            {
                if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay)
                {
                    m_Player.Unload();
                    Cosmic::EntitySelection::SetByName(IdEntity(ESC_RIGHT), "Drive");
                }
                m_Recorder.Clear();
                m_Recorder.ReserveCapacity(k_RecordCap);
                for (auto& r : m_Ring) r.Clear();
                m_Recording    = true;
                m_RecordStatus = "Recording...";
                m_Panel.SetMode(Cosmic::TelemetryPanel::Mode::Live);
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("  Stop  ##recstop"))
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
        if (ImGui::Button("  Export CSV + bin  ##recexport"))
        {
            m_Recorder.Flush("logs", m_SessionName, k_SampleRate);
            const std::string dest = m_SessionName.empty() ? "<timestamp>" : m_SessionName;
            m_RecordStatus = "Exporting -> logs/" + dest + "/";
            m_WasFlushing  = true;
        }
        if (!canExport) ImGui::EndDisabled();

        ImGui::Spacing();
        if (m_Recording) ImGui::TextColored({ 1.0f, 0.25f, 0.25f, 1.0f }, "  RECORDING");
        else             ImGui::TextColored({ 0.6f, 0.6f, 0.6f, 1.0f }, "  Monitoring (rolling buffer)");
        if (m_Recorder.IsFlushing())
            ImGui::TextColored({ 1.0f, 0.75f, 0.1f, 1.0f }, "Status: %s", m_RecordStatus.c_str());
        else if (m_RecordStatus.rfind("Export complete", 0) == 0)
            ImGui::TextColored(k_LiveColor, "Status: %s", m_RecordStatus.c_str());
        else
            ImGui::Text("Status: %s", m_RecordStatus.c_str());
        ImGui::Text("Frames: %zu   Duration: %.2f s",
                    m_Recorder.GetTotalFrameCount(), m_Recorder.GetRecordedDuration());
    }

    // =========================================================================
    // Reusable per-ESC plot pass
    // =========================================================================
    void TelemHub::DrawEscPlots(int id)
    {
        const bool weapon = (id == ESC_WEAPON);
        const auto names  = weapon ? WeaponChannelNames() : DriveChannelNames();
        const int  chCount= weapon ? WCH_COUNT : DCH_COUNT;
        Ring&      r      = m_Ring[id];
        const ImVec4 col  = ColorFor(id);

        float xMin = 0.0f, xMax = 1.0f;
        if (r.count > 0)
        {
            xMin = r.times[r.offset % Ring::Cap];
            xMax = r.times[(r.offset + r.count - 1) % Ring::Cap];
            if (xMax <= xMin) xMax = xMin + 1.0f;
        }

        for (int c = 0; c < chCount; ++c)
        {
            float predMax = 0.0f;
            if (weapon && c == WCH_WPNRPM) predMax = m_ModelResult.MaxWeaponRPM;
            if (weapon && c == WCH_TIP)    predMax = m_ModelResult.MaxTipSpeedMph;

            float yMin = FLT_MAX, yMax = -FLT_MAX;
            for (int i = 0; i < r.count; ++i)
            {
                float v = r.ch[c][(r.offset + i) % Ring::Cap];
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
                if (r.count > 0)
                {
                    ImPlotSpec spec; spec.LineColor = col; spec.Offset = r.offset;
                    ImPlot::PlotLine(names[c].c_str(), r.times.data(), r.ch[c].data(), r.count, spec);
                }
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
    }

} // namespace Workspace
