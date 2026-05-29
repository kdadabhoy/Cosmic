#pragma once

// TelemetryPanel.h
// Last Modified: 5/29/2026

/**
 * ============================================================================
 * TelemetryPanel — ImGui/ImPlot display for live and replayed telemetry
 * ============================================================================
 *
 * OVERVIEW
 * --------
 * TelemetryPanel bridges DataRecorder (live) or DataPlayer (replay) with
 * ImGui/ImPlot rendering. It owns the full replay lifecycle when a DataPlayer
 * is attached: the panel's OnUpdate() advances the player each frame, and its
 * OnImGuiRender() draws Load/Browse, transport controls, charts, and inspector.
 *
 * MODE
 * ----
 * The panel tracks an explicit Mode so data sources are unambiguous:
 *
 *   Mode::None    — no source attached yet.
 *   Mode::Live    — DataRecorder is the active source (live simulation).
 *   Mode::Replay  — DataPlayer is the active source (loaded recording).
 *
 * SetRecorder() switches to Live. The panel switches to Replay automatically
 * when the user successfully loads a file through the built-in Load button.
 * The layer can also call SetMode(Mode::Live) to override (e.g. when a new
 * recording session starts while a replay was loaded).
 *
 * SUBSCRIPTION
 * ------------
 * The panel subscribes to EntitySelection::OnChanged in its constructor and
 * stores the returned SubscriptionHandle. The destructor calls Unsubscribe()
 * so that the captured `this` is never invoked after the panel is destroyed.
 *
 * CIRCULAR BUFFER
 * ---------------
 * Up to k_PlotCapacity = 512 samples per channel. Ring described by:
 *   m_PlotOffset — index of the oldest sample (read head)
 *   m_PlotCount  — number of valid samples in the ring
 * Write index: (m_PlotOffset + m_PlotCount) % k_PlotCapacity
 * When full: m_PlotOffset advances by 1, oldest sample is discarded.
 *
 * INSPECTOR PRIORITY
 * ------------------
 *   1. Entity inspector — registered for the exact entity name.
 *   2. Tag inspector    — registered for the entity's tag string.
 *   3. Auto fallback    — raw channel values via ImGui::Text.
 *
 * ============================================================================
 */

#include "core/Core.h"
#include "telemetry/TelemetryChannel.h"
#include "telemetry/EntitySelection.h"
#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Cosmic
{
    class COSMIC_API TelemetryPanel
    {
    public:
        /**
         * @brief Construct and subscribe to EntitySelection::OnChanged.
         * Stores the subscription handle for safe cleanup on destruction.
         */
        TelemetryPanel();

        /**
         * @brief Unsubscribe from EntitySelection so dangling callbacks are impossible.
         */
        ~TelemetryPanel();

        // -------------------------------------------------------------------------
        // Mode
        // -------------------------------------------------------------------------

        enum class Mode { None, Live, Replay };

        /**
         * @brief Explicitly set the active data source mode.
         * Live → samples from DataRecorder. Replay → samples from DataPlayer.
         * The panel switches to Replay automatically on a successful file load.
         */
        void SetMode(Mode mode);
        Mode GetMode() const { return m_Mode; }

        // -------------------------------------------------------------------------
        // Data sources
        // -------------------------------------------------------------------------

        /**
         * @brief Attach a live DataRecorder. Also sets mode to Live if non-null.
         */
        void SetRecorder(DataRecorder* recorder);

        /**
         * @brief Attach a DataPlayer. Mode is NOT changed here — it switches
         * to Replay only when a file is successfully loaded through the panel UI.
         */
        void SetPlayer(DataPlayer* player);

        // -------------------------------------------------------------------------
        // Inspector callbacks
        // -------------------------------------------------------------------------

        using InspectorFn = std::function<void(const std::string& name, const TelemetryFrame&)>;

        /** @brief Register a display callback for entities whose tag matches. */
        void RegisterTagInspector(const std::string& tag, InspectorFn fn);

        /** @brief Register a display callback for a specific entity name (highest priority). */
        void RegisterEntityInspector(const std::string& entityName, InspectorFn fn);

        // -------------------------------------------------------------------------
        // Per-frame hooks
        // -------------------------------------------------------------------------

        /**
         * @brief Advance playback (replay mode) and push the current frame into
         * the ring buffers for the selected entity.
         *
         * Call once per render frame from the owning layer's OnUpdate.
         *
         * @param dt  Frame delta-time in seconds.
         */
        void OnUpdate(float dt);

        /**
         * @brief Draw all panel UI inside the calling ImGui window scope.
         *
         * Sections rendered:
         *   - Replay section (when DataPlayer attached): file path, Browse, Load, status.
         *   - Entity selector combo.
         *   - Playback transport controls (replay mode only).
         *   - ImPlot line charts (one per channel, circular-buffer sourced).
         *   - Inspector section (entity → tag → auto raw-value fallback).
         *
         * The caller must already be inside an ImGui::Begin/End block.
         */
        void OnImGuiRender();

    private:
        static constexpr int k_PlotCapacity = 512;

        // -------------------------------------------------------------------------
        // Internal helpers
        // -------------------------------------------------------------------------

        void OnSelectionChanged(const std::string& name, const std::string& tag);
        void RebuildBuffers(const EntityTelemetryInfo& info);
        void PushFrame(const TelemetryFrame& frame);
        void DrawReplayLoader();
        void DrawPlots();
        void DrawPlaybackControls();
        void DrawInspector(const TelemetryFrame& frame);

        // -------------------------------------------------------------------------
        // State
        // -------------------------------------------------------------------------

        EntitySelection::SubscriptionHandle m_SubHandle = 0;
        Mode           m_Mode     = Mode::None;
        DataRecorder*  m_Recorder = nullptr;
        DataPlayer*    m_Player   = nullptr;

        // Entity selection
        std::string              m_SelectedName;
        std::string              m_SelectedTag;
        std::vector<std::string> m_ChannelNames;

        // Circular buffers — [channel][sample_index]
        std::vector<std::vector<float>> m_PlotBuffers;
        std::vector<float>              m_PlotTimes;
        int m_PlotOffset = 0;
        int m_PlotCount  = 0;

        TelemetryFrame m_LastFrame;

        // Replay loader UI state
        std::string m_ReplayPath      = "logs/";
        std::string m_ReplayStatus    = "No recording loaded.";
        bool        m_ReplayLoadOk    = false;
        float       m_LastReplayPos   = -1.0f; // guards duplicate pushes when paused

        std::unordered_map<std::string, InspectorFn> m_TagInspectors;
        std::unordered_map<std::string, InspectorFn> m_EntityInspectors;
    };

} // namespace Cosmic
