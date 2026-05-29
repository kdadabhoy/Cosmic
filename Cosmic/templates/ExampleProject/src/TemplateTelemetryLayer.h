#pragma once

// TemplateTelemetryLayer.h
// Last Modified: 5/29/2026

/**
 * ============================================================================
 * TemplateTelemetryLayer
 * ============================================================================
 *
 * Demonstrates all five telemetry systems working together with 20 agents:
 *
 *   1. AgentSystem   — ParallelSystem steering 20 agents toward random targets.
 *   2. DataRecorder  — captures 5 float channels per agent each fixed tick.
 *   3. EntityPicker  — CPU AABB picking on left-click (SelectableComponent).
 *   4. EntitySelection — global selected-entity service.
 *   5. TelemetryPanel — owns the full replay UI (Load/Browse, transport,
 *                       entity selector, ImPlot charts, inspector).
 *
 * RESPONSIBILITIES  (layer vs panel split)
 * ----------------------------------------
 *   Layer:  scene, camera, rendering, recording controls (Start/Stop/Export),
 *           entity picking, trail visualisation.
 *   Panel:  everything else — replay load, entity selector, transport
 *           controls, per-channel charts, inspector callbacks.
 *
 * REPLAY POSITION SYNC
 * --------------------
 * When the DataPlayer is loaded (panel switches to Replay mode), OnUpdate
 * overrides each entity's TransformComponent.Position from the player frames
 * so the visual simulation matches the recorded data at the current position.
 */

#include <Cosmic.h>
#include "AgentSystem.h"
#include <deque>
#include <string>
#include <glm/glm.hpp>

namespace Workspace
{
    class TemplateTelemetryLayer : public Cosmic::Layer
    {
    public:
        TemplateTelemetryLayer();
        virtual ~TemplateTelemetryLayer() override = default;

        virtual void OnAttach()                          override;
        virtual void OnDetach()                          override;
        virtual void OnUpdate(float ts)                  override;
        virtual void OnFixedUpdate(float dt)             override;
        virtual void OnImGuiRender()                     override;
        virtual void OnEvent(Cosmic::Event& e)           override;

    private:
        // Scene & camera
        Cosmic::Ref<Cosmic::Scene>           m_Scene;
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };

        // Telemetry — panel owns replay controls; layer owns recording controls.
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;

        // Non-owning pointer for profiling readout (scene owns the system).
        AgentSystem* m_AgentSystem = nullptr;

        // Recording state
        bool        m_Recording    = false;
        bool        m_WasFlushing  = false;
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        // Trail — position history of the currently selected entity.
        std::deque<glm::vec2> m_Trail;
        std::string           m_TrailedName;
        float                 m_LastPlayerPos = 0.0f;

        // Simulation constants
        static constexpr int    k_AgentCount      = 20;
        static constexpr float  k_Bounds          = 5.0f;
        static constexpr int    k_TrailLength     = 200;
        static constexpr float  k_SampleRate      = 60.0f;
        // 5-minute recording capacity pre-reserved at startup.
        static constexpr size_t k_RecordCapacity  = static_cast<size_t>(k_SampleRate * 300.0f);
    };

} // namespace Workspace
