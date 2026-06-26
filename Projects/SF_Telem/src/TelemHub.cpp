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
        const ImVec4 k_OpenColor   = { 1.00f, 0.70f, 0.20f, 1.0f };  // open but no data yet

        ImVec4 ColorFor(int id)
        {
            return id == ESC_RIGHT ? k_RightColor : id == ESC_LEFT ? k_LeftColor : k_WeaponColor;
        }

        // Re-scan the available COM ports, keeping the current selection by NAME so
        // a changing list can never leave us pointed at the wrong port. Falls back
        // to the first port (or empty) when the selected one disappears.
        void RefreshPortList(std::vector<std::string>& ports, std::string& selected)
        {
            ports = Cosmic::SerialPort::GetAvailablePorts();
            if (ports.empty()) { selected.clear(); return; }
            if (std::find(ports.begin(), ports.end(), selected) == ports.end())
                selected = ports.front();
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
            // InputInt's -/+ step buttons sit INSIDE the item width and scale with
            // FramePadding, so a fixed 80px left almost no room for the digits with
            // the modern (roomier) themes. Size the field off the actual frame
            // height: two square step buttons + spacing + room for ~3 digits.
            const float btn = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth(btn * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f + 56.0f);
            ImGui::InputInt(label, &pin);
            ClampPin(pin);
            ImGui::SameLine();
            ImGui::TextDisabled("= GPIO%d, pad %s", pin, PadName(pin).c_str());
        }

        // Legend shown under the pinout image (explains the board's naming scheme).
        const char* k_PinoutCaption =
            "Reference diagram for a 30-pin ESP32-WROOM-32 dev module. Use it to translate the\n"
            "GPIO numbers the firmware needs into the pads silk-screened on your board.\n"
            "\n"
            "Reading the board:\n"
            "- The 1-30 numbers around the edge are PHYSICAL pin POSITIONS, not GPIOs - never used in code.\n"
            "- The inner labels are each pin's GPIO / function - this is what the code and the pin fields use:\n"
            "    'D<n>' pads  = GPIO<n>      (D13 = GPIO13, D34 = GPIO34, ...)\n"
            "    named pads   : RX2=GPIO16  TX2=GPIO17  RX0=GPIO3  TX0=GPIO1  VP=GPIO36  VN=GPIO39\n"
            "    3V3 / GND / VIN / EN are power/control pins, not GPIOs.\n"
            "\n"
            "This project's wiring:  Right = GPIO16 (RX2)   Left = GPIO17 (TX2)   Weapon = GPIO13 (D13).\n"
            "Enter those GPIO numbers in the pin fields above (16 -> pad RX2, 17 -> pad TX2, 13 -> pad D13).\n"
            "Avoid flash pins GPIO6-11 and strapping pins GPIO0/2/5/12/15; GPIO34-39 are input-only.";
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

        RefreshPortList(m_Ports, m_SelectedPort);
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

        // Auto-refresh the port list (~1 Hz) so freshly paired / unplugged devices
        // show up without clicking Refresh. Skip while a session is live so the
        // selection isn't disturbed mid-connection.
        m_PortScanClock += std::abs(ts);
        if (m_PortScanClock >= 1.0f)
        {
            m_PortScanClock = 0.0f;
            if (!m_Serial.IsOpen())
                RefreshPortList(m_Ports, m_SelectedPort);
        }

        // Auto-reconnect: if the user asked to stay connected but data has stopped
        // (soft Bluetooth stall while still "open", or a hard unplug), periodically
        // drop and reopen the selected port until the stream returns. Toggle off
        // (m_AutoReconnect) to only ever connect manually.
        if (m_AutoReconnect && m_WantConnection)
        {
            const bool receiving = (m_AppClock - m_LastByteTime) < 1.0f;
            if (receiving) m_ReconnectClock = 0.0f;
            else
            {
                m_ReconnectClock += std::abs(ts);
                if (m_ReconnectClock >= k_ReconnectInterval)
                {
                    m_ReconnectClock = 0.0f;
                    if (m_Serial.IsOpen()) m_Serial.Close();
                    RefreshPortList(m_Ports, m_SelectedPort);
                    if (!m_SelectedPort.empty()
                        && m_Serial.Open(m_SelectedPort, (uint32_t)m_BaudRates[m_BaudIndex]))
                        m_RxAccumulator.clear();
                }
            }
        }

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
        m_LastByteTime = m_AppClock;
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
            RefreshPortList(m_Ports, m_SelectedPort);
        ImGui::SameLine();
        ImGui::TextDisabled("(auto-refreshes)");

        const char* curPort = m_SelectedPort.empty() ? "No Ports Found" : m_SelectedPort.c_str();

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("COM Port", curPort))
        {
            for (const auto& p : m_Ports)
                if (ImGui::Selectable(p.c_str(), p == m_SelectedPort)) m_SelectedPort = p;
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
        ImGui::Checkbox("Auto-reconnect", &m_AutoReconnect);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Automatically re-open the selected port and resume the stream\n"
                              "if data stops (e.g. the ESP32 was unplugged or power-cycled).\n"
                              "Turn off to only connect manually.");

        if (m_Serial.IsOpen())
        {
            // Distinguish "port open" from "actually receiving": a Bluetooth COM
            // port opens fine even when the ESP32 isn't streaming, so flag a dead
            // link instead of falsely claiming a good connection.
            const bool receiving = (m_AppClock - m_LastByteTime) < 1.0f;
            const bool retrying  = m_AutoReconnect && m_WantConnection;
            if (receiving)
                ImGui::TextColored(k_LiveColor, "RECEIVING (%s)", m_SelectedPort.c_str());
            else
                ImGui::TextColored(k_OpenColor, retrying ? "OPEN - no data (%s) - retrying..."
                                                         : "OPEN - no data (%s)", m_SelectedPort.c_str());
            if (ImGui::Button("Disconnect", ImVec2(-1, 0))) { m_WantConnection = false; m_Serial.Close(); }
        }
        else if (m_AutoReconnect && m_WantConnection)
        {
            // Hard drop (port closed) while the user still wants to be connected —
            // OnUpdate is retrying the reopen in the background.
            ImGui::TextColored(k_OpenColor, "RECONNECTING (%s)...", m_SelectedPort.c_str());
            if (ImGui::Button("Stop / Disconnect", ImVec2(-1, 0))) m_WantConnection = false;
        }
        else
        {
            ImGui::BeginDisabled(m_SelectedPort.empty());
            if (ImGui::Button("Connect", ImVec2(-1, 0)))
                if (m_Serial.Open(m_SelectedPort, (uint32_t)m_BaudRates[m_BaudIndex]))
                {
                    m_RxAccumulator.clear();
                    m_WantConnection = true;
                }
            ImGui::EndDisabled();
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
        if (ImGui::CollapsingHeader("Arduino Firmware", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("GPIO numbers (NOT the 1-30 board positions)");
            ImGui::SameLine(); FwHelp();

            PinInput("Right##fwr",  m_FwRightPin);
            PinInput("Left##fwl",   m_FwLeftPin);
            PinInput("Weapon##fww", m_FwWeaponPin);

            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Bluetooth name", m_FwBtName, sizeof(m_FwBtName));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Name the ESP32 advertises over Bluetooth (BT_DEVICE_NAME in the\n"
                                  "sketch). Pair this name in Windows, then connect its COM port here.\n"
                                  "Both 'Copy' buttons below bake in whatever you type.");

            ImGui::Spacing();
            if (ImGui::Button("Copy firmware (.ino)"))
                ImGui::SetClipboardText(BuildMainFirmware(m_FwRightPin, m_FwLeftPin, m_FwWeaponPin, m_FwBtName).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The real flight sketch. Reads all three ESC telemetry UARTs and re-streams\n"
                    "them over Bluetooth, with the GPIO pins above baked into the #defines.\n"
                    "Use this once the ESP32 is wired to the ESCs.");
            ImGui::SameLine();
            if (ImGui::Button("Copy simulator"))
                ImGui::SetClipboardText(BuildSimulatorFirmware(m_FwBtName).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "A self-contained test sketch - NO ESCs and NO wiring needed.\n"
                    "It synthesizes 2 drive + 1 weapon ESC in the exact wire format and streams\n"
                    "them over Bluetooth (+USB) at ~40 Hz: the drives weave left/right and the\n"
                    "weapon runs a 12 s spin-up / hold / spin-down cycle.\n"
                    "Flash it to prove the Bluetooth link and the host decode/plot pipeline\n"
                    "before connecting any real ESC.");
            ImGui::SameLine();
            if (ImGui::Button("Pinout"))
            {
                m_ShowPinout = !m_ShowPinout;
                if (m_ShowPinout && !m_PinoutTex)
                    m_PinoutTex = Cosmic::Texture2D::Create(
                        Cosmic::FileSystem::Resolve("project://images/ESP32_Dev_Pin_Layout.png"));
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Toggle the 30-pin ESP32 board diagram, with a legend mapping\n"
                                  "the silk pad names to GPIO numbers.");
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
        // Always follow the tail while Auto-scroll is on (not only when already at the
        // bottom) so it keeps tracking no matter where the user last scrolled.
        if (m_AutoScrollLog) ImGui::SetScrollHereY(1.0f);
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
