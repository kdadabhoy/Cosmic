// TestHub.cpp — see TestHub.h.

#include "TestHub.h"
#include "FirmwareTemplates.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_Live = { 0.30f, 1.00f, 0.45f, 1.0f };
        const ImVec4 k_Dead = { 1.00f, 0.35f, 0.35f, 1.0f };
        const ImVec4 k_Open = { 1.00f, 0.70f, 0.20f, 1.0f };  // open but no data yet

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

        // "(?)" hint with ESP32 pin guidance (consistent with the wiring diagram).
        void FwHelp()
        {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Enter GPIO numbers - NOT the 1-30 board positions.\n"
                    "  Drive/Right -> GPIO16  (pad RX2,  UART1 RX)\n"
                    "  Left  drive -> GPIO17  (pad TX2,  UART2 RX)\n"
                    "  Weapon      -> GPIO13  (pad D13,  UART0 RX; TX stays on GPIO1 for USB)\n"
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

    void TestHub::Init()    {}
    void TestHub::Shutdown() { if (m_Serial.IsOpen()) m_Serial.Close(); }

    bool TestHub::Stale(int id) const
    {
        return !m_HasData[id] || (m_AppClock - m_LastSeen[id]) > k_StaleTimeout;
    }

    bool TestHub::SniffActive(int id) const
    {
        return (m_AppClock - m_Sniff[id].lastSeen) < k_StaleTimeout && m_Sniff[id].intervalBytes > 0;
    }

    float TestHub::Cur(int id)   const { return id == ESC_WEAPON ? m_Weapon.currentA : m_Drive[id].currentA; }
    float TestHub::Volt(int id)  const { return id == ESC_WEAPON ? m_Weapon.voltageV : m_Drive[id].voltageV; }
    float TestHub::Rpm(int id)   const { return id == ESC_WEAPON ? m_Weapon.weaponRPM : m_Drive[id].motorRPM; }
    float TestHub::Speed(int id) const { return id == ESC_WEAPON ? 0.0f : m_Drive[id].speedMph; }

    // =========================================================================
    void TestHub::OnUpdate(float ts)
    {
        m_AppClock += std::fabs(ts);
        PumpSerial();

        // Auto-refresh the port list (~1 Hz) so freshly paired / unplugged devices
        // show up without clicking Refresh. Skip while a session is live so the
        // selection isn't disturbed mid-connection.
        m_PortScanClock += std::fabs(ts);
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
                m_ReconnectClock += std::fabs(ts);
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

        m_RateClock += std::fabs(ts);
        if (m_RateClock >= 1.0f)
        {
            for (int i = 0; i < ESC_COUNT; ++i)
            {
                m_Fps[i]       = (float)m_FpsAccumN[i] / m_RateClock; m_FpsAccumN[i] = 0;
                m_Sniff[i].bps = (float)m_SniffAccum[i] / m_RateClock; m_SniffAccum[i] = 0;
            }
            m_LinkBps = (float)m_LinkAccumBytes / m_RateClock; m_LinkAccumBytes = 0;
            m_RateClock = 0.0f;
        }
    }

    // =========================================================================
    // Append a chunk to the two accumulating raw logs: a space-separated hex dump
    // and a printable-ASCII translation (non-printables shown as '.', newlines kept
    // so frames line up). Both are capped — trim the oldest half when over the cap.
    void TestHub::AppendRaw(const std::string& chunk)
    {
        char hx[4];
        for (unsigned char b : chunk)
        {
            std::snprintf(hx, sizeof(hx), "%02X ", b);
            m_RawHex += hx;
            if (b == '\n') m_RawHex += '\n';   // break hex lines on frame boundaries (aligns with ASCII)
            m_RawAscii += (b == '\n' || (b >= 0x20 && b < 0x7F)) ? (char)b : '.';
        }
        if (m_RawHex.size()   > k_RawLogCap) m_RawHex.erase(0,   m_RawHex.size()   - k_RawLogCap / 2);
        if (m_RawAscii.size() > k_RawLogCap) m_RawAscii.erase(0, m_RawAscii.size() - k_RawLogCap / 2);
    }

    void TestHub::PumpSerial()
    {
        if (!m_Serial.IsOpen()) return;

        std::string chunk = m_Serial.FlushBuffer();
        if (chunk.empty()) return;

        m_TotalBytes     += chunk.size();
        m_LinkAccumBytes += chunk.size();
        m_LastByteTime    = m_AppClock;
        AppendRaw(chunk);

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

            HandleLine(line);
        }
    }

    void TestHub::HandleLine(const std::string& line)
    {
        // --- Raw-activity report from the sniffer sketch ---
        if (line.rfind("SNIFF,", 0) == 0)
        {
            char tag = 0; unsigned long interval = 0, total = 0; char hex[96] = { 0 };
            const int n = std::sscanf(line.c_str() + 6, "%c,%lu,%lu,%95s",
                                      &tag, &interval, &total, hex);
            if (n >= 3)
            {
                const int id = IdFromChar(tag);
                if (id >= 0 && id < ESC_COUNT)
                {
                    m_Sniff[id].total         = total;
                    m_Sniff[id].intervalBytes = interval;
                    m_Sniff[id].lastSeen      = m_AppClock;
                    m_SniffAccum[id]         += interval;
                    if (n >= 4) m_Sniff[id].lastHex = hex;
                }
            }
            return;
        }

        // --- Decoded ESC frame ---
        if (line[0] == '$')
        {
            RawPacket pkt;
            if (ParseFrame(line, pkt) && pkt.id >= 0 && pkt.id < ESC_COUNT)
            {
                const int id = pkt.id;
                if (IsDrive(id))
                {
                    m_Drive[id]    = DriveSample::Decode(pkt, m_DriveCfg);
                    m_MaxRpm[id]   = std::max(m_MaxRpm[id],   m_Drive[id].motorRPM);
                    m_MaxSpeed[id] = std::max(m_MaxSpeed[id], m_Drive[id].speedMph);
                }
                else
                {
                    m_Weapon     = WeaponSample::Decode(pkt, m_WeaponCfg);
                    m_MaxRpm[id] = std::max(m_MaxRpm[id], m_Weapon.weaponRPM);
                    m_MaxTip     = std::max(m_MaxTip,     m_Weapon.tipSpeedMph);
                }
                m_MaxCur[id]  = std::max(m_MaxCur[id],  Cur(id));
                m_MaxVolt[id] = std::max(m_MaxVolt[id], Volt(id));

                m_HasData[id]  = true;
                m_LastSeen[id] = m_AppClock;
                ++m_Good[id];
                ++m_FpsAccumN[id];
                m_LastFrame[id] = line;
            }
            else
            {
                ++m_BadFrames;
            }
            return;
        }

        // '#' heartbeats / anything else: already logged + counted as raw bytes.
    }

    // =========================================================================
    void TestHub::ResetCounts()
    {
        for (int i = 0; i < ESC_COUNT; ++i)
        {
            m_Good[i] = 0; m_FpsAccumN[i] = 0; m_Fps[i] = 0.0f;
            m_MaxCur[i] = m_MaxVolt[i] = m_MaxRpm[i] = m_MaxSpeed[i] = 0.0f;
            m_Sniff[i] = Sniff{};
            m_SniffAccum[i] = 0;
            m_HasData[i] = false;
            m_LastFrame[i].clear();
        }
        m_MaxTip = 0.0f;
        m_BadFrames = 0;
        m_TotalBytes = 0; m_LinkAccumBytes = 0; m_LinkBps = 0.0f;
        ClearRaw();
    }

    // =========================================================================
    // Serial Link UI
    // =========================================================================
    void TestHub::DrawSerialPanel()
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
                ImGui::TextColored(k_Live, "RECEIVING (%s)", m_SelectedPort.c_str());
            else
                ImGui::TextColored(k_Open, retrying ? "OPEN - no data (%s) - retrying..."
                                                    : "OPEN - no data (%s)", m_SelectedPort.c_str());
            if (ImGui::Button("Disconnect", ImVec2(-1, 0))) { m_WantConnection = false; m_Serial.Close(); }
        }
        else if (m_AutoReconnect && m_WantConnection)
        {
            // Hard drop (port closed) while the user still wants to be connected —
            // OnUpdate is retrying the reopen in the background.
            ImGui::TextColored(k_Open, "RECONNECTING (%s)...", m_SelectedPort.c_str());
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

        ImGui::Spacing();
        ImGui::Text("Link: %.0f B/s   total %llu", m_LinkBps, (unsigned long long)m_TotalBytes);
        ImGui::Text("Frames good R:%llu L:%llu W:%llu   bad:%llu",
                    (unsigned long long)m_Good[ESC_RIGHT],
                    (unsigned long long)m_Good[ESC_LEFT],
                    (unsigned long long)m_Good[ESC_WEAPON],
                    (unsigned long long)m_BadFrames);

        // ---- Arduino firmware: copy the sketch for the ACTIVE test screen ----
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Arduino Firmware", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("GPIO numbers (NOT the 1-30 board positions)");
            ImGui::SameLine(); FwHelp();

            std::string sketch;
            switch (m_ActiveTest)
            {
            case 1: // single weapon
                PinInput("Weapon##fww", m_FwWeaponPin);
                sketch = BuildSingleWeapon(m_FwWeaponPin, m_FwBtName);
                break;
            case 2: // dual drive
                PinInput("Right##fwr", m_FwRightPin);
                PinInput("Left##fwl",  m_FwLeftPin);
                sketch = BuildDualDrive(m_FwRightPin, m_FwLeftPin, m_FwBtName);
                break;
            case 3: // sniffer
                PinInput("Right##fwr",  m_FwRightPin);
                PinInput("Left##fwl",   m_FwLeftPin);
                PinInput("Weapon##fww", m_FwWeaponPin);
                ImGui::TextDisabled("Mode:"); ImGui::SameLine();
                if (ImGui::RadioButton("Raw##fwsniff", m_FwSnifferMode == 0)) m_FwSnifferMode = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("Decode##fwsniff", m_FwSnifferMode == 1)) m_FwSnifferMode = 1;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Raw counts every byte on each wire (proves telem is present).\n"
                                      "Decode runs the real self-syncing parser and reports good frames\n"
                                      "vs CRC drops (proves the bytes are VALID, not just present).");
                sketch = m_FwSnifferMode == 1
                       ? BuildDecodeSniffer(m_FwRightPin, m_FwLeftPin, m_FwWeaponPin, m_FwBtName)
                       : BuildSniffer(m_FwRightPin, m_FwLeftPin, m_FwWeaponPin, m_FwBtName);
                break;
            default: // 0 = single drive
                PinInput("Drive##fwd", m_FwRightPin);
                ImGui::TextDisabled("Side:"); ImGui::SameLine();
                if (ImGui::RadioButton("R##fwside", m_FwDriveSide == 0)) m_FwDriveSide = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("L##fwside", m_FwDriveSide == 1)) m_FwDriveSide = 1;
                sketch = BuildSingleDrive(m_FwRightPin, m_FwDriveSide == 0 ? 'R' : 'L', m_FwBtName);
                break;
            }

            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Bluetooth name", m_FwBtName, sizeof(m_FwBtName));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Name the ESP32 advertises over Bluetooth (BT_DEVICE_NAME in the\n"
                                  "sketch). Pair this name in Windows, then connect its COM port here.\n"
                                  "'Copy sketch' bakes in whatever you type.");

            ImGui::Spacing();
            if (ImGui::Button("Copy sketch (.ino)")) ImGui::SetClipboardText(sketch.c_str());
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
            ImGui::TextDisabled("Sketch follows the active test screen. Paste into Arduino IDE.");
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

} // namespace Workspace
