#pragma once

// EscTelemetry.h
// Shear_Force_TelemApp
//
// ============================================================================
// ESC telemetry protocol + decoding  (host side)
// ============================================================================
//
// The ESP32 forwards RAW KISS ESC fields over Bluetooth-SPP (a Windows COM
// port).  All engineering conversion happens HERE on the PC so the constants
// (motor pole pairs, gear ratio, wheel diameter, ...) can be tuned live in the
// UI without reflashing the microcontroller.
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated)
// ---------------------------------------------------------
//      $<id>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $            start-of-frame token
//   <id>         1-based ESC index (1..3) — lets one link carry many ESCs
//   <temp>       uint8   raw KISS temperature (deg C, already in C)
//   <vraw>       uint16  raw voltage      (centi-volts,  V  = vraw / 100)
//   <iraw>       uint16  raw current      (centi-amps,   A  = iraw / 100)
//   <craw>       uint16  raw consumption  (mAh, used directly)
//   <erpmraw>    uint16  raw eRPM/100     (eRPM = erpmraw * 100)
//   *            end-of-payload token
//   <HH>         two hex digits: XOR checksum of every char between '$' and '*'
//   \n           end-of-frame
//
// ASCII framing is deliberate: it is human-readable in any serial monitor and
// it never contains a NUL byte, so it survives string-based serial buffers.
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace Workspace
{
    // -------------------------------------------------------------------------
    // Channel layout — index order is the contract with DataRecorder/Player.
    // Keep this in sync with EscSample::ToChannels().
    // -------------------------------------------------------------------------
    enum EscChannel
    {
        ESC_CH_TEMP = 0,   // deg C
        ESC_CH_VOLTAGE,    // V
        ESC_CH_CURRENT,    // A
        ESC_CH_CONSUMPTION,// mAh
        ESC_CH_ERPM,       // electrical RPM
        ESC_CH_MOTOR_RPM,  // mechanical motor RPM
        ESC_CH_SPEED,      // ground speed (mph)
        ESC_CH_POWER,      // W
        ESC_CH_COUNT
    };

    inline std::vector<std::string> EscChannelNames()
    {
        return {
            "Temp_C", "Voltage_V", "Current_A", "Consumption_mAh",
            "eRPM",   "MotorRPM",  "Speed_mph", "Power_W"
        };
    }

    // -------------------------------------------------------------------------
    // EscConfig — host-side conversion constants (edited live in the UI).
    // -------------------------------------------------------------------------
    struct EscConfig
    {
        float VoltageScale   = 0.01f;  // V   per raw count   (KISS = centi-volts)
        float CurrentScale   = 0.01f;  // A   per raw count   (KISS = centi-amps)
        float ErpmScale      = 100.0f; // eRPM per raw count  (KISS sends eRPM/100)

        int   PolePairs      = 7;      // motor pole pairs (14-pole motor = 7)
        float GearRatio      = 19.0f;  // motor : wheel reduction
        float SlipFactor     = 0.933f; // empirical drivetrain slip / efficiency
        float WheelDiameterIn = 3.5f;  // drive wheel diameter (inches)
    };

    // -------------------------------------------------------------------------
    // EscRawPacket — exactly what came off the wire, pre-conversion.
    // -------------------------------------------------------------------------
    struct EscRawPacket
    {
        int      id          = 0;
        uint8_t  temp        = 0;
        uint16_t voltageRaw  = 0;
        uint16_t currentRaw  = 0;
        uint16_t consumption = 0;
        uint16_t erpmRaw     = 0;
    };

    // -------------------------------------------------------------------------
    // EscSample — decoded engineering values, ready to record/plot/render.
    // -------------------------------------------------------------------------
    struct EscSample
    {
        float tempC       = 0.0f;
        float voltageV    = 0.0f;
        float currentA    = 0.0f;
        float consumption = 0.0f; // mAh
        float eRPM        = 0.0f;
        float motorRPM    = 0.0f;
        float speedMph    = 0.0f;
        float powerW      = 0.0f;

        // Flatten into the channel-ordered vector DataRecorder expects.
        std::vector<float> ToChannels() const
        {
            return { tempC, voltageV, currentA, consumption,
                     eRPM, motorRPM, speedMph, powerW };
        }

        static EscSample Decode(const EscRawPacket& p, const EscConfig& cfg)
        {
            EscSample s;
            s.tempC       = static_cast<float>(p.temp);
            s.voltageV    = p.voltageRaw * cfg.VoltageScale;
            s.currentA    = p.currentRaw * cfg.CurrentScale;
            s.consumption = static_cast<float>(p.consumption);
            s.eRPM        = p.erpmRaw   * cfg.ErpmScale;

            const int   pp   = (cfg.PolePairs   > 0) ? cfg.PolePairs   : 1;
            const float gr   = (cfg.GearRatio   != 0.0f) ? cfg.GearRatio  : 1.0f;
            const float slip = (cfg.SlipFactor  != 0.0f) ? cfg.SlipFactor : 1.0f;

            s.motorRPM = s.eRPM / static_cast<float>(pp);

            // Wheel RPM -> inches/min -> mph (1 mph == 1056 in/min).
            const float wheelRPM = s.motorRPM / gr / slip;
            const float circIn   = 3.14159265f * cfg.WheelDiameterIn;
            s.speedMph = wheelRPM * circIn / 1056.0f;

            s.powerW = s.voltageV * s.currentA;
            return s;
        }
    };

    // -------------------------------------------------------------------------
    // XOR checksum over [begin, end) — matches the ESP32 side exactly.
    // -------------------------------------------------------------------------
    inline uint8_t EscChecksum(const char* begin, const char* end)
    {
        uint8_t crc = 0;
        for (const char* p = begin; p < end; ++p)
            crc ^= static_cast<uint8_t>(*p);
        return crc;
    }

    // -------------------------------------------------------------------------
    // ParseFrame — validate framing + checksum and extract a raw packet.
    //
    // `line` is one '\n'-stripped frame (leading/trailing '\r' tolerated by the
    // caller).  Returns false on any framing, checksum, or field error so that
    // status/heartbeat text the ESP32 also emits is simply ignored.
    // -------------------------------------------------------------------------
    inline bool ParseFrame(const std::string& line, EscRawPacket& out)
    {
        if (line.size() < 5 || line.front() != '$')
            return false;

        const size_t star = line.find('*');
        if (star == std::string::npos || star + 3 > line.size())
            return false; // need '*' followed by at least two checksum digits

        // Checksum covers everything between '$' and '*' (exclusive).
        const char*  payloadBegin = line.data() + 1;
        const char*  payloadEnd   = line.data() + star;
        const uint8_t calc        = EscChecksum(payloadBegin, payloadEnd);

        unsigned int given = 0;
        if (std::sscanf(line.c_str() + star + 1, "%2x", &given) != 1)
            return false;
        if (static_cast<uint8_t>(given) != calc)
            return false;

        int id = 0, temp = 0, v = 0, i = 0, c = 0, e = 0;
        if (std::sscanf(payloadBegin, "%d,%d,%d,%d,%d,%d",
                        &id, &temp, &v, &i, &c, &e) != 6)
            return false;

        out.id          = id;
        out.temp        = static_cast<uint8_t>(temp);
        out.voltageRaw  = static_cast<uint16_t>(v);
        out.currentRaw  = static_cast<uint16_t>(i);
        out.consumption = static_cast<uint16_t>(c);
        out.erpmRaw     = static_cast<uint16_t>(e);
        return true;
    }

} // namespace Workspace
