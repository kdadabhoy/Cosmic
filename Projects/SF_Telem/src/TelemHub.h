#pragma once

// TelemHub.h
// SF_Telem
//
// ============================================================================
// TelemHub — the single shared telemetry backbone for the whole app.
// ============================================================================
//
// Owned by the SF_Telem root manager and passed by pointer to every screen
// (Main / Drivetrain / Weapon). Centralising it means ONE serial connection,
// ONE recorder and ONE set of decoded samples that survive screen switches.
//
// Responsibilities:
//   * Own the COM port; drain + parse framed packets; route R/L/W to the right
//     decode (drive vs weapon) — fully tolerant of any ESC being unplugged.
//   * Track latest sample, presence/staleness, packet counts and live+max stats
//     (with reset) per ESC.
//   * Keep scrolling plot rings per ESC and drive the engine telemetry stack
//     (DataRecorder -> CSV/.bin -> DataPlayer replay).
//   * Run the predicted weapon spin-up model (WeaponModel.h).
//   * Provide the shared Serial + Recording UI and a reusable per-ESC plot pass.
// ============================================================================

#include <Cosmic.h>
#include "Telemetry.h"
#include "WeaponModel.h"

#include <array>
#include <string>
#include <vector>

namespace Workspace
{
    class TelemHub
    {
    public:
        // -------- Scrolling plot ring (sized for the widest channel set) --------
        struct Ring
        {
            static constexpr int Cap = 512;
            std::vector<float>                            times{ std::vector<float>(Cap, 0.0f) };
            std::array<std::vector<float>, WCH_COUNT>     ch;
            int   offset = 0;
            int   count  = 0;
            float lastT  = -1.0f;

            Ring() { for (auto& c : ch) c.assign(Cap, 0.0f); }
            void Clear();
            void Push(float t, const std::vector<float>& values);
        };

        // -------- Plot interactivity (shared toolbar + per-channel Y config) --------
        // One shared PlotView for the panel (the three ESC tabs are mutually exclusive),
        // plus a per-(ESC,channel) Y-axis config edited from each plot's right-click menu.
        struct PlotView
        {
            bool   follow       = true;     // X auto-scrolls to the newest sample (live default)
            float  windowSec    = 10.0f;    // trailing window width while following
            bool   showStats    = true;     // min/max/avg/last caption under each plot
            bool   showMinMax   = true;     // TagY markers at the visible min & max
            bool   seekOnDrag   = true;     // replay: drag the playhead to scrub
            bool   fitRequested = false;    // one-shot "Fit all" -> data extents
            bool   replayDefaultApplied = false; // set follow=false once when replay starts
            double linkXMin = 0.0, linkXMax = 1.0; // shared X across all channels
        };
        struct YAxisCfg
        {
            bool  autoFit = true;                 // ON => AutoFit; OFF => manual scroll/zoom
            bool  capMin = false, capMax = false; // hard caps
            float yMin = 0.0f, yMax = 1.0f;       // cap values
            bool  useStep = false; float step = 1.0f; // fixed tick interval
        };

        // The serial connection is owned by the root manager and shared with the
        // testing hub, so one connection persists across screen switches. The root
        // drives link.OnUpdate()/Shutdown(); this hub only polls bytes when active.
        void Init(Cosmic::SerialLink* link);  // register recorder entities + panel inspectors
        void Shutdown();
        void OnUpdate(float ts);     // clock, serial pump, panel, model, rings, flush
        void RecordFixed(float dt);  // continuous capture (call from OnFixedUpdate)

        // Feed received bytes straight into the framing + decode path, bypassing the
        // serial read. This is the pure half of PumpSerial() — chunks may split mid
        // line and are reassembled across calls. Public so the protocol path can be
        // exercised headlessly without a COM port; the app itself always goes through
        // OnUpdate() -> PumpSerial().
        void IngestChunk(const std::string& chunk);

        // ---- Frame counters (parse health) ----
        uint64_t GoodFrames() const { return m_GoodFrames; }
        uint64_t BadFrames()  const { return m_BadFrames; }
        uint64_t PacketCount(int id) const { return m_PacketCount[id]; }

        // ---- Presence / mode ----
        bool HasData(int id)  const { return m_HasData[id]; }
        bool Stale(int id)    const;
        bool Present(int id)  const { return m_HasData[id] && !Stale(id); }
        bool AnyPresent()     const;
        bool Replaying()      const;

