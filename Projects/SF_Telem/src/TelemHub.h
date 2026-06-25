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

        void Init();      // register recorder entities + panel inspectors + ports
        void Shutdown();
        void OnUpdate(float ts);     // clock, serial pump, panel, model, rings, flush
        void RecordFixed(float dt);  // continuous capture (call from OnFixedUpdate)

        // ---- Presence / mode ----
        bool HasData(int id)  const { return m_HasData[id]; }
        bool Stale(int id)    const;
        bool Present(int id)  const { return m_HasData[id] && !Stale(id); }
        bool AnyPresent()     const;
        bool Replaying()      const;

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

        // ---- Reusable per-ESC plot pass (one ImPlot chart per channel) ----
        void DrawEscPlots(int id);

    private:
        void PumpSerial();
        void SampleRings();
        void RecomputeModel();
        float ModelEffectiveVoltage() const;
        float TipRadiusM() const { return (m_WeaponCfg.WeaponDiameterIn * 0.0254f) * 0.5f; }

    private:
        // --- Serial ---
        Cosmic::SerialPort       m_Serial;
        std::vector<std::string> m_Ports;
        int                      m_PortIndex = 0;
        const std::vector<int>   m_BaudRates = { 9600, 19200, 38400, 57600,
                                                 115200, 230400, 460800, 921600 };
        int                      m_BaudIndex = 4; // 115200
        std::string              m_RxAccumulator;
        std::string              m_Log;
        bool                     m_AutoScrollLog = true;
        uint64_t                 m_GoodFrames = 0;
        uint64_t                 m_BadFrames  = 0;

        // --- Arduino firmware copy UI (Serial Link -> Arduino Firmware) ---
        int  m_FwRightPin  = 16;   // GPIO defaults match the wiring diagram
        int  m_FwLeftPin   = 17;
        int  m_FwWeaponPin = 13;
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

        // --- Telemetry pipeline ---
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;
        uint32_t               m_RecordId[ESC_COUNT] = { 0, 0, 0 };

        Cosmic::TelemetryPanel::Mode m_LastMode = Cosmic::TelemetryPanel::Mode::None;

        // --- Recording ---
        bool        m_Recording   = false;
        bool        m_WasFlushing = false;
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        float m_AppClock = 0.0f;

        static constexpr float  k_SampleRate   = 60.0f;
        static constexpr size_t k_RecordCap    = static_cast<size_t>(60.0f * 300.0f);
        static constexpr float  k_StaleTimeout = 1.5f;
    };

} // namespace Workspace
