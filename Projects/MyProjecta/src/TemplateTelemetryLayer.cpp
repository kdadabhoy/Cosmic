// TemplateTelemetryLayer.cpp
// Last Modified: 5/29/2026

#include "TemplateTelemetryLayer.h"

#include <Cosmic.h>
#include <imgui.h>
#include <glm/glm.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace Workspace
{
    // =========================================================================
    // Helpers
    // =========================================================================

    namespace
    {
        std::string AgentName(int index)
        {
            std::ostringstream ss;
            ss << "Agent_" << std::setw(2) << std::setfill('0') << index;
            return ss.str();
        }
    }

    // =========================================================================
    // Construction
    // =========================================================================

    TemplateTelemetryLayer::TemplateTelemetryLayer()
        : Cosmic::Layer("TemplateTelemetryLayer")
    {
    }

    // =========================================================================
    // OnAttach
    // =========================================================================

    void TemplateTelemetryLayer::OnAttach()
    {
        CS_INFO("TemplateTelemetryLayer: Attaching — spawning {} agents.", k_AgentCount);

        m_Scene = Cosmic::Scene::Create();

        std::mt19937  rng{ std::random_device{}() };
        std::uniform_real_distribution<float> posDist(-k_Bounds * 0.8f, k_Bounds * 0.8f);
        std::uniform_real_distribution<float> spdDist(2.0f, 5.0f);

        for (int i = 0; i < k_AgentCount; ++i)
        {
            const std::string name = AgentName(i);

            Cosmic::Entity e = m_Scene->CreateEntity(name);

            // Scene::CreateEntity already adds TransformComponent.
            auto& transform    = e.GetComponent<Cosmic::TransformComponent>();
            transform.Position = { posDist(rng), posDist(rng), 0.0f };
            transform.Scale    = { 0.18f, 0.18f };

            e.AddComponent<Cosmic::SelectableComponent>();

            auto& agent      = e.AddComponent<AgentComponent>();
            agent.position   = { transform.Position.x, transform.Position.y };
            agent.target     = { posDist(rng), posDist(rng) };
            agent.speed      = spdDist(rng);

            agent.recordId = m_Recorder.Register(
                name, "Agent",
                { "PosX", "PosY", "Speed", "Heading", "Power" }
            );
        }

        // Pre-allocate recording buffers so the hot path is zero-alloc.
        m_Recorder.ReserveCapacity(k_RecordCapacity);

        m_AgentSystem = &m_Scene->AddSystem<AgentSystem>(&m_Recorder, k_Bounds);

        // Wire panel to both sources; mode starts Live.
        m_Panel.SetRecorder(&m_Recorder);
        m_Panel.SetPlayer(&m_Player);

        // Tag inspector: readable channel layout for "Agent" entities.
        m_Panel.RegisterTagInspector("Agent",
            [](const std::string& name, const Cosmic::TelemetryFrame& f)
            {
                ImGui::Text("Entity: %s", name.c_str());
                if (f.values.size() >= 5)
                {
                    ImGui::Text("Position : (%.2f, %.2f)", f.values[0], f.values[1]);
                    ImGui::Text("Speed    : %.3f u/s",     f.values[2]);
                    ImGui::Text("Heading  : %.2f rad",     f.values[3]);
                    ImGui::Text("Power    : %.3f",         f.values[4]);
                }
            });

        CS_INFO("TemplateTelemetryLayer: OnAttach complete.");
    }

    // =========================================================================
    // OnDetach
    // =========================================================================

    void TemplateTelemetryLayer::OnDetach()
    {
        m_Recorder.WaitForFlush();
        Cosmic::EntitySelection::Clear();
        m_Scene.reset();
        CS_INFO("TemplateTelemetryLayer: Detached.");
    }

    // =========================================================================
    // OnUpdate
    // =========================================================================

    void TemplateTelemetryLayer::OnUpdate(float ts)
    {
        // ts already carries both global and layer scale — applied by the root
        // manager (MyProjecta) before dispatch.  Use it directly everywhere.
        m_Camera.OnUpdate(ts);

        // Detect flush completion and update the status label on the falling edge.
        const bool flushing = m_Recorder.IsFlushing();
        if (m_WasFlushing && !flushing)
            m_RecordStatus = "Export complete.";
        m_WasFlushing = flushing;

        // Advance panel (ticks player if in replay mode, pushes frame to ring buffer).
        m_Panel.OnUpdate(ts);

        // -----------------------------------------------------------------------
        // Replay position sync
        // When a recording is loaded the panel has switched to Replay mode.
        // Override each entity's TransformComponent from the interpolated player
        // frame so the visual scene matches the current playback position.
        // -----------------------------------------------------------------------
        if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay
            && m_Player.IsLoaded())
        {
            auto view = m_Scene->View<Cosmic::TagComponent,
                                      Cosmic::TransformComponent>();
            for (auto rawE : view)
            {
                const std::string& entityName = view.get<Cosmic::TagComponent>(rawE).Tag;
                Cosmic::TelemetryFrame frame;
                if (m_Player.GetFrame(entityName, frame) && frame.values.size() >= 2)
                {
                    auto& t = view.get<Cosmic::TransformComponent>(rawE);
                    t.Position.x = frame.values[0];
                    t.Position.y = frame.values[1];
                }
            }
        }

        // -----------------------------------------------------------------------
        // Trail — build position history for the selected entity
        // -----------------------------------------------------------------------
        const std::string selectedName = Cosmic::EntitySelection::GetName();

        if (selectedName != m_TrailedName)
        {
            m_Trail.clear();
            m_TrailedName   = selectedName;
            m_LastPlayerPos = m_Player.GetPosition();
        }

        if (!selectedName.empty())
        {
            // Detect scrub: actual position change > what one natural step would produce.
            if (m_Player.IsLoaded())
            {
                const float expectedStep =
                    std::abs(ts * m_Player.GetSpeed()) + 0.1f;
                const float actualStep =
                    std::abs(m_Player.GetPosition() - m_LastPlayerPos);
                if (actualStep > expectedStep)
                    m_Trail.clear();
                m_LastPlayerPos = m_Player.GetPosition();
            }

            // Sample world position from the scene (already updated above for replay).
            // Using GetEntity() for O(1) lookup instead of O(N) tag scan.
            glm::vec2 pos    = { 0.0f, 0.0f };
            bool      gotPos = false;

            Cosmic::Entity live = Cosmic::EntitySelection::GetEntity();
            if (live && live.HasComponent<Cosmic::TransformComponent>())
            {
                const auto& t = live.GetComponent<Cosmic::TransformComponent>();
                pos    = { t.Position.x, t.Position.y };
                gotPos = true;
            }
            else if (m_Player.IsLoaded())
            {
                // Replay mode: entity handle is invalid; read from player directly.
                Cosmic::TelemetryFrame frame;
                if (m_Player.GetFrame(selectedName, frame) && frame.values.size() >= 2)
                {
                    pos    = { frame.values[0], frame.values[1] };
                    gotPos = true;
                }
            }

            if (gotPos)
            {
                if (m_Trail.empty() || glm::length(pos - m_Trail.back()) > 0.005f)
                {
                    m_Trail.push_back(pos);
                    if (static_cast<int>(m_Trail.size()) > k_TrailLength)
                        m_Trail.pop_front();
                }
            }
        }

        // -----------------------------------------------------------------------
        // Render
        // -----------------------------------------------------------------------
        const std::string selName = Cosmic::EntitySelection::GetName();

        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

        // Trail — gold dots fading from oldest (faint, small) to newest (solid, large)
        if (!m_Trail.empty())
        {
            const int n = static_cast<int>(m_Trail.size());
            for (int i = 0; i < n; ++i)
            {
                const float t     = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 1.0f;
                const float alpha = 0.06f + t * 0.60f;
                const float size  = 0.025f + t * 0.03f;

                Cosmic::Renderer2D::DrawCircle(
                    { m_Trail[i].x, m_Trail[i].y, -0.01f },
                    { size, size },
                    { 1.0f, 0.85f, 0.1f, alpha },
                    1.0f, 0.25f
                );
            }
        }

        // Agents
        auto view = m_Scene->View<Cosmic::TagComponent, Cosmic::TransformComponent>();
        for (auto rawE : view)
        {
            const auto& tag       = view.get<Cosmic::TagComponent>(rawE);
            const auto& transform = view.get<Cosmic::TransformComponent>(rawE);
            const bool  selected  = (!selName.empty() && tag.Tag == selName);

            if (selected)
            {
                // Outer ring — bright white halo
                Cosmic::Renderer2D::DrawCircle(
                    transform.Position,
                    transform.Scale * 2.0f,
                    { 1.0f, 1.0f, 1.0f, 0.35f },
                    0.15f, 0.05f
                );
                // Inner filled circle — vivid gold
                Cosmic::Renderer2D::DrawCircle(
                    transform.Position,
                    transform.Scale * 1.5f,
                    { 1.0f, 0.75f, 0.0f, 1.0f },
                    1.0f, 0.015f
                );
            }
            else
            {
                // Normal agent — solid blue circle
                Cosmic::Renderer2D::DrawCircle(
                    transform.Position,
                    transform.Scale,
                    { 0.22f, 0.58f, 1.0f, 0.90f },
                    1.0f, 0.015f
                );
            }
        }

        Cosmic::Renderer2D::EndScene();
    }

    // =========================================================================
    // OnFixedUpdate
    // =========================================================================

    void TemplateTelemetryLayer::OnFixedUpdate(float dt)
    {
        // dt already carries both global and layer scale — applied by the root
        // manager before dispatch.  Guard <= 0 to detect pause / rewind as normal.
        if (dt <= 0.0f) return;

        // Only run simulation and advance the recorder clock when live.
        if (m_Panel.GetMode() != Cosmic::TelemetryPanel::Mode::Replay)
            m_Scene->OnFixedUpdate(dt);

        if (m_Recording)
            m_Recorder.Tick(dt);
    }

    // =========================================================================
    // OnImGuiRender
    // =========================================================================

    void TemplateTelemetryLayer::OnImGuiRender()
    {
        // -----------------------------------------------------------------------
        // "Project Inspector" — transport bar first, then recording controls
        // -----------------------------------------------------------------------
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f },
                           "Telemetry Demo  |  %d Agents", k_AgentCount);
        ImGui::Separator();
        ImGui::Spacing();

        // Transport controls at the very top — always reachable without scrolling.
        m_Panel.DrawTransportControls();

        ImGui::Spacing();
        ImGui::SeparatorText("Recording");
        ImGui::Spacing();

        // Session name
        {
            char buf[64] = {};
            strncpy_s(buf, sizeof(buf), m_SessionName.c_str(), _TRUNCATE);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::InputText("Session##rec_name", buf, sizeof(buf)))
                m_SessionName = buf;
            ImGui::SameLine();
            ImGui::TextDisabled("(blank = timestamp)");
        }

        ImGui::Spacing();

        // Start / Stop
        if (!m_Recording)
        {
            if (ImGui::Button("  Start Recording  ##rec_start"))
            {
                m_Recorder.Clear();
                m_Recorder.ReserveCapacity(k_RecordCapacity);
                m_Recording    = true;
                m_RecordStatus = "Recording...";
                m_Panel.SetMode(Cosmic::TelemetryPanel::Mode::Live);
                CS_INFO("TemplateTelemetryLayer: Recording started.");
            }
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
            if (ImGui::Button("  Stop Recording   ##rec_stop"))
            {
                m_Recording    = false;
                m_RecordStatus = "Stopped. Ready to export.";
                CS_INFO("TemplateTelemetryLayer: Recording stopped ({:.2f}s).",
                        m_Recorder.GetRecordedDuration());
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();

        // Export — disabled while recording or a flush is already in flight.
        const bool canExport = !m_Recording
                               && !m_Recorder.IsFlushing()
                               && (m_Recorder.GetTotalFrameCount() > 0);
        if (!canExport) ImGui::BeginDisabled();
        if (ImGui::Button("  Export  ##rec_export"))
        {
            m_Recorder.Flush("logs", m_SessionName, k_SampleRate);
            const std::string dest = m_SessionName.empty() ? "<timestamp>" : m_SessionName;
            m_RecordStatus = "Exporting... -> logs/" + dest + "/";
            m_WasFlushing  = true;
        }
        if (!canExport) ImGui::EndDisabled();

        ImGui::Spacing();

        // Colour-code: orange while flushing, green on complete, plain otherwise.
        if (m_Recorder.IsFlushing())
            ImGui::TextColored({ 1.0f, 0.75f, 0.1f, 1.0f }, "Status:   %s", m_RecordStatus.c_str());
        else if (m_RecordStatus.rfind("Export complete", 0) == 0)
            ImGui::TextColored({ 0.2f, 1.0f, 0.35f, 1.0f }, "Status:   %s", m_RecordStatus.c_str());
        else
            ImGui::Text("Status:   %s", m_RecordStatus.c_str());
        ImGui::Text("Frames:   %zu", m_Recorder.GetTotalFrameCount());
        ImGui::Text("Duration: %.2f s", m_Recorder.GetRecordedDuration());

        if (m_AgentSystem)
        {
            ImGui::Spacing();
            ImGui::SeparatorText("AgentSystem Timings");
            ImGui::Text("Prepare : %.3f ms", m_AgentSystem->TimePrepareMs);
            ImGui::Text("Execute : %.3f ms", m_AgentSystem->TimeExecuteMs);
            ImGui::Text("Merge   : %.3f ms", m_AgentSystem->TimeMergeMs);
        }

        ImGui::End();

        // -----------------------------------------------------------------------
        // "Telemetry" — replay loader, entity selector, plots, inspector
        // -----------------------------------------------------------------------
        ImGui::Begin("Telemetry");
        m_Panel.OnImGuiRender();
        ImGui::End();
    }

    // =========================================================================
    // OnEvent
    // =========================================================================

    void TemplateTelemetryLayer::OnEvent(Cosmic::Event& e)
    {
        m_Camera.OnEvent(e);

        Cosmic::EventDispatcher dispatcher(e);
        dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
            [this](Cosmic::MouseButtonPressedEvent& ev) -> bool
            {
                if (ev.GetMouseButton() != CS_MOUSE_BUTTON_LEFT)
                    return false;

                // Skip picking during replay — no live entities to select by handle.
                if (m_Panel.GetMode() == Cosmic::TelemetryPanel::Mode::Replay)
                    return false;

                glm::vec2 mousePos = Cosmic::Input::GetMousePosition();
                auto& window       = Cosmic::Application::Get().GetWindow();
                glm::vec2 vpSize   = {
                    static_cast<float>(window.GetWidth()),
                    static_cast<float>(window.GetHeight())
                };

                glm::vec2 worldPos = Cosmic::EntityPicker::ScreenToWorld(
                    m_Camera.GetCamera(), mousePos, vpSize);

                Cosmic::Entity hit = Cosmic::EntityPicker::Pick(m_Scene, worldPos);

                if (hit)
                {
                    const std::string& name =
                        hit.GetComponent<Cosmic::TagComponent>().Tag;
                    Cosmic::EntitySelection::Set(hit, name, "Agent");
                    ev.Handled = true;
                    return true;
                }

                return false;
            });
    }

} // namespace Workspace
