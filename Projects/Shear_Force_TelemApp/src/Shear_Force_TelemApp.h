#pragma once

// Shear_Force_TelemApp.h
//
// ============================================================================
// Shear_Force_TelemApp — ESP32 / ESC telemetry application
// ============================================================================
//
// A single Cosmic plugin layer that:
//   1. Connects to an ESP32 over a Bluetooth-SPP COM port (Cosmic::SerialPort,
//      which already does the threaded background reads).
//   2. Parses framed, checksummed RAW ESC packets and decodes them to
//      engineering units on the host (see EscTelemetry.h).
//   3. Feeds the decoded channels into the engine telemetry stack
//      (DataRecorder -> live ImPlot charts -> CSV/.bin export -> DataPlayer
//      replay) exactly like the engine's TemplateTelemetryLayer.
//   4. Drives a simple moving square per ESC as a placeholder visual.
//
// Built for 1 ESC today, scales to k_MaxEsc (3) with no structural change —
// just raise k_EscCount.
// ============================================================================

#include <Cosmic.h>
#include "EscTelemetry.h"

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace Workspace
{
    // EscComponent — links a scene entity to its 1-based ESC id for picking,
    // replay position sync, and rendering.
    struct EscComponent
    {
        int id = 0;
    };

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
        // Per-ESC runtime state.
        struct EscState
        {
            uint32_t  recordId   = 0;
            EscSample sample;                 // latest decoded values
            bool      hasData    = false;
            float     lastSeen   = 0.0f;      // app-clock seconds of last packet
            uint64_t  packetCount = 0;
            glm::vec4 color      = { 0.3f, 0.5f, 1.0f, 1.0f }; // current square tint
        };

        // UI / pipeline helpers
        void DrawSerialWindow();
        void DrawControlsWindow();
        void DrawTelemetryWindow();
        void PumpSerial();                    // drain + parse the COM buffer
        void RenderScene();
        std::string EscName(int id) const;    // "ESC_1" ...

    private:
        // --- Serial ---
        Cosmic::SerialPort       m_Serial;
        std::vector<std::string> m_AvailablePorts;
        int                      m_SelectedPortIndex = 0;
        const std::vector<int>   m_BaudRates = { 9600, 19200, 38400, 57600,
                                                 115200, 230400, 460800, 921600 };
        int                      m_SelectedBaudIndex = 4; // 115200
        std::string              m_RxAccumulator;          // partial-line carryover
        std::string              m_Log;                    // raw serial monitor text
        bool                     m_AutoScrollLog = true;
        uint64_t                 m_GoodFrames = 0;
        uint64_t                 m_BadFrames  = 0;

        // --- Decode config (live-editable) ---
        EscConfig m_Config;

        // --- Scene / camera / visual ---
        Cosmic::Ref<Cosmic::Scene>           m_Scene;
        Cosmic::OrthographicCameraController m_Camera{ 1280.0f / 720.0f };

        // --- Telemetry pipeline ---
        Cosmic::DataRecorder   m_Recorder;
        Cosmic::DataPlayer     m_Player;
        Cosmic::TelemetryPanel m_Panel;

        std::array<EscState, 3> m_Esc;        // indexed [0 .. k_EscCount-1]

        // --- Recording state ---
        bool        m_Recording   = false;
        bool        m_WasFlushing  = false;
        std::string m_SessionName;
        std::string m_RecordStatus = "Ready.";

        float m_AppClock = 0.0f;              // monotonic seconds for liveness

        // --- Constants ---
        static constexpr int    k_MaxEsc        = 3;
        static constexpr int    k_EscCount      = 1;   // raise to 3 later
        static constexpr float  k_SampleRate    = 60.0f;
        static constexpr size_t k_RecordCapacity =
            static_cast<size_t>(k_SampleRate * 300.0f); // 5 min pre-reserved
        static constexpr float  k_StaleTimeout  = 1.5f; // s without packets = stale
    };

} // namespace Workspace

// Stable cross-DLL type hash for the EnTT component (engine owns the registry).
CS_REGISTER_COMPONENT(Workspace::EscComponent)