        // ---- Replay transport (thin wrappers over the player, for the plot playhead) ----
        float ReplayPosition() const;
        float ReplayDuration() const;
        void  SeekReplay(float seconds);

        // ---- Live readings (headline values for the data boxes) ----
        float Cur(int id)   const;   // A
        float Volt(int id)  const;   // V
        float Rpm(int id)   const;   // motorRPM (drive) / weaponRPM (weapon)
        float Speed(int id) const;   // mph (drive)
        float Tip()         const { return m_Weapon.tipSpeedMph; } // mph (weapon)

        const DriveSample&  GetDrive(int id) const { return m_Drive[id]; }
        const WeaponSample& GetWeapon()      const { return m_Weapon; }

        // ---- Predicted ----
        float PredictedRpm(int id) const;          // drive: Kv*V ; weapon: model steady-state
        const WeaponModelResult& ModelResult() const { return m_ModelResult; }
        WeaponModelConfig& ModelCfg() { return m_Model; }   // call MarkModelDirty() after edits
        void  MarkModelDirty() { m_ModelDirty = true; }
        bool  ModelUsesLiveVoltage() const { return m_ModelUseLiveVoltage; }
        void  SetModelUseLiveVoltage(bool b) { m_ModelUseLiveVoltage = b; m_ModelDirty = true; }
        float ModelVoltageUsed() const { return m_ModelLastVoltage; }

        // ---- Max stats (+ reset) ----
        float MaxCur(int id)  const { return m_MaxCur[id]; }
        float MaxVolt(int id) const { return m_MaxVolt[id]; }
        float MaxRpm(int id)  const { return m_MaxRpm[id]; }
        float MaxSpeed(int id)const { return m_MaxSpeed[id]; }
        float MaxTip()        const { return m_MaxTip; }
        void  ResetMax(int id);
        void  ResetMaxAll();

        // ---- Average stats (mean since last reset / over the stats window) ----
        float AvgCur(int id)   const { return m_StatCount[id] ? (float)(m_SumCur[id]   / m_StatCount[id]) : 0.0f; }
        float AvgVolt(int id)  const { return m_StatCount[id] ? (float)(m_SumVolt[id]  / m_StatCount[id]) : 0.0f; }
        float AvgRpm(int id)   const { return m_StatCount[id] ? (float)(m_SumRpm[id]   / m_StatCount[id]) : 0.0f; }
        float AvgSpeed(int id) const { return m_StatCount[id] ? (float)(m_SumSpeed[id] / m_StatCount[id]) : 0.0f; }
        float AvgTip()         const { return m_StatCount[ESC_WEAPON] ? (float)(m_SumTip / m_StatCount[ESC_WEAPON]) : 0.0f; }

        // Averaging window (seconds): stats auto-reset after this long, so the
        // average reflects the last window. 0 = never (mean accumulates until a
        // manual reset). Bind directly in the UI; ResetStats() also restarts it.
        float& StatsWindowSec() { return m_StatsWindowSec; }
        void   ResetStats() { ResetMaxAll(); m_LastStatsReset = m_AppClock; }

        // ---- Configs (edited by panels) ----
        DriveConfig&  DriveCfg()  { return m_DriveCfg; }
        WeaponConfig& WeaponCfg() { return m_WeaponCfg; }   // call MarkModelDirty() if gear/dia change

        // ---- Engine telemetry stack ----
        Cosmic::TelemetryPanel& Panel() { return m_Panel; }

        // ---- Shared UI (drawn by the root so they persist across screens) ----
        void DrawSerialPanel();        // "Serial Link"
        void DrawRecordingControls();  // recording + status (inside a parent window)

        // ---- Reusable plot pass (split so Main can tab and Replay can show 3 cols) ----
        void DrawPlotToolbar();        // shared toolbar (Follow/Window/Fit/Stats/Min-Max); drives linked X
        void DrawEscChannels(int id);  // one ESC's per-channel charts (no toolbar)

    private:
        void PumpSerial();
        void SampleRings();
        void ApplyReplayFrame();   // during replay: drive m_Drive/m_Weapon from the player
        void RecomputeModel();
        float ModelEffectiveVoltage() const;
        float TipRadiusM() const { return (m_WeaponCfg.WeaponDiameterIn * 0.0254f) * 0.5f; }

    private:
        // --- Serial ---
        // Shared engine component (owned by the root manager). Owns the port list,
        // baud, async (non-blocking) connect + auto-reconnect, and the connection
        // menu UI. Set in Init(); never owned/destroyed here.
        Cosmic::SerialLink*      m_Link = nullptr;
        std::string              m_RxAccumulator;       // line-framing buffer for parsing
        std::string              m_Log;
        bool                     m_AutoScrollLog = true;
        uint64_t                 m_GoodFrames = 0;
        uint64_t                 m_BadFrames  = 0;

