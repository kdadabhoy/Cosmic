// TelemHub.cpp — see TelemHub.h for the overview.

#include "TelemHub.h"
#include "FirmwareTemplates.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>

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
    void TelemHub::Init(Cosmic::SerialLink* link)
    {
        m_Link = link;   // shared, root-owned connection

        for (int i = 0; i < ESC_COUNT; ++i)
        {
            const auto channels = IsDrive(i) ? DriveChannelNames() : WeaponChannelNames();
            m_RecordId[i] = m_Recorder.Register(IdEntity(i), TagFor(i), channels);
        }
        m_Recorder.ReserveCapacity(k_RecordCap);

        m_Panel.SetRecorder(&m_Recorder);
        m_Panel.SetPlayer(&m_Player);
        // Point the replay loader at the same folder recordings are written to, so
        // Browse opens where the .bin/.csv files actually live.
        m_Panel.SetReplayPath(std::string(k_RecordDir) + "/");

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

        Cosmic::EntitySelection::SetByName(IdEntity(ESC_RIGHT), "Drive");

        m_Log.reserve(1 << 16);
        m_RxAccumulator.reserve(1 << 12);
        m_LastMode = m_Panel.GetMode();
        RecomputeModel();
    }

    void TelemHub::Shutdown()
    {
        // Failsafe: if an intentional recording was made but never exported, write it
        // out now so returning to the launcher / closing the app never loses it.
        if (m_RecordingDirty && m_Recorder.GetTotalFrameCount() > 0 && !m_Recorder.IsFlushing())
            m_Recorder.Flush(k_RecordDir, m_SessionName, k_SampleRate);

        m_Recorder.DisableAutosave();
        // The shared serial link is shut down by the root manager that owns it.
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

    float TelemHub::ReplayPosition() const { return m_Player.GetPosition(); }
    float TelemHub::ReplayDuration() const { return m_Player.GetDuration(); }
    void  TelemHub::SeekReplay(float seconds)
    {
        const float dur = m_Player.GetDuration();
        if (seconds < 0.0f)  seconds = 0.0f;
        if (dur > 0.0f && seconds > dur) seconds = dur;
        m_Player.SetPosition(seconds);
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

        // Port scanning + async (non-blocking) auto-reconnect are driven by the
        // root manager (the shared SerialLink persists across screens); here we
        // only drain whatever bytes have arrived while this screen is active.
        // While replaying we ignore live bytes so they can't overwrite the
        // replayed sample being shown on the dashboard.
        if (!Replaying()) PumpSerial();
        m_Panel.OnUpdate(ts);   // advances the player position when in Replay mode

        const auto mode = m_Panel.GetMode();
        if (mode != m_LastMode)
        {
            for (auto& r : m_Ring) r.Clear();
            m_LastMode = mode;
        }

        // Replay drives the live diagram: pull the player's frame at the current
        // position back into the decoded samples so the weapon/drivetrain photos,
        // readout boxes and stat panels animate with the scrubber (the rings/plots
        // are fed separately in SampleRings()).
        if (Replaying()) ApplyReplayFrame();

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
        // Reset the framing buffer on each fresh (re)connect so a partial line from
        // a previous session can't corrupt the first frame of the new one.
        if (m_Link->ConsumeJustConnected()) m_RxAccumulator.clear();

        std::string chunk = m_Link->Poll();
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
    // Replay -> live samples. Map each entity's player frame (recorded decoded
    // channels, DCH_*/WCH_* order) back into m_Drive/m_Weapon and mark it present
    // so the dashboard reads green and shows the replayed values. Entities with no
    // frame at this position are left to go stale on their own.
    // =========================================================================
    void TelemHub::ApplyReplayFrame()
    {
        for (int i = 0; i < ESC_COUNT; ++i)
        {
            Cosmic::TelemetryFrame frame;
            if (!m_Player.GetFrame(IdEntity(i), frame)) continue;

            if (IsDrive(i)) m_Drive[i] = DriveSample::FromChannels(frame.values);
            else            m_Weapon   = WeaponSample::FromChannels(frame.values);

            m_HasData[i]  = true;
            m_LastSeen[i] = m_AppClock;   // keep Present() green while scrubbing/playing
        }
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

        // Shared connection menu (ports / baud / auto-reconnect / status / connect).
        m_Link->DrawConnectionUI();

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

        ImGui::Checkbox("Auto-export on stop", &m_AutoExportOnStop);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Write the recording to %s/<session>/ automatically when you press Stop.\n"
                              "Regardless of this setting, a rolling autosave is written to %s/ every\n"
                              "%.0f s while recording, and any unexported data is flushed on exit \xE2\x80\x94 so a\n"
                              "crash or forgotten export never loses the run.",
                              k_RecordDir, k_AutoSaveDir, k_AutoSaveInterval);

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
                m_Recording      = true;
                m_RecordingDirty = true;   // user intends to keep this run
                m_RecordStatus = "Recording...";
                m_Panel.SetMode(Cosmic::TelemetryPanel::Mode::Live);
                // Crash failsafe: roll a snapshot to _autosave/ every few seconds.
                m_Recorder.SetAutosave(k_AutoSaveDir, m_SessionName, k_AutoSaveInterval, k_SampleRate);
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("  Stop  ##recstop"))
            {
                m_Recording = false;
                m_Recorder.DisableAutosave();
                if (m_AutoExportOnStop && m_Recorder.GetTotalFrameCount() > 0 && !m_Recorder.IsFlushing())
                {
                    m_Recorder.Flush(k_RecordDir, m_SessionName, k_SampleRate);
                    const std::string dest = m_SessionName.empty() ? "<timestamp>" : m_SessionName;
                    m_RecordStatus = "Exporting -> " + std::string(k_RecordDir) + "/" + dest + "/";
                    m_WasFlushing  = true;
                    m_RecordingDirty = false;
                }
                else
                {
                    m_RecordStatus = "Stopped. Ready to export.";
                }
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        const bool canExport = !m_Recording && !m_Recorder.IsFlushing()
                               && m_Recorder.GetTotalFrameCount() > 0;
        if (!canExport) ImGui::BeginDisabled();
        if (ImGui::Button("  Export CSV + bin  ##recexport"))
        {
            m_Recorder.Flush(k_RecordDir, m_SessionName, k_SampleRate);
            const std::string dest = m_SessionName.empty() ? "<timestamp>" : m_SessionName;
            m_RecordStatus = "Exporting -> " + std::string(k_RecordDir) + "/" + dest + "/";
            m_WasFlushing  = true;
            m_RecordingDirty = false;
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
    // Reusable per-ESC plot pass — interactive (zoom/pan/box), shared linked X,
    // per-plot Y autofit/caps/step (right-click), visible-range stats, and a
    // replay playhead (draggable to seek).
    // =========================================================================
    void TelemHub::DrawEscPlots(int id)
    {
        const bool   weapon = (id == ESC_WEAPON);
        const auto   names  = weapon ? WeaponChannelNames() : DriveChannelNames();
        const int    chCount= weapon ? WCH_COUNT : DCH_COUNT;
        Ring&        r      = m_Ring[id];
        const ImVec4 col    = ColorFor(id);
        PlotView&    pv     = m_PlotView;
        const bool   replay = Replaying();

        // Full data X-extent (for "Fit" and the follow window anchor).
        float xMin = 0.0f, xMax = 1.0f;
        if (r.count > 0)
        {
            xMin = r.times[r.offset % Ring::Cap];
            xMax = r.times[(r.offset + r.count - 1) % Ring::Cap];
            if (xMax <= xMin) xMax = xMin + 1.0f;
        }

        // Entering replay defaults to free interaction (follow off) and fits the
        // whole recording once, so it opens framed and ready to zoom/scrub.
        if (replay && !pv.replayDefaultApplied)
        {
            pv.follow = false;
            pv.fitRequested = true;
            pv.replayDefaultApplied = true;
        }
        if (!replay) pv.replayDefaultApplied = false;

        // ---- Toolbar (once per panel) ----
        ImGui::Checkbox(replay ? "Follow playhead" : "Follow", &pv.follow);
        if (pv.follow)
        {
            ImGui::SameLine(); ImGui::SetNextItemWidth(120);
            ImGui::SliderFloat("Window (s)", &pv.windowSec, 1.0f, 120.0f, "%.0f");
        }
        ImGui::SameLine(); if (ImGui::Button("Fit")) pv.fitRequested = true;
        ImGui::SameLine(); ImGui::Checkbox("Stats",   &pv.showStats);
        ImGui::SameLine(); ImGui::Checkbox("Min/Max", &pv.showMinMax);
        if (replay) { ImGui::SameLine(); ImGui::Checkbox("Drag to seek", &pv.seekOnDrag); }
        ImGui::TextDisabled("scroll = zoom   drag = pan   right-drag = box   right-click = Y options   dbl-click = fit");

        // ---- Drive the shared, linked X axis ----
        const float playPos = replay ? ReplayPosition() : 0.0f;
        if (pv.follow)
        {
            const float anchor = replay ? playPos : xMax;
            pv.linkXMax = anchor;
            pv.linkXMin = anchor - pv.windowSec;
        }
        if (pv.fitRequested) { pv.linkXMin = xMin; pv.linkXMax = xMax; pv.fitRequested = false; }

        for (int c = 0; c < chCount; ++c)
        {
            float predMax = 0.0f;
            if (weapon && c == WCH_WPNRPM) predMax = m_ModelResult.MaxWeaponRPM;
            if (weapon && c == WCH_TIP)    predMax = m_ModelResult.MaxTipSpeedMph;

            YAxisCfg& y = m_YCfg[id][c];
            const ImPlotAxisFlags yFlags = (y.autoFit && !y.capMin && !y.capMax)
                                         ? ImPlotAxisFlags_AutoFit : 0;

            // Full-ring data extent for this channel (used to place step ticks when
            // a side isn't hard-capped, so we don't need GetPlotLimits during setup).
            float dMin = FLT_MAX, dMax = -FLT_MAX;
            for (int i = 0; i < r.count; ++i)
            {
                const float v = r.ch[c][(r.offset + i) % Ring::Cap];
                dMin = std::min(dMin, v); dMax = std::max(dMax, v);
            }
            if (dMin > dMax) { dMin = 0.0f; dMax = 1.0f; }
            if (predMax > 0.0f) dMax = std::max(dMax, predMax);

            // Stats over the visible X-range (filled inside the plot, drawn after).
            float vMin = FLT_MAX, vMax = -FLT_MAX, vSum = 0.0f, vLast = 0.0f; int vN = 0;
            bool  hovered = false;

            // No menus (our right-click owns Y options); no box-select so a right
            // click can't be mistaken for a box drag.
            if (ImPlot::BeginPlot(names[c].c_str(), ImVec2(-1.0f, 140.0f),
                                  ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect))
            {
                // X — shared/linked across all channels in this tab.
                ImPlot::SetupAxis(ImAxis_X1, nullptr);
                ImPlot::SetupAxisLinks(ImAxis_X1, &pv.linkXMin, &pv.linkXMax);

                // Y — autofit / free / hard caps / fixed step. Any cap (one side or
                // both) fixes the axis: the capped side uses the cap value, the
                // uncapped side follows the channel's data extent (padded). This is
                // why a lone Y-max now takes effect immediately.
                ImPlot::SetupAxis(ImAxis_Y1, nullptr, yFlags);
                if (y.capMin || y.capMax)
                {
                    float lo = y.capMin ? y.yMin : dMin;
                    float hi = y.capMax ? y.yMax : dMax;
                    const float pad = std::max(1e-3f, (hi - lo) * 0.05f);
                    if (!y.capMin) lo -= pad;
                    if (!y.capMax) hi += pad;
                    if (hi <= lo) hi = lo + 1.0f;
                    ImPlot::SetupAxisLimits(ImAxis_Y1, lo, hi, ImPlotCond_Always);
                }

                if (y.useStep && y.step > 0.0f)
                {
                    const double lo = y.capMin ? (double)y.yMin : (double)dMin;
                    const double hi = y.capMax ? (double)y.yMax : (double)dMax;
                    if (hi > lo)
                    {
                        const int n = (int)((hi - lo) / y.step + 0.5) + 1;
                        if (n >= 2 && n <= 1000) ImPlot::SetupAxisTicks(ImAxis_Y1, lo, hi, n);
                    }
                }

                // Series.
                if (r.count > 0)
                {
                    ImPlotSpec spec; spec.LineColor = col; spec.Offset = r.offset;
                    ImPlot::PlotLine(names[c].c_str(), r.times.data(), r.ch[c].data(), r.count, spec);
                }

                // Predicted-max reference line spanning the visible X-range.
                if (predMax > 0.0f)
                {
                    const float px[2] = { (float)pv.linkXMin, (float)pv.linkXMax };
                    const float py[2] = { predMax, predMax };
                    ImPlotSpec ps; ps.LineColor = k_PredColor;
                    ImPlot::PlotLine("Predicted max", px, py, 2, ps);
                }

                // Stats over the currently visible X-range.
                const ImPlotRect lim = ImPlot::GetPlotLimits();
                for (int i = 0; i < r.count; ++i)
                {
                    const int   idx = (r.offset + i) % Ring::Cap;
                    const float t   = r.times[idx];
                    if (t < lim.X.Min || t > lim.X.Max) continue;
                    const float v = r.ch[c][idx];
                    vMin = std::min(vMin, v); vMax = std::max(vMax, v);
                    vSum += v; vLast = v; ++vN;
                }

                if (pv.showMinMax && vN > 0)
                {
                    ImPlot::TagY(vMin, col, "min %.1f", vMin);
                    ImPlot::TagY(vMax, col, "max %.1f", vMax);
                }

                // Replay playhead — draggable to scrub the whole dashboard.
                if (replay)
                {
                    const ImVec4 phCol(0.95f, 0.85f, 0.20f, 1.0f);
                    if (pv.seekOnDrag)
                    {
                        double p = playPos;
                        if (ImPlot::DragLineX(9000 + c, &p, phCol, 1.5f))
                            SeekReplay((float)p);
                    }
                    else
                    {
                        ImPlotSpec ph; ph.LineColor = phCol;
                        ImPlot::PlotInfLines("##playhead", &playPos, 1, ph);
                    }
                }

                hovered = ImPlot::IsPlotHovered();
                ImPlot::EndPlot();
            }

            // Right-click -> per-plot Y options popup.
            char popupId[40]; snprintf(popupId, sizeof(popupId), "##ycfg_%d_%d", id, c);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup(popupId);

            if (ImGui::BeginPopup(popupId))
            {
                ImGui::TextDisabled("%s - Y axis", names[c].c_str());

                // Auto-fit and hard caps are mutually exclusive: a cap means a fixed
                // range, so Auto-fit is disabled (and unchecked) while either cap is on.
                const bool anyCap = y.capMin || y.capMax;
                ImGui::BeginDisabled(anyCap);
                ImGui::Checkbox("Auto-fit Y", &y.autoFit);
                ImGui::EndDisabled();
                ImGui::Separator();

                if (ImGui::Checkbox("##capmin", &y.capMin) && y.capMin) y.autoFit = false;
                ImGui::SameLine();
                ImGui::BeginDisabled(!y.capMin);
                ImGui::SetNextItemWidth(130); ImGui::InputFloat("Y min", &y.yMin);
                ImGui::EndDisabled();

                if (ImGui::Checkbox("##capmax", &y.capMax) && y.capMax) y.autoFit = false;
                ImGui::SameLine();
                ImGui::BeginDisabled(!y.capMax);
                ImGui::SetNextItemWidth(130); ImGui::InputFloat("Y max", &y.yMax);
                ImGui::EndDisabled();

                ImGui::Separator();
                ImGui::Checkbox("##usestep", &y.useStep); ImGui::SameLine();
                ImGui::BeginDisabled(!y.useStep);
                ImGui::SetNextItemWidth(130); ImGui::InputFloat("Tick step", &y.step);
                ImGui::EndDisabled();

                ImGui::Separator();
                if (ImGui::Button("Reset")) y = YAxisCfg{};
                ImGui::EndPopup();
            }

            // Visible-range stats caption.
            if (pv.showStats)
            {
                if (vN > 0)
                    ImGui::TextDisabled("min %.1f   max %.1f   avg %.1f   last %.1f",
                                        vMin, vMax, vSum / (float)vN, vLast);
                else
                    ImGui::TextDisabled("(no samples in view)");
            }
        }
    }

} // namespace Workspace
