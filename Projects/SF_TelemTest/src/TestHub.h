#pragma once

// TestHub.h
// ============================================================================
// TestHub — shared telemetry backbone for the SF_TelemTest bench app.
// ============================================================================
//
// One serial connection feeding four test screens. The pump classifies each
// incoming line three ways:
//
//   * "$<S>,...*HH"  decoded ESC frames (single/dual drive + weapon tests):
//                    per-ESC good/bad counts, frames/sec, last raw frame, and a
//                    decoded sample so values can be eyeballed for sanity.
//   * "SNIFF,<tag>,<interval>,<total>,<hex>"  raw-activity reports from the
//                    sniffer sketch (bytes seen on each ESC telemetry wire,
//                    valid or not).
//   * everything else is counted toward raw link bytes (proves the ESP32 is
//                    alive) and shown in the raw byte dump.
//
// Reuses the same wire protocol + decode as SF_Telem (Telemetry.h), so the test
// firmware and the main firmware speak the same language.
// ============================================================================

#include <Cosmic.h>
#include "Telemetry.h"

#include <string>
#include <vector>

namespace Workspace
{
    class TestHub
    {
    public:
        void Init();
        void Shutdown();
        void OnUpdate(float ts);     // clock + serial pump + rate windows

        // ---- Serial ----
        void DrawSerialPanel();      // "Serial Link" window
        bool Connected() const { return m_Serial.IsOpen(); }

        // The firmware copy section auto-matches the active test screen; the root
        // pushes the active mode here each frame (0=drive,1=weapon,2=dual,3=sniff).
        void SetActiveTest(int mode) { m_ActiveTest = mode; }

        // ---- Per-ESC decoded-frame state (R / L / W) ----
        bool     HasData(int id) const { return m_HasData[id]; }
        bool     Stale(int id)   const;
        bool     Present(int id) const { return m_HasData[id] && !Stale(id); }
        uint64_t Good(int id)    const { return m_Good[id]; }
        float    Fps(int id)     const { return m_Fps[id]; }
        const std::string& LastFrame(int id) const { return m_LastFrame[id]; }
        uint64_t BadFrames()     const { return m_BadFrames; }

        const DriveSample&  GetDrive(int id) const { return m_Drive[id]; }
        const WeaponSample& GetWeapon()      const { return m_Weapon; }

        float Cur(int id)   const;
        float Volt(int id)  const;
        float Rpm(int id)   const;   // motorRPM (drive) / weaponRPM (weapon)
        float Speed(int id) const;   // mph (drive)
        float Tip()         const { return m_Weapon.tipSpeedMph; }

        float MaxCur(int id)   const { return m_MaxCur[id]; }
        float MaxVolt(int id)  const { return m_MaxVolt[id]; }
        float MaxRpm(int id)   const { return m_MaxRpm[id]; }
        float MaxSpeed(int id) const { return m_MaxSpeed[id]; }
        float MaxTip()         const { return m_MaxTip; }

        // ---- Raw link activity (ESP32 -> PC) ----
        uint64_t TotalBytes()  const { return m_TotalBytes; }
        float    BytesPerSec() const { return m_LinkBps; }
        const std::string& RawHex()   const { return m_RawHex; }   // accumulating hex dump
        const std::string& RawAscii() const { return m_RawAscii; } // same bytes, translated
        void ClearRaw() { m_RawHex.clear(); m_RawAscii.clear(); } // clears both raw logs

        // ---- Per-wire sniff stats (ESC -> ESP32, from SNIFF lines) ----
        struct Sniff
        {
            uint64_t    total         = 0;     // total bytes seen on this wire
            uint64_t    intervalBytes = 0;     // bytes in the last reported interval
            float       bps           = 0.0f;  // bytes/sec
            float       lastSeen      = -100.0f;
            std::string lastHex;               // hex of the last few bytes
        };
        const Sniff& GetSniff(int id) const { return m_Sniff[id]; }
        bool         SniffActive(int id) const;

