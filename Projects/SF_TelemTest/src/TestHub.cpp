// TestHub.cpp — see TestHub.h.

#include "TestHub.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_Live = { 0.30f, 1.00f, 0.45f, 1.0f };
        const ImVec4 k_Dead = { 1.00f, 0.35f, 0.35f, 1.0f };
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
    void TestHub::AppendRawTail(const std::string& chunk)
    {
        m_RawTail += chunk;
        if (m_RawTail.size() > k_RawTailCap)
            m_RawTail.erase(0, m_RawTail.size() - k_RawTailCap);
    }

    void TestHub::PumpSerial()
    {
        if (!m_Serial.IsOpen()) return;

        std::string chunk = m_Serial.FlushBuffer();
        if (chunk.empty()) return;

        m_TotalBytes     += chunk.size();
        m_LinkAccumBytes += chunk.size();
        AppendRawTail(chunk);

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
        m_RawTail.clear();
    }

    // =========================================================================
    // Serial Link UI
    // =========================================================================
    void TestHub::DrawSerialPanel()
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
            ImGui::TextColored(k_Live, "CONNECTED");
            ImGui::SameLine();
            if (ImGui::Button("Disconnect", ImVec2(-1, 0))) m_Serial.Close();
        }

        ImGui::Spacing();
        ImGui::Text("Link: %.0f B/s   total %llu", m_LinkBps, (unsigned long long)m_TotalBytes);
        ImGui::Text("Frames good R:%llu L:%llu W:%llu   bad:%llu",
                    (unsigned long long)m_Good[ESC_RIGHT],
                    (unsigned long long)m_Good[ESC_LEFT],
                    (unsigned long long)m_Good[ESC_WEAPON],
                    (unsigned long long)m_BadFrames);

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

} // namespace Workspace
