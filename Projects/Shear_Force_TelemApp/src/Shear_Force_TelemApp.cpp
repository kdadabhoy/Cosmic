// Shear_Force_TelemApp.cpp
//
// ESP32 / ESC telemetry application — see Shear_Force_TelemApp.h for overview.

#include "Shear_Force_TelemApp.h"

#include <imgui.h>
#include <implot.h>
#include <glm/glm.hpp>
#include <glm/common.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace Workspace
{
    // =========================================================================
    // Visual mapping — shared by live + replay so the square looks identical
    // in both. Channels are EscChannel-ordered (see EscTelemetry.h).
    // =========================================================================
    namespace
    {
        constexpr float k_SpeedFullScaleMph = 30.0f; // speed that pins square to edge
        constexpr float k_TempHotC          = 80.0f; // temp that pins colour to red
        constexpr float k_RowSpacing        = 1.4f;
        constexpr float k_SquareSize        = 0.6f;

        // Map a channel vector -> world position + colour for ESC row `row`.
        void MapSquare(const std::vector<float>& ch, int row,
                       glm::vec3& outPos, glm::vec4& outColor)
        {
            float speed = (ch.size() > ESC_CH_SPEED) ? ch[ESC_CH_SPEED] : 0.0f;
            float temp  = (ch.size() > ESC_CH_TEMP)  ? ch[ESC_CH_TEMP]  : 0.0f;

            float nx = glm::clamp(speed / k_SpeedFullScaleMph, -1.0f, 1.0f);
            float y  = (1 - row) * k_RowSpacing; // row 0 centred, others stacked down

            outPos = { nx * 2.5f, y, 0.0f };

            float hot = glm::clamp(temp / k_TempHotC, 0.0f, 1.0f);
            outColor  = glm::vec4(0.15f + 0.85f * hot,        // R rises with temp
                                  0.55f * (1.0f - hot) + 0.15f,
                                  1.0f - 0.85f * hot,         // B falls with temp
                                  1.0f);
        }
    }

    // =========================================================================
    // Construction
    // =========================================================================
    Shear_Force_TelemApp::Shear_Force_TelemApp()
        : Cosmic::Layer("Shear_Force_TelemApp")
    {
    }

    std::string Shear_Force_TelemApp::EscName(int id) const
    {
        return "ESC_" + std::to_string(id);
    }

    // =========================================================================
    // OnAttach
    // =========================================================================
    void Shear_Force_TelemApp::OnAttach()
    {
        CS_INFO("Shear_Force_TelemApp: Attaching — {} ESC(s).", k_EscCount);

        // 1. VFS + log redirection (same as the generated template).
        Cosmic::FileSystem::SetActiveProject("Shear_Force_TelemApp");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        // 2. Scene + one selectable entity per ESC (for picking + replay sync).
        m_Scene = Cosmic::Scene::Create();

        const auto channels = EscChannelNames();
        for (int i = 0; i < k_EscCount; ++i)
        {
            const int   id   = i + 1;            // 1-based to match the wire id
            const std::string name = EscName(id);

            Cosmic::Entity e = m_Scene->CreateEntity(name);
            auto& t   = e.GetComponent<Cosmic::TransformComponent>();
            t.Position = { 0.0f, (1 - i) * k_RowSpacing, 0.0f };
            t.Scale    = { k_SquareSize, k_SquareSize };

            e.AddComponent<Cosmic::SelectableComponent>();
            e.AddComponent<EscComponent>().id = id;

            m_Esc[i].recordId = m_Recorder.Register(name, "ESC", channels);
        }

        m_Recorder.ReserveCapacity(k_RecordCapacity);

        // 3. Wire the telemetry panel: Live source = recorder, replay = player.
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

        // 4. Serial port discovery; auto-select the first ESC so charts light up.
        m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
        Cosmic::EntitySelection::SetByName(EscName(1), "ESC");

        m_Log.reserve(1 << 16);
        m_RxAccumulator.reserve(1 << 12);

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
        m_Scene.reset();
        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Shear_Force_TelemApp: Detached.");
    }

    // =========================================================================
    // OnUpdate — clock, serial pump, panel, replay sync, render
    // =========================================================================
    void Shear_Force_TelemApp::OnUpdate(float ts)
    {
        m_AppClock += std::abs(ts);
        m_Camera.OnUpdate(ts);

        // Export-complete status edge.
        const bool flushing = m_Recorder.IsFlushing();
        if (m_WasFlushing && !flushing)
            m_RecordStatus = "Export complete.";
        m_WasFlushing = flushing;

        // Drain the COM buffer and decode any complete frames.
        PumpSerial();

        // Advance panel (ticks the player in replay; pushes the live ring buffer).
        m_Panel.OnUpdate(ts);

        const bool replay = (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
                             && m_Player.IsLoaded());

        // Drive each ESC entity's transform + colour, from live sample or the
        // interpolated replay frame, using the same mapping for both.
        auto view = m_Scene->View<EscComponent, Cosmic::TransformComponent>();
        for (auto rawE : view)
        {
            const int id  = view.get<EscComponent>(rawE).id;
            const int idx = id - 1;
            if (idx < 0 || idx >= k_EscCount) continue;

            glm::vec3 pos;
            glm::vec4 color;

            if (replay)
            {
                Cosmic::TelemetryFrame frame;
                if (m_Player.GetFrame(EscName(id), frame))
                    MapSquare(frame.values, idx, pos, color);
                else
                    continue;
            }
            else
            {
                MapSquare(m_Esc[idx].sample.ToChannels(), idx, pos, color);

                // Dim the square when the link has gone quiet.
                const bool stale = !m_Esc[idx].hasData
                                   || (m_AppClock - m_Esc[idx].lastSeen) > k_StaleTimeout;
                if (stale) color.a = 0.25f;
            }

            auto& t = view.get<Cosmic::TransformComponent>(rawE);
            t.Position.x = pos.x;
            t.Position.y = pos.y;
            m_Esc[idx].color = color;
        }

        RenderScene();
    }

    // =========================================================================
    // OnFixedUpdate — sample the latest decoded values at a fixed rate
    //
    // We record every fixed tick (not only while "Recording") so the live
    // ImPlot charts scroll continuously during monitoring. Start Recording
    // calls Clear() to begin a clean capture; Tick() always advances the clock
    // so timestamps are monotonic for both live plots and exported files.
    // =========================================================================
    void Shear_Force_TelemApp::OnFixedUpdate(float dt)
    {
        if (dt <= 0.0f) return; // paused / rewinding — handled by replay transport
        if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay) return;

        for (int i = 0; i < k_EscCount; ++i)
            m_Recorder.Record(m_Esc[i].recordId, m_Esc[i].sample.ToChannels());

        m_Recorder.Tick(dt);
    }

    // =========================================================================
    // PumpSerial — pull bytes from the threaded reader, split into lines,
    // decode ESC frames, and update per-ESC state.
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

            // Mirror everything into the raw monitor (bounded).
            m_Log += line;
            m_Log += '\n';
            if (m_Log.size() > 100000) m_Log.erase(0, 40000);

            EscRawPacket pkt;
            if (ParseFrame(line, pkt))
            {
                ++m_GoodFrames;
                const int idx = pkt.id - 1;
                if (idx >= 0 && idx < k_EscCount)
                {
                    EscState& s = m_Esc[idx];
                    s.sample      = EscSample::Decode(pkt, m_Config);
                    s.hasData     = true;
                    s.lastSeen    = m_AppClock;
                    ++s.packetCount;
                }
            }
            else
            {
                ++m_BadFrames; // status/heartbeat text or corruption — ignored
            }
        }

        if (m_RxAccumulator.size() > 4096) // runaway guard (no newline seen)
            m_RxAccumulator.clear();
    }

    // =========================================================================
    // RenderScene — draw one square per ESC at its (mapped) transform.
    // =========================================================================
    void Shear_Force_TelemApp::RenderScene()
    {
        const std::string selName = Cosmic::EntitySelection::GetName();

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

        auto view = m_Scene->View<EscComponent, Cosmic::TagComponent,
                                  Cosmic::TransformComponent>();
        for (auto rawE : view)
        {
            const int   idx       = view.get<EscComponent>(rawE).id - 1;
            const auto& tag       = view.get<Cosmic::TagComponent>(rawE);
            const auto& transform = view.get<Cosmic::TransformComponent>(rawE);
            if (idx < 0 || idx >= k_EscCount) continue;

            const bool selected = (!selName.empty() && tag.Tag == selName);

            // Selection halo.
            if (selected)
                Cosmic::Renderer2D::DrawQuad(
                    { transform.Position.x, transform.Position.y, -0.01f },
                    transform.Scale * 1.35f,
                    glm::vec4(1.0f, 1.0f, 1.0f, 0.4f));

            Cosmic::Renderer2D::DrawQuad(transform.Position,
                                         transform.Scale,
                                         m_Esc[idx].color);
        }

        Cosmic::Renderer2D::EndScene();
    }

    // =========================================================================
    // OnImGuiRender
    // =========================================================================
    void Shear_Force_TelemApp::OnImGuiRender()
    {
        DrawControlsWindow();
        DrawSerialWindow();
        DrawTelemetryWindow();
    }

    // -------------------------------------------------------------------------
    // Controls — recording, replay transport, decode constants, engine stats.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawControlsWindow()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f },
                           "Shear Force Telemetry  |  %d ESC", k_EscCount);
        ImGui::Separator();
        ImGui::Spacing();

        // Replay transport (no-op unless a recording is loaded).
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
                    Cosmic::EntitySelection::SetByName(EscName(1), "ESC");
                }
                m_Recorder.Clear();
                m_Recorder.ReserveCapacity(k_RecordCapacity);
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

        const bool canExport = !m_Recording
                               && !m_Recorder.IsFlushing()
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
        ImGui::TextDisabled("Capture is continuous; Start clears the buffer, Export saves it.");

        // ---- Decode constants (host-side, live editable) ----
        ImGui::Spacing();
        ImGui::SeparatorText("Decode Constants");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Motor pole pairs", &m_Config.PolePairs);
        if (m_Config.PolePairs < 1) m_Config.PolePairs = 1;
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Gear ratio", &m_Config.GearRatio, 0.0f, 0.0f, "%.2f");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Wheel dia (in)", &m_Config.WheelDiameterIn, 0.0f, 0.0f, "%.2f");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Slip factor", &m_Config.SlipFactor, 0.0f, 0.0f, "%.3f");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Volt scale (V/cnt)", &m_Config.VoltageScale, 0.0f, 0.0f, "%.4f");
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputFloat("Curr scale (A/cnt)", &m_Config.CurrentScale, 0.0f, 0.0f, "%.4f");
        ImGui::TextDisabled("Constants apply to newly received packets.");

        // ---- Engine / frame stats ----
        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Serial — port selection, connect/disconnect, raw monitor.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawSerialWindow()
    {
        ImGui::Begin("Serial Link");

        if (ImGui::Button("Refresh Ports"))
        {
            m_AvailablePorts = Cosmic::SerialPort::GetAvailablePorts();
            if (m_SelectedPortIndex >= (int)m_AvailablePorts.size())
                m_SelectedPortIndex = 0;
        }

        const char* curPort = m_AvailablePorts.empty() ? "No Ports Found"
            : (m_SelectedPortIndex < (int)m_AvailablePorts.size()
               ? m_AvailablePorts[m_SelectedPortIndex].c_str() : "Error");

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("COM Port", curPort))
        {
            for (int n = 0; n < (int)m_AvailablePorts.size(); ++n)
                if (ImGui::Selectable(m_AvailablePorts[n].c_str(), m_SelectedPortIndex == n))
                    m_SelectedPortIndex = n;
            ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::BeginCombo("Baud", std::to_string(m_BaudRates[m_SelectedBaudIndex]).c_str()))
        {
            for (int n = 0; n < (int)m_BaudRates.size(); ++n)
                if (ImGui::Selectable(std::to_string(m_BaudRates[n]).c_str(), m_SelectedBaudIndex == n))
                    m_SelectedBaudIndex = n;
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
                {
                    m_RxAccumulator.clear();
                    CS_INFO("Serial: opened {}.", m_AvailablePorts[m_SelectedPortIndex]);
                }
                else
                {
                    CS_WARN("Serial: failed to open {}.", m_AvailablePorts[m_SelectedPortIndex]);
                }
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
        for (int i = 0; i < k_EscCount; ++i)
        {
            const EscState& s = m_Esc[i];
            const bool stale = !s.hasData || (m_AppClock - s.lastSeen) > k_StaleTimeout;
            ImGui::TextColored(stale ? ImVec4(0.7f, 0.7f, 0.7f, 1.0f)
                                     : ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                               "ESC %d: %s  (%llu pkts)", i + 1,
                               stale ? "no data" : "live",
                               (unsigned long long)s.packetCount);
        }

        ImGui::Separator();
        if (ImGui::Button("Copy Log")) ImGui::SetClipboardText(m_Log.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Clear Log")) m_Log.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScrollLog);

        ImGui::BeginChild("##rawmon", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(m_Log.c_str());
        if (m_AutoScrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    // Telemetry — replay loader, entity selector, ImPlot charts, inspector.
    // -------------------------------------------------------------------------
    void Shear_Force_TelemApp::DrawTelemetryWindow()
    {
        ImGui::Begin("Telemetry");
        m_Panel.OnImGuiRender();
        ImGui::End();
    }

    // =========================================================================
    // OnEvent — camera control + click-to-select ESC squares (live only).
    // =========================================================================
    void Shear_Force_TelemApp::OnEvent(Cosmic::Event& e)
    {
        m_Camera.OnEvent(e);

        Cosmic::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
            [this](Cosmic::MouseButtonPressedEvent& ev) -> bool
            {
                if (ev.GetMouseButton() != CS_MOUSE_BUTTON_LEFT) return false;
                if (!m_Panel.IsPickingEnabled())                 return false;
                if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay)
                    return false;

                auto&     app    = Cosmic::Application::Get();
                glm::vec2 vpPos  = app.GetViewportPos();
                glm::vec2 vpSize = app.GetViewportSize();
                glm::vec2 mouse  = Cosmic::Input::GetMousePosition() - vpPos;

                if (mouse.x < 0.0f || mouse.y < 0.0f ||
                    mouse.x > vpSize.x || mouse.y > vpSize.y)
                    return false;

                glm::vec2 world = Cosmic::EntityPicker::ScreenToWorld(
                    m_Camera.GetCamera(), mouse, vpSize);
                Cosmic::Entity hit = Cosmic::EntityPicker::Pick(m_Scene, world);

                if (hit)
                {
                    const std::string& name = hit.GetComponent<Cosmic::TagComponent>().Tag;
                    Cosmic::EntitySelection::Set(hit, name, "ESC");
                    ev.Handled = true;
                    return true;
                }
                return false;
            });
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