        // --- Arduino firmware copy UI (Serial Link -> Arduino Firmware) ---
        int  m_FwRightPin  = 16;   // GPIO defaults match the wiring diagram
        int  m_FwLeftPin   = 13;
        int  m_FwWeaponPin = 17;
        char m_FwBtName[32] = "SF_Telem";  // Bluetooth device name baked into the sketch
        bool m_ShowPinout  = false;
        Cosmic::Ref<Cosmic::Texture2D> m_PinoutTex;

        // --- Configs ---
        DriveConfig  m_DriveCfg;
        WeaponConfig m_WeaponCfg;

        // --- Predicted weapon model ---
        WeaponModelConfig m_Model;
        WeaponModelResult m_ModelResult;
        bool  m_ModelDirty          = true;
        bool  m_ModelUseLiveVoltage = true;
        float m_ModelLastVoltage    = -1.0f;

        // --- Per-ESC state ---
        DriveSample  m_Drive[ESC_COUNT];   // only [RIGHT],[LEFT] used
        WeaponSample m_Weapon;
        bool         m_HasData[ESC_COUNT]   = { false, false, false };
        float        m_LastSeen[ESC_COUNT]  = { 0, 0, 0 };
        uint64_t     m_PacketCount[ESC_COUNT] = { 0, 0, 0 };

        // --- Max stats ---
        float m_MaxCur[ESC_COUNT]   = { 0, 0, 0 };
        float m_MaxVolt[ESC_COUNT]  = { 0, 0, 0 };
        float m_MaxRpm[ESC_COUNT]   = { 0, 0, 0 };
        float m_MaxSpeed[ESC_COUNT] = { 0, 0, 0 };
        float m_MaxTip = 0.0f;

        // --- Running-average accumulators (mean since last stats reset) ---
        double   m_SumCur[ESC_COUNT]   = { 0, 0, 0 };
        double   m_SumVolt[ESC_COUNT]  = { 0, 0, 0 };
        double   m_SumRpm[ESC_COUNT]   = { 0, 0, 0 };
        double   m_SumSpeed[ESC_COUNT] = { 0, 0, 0 };
        double   m_SumTip = 0.0;
        uint64_t m_StatCount[ESC_COUNT] = { 0, 0, 0 };
        float    m_StatsWindowSec = 0.0f;   // 0 = never auto-reset
        float    m_LastStatsReset = 0.0f;

        // --- Rings ---
        Ring m_Ring[ESC_COUNT];

        // --- Plot interactivity ---
        PlotView m_PlotView;
        YAxisCfg m_YCfg[ESC_COUNT][WCH_COUNT];   // WCH_COUNT (9) also covers drive (8)

        // --- Telemetry pipeline ---
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;
        uint32_t               m_RecordId[ESC_COUNT] = { 0, 0, 0 };

        Cosmic::TelemetryPanel::Mode m_LastMode = Cosmic::TelemetryPanel::Mode::None;

        // --- Recording ---
        bool        m_Recording          = false;
        bool        m_WasFlushing        = false;
        bool        m_AutoExportOnStop   = true;   // checkbox: export to k_RecordDir when Stop is pressed
        bool        m_RecordingDirty     = false;  // an intentional recording exists that hasn't been exported
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        float m_AppClock = 0.0f;
        float m_LastReplayStatPos = -1.0f;  // accumulate replay stats once per advanced frame

        static constexpr float  k_SampleRate   = 60.0f;
        static constexpr size_t k_RecordCap    = static_cast<size_t>(60.0f * 300.0f);
        static constexpr float  k_StaleTimeout = 1.5f;

        // Recordings land in a top-level, project-namespaced folder next to the exe
        // (e.g. recordings/SF_Telem/<session>/) — discoverable, and won't collide
        // with other projects' recordings. Kept separate from the app's logs/.
        static constexpr const char* k_RecordDir = "recordings/SF_Telem";

        // Crash-failsafe autosave: a rolling snapshot written every few seconds
        // while recording, so a hard crash loses at most k_AutoSaveInterval seconds.
        static constexpr const char* k_AutoSaveDir      = "recordings/SF_Telem/_autosave";
        static constexpr float       k_AutoSaveInterval = 5.0f;
    };

} // namespace Workspace
