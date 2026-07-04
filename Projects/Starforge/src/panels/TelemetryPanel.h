#pragma once

// panels/TelemetryPanel.h
//
// ============================================================================
// Starforge — Simulation instrumentation / telemetry panel (E20).
// ============================================================================
//
// Turns the editor into a test rig: mark any reflected numeric field "Recorded"
// (Inspector right-click, or "Add from selection" here), press Play, and this
// panel samples every marked channel ONCE PER FIXED STEP into the engine's
// columnar telemetry store (DataRecorder), draws live docked ImPlot scopes, and
// on Stop keeps the take so you can scrub the timeline, export CSV (DataExport),
// and reload it later (DataPlayer). Takes autosave to user://starforge/takes/
// with DataRecorder's crash-failsafe pattern.
//
// Scripts can push extra channels through the engine's generic telemetry seam
// (Cosmic::ITelemetrySink) — this panel IS that sink; scripts write
// Telemetry().Push("thrust_N", v) and the values land in the same store. The
// engine stays name-agnostic; the panel owns all the UI.
// ============================================================================

#include <Cosmic.h>

#include "TelemetryRecording.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    class TelemetryPanel : public Cosmic::ITelemetrySink
    {
    public:
        void OnImGuiRender(EditorContext& ctx);

        // ---- Play lifecycle (driven by StarforgeApp) ------------------------
        void OnPlayStart(EditorContext& ctx, float fixedDt);   // arm: resolve marks, reset take
        void OnFixedStep(EditorContext& ctx);                  // sample one step (after FixedTick)
        void OnPlayStop(EditorContext& ctx);                   // flush + keep the take for scrub

        // ---- ITelemetrySink — scripts push here during FixedTick ------------
        void Push(entt::entity source, const char* channel, float value) override;

    private:
        // One plotted time-series (reflected field component OR script channel).
        struct Channel
        {
            std::string        label;          // unique, <= 31 chars (bin cap)
            int                group = 0;      // which plot it draws into
            bool               visible = true;
            std::vector<float> samples;        // one per fixed step (== m_Times.size())

            bool          isScript = false;
            // reflected source:
            entt::id_type typeId = 0;
            std::string   field;
            int           comp = -1;
            uint64_t      uuid = 0;
            entt::entity  handle = entt::null;
            // script source:
            uint32_t      scriptRaw = 0;       // entt::entity as a raw id
            std::string   scriptChannel;
        };
        struct Group { std::string title; };

        enum class Mode { Empty, Recording, Stopped, Loaded };

        // ---- capture helpers ------------------------------------------------
        void  ResetTake();
        void  AddScriptChannel(EditorContext& ctx, uint32_t raw, const std::string& name);
        void  ArmRecorder(EditorContext& ctx);
        void  DedupLabels();
        int   GroupFor(const std::string& key, const std::string& title);
        float ReadReflected(EditorContext& ctx, const Channel& ch);

        // ---- UI helpers -----------------------------------------------------
        void DrawMarksManager(EditorContext& ctx);
        void DrawPlots(EditorContext& ctx);
        void ExportCsv(EditorContext& ctx);
        void RefreshTakeList();
        bool LoadTake(EditorContext& ctx, const std::string& folder);

        // ---- state ----------------------------------------------------------
        Mode m_Mode = Mode::Empty;

        std::vector<Channel>                 m_Channels;   // fixed after arm
        std::vector<Group>                   m_Groups;
        std::unordered_map<std::string, int> m_GroupIndex;
        std::vector<float>                   m_Times;      // shared x-axis
        std::unordered_set<std::string>      m_ScriptKeys; // "raw:name" already channeled

        // Script push scratch for the current fixed step: raw entt id -> {channel -> value}.
        std::unordered_map<uint32_t, std::unordered_map<std::string, float>> m_Scratch;

        float m_FixedDt   = 1.0f / 60.0f;
        int   m_StepIndex = 0;
        bool  m_Armed     = false;
        bool  m_WarnedLate = false;

        std::unique_ptr<Cosmic::DataRecorder> m_Rec;       // persistent store (bin/CSV/autosave)
        uint32_t    m_TakeId = 0;
        std::string m_TakeName;        // folder stem: <scene>_<timestamp>
        std::string m_LastTakeDir;     // last flushed take (absolute-ish path)

        Cosmic::DataPlayer m_Player;   // reloaded takes (after restart / "Load")

        // UI knobs.
        bool  m_Follow = true;         // live: lock X to the trailing window
        float m_Window = 10.0f;        // seconds shown while following
        float m_Scrub  = 0.0f;         // stopped/loaded: playhead time
        float m_PlotHeight = 150.0f;

        std::vector<std::string> m_TakeFolders;   // enumerated take dirs
        bool  m_TakeListDirty = true;
    };
}
