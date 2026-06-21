#pragma once

// WeaponTelemetry.h
// SF_Telem_Weapon
//
// ============================================================================
// Weapon-motor ESC telemetry protocol + decoding  (host side)
// ============================================================================
//
// One ESP32 reads a SINGLE weapon-motor ESC on one UART pin and forwards RAW
// KISS fields over Bluetooth-SPP (a Windows COM port). All engineering
// conversion (volts, amps, RPM, tip speed) happens HERE on the PC so the
// constants (motor pole pairs, gear/belt ratio, weapon diameter, ...) can be
// tuned live in the UI without reflashing.
//
// This is the single-ESC sibling of Shear_Force_TelemApp's EscTelemetry.h:
// same fields and decode math, but with the Right/Left side tag removed and
// the drivetrain "ground speed" replaced by weapon RPM + tip speed.
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated)
// ---------------------------------------------------------
//      $<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $            start-of-frame token
//   <temp>       uint8   raw KISS temperature (deg C, already in C)
//   <vraw>       uint16  raw voltage      (centi-volts,  V  = vraw / 100)
//   <iraw>       uint16  raw current      (centi-amps,   A  = iraw / 100)
//   <craw>       uint16  raw consumption  (mAh, used directly)
//   <erpmraw>    uint16  raw eRPM/100     (eRPM = erpmraw * 100)
//   *            end-of-payload token
//   <HH>         two hex digits: XOR checksum of every char between '$' and '*'
//   \n           end-of-frame
//
// ASCII framing is deliberate: human-readable in any serial monitor and never
// contains a NUL byte, so it survives string-based serial buffers. '#'-prefixed
// lines (heartbeat/status) fail the parse and are simply ignored.
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace Workspace
{
    // -------------------------------------------------------------------------
    // The single entity name used everywhere (recorder, player, selection).
    // -------------------------------------------------------------------------
    inline const char* WeaponEntity() { return "ESC_Weapon"; }
    inline const char* WeaponTag()    { return "ESC"; }

    // -------------------------------------------------------------------------
    // Channel layout — index order is the contract with DataRecorder/Player.
    // Keep this in sync with WeaponSample::ToChannels() and WeaponChannelNames().
    // -------------------------------------------------------------------------
    enum WeaponChannel
    {
        WPN_CH_TEMP = 0,    // deg C
        WPN_CH_VOLTAGE,     // V
        WPN_CH_CURRENT,     // A
        WPN_CH_CONSUMPTION, // mAh
        WPN_CH_ERPM,        // electrical RPM
        WPN_CH_MOTOR_RPM,   // mechanical motor RPM
        WPN_CH_WEAPON_RPM,  // weapon RPM (after gear/belt reduction)
        WPN_CH_TIP_SPEED,   // weapon tip speed (mph)
        WPN_CH_POWER,       // W
        WPN_CH_COUNT
    };

    inline std::vector<std::string> WeaponChannelNames()
    {
        return {
            "Temp_C", "Voltage_V", "Current_A", "Consumption_mAh",
            "eRPM",   "MotorRPM",  "WeaponRPM", "TipSpeed_mph", "Power_W"
        };
    }

    // -------------------------------------------------------------------------
    // WeaponConfig — host-side conversion constants (edited live in the UI).
    // -------------------------------------------------------------------------
    struct WeaponConfig
    {
        float VoltageScale     = 0.01f;  // V    per raw count   (KISS = centi-volts)
        float CurrentScale     = 0.01f;  // A    per raw count   (KISS = centi-amps)
        float ErpmScale        = 100.0f; // eRPM per raw count   (KISS sends eRPM/100)

        int   PolePairs        = 3;      // motor pole pairs (6-pole motor = 3; 14-pole = 7)
        float GearRatio        = 1.0f;   // motor : weapon reduction (1.0 = direct drive)
        float WeaponDiameterIn = 4.0f;   // weapon disk/bar diameter (inches)
    };

    // -------------------------------------------------------------------------
    // WeaponRawPacket — exactly what came off the wire, pre-conversion.
    // -------------------------------------------------------------------------
    struct WeaponRawPacket
    {
        uint8_t  temp        = 0;
        uint16_t voltageRaw  = 0;
        uint16_t currentRaw  = 0;
        uint16_t consumption = 0;
        uint16_t erpmRaw     = 0;
    };

    // -------------------------------------------------------------------------
    // WeaponSample — decoded engineering values, ready to record/plot.
    // -------------------------------------------------------------------------
    struct WeaponSample
    {
        float tempC       = 0.0f;
        float voltageV    = 0.0f;
        float currentA    = 0.0f;
        float consumption = 0.0f; // mAh
        float eRPM        = 0.0f;
        float motorRPM    = 0.0f;
        float weaponRPM   = 0.0f;
        float tipSpeedMph = 0.0f;
        float powerW      = 0.0f;

        // Flatten into the channel-ordered vector DataRecorder expects.
        std::vector<float> ToChannels() const
        {
            return { tempC, voltageV, currentA, consumption,
                     eRPM, motorRPM, weaponRPM, tipSpeedMph, powerW };
        }

        static WeaponSample Decode(const WeaponRawPacket& p, const WeaponConfig& cfg)
        {
            WeaponSample s;
            s.tempC       = static_cast<float>(p.temp);
            s.voltageV    = p.voltageRaw * cfg.VoltageScale;
            s.currentA    = p.currentRaw * cfg.CurrentScale;
            s.consumption = static_cast<float>(p.consumption);
            s.eRPM        = p.erpmRaw   * cfg.ErpmScale;

            const int   pp = (cfg.PolePairs > 0)    ? cfg.PolePairs : 1;
            const float gr = (cfg.GearRatio != 0.0f) ? cfg.GearRatio : 1.0f;

            s.motorRPM  = s.eRPM / static_cast<float>(pp);
            s.weaponRPM = s.motorRPM / gr;

            // Tip speed: weapon-rim circumference per minute -> mph (1 mph == 1056 in/min).
            const float circIn = 3.14159265f * cfg.WeaponDiameterIn;
            s.tipSpeedMph = s.weaponRPM * circIn / 1056.0f;

            s.powerW = s.voltageV * s.currentA;
            return s;
        }
    };

    // -------------------------------------------------------------------------
    // XOR checksum over [begin, end) — matches the ESP32 side exactly.
    // -------------------------------------------------------------------------
    inline uint8_t WeaponChecksum(const char* begin, const char* end)
    {
        uint8_t crc = 0;
        for (const char* p = begin; p < end; ++p)
            crc ^= static_cast<uint8_t>(*p);
        return crc;
    }

    // -------------------------------------------------------------------------
    // ParseFrame — validate framing + checksum, extract raw packet.
    //
    // `line` is one '\n'-stripped frame (trailing '\r' stripped by the caller).
    // Returns false on any framing, checksum, or field error so that the
    // ESP32's '#' status/heartbeat lines are simply ignored.
    // -------------------------------------------------------------------------
    inline bool ParseFrame(const std::string& line, WeaponRawPacket& out)
    {
        if (line.size() < 6 || line.front() != '$')
            return false;

        const size_t star = line.find('*');
        if (star == std::string::npos || star + 3 > line.size())
            return false; // need '*' followed by at least two checksum digits

        // Checksum covers everything between '$' and '*' (exclusive).
        const char*   payloadBegin = line.data() + 1;
        const char*   payloadEnd   = line.data() + star;
        const uint8_t calc         = WeaponChecksum(payloadBegin, payloadEnd);

        unsigned int given = 0;
        if (std::sscanf(line.c_str() + star + 1, "%2x", &given) != 1)
            return false;
        if (static_cast<uint8_t>(given) != calc)
            return false;

        int temp = 0, v = 0, i = 0, c = 0, e = 0;
        if (std::sscanf(payloadBegin, "%d,%d,%d,%d,%d",
                        &temp, &v, &i, &c, &e) != 5)
            return false;

        out.temp        = static_cast<uint8_t>(temp);
        out.voltageRaw  = static_cast<uint16_t>(v);
        out.currentRaw  = static_cast<uint16_t>(i);
        out.consumption = static_cast<uint16_t>(c);
        out.erpmRaw     = static_cast<uint16_t>(e);
        return true;
    }

} // namespace Workspace