        // ---- Configs (decode constants) ----
        DriveConfig&  DriveCfg()  { return m_DriveCfg; }
        WeaponConfig& WeaponCfg() { return m_WeaponCfg; }

        void ResetCounts();

    private:
        void PumpSerial();
        void HandleLine(const std::string& line);
        void AppendRaw(const std::string& chunk);   // appends to m_RawHex + m_RawAscii

    private:
        // --- Serial ---
        Cosmic::SerialPort       m_Serial;
        std::vector<std::string> m_Ports;
        std::string              m_SelectedPort;        // selected by NAME, not index
        const std::vector<int>   m_BaudRates = { 9600, 19200, 38400, 57600,
                                                 115200, 230400, 460800, 921600 };
        int                      m_BaudIndex = 4; // 115200
        bool                     m_AutoReconnect = true;  // re-open the link when data stops
        bool                     m_WantConnection = false; // user intends to stay connected
        float                    m_ReconnectClock = 0.0f;
        std::string              m_RxAccumulator;
        std::string              m_Log;
        bool                     m_AutoScrollLog = true;

        // --- Arduino firmware copy UI (auto-matches the active test screen) ---
        int  m_ActiveTest  = 0;    // 0=drive 1=weapon 2=dual 3=sniff
        int  m_FwRightPin  = 16;   // GPIO defaults match the wiring diagram
        int  m_FwLeftPin   = 17;
        int  m_FwWeaponPin = 13;
        int  m_FwDriveSide = 0;    // single-drive tag: 0='R', 1='L'
        char m_FwBtName[32] = "SF_TelemTest";  // Bluetooth device name baked into the sketch
        bool m_ShowPinout  = false;
        Cosmic::Ref<Cosmic::Texture2D> m_PinoutTex;

        // --- Configs ---
        DriveConfig  m_DriveCfg;
        WeaponConfig m_WeaponCfg;

        // --- Per-ESC decoded state ---
        DriveSample  m_Drive[ESC_COUNT];
        WeaponSample m_Weapon;
        bool         m_HasData[ESC_COUNT]   = { false, false, false };
        float        m_LastSeen[ESC_COUNT]  = { 0, 0, 0 };
        uint64_t     m_Good[ESC_COUNT]      = { 0, 0, 0 };
        std::string  m_LastFrame[ESC_COUNT];
        uint64_t     m_BadFrames = 0;

        // --- Max stats ---
        float m_MaxCur[ESC_COUNT]   = { 0, 0, 0 };
        float m_MaxVolt[ESC_COUNT]  = { 0, 0, 0 };
        float m_MaxRpm[ESC_COUNT]   = { 0, 0, 0 };
        float m_MaxSpeed[ESC_COUNT] = { 0, 0, 0 };
        float m_MaxTip = 0.0f;

        // --- Rate windows ---
        uint64_t m_FpsAccumN[ESC_COUNT] = { 0, 0, 0 };
        float    m_Fps[ESC_COUNT]       = { 0, 0, 0 };

        // --- Raw link ---
        uint64_t    m_TotalBytes    = 0;
        uint64_t    m_LinkAccumBytes = 0;
        float       m_LinkBps       = 0.0f;
        float       m_LastByteTime  = -100.0f;  // m_AppClock of the last byte received
        std::string m_RawHex;                   // accumulating hex view (capped)
        std::string m_RawAscii;                 // accumulating ASCII translation (capped)

        // --- Sniff ---
        Sniff    m_Sniff[ESC_COUNT];
        uint64_t m_SniffAccum[ESC_COUNT] = { 0, 0, 0 };

        float m_AppClock     = 0.0f;
        float m_RateClock    = 0.0f;
        float m_PortScanClock = 1.0f;  // start >= interval so the first scan is immediate

        static constexpr float  k_StaleTimeout      = 1.5f;
        static constexpr float  k_ReconnectInterval = 3.0f;  // s of no-data before each retry
        static constexpr size_t k_RawLogCap         = 65536; // raw hex/ascii log byte cap
    };

} // namespace Workspace
