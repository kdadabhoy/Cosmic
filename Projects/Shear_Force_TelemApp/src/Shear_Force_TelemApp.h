#pragma once

// Shear_Force_TelemApp.h
//
// ============================================================================
// Shear_Force_TelemApp — ESP32 / dual-ESC drive telemetry application
// ============================================================================
//
// A single Cosmic plugin layer that:
//   1. Connects to an ESP32 over a Bluetooth-SPP COM port (Cosmic::SerialPort).
//   2. Parses framed, checksummed RAW packets tagged Right/Left and decodes
//      them to engineering units on the host (see EscTelemetry.h).
//   3. Feeds BOTH sides into the engine telemetry stack (DataRecorder -> CSV /
//      .bin export -> DataPlayer replay).
//   4. Overlays Right vs Left for every channel in a live/replay dashboard.
//   5. Drives a differential-drive robot visual from the two wheel speeds so
//      you can see the robot translate and turn.
//
// Fault tolerance: the two sides are fully independent. If one ESC's telemetry
// dies, that side is flagged stale (error note) and the app keeps running on
// the surviving side.
// ============================================================================

#include <Cosmic.h>
#include "EscTelemetry.h"

#include <array>
#include <deque>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Workspace
{
    class Shear_Force_TelemApp : public Cosmic::Layer
    {
    public:
        Shear_Force_TelemApp();
        virtual ~Shear_Force_TelemApp() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnFixedUpdate(float dt)   override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        // -------- Per-side runtime state --------
        struct SideState
        {
            uint32_t  recordId    = 0;
            EscSample sample;                 // latest decoded values
            bool      hasData     = false;
            float     lastSeen    = 0.0f;     // app-clock seconds of last packet
            uint64_t  packetCount = 0;
        };

        // -------- Overlay-plot ring buffer (one per side) --------
        struct PlotRing
        {
            static constexpr int Cap = 512;
            std::vector<float>                     times{ std::vector<float>(Cap, 0.0f) };
            std::array<std::vector<float>, ESC_CH_COUNT> ch;
            int   offset = 0;
            int   count  = 0;
            float lastT  = -1.0f;             // de-dupe guard

            PlotRing() { for (auto& c : ch) c.assign(Cap, 0.0f); }
            void Clear();
            void Push(float t, const std::vector<float>& values);
        };

        // -------- Pipeline / UI helpers --------
        void DrawSerialWindow();
        void DrawControlsWindow();
        void DrawDashboardWindow();   // Right-vs-Left overlay plots + per-side status
        void DrawTelemetryWindow();   // engine panel: replay loader + drill-down
        void PumpSerial();            // drain + parse the COM buffer
        void SampleForDisplay(float ts); // fill rings + integrate robot pose
        void IntegratePose(float vRight, float vLeft, float dt);
        void RenderRobot();
        bool SideStale(int side) const;

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
        EscConfig m_Config;

        // --- Camera ---
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };

        // --- Telemetry pipeline ---
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;

        std::array<SideState, SIDE_COUNT> m_Side;
        std::array<PlotRing,  SIDE_COUNT> m_Ring;

        // --- Robot kinematics (differential drive) ---
        glm::vec2 m_RobotPos      = { 0.0f, 0.0f };
        float     m_RobotHeading  = 1.5708f;       // facing +Y to start
        std::deque<glm::vec2> m_Trail;
        float     m_TrackWidth    = 0.8f;          // world units between wheels
        float     m_SpeedScale    = 0.12f;         // world units/sec per mph
        bool      m_InvertHeading = false;         // flip turn direction if wired opposite

        // --- Replay/live bookkeeping ---
        Cosmic::TelemetryPanel::Mode m_LastMode = Cosmic::TelemetryPanel::Mode::None;
        float m_LastReplayPos = -1.0f;

        // --- Recording state ---
        bool        m_Recording   = false;
        bool        m_WasFlushing  = false;
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        float m_AppClock = 0.0f;

        // --- Constants ---
        static constexpr float  k_SampleRate     = 60.0f;
        static constexpr size_t k_RecordCapacity =
            static_cast<size_t>(k_SampleRate * 300.0f); // 5 min pre-reserved
        static constexpr float  k_StaleTimeout   = 1.5f; // s without packets = stale
        static constexpr int    k_TrailLength    = 240;
    };

} // namespace Workspace
