// Shear_Force_TelemApp.cpp
//
// ESP32 / dual-ESC drive telemetry — see Shear_Force_TelemApp.h for overview.

#include "Shear_Force_TelemApp.h"

#include <imgui.h>
#include <implot.h>
#include <glm/glm.hpp>
#include <glm/common.hpp>

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_RightColor = { 1.00f, 0.35f, 0.35f, 1.0f }; // red  = Right
        const ImVec4 k_LeftColor  = { 0.35f, 0.70f, 1.00f, 1.0f }; // blue = Left
    }

    // =========================================================================
    // PlotRing
    // =========================================================================
    void Shear_Force_TelemApp::PlotRing::Clear()
    {
        offset = 0;
        count  = 0;
        lastT  = -1.0f;
    }

    void Shear_Force_TelemApp::PlotRing::Push(float t, const std::vector<float>& values)
    {
        const int writeIdx = (offset + count) % Cap;
        times[writeIdx] = t;
        for (int c = 0; c < ESC_CH_COUNT; ++c)
            ch[c][writeIdx] = (c < (int)values.size()) ? values[c] : 0.0f;

        if (count < Cap) ++count;
        else             offset = (offset + 1) % Cap;
        lastT = t;
    }

    // =========================================================================
    // Construction
    // =========================================================================
    Shear_Force_TelemApp::Shear_Force_TelemApp()
        : Cosmic::Layer("Shear_Force_TelemApp")
    {
    }

    // =========================================================================
    // OnAttach
    // =========================================================================
    void Shear_Force_TelemApp::OnAttach()
    {
        CS_INFO("Shear_Force_TelemApp: Attaching — dual drive ESC.");

        Cosmic::FileSystem::SetActiveProject("Shear_Force_TelemApp");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        const auto channels = EscChannelNames();
        for (int s = 0; s < SIDE_COUNT; ++s)
            m_Side[s].recordId = m_Recorder.Register(SideEntity(s), "ESC", channels);

        m_Recorder.ReserveCapacity(k_RecordCapacity);

        m_Panel.SetRecorder(&m_Recorder);
        m_Panel.SetPlayer(&m_Player);

        m_Panel.RegisterTagInspector("ESC",
            [](const std::string& name, const Cosmic::TelemetryFrame& f)
            {
                ImGui::Text("Entity: %s", name.c_str());
                if (f.values.size() >= ESC_CH_COUNT)
                {
                    ImGui::Text("Temp        : %.1f C",   f.values[ESC_CH_TEMP]);
                    ImGui::Text("Voltage     : %.2f V",   f.values[ESC_CH_VOLTAGE]);
                    ImGui::Text("Current     : %.2f A",   f.values[ESC_CH_CURRENT]);
                    ImGui::Text("Consumption : %.0f mAh", f.values[ESC_CH_CONSUMPTION]);
                    ImGui::Text("eRPM        : %.0f",     f.values[ESC_CH_ERPM]);
                    ImGui::Text("Motor RPM   : %.0f",     f.values[ESC_CH_MOTOR_RPM]);
                    ImGui::Text("Speed       : %.2f mph", f.values[ESC_CH_SPEED]);
                    ImGui::Text("Power       : %.1f W",   f.values[ESC_CH_POWER]);
                }
            });

        m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
        Cosmic::EntitySelection::SetByName(SideEntity(SIDE_RIGHT), "ESC");

        m_Log.reserve(1 << 16);
        m_RxAccumulator.reserve(1 << 12);
        m_LastMode = m_Panel.GetMode();

        CS_INFO("Shear_Force_TelemApp: OnAttach complete.");
    }

    // =========================================================================
    // OnDetach
    // =========================================================================
    void Shear_Force_TelemApp::OnDetach()
    {
        m_Serial.Close();
        m_Recorder.WaitForFlush();
        Cosmic::EntitySelection::Clear();
        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Shear_Force_TelemApp: Detached.");
    }

    bool Shear_Force_TelemApp::SideStale(int side) const
    {
        const SideState& s = m_Side[side];
        return !s.hasData || (m_AppClock - s.lastSeen) > k_StaleTimeout;
    }

    // =========================================================================
    // OnUpdate
    // =========================================================================
    void Shear_Force_TelemApp::OnUpdate(float ts)
    {
        m_AppClock += std::abs(ts);
        m_Camera.OnUpdate(ts);

        const bool flushing = m_Recorder.IsFlushing();
        if (m_WasFlushing && !flushing) m_RecordStatus = "Export complete.";
        m_WasFlushing = flushing;

        PumpSerial();
        m_Panel.OnUpdate(ts);

        // Clear rolling state when switching between Live and Replay.
        const auto mode = m_Panel.GetMode();
        if (mode != m_LastMode)
        {
            for (auto& r : m_Ring) r.Clear();
            m_RobotPos = { 0.0f, 0.0f };
            m_RobotHeading = 1.5708f;
            m_Trail.clear();
            m_LastReplayPos = -1.0f;
            m_LastMode = mode;
        }

        SampleForDisplay(ts);
        RenderRobot();
    }

    // =========================================================================
    // OnFixedUpdate — record both sides at a fixed rate (continuous capture so
    // live charts scroll; Start Recording clears for a clean segment).
    // =========================================================================
    void Shear_Force_TelemApp::OnFixedUpdate(float dt)
    {
        if (dt <= 0.0f) return;
        if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay) return;

        for (int s = 0; s < SIDE_COUNT; ++s)
            m_Recorder.Record(m_Side[s].recordId, m_Side[s].sample.ToChannels());

        m_Recorder.Tick(dt);
    }

    // =========================================================================
    // PumpSerial — drain the threaded reader, split lines, route by side tag.
    // =========================================================================
    void Shear_Force_TelemApp::PumpSerial()
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

            EscRawPacket pkt;
            if (ParseFrame(line, pkt) && pkt.side >= 0 && pkt.side < SIDE_COUNT)
            {
                ++m_GoodFrames;
                SideState& s = m_Side[pkt.side];
                s.sample   = EscSample::Decode(pkt, m_Config);
                s.hasData  = true;
                s.lastSeen = m_AppClock;
                ++s.packetCount;
            }
            else
            {
                ++m_BadFrames; // '#' status / heartbeat / corruption — ignored
            }
        }

        if (m_RxAccumulator.size() > 4096) m_RxAccumulator.clear();
    }

    // =========================================================================
    // SampleForDisplay — fill the overlay rings and integrate the robot pose
    // from both wheel speeds, in either Live or Replay mode.
    // =========================================================================
    void Shear_Force_TelemApp::SampleForDisplay(float ts)
    {
        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());

        float vWorld[SIDE_COUNT] = { 0.0f, 0.0f };

        if (replay)
        {
            const float pos = m_Player.GetPosition();

            for (int s = 0; s < SIDE_COUNT; ++s)
            {
                Cosmic::TelemetryFrame frame;
                if (m_Player.GetFrame(SideEntity(s), frame))
                {
                    if (pos != m_Ring[s].lastT)
                        m_Ring[s].Push(pos, frame.values);
                    if (frame.values.size() > ESC_CH_SPEED)
                        vWorld[s] = frame.values[ESC_CH_SPEED] * m_SpeedScale;
                }
            }

            // Integrate over REPLAY time so the path follows the recording at
            // any playback speed; reset on a backward/large scrub (can't rebuild).
            float dpos = (m_LastReplayPos < 0.0f) ? 0.0f : (pos - m_LastReplayPos);
            m_LastReplayPos = pos;
            if (dpos < 0.0f || dpos > 0.5f)
            {
                m_RobotPos = { 0.0f, 0.0f };
                m_RobotHeading = 1.5708f;
                m_Trail.clear();
                dpos = 0.0f;
            }
            IntegratePose(vWorld[SIDE_RIGHT], vWorld[SIDE_LEFT], dpos);
        }
        else
        {
            for (int s = 0; s < SIDE_COUNT; ++s)
            {
                Cosmic::TelemetryFrame frame;
                if (m_Recorder.GetCurrentFrame(SideEntity(s), frame)
                    && frame.timestamp != m_Ring[s].lastT)
                    m_Ring[s].Push(frame.timestamp, frame.values);

                // A stale link contributes no drive (honest "no signal").
                vWorld[s] = SideStale(s) ? 0.0f
                                         : m_Side[s].sample.speedMph * m_SpeedScale;
            }
            IntegratePose(vWorld[SIDE_RIGHT], vWorld[SIDE_LEFT], std::max(ts, 0.0f));
        }
    }

    // =========================================================================
    // IntegratePose — differential-drive kinematics (speeds already in
    // world-units/second).
    //
    //   v     = (vR + vL) / 2                 forward speed
    //   omega = (vR - vL) / trackWidth        yaw rate (right faster => turn left)
    // =========================================================================
    void Shear_Force_TelemApp::IntegratePose(float vRight, float vLeft, float dt)
    {
        if (dt <= 0.0f) return;

        const float track = (m_TrackWidth > 0.05f) ? m_TrackWidth : 0.05f;
        const float v     = 0.5f * (vRight + vLeft);
        float       omega = (vRight - vLeft) / track;
        if (m_InvertHeading) omega = -omega;

        m_RobotHeading += omega * dt;
        m_RobotPos.x   += v * std::cos(m_RobotHeading) * dt;
        m_RobotPos.y   += v * std::sin(m_RobotHeading) * dt;

        m_RobotPos.x = glm::clamp(m_RobotPos.x, -4.5f, 4.5f);
        m_RobotPos.y = glm::clamp(m_RobotPos.y, -4.5f, 4.5f);

        if (m_Trail.empty() || glm::length(m_RobotPos - m_Trail.back()) > 0.03f)
        {
            m_Trail.push_back(m_RobotPos);
            if ((int)m_Trail.size() > k_TrailLength) m_Trail.pop_front();
        }
    }

    // =========================================================================
    // RenderRobot — trail + chassis + wheels (coloured by per-side health).
    // =========================================================================
    void Shear_Force_TelemApp::RenderRobot()
    {
        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());
        const bool rightOk = replay || !SideStale(SIDE_RIGHT);
        const bool leftOk  = replay || !SideStale(SIDE_LEFT);

        const float h   = m_RobotHeading;
        const glm::vec2 fwd  = { std::cos(h), std::sin(h) };
        const glm::vec2 left = { -std::sin(h), std::cos(h) };

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

        // Faint arena border.
        Cosmic::Renderer2D::DrawRect({ 0.0f, 0.0f, -0.05f }, { 9.0f, 9.0f },
                                     { 0.3f, 0.3f, 0.35f, 1.0f });

        // Trail — fading gold dots.
        const int n = (int)m_Trail.size();
        for (int i = 0; i < n; ++i)
        {
            const float t = (n > 1) ? (float)i / (n - 1) : 1.0f;
            Cosmic::Renderer2D::DrawQuad({ m_Trail[i].x, m_Trail[i].y, -0.02f },
                                         { 0.05f, 0.05f },
                                         { 1.0f, 0.85f, 0.1f, 0.1f + 0.6f * t });
        }

        const glm::vec3 pos = { m_RobotPos.x, m_RobotPos.y, 0.0f };

        // Chassis — long axis along heading.
        Cosmic::Renderer2D::DrawRotatedQuad(pos, { 0.7f, 0.45f }, h,
                                            { 0.55f, 0.58f, 0.65f, 1.0f });

        // Front indicator.
        glm::vec3 nose = { pos.x + fwd.x * 0.42f, pos.y + fwd.y * 0.42f, 0.01f };
        Cosmic::Renderer2D::DrawRotatedQuad(nose, { 0.14f, 0.30f }, h,
                                            { 1.0f, 0.9f, 0.2f, 1.0f });

        // Wheels — right (red side) and left (blue side); dim when that ESC is dead.
        const glm::vec4 rightCol = rightOk ? glm::vec4(0.2f, 0.95f, 0.4f, 1.0f)
                                           : glm::vec4(0.9f, 0.15f, 0.15f, 1.0f);
        const glm::vec4 leftCol  = leftOk  ? glm::vec4(0.2f, 0.95f, 0.4f, 1.0f)
                                           : glm::vec4(0.9f, 0.15f, 0.15f, 1.0f);
        glm::vec3 rW = { pos.x - left.x * 0.30f, pos.y - left.y * 0.30f, 0.02f };
        glm::vec3 lW = { pos.x + left.x * 0.30f, pos.y + left.y * 0.30f, 0.02f };
        Cosmic::Renderer2D::DrawRotatedQuad(rW, { 0.28f, 0.12f }, h, rightCol);
        Cosmic::Renderer2D::DrawRotatedQuad(lW, { 0.28f, 0.12f }, h, leftCol);

        Cosmic::Renderer2D::EndScene();
    }

    // =========================================================================
    // OnImGuiRender
    // =========================================================================
    void Shear_Force_TelemApp::OnImGuiRender()
    {
        DrawControlsWindow();
        DrawSerialWindow();
        DrawDashboardWindow();
        DrawTelemetryWindow();
    }

    // -------------------------------------------------------------------------
    // Controls — transport, recording, decode + kinematics constants.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawControlsWindow()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Shear Force Telemetry  |  Drive R+L");
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
                    Cosmic::EntitySelection::SetByName(SideEntity(SIDE_RIGHT), "ESC");
                }
                m_Recorder.Clear();
                m_Recorder.ReserveCapacity(k_RecordCapacity);
                for (auto& r : m_Ring) r.Clear(); // restart charts on the new timeline
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
        ImGui::SeparatorText("Decode Constants (both motors)");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputInt("Motor pole pairs", &m_Config.PolePairs);
        if (m_Config.PolePairs < 1) m_Config.PolePairs = 1;
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Gear ratio", &m_Config.GearRatio, 0,0,"%.2f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Wheel dia (in)", &m_Config.WheelDiameterIn, 0,0,"%.2f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Slip factor", &m_Config.SlipFactor, 0,0,"%.3f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Volt scale", &m_Config.VoltageScale, 0,0,"%.4f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Curr scale", &m_Config.CurrentScale, 0,0,"%.4f");

        // ---- Robot kinematics ----
        ImGui::Spacing();
        ImGui::SeparatorText("Robot Kinematics");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Track width", &m_TrackWidth, 0,0,"%.2f");
        ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Speed scale", &m_SpeedScale, 0,0,"%.3f");
        ImGui::Checkbox("Invert turn direction", &m_InvertHeading);
        if (ImGui::Button("Reset Robot Pose"))
        {
            m_RobotPos = { 0.0f, 0.0f };
            m_RobotHeading = 1.5708f;
            m_Trail.clear();
        }
        ImGui::TextDisabled("Note: KISS telemetry is unsigned — direction/reverse\n"
                            "isn't known, so the visual assumes forward drive.");

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Serial — port selection, connect, per-side health, raw monitor.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawSerialWindow()
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
    // Dashboard — per-side health banner + Right-vs-Left overlay charts.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawDashboardWindow()
    {
        ImGui::Begin("Drive Dashboard");

        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());

        // ---- Per-side health banner ----
        for (int s = 0; s < SIDE_COUNT; ++s)
        {
            const ImVec4 tint = (s == SIDE_RIGHT) ? k_RightColor : k_LeftColor;
            ImGui::TextColored(tint, "%-5s", SideLabel(s));
            ImGui::SameLine();

            if (replay)
            {
                Cosmic::TelemetryFrame f;
                if (m_Player.GetFrame(SideEntity(s), f) && f.values.size() >= ESC_CH_COUNT)
                    ImGui::Text(": %.2f mph   %.1f A   %.0f rpm",
                                f.values[ESC_CH_SPEED], f.values[ESC_CH_CURRENT],
                                f.values[ESC_CH_MOTOR_RPM]);
                else
                    ImGui::Text(": (no replay data)");
            }
            else if (SideStale(s))
            {
                ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f },
                                   ": NO SIGNAL — check %s ESC telem wire", SideLabel(s));
            }
            else
            {
                const EscSample& v = m_Side[s].sample;
                ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f },
                                   ": LIVE  %.2f mph   %.1f A   %.0f rpm",
                                   v.speedMph, v.currentA, v.motorRPM);
            }
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Right (red)  vs  Left (blue)");

        const auto names = EscChannelNames();

        auto ringSpan = [](const PlotRing& r, float& lo, float& hi)
        {
            if (r.count <= 0) return;
            float o = r.times[r.offset % PlotRing::Cap];
            float nw = r.times[(r.offset + r.count - 1) % PlotRing::Cap];
            lo = std::min(lo, o);
            hi = std::max(hi, nw);
        };

        for (int c = 0; c < ESC_CH_COUNT; ++c)
        {
            // X range across both rings.
            float xMin = FLT_MAX, xMax = -FLT_MAX;
            ringSpan(m_Ring[SIDE_RIGHT], xMin, xMax);
            ringSpan(m_Ring[SIDE_LEFT],  xMin, xMax);
            if (xMin > xMax) { xMin = 0.0f; xMax = 1.0f; }
            if (xMax <= xMin) xMax = xMin + 1.0f;

            // Y range across both rings for this channel.
            float yMin = FLT_MAX, yMax = -FLT_MAX;
            for (int s = 0; s < SIDE_COUNT; ++s)
            {
                const PlotRing& r = m_Ring[s];
                for (int i = 0; i < r.count; ++i)
                {
                    float v = r.ch[c][(r.offset + i) % PlotRing::Cap];
                    yMin = std::min(yMin, v);
                    yMax = std::max(yMax, v);
                }
            }
            if (yMin > yMax)       { yMin = 0.0f; yMax = 1.0f; }
            else if (yMin == yMax) { yMin -= 0.5f; yMax += 0.5f; }
            const float pad = (yMax - yMin) * 0.1f;

            ImPlot::SetNextAxisLimits(ImAxis_X1, xMin, xMax, ImPlotCond_Always);
            ImPlot::SetNextAxisLimits(ImAxis_Y1, yMin - pad, yMax + pad, ImPlotCond_Always);

            if (ImPlot::BeginPlot(names[c].c_str(), ImVec2(-1.0f, 130.0f)))
            {
                if (m_Ring[SIDE_RIGHT].count > 0)
                {
                    ImPlotSpec spec; spec.LineColor = k_RightColor;
                    spec.Offset = m_Ring[SIDE_RIGHT].offset;
                    ImPlot::PlotLine("Right", m_Ring[SIDE_RIGHT].times.data(),
                                     m_Ring[SIDE_RIGHT].ch[c].data(),
                                     m_Ring[SIDE_RIGHT].count, spec);
                }
                if (m_Ring[SIDE_LEFT].count > 0)
                {
                    ImPlotSpec spec; spec.LineColor = k_LeftColor;
                    spec.Offset = m_Ring[SIDE_LEFT].offset;
                    ImPlot::PlotLine("Left", m_Ring[SIDE_LEFT].times.data(),
                                     m_Ring[SIDE_LEFT].ch[c].data(),
                                     m_Ring[SIDE_LEFT].count, spec);
                }
                ImPlot::EndPlot();
            }
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Telemetry — engine panel: replay loader + single-side drill-down + inspector.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawTelemetryWindow()
    {
        ImGui::Begin("Telemetry (drill-down)");
        m_Panel.OnImGuiRender();
        ImGui::End();
    }

    // =========================================================================
    // OnEvent — camera only (the robot visual is the focus; no entity picking).
    // =========================================================================
    void Shear_Force_TelemApp::OnEvent(Cosmic::Event& e)
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
        return new Workspace::Shear_Force_TelemApp();
    }
}
