#pragma once

// SF_Telem_Weapon.h
//
// ============================================================================
// SF_Telem_Weapon — ESP32 / single weapon-motor ESC telemetry application
// ============================================================================
//
// A single Cosmic plugin layer that:
//   1. Connects to an ESP32 over a Bluetooth-SPP COM port (Cosmic::SerialPort).
//   2. Parses framed, checksummed RAW packets and decodes them to engineering
//      units on the host (see WeaponTelemetry.h).
//   3. Feeds the weapon ESC into the engine telemetry stack (DataRecorder ->
//      CSV / .bin export -> DataPlayer replay).
//   4. Plots every channel live (and in replay) on its own chart.
//
// This is the single-ESC sibling of Shear_Force_TelemApp. It deliberately drops
// the dual Right/Left overlay, the differential-drive robot visual, and the
// map/grid — a weapon motor has no kinematics to draw, so the value is the
// plots, the data, and the CSV/bin export.
// ============================================================================

#include <Cosmic.h>
#include "WeaponTelemetry.h"
#include "WeaponModel.h"

#include <array>
#include <string>
#include <vector>

namespace Workspace
{
    class SF_Telem_Weapon : public Cosmic::Layer
    {
    public:
        SF_Telem_Weapon();
        virtual ~SF_Telem_Weapon() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnFixedUpdate(float dt)   override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        // -------- Plot ring buffer (scrolling history per channel) --------
        struct PlotRing
        {
            static constexpr int Cap = 512;
            std::vector<float>                               times{ std::vector<float>(Cap, 0.0f) };
            std::array<std::vector<float>, WPN_CH_COUNT>     ch;
            int   offset = 0;
            int   count  = 0;
            float lastT  = -1.0f;             // de-dupe guard

            PlotRing() { for (auto& c : ch) c.assign(Cap, 0.0f); }
            void Clear();
            void Push(float t, const std::vector<float>& values);
        };

        // -------- Pipeline / UI helpers --------
        void DrawSerialWindow();
        void DrawControlsWindow();    // transport, recording, decode constants
        void DrawDashboardWindow();   // health banner + per-channel plots
        void DrawModelWindow();       // predictive spin-up model + measured-vs-theory
        void DrawTelemetryWindow();   // engine panel: replay loader + drill-down
        void PumpSerial();            // drain + parse the COM buffer
        void SampleForDisplay();      // fill the plot ring (live or replay)
        void RecomputeModel();        // re-run the spin-up sim from current inputs
        float TipRadiusM() const;     // tip radius (m) from the decode weapon diameter
        float ModelEffectiveVoltage() const; // live measured V, or the manual input
        bool WeaponStale() const;

    private:
        // --- Serial ---
        Cosmic::SerialPort       m_Serial;
        std::vector<std::string> m_AvailablePorts;
        int                      m_SelectedPortIndex = 0;
        const std::vector<int>   m_BaudRates = { 9600, 19200, 38400, 57600,
                                                 115200, 230400, 460800, 921600 };
        int                      m_SelectedBaudIndex = 4; // 115200
        std::string              m_RxAccumulator;
        std::string              m_Log;
        bool                     m_AutoScrollLog = true;
        uint64_t                 m_GoodFrames = 0;
        uint64_t                 m_BadFrames  = 0;

        // --- Decode config (live-editable) ---
        WeaponConfig m_Config;

        // --- Predictive spin-up model (ported spreadsheet) ---
        WeaponModelConfig m_Model;
        WeaponModelResult m_ModelResult;
        bool              m_ModelDirty          = true;
        bool              m_ModelUseLiveVoltage = true;  // drive prediction off measured V
        float             m_ModelLastVoltage    = -1.0f; // V used by the last recompute

        // --- Camera (drives the empty viewport clear; no scene is rendered) ---
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };

        // --- Telemetry pipeline ---
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;

        // --- Single-ESC runtime state ---
        uint32_t     m_RecordId    = 0;
        WeaponSample m_Sample;                // latest decoded values
        bool         m_HasData     = false;
        float        m_LastSeen    = 0.0f;    // app-clock seconds of last packet
        uint64_t     m_PacketCount = 0;
        PlotRing     m_Ring;

        // --- Replay/live bookkeeping ---
        Cosmic::TelemetryPanel::Mode m_LastMode = Cosmic::TelemetryPanel::Mode::None;

        // --- Recording state ---
        bool        m_Recording   = false;
        bool        m_WasFlushing = false;
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        float m_AppClock = 0.0f;

        // --- Constants ---
        static constexpr float  k_SampleRate     = 60.0f;
        static constexpr size_t k_RecordCapacity =
            static_cast<size_t>(k_SampleRate * 300.0f); // 5 min pre-reserved
        static constexpr float  k_StaleTimeout   = 1.5f; // s without packets = stale
    };

} // namespace Workspace
