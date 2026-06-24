#pragma once

// Telemetry.h
// SF_Telem
//
// ============================================================================
// Unified ESC telemetry protocol + decoding for THREE ESCs
//   - 2 drive ESCs : Right (R) + Left (L)   -> drivetrain decode (ground speed)
//   - 1 weapon ESC : Weapon (W)             -> weapon decode (weapon RPM / tip)
// ============================================================================
//
// One ESP32 reads all three ESCs on separate UART pins and forwards RAW KISS
// fields over Bluetooth-SPP (a Windows COM port). All engineering conversion
// happens HERE on the PC so constants can be tuned live without reflashing.
//
// WIRE FORMAT  (one ASCII line per packet, '\n' terminated)
// ---------------------------------------------------------
//      $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//
//   $          start-of-frame
//   <S>        side tag: 'R' (right drive), 'L' (left drive), 'W' (weapon).
//              Routes the packet to the correct ESC; an unknown tag fails parse.
//   <temp>     uint8  raw KISS temperature (deg C)
//   <vraw>     uint16 raw voltage      (centi-volts)
//   <iraw>     uint16 raw current      (centi-amps)
//   <craw>     uint16 raw consumption  (mAh)
//   <erpmraw>  uint16 raw eRPM/100
//   *          end-of-payload
//   <HH>       XOR checksum of every char between '$' and '*', 2 hex digits
//   \n         end-of-frame
//
// ROBUSTNESS: the three ESCs are fully independent. If a telemetry wire is not
// plugged in, that side simply never sends '$' frames; the host flags it absent
// /stale and keeps running on whichever sides ARE streaming. '#'-prefixed lines
// (heartbeat) fail the parse and are ignored.
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace Workspace
{
    // -------------------------------------------------------------------------
    // ESC identity. Index order is the storage order everywhere.
    // -------------------------------------------------------------------------
    enum EscId
    {
        ESC_RIGHT  = 0,
        ESC_LEFT   = 1,
        ESC_WEAPON = 2,
        ESC_COUNT  = 3
    };

    inline int  IdFromChar(char c) { return c == 'R' ? ESC_RIGHT
                                          : c == 'L' ? ESC_LEFT
                                          : c == 'W' ? ESC_WEAPON : -1; }
    inline char IdToChar(int id)   { return id == ESC_RIGHT ? 'R' : id == ESC_LEFT ? 'L' : 'W'; }
    inline const char* IdLabel(int id) { return id == ESC_RIGHT ? "Right" : id == ESC_LEFT ? "Left" : "Weapon"; }
    inline std::string IdEntity(int id){ return id == ESC_RIGHT ? "ESC_Right" : id == ESC_LEFT ? "ESC_Left" : "ESC_Weapon"; }
    inline bool IsDrive(int id)    { return id == ESC_RIGHT || id == ESC_LEFT; }

    // -------------------------------------------------------------------------
    // Channel layouts — index order is the contract with DataRecorder/Player.
    // -------------------------------------------------------------------------
    enum DriveChannel
    {
        DCH_TEMP = 0, DCH_VOLT, DCH_CURR, DCH_CONS,
        DCH_ERPM, DCH_MOTRPM, DCH_SPEED, DCH_POWER, DCH_COUNT
    };
    inline std::vector<std::string> DriveChannelNames()
    {
        return { "Temp_C", "Voltage_V", "Current_A", "Consumption_mAh",
                 "eRPM", "MotorRPM", "Speed_mph", "Power_W" };
    }

    enum WeaponChannel
    {
        WCH_TEMP = 0, WCH_VOLT, WCH_CURR, WCH_CONS,
        WCH_ERPM, WCH_MOTRPM, WCH_WPNRPM, WCH_TIP, WCH_POWER, WCH_COUNT
    };
    inline std::vector<std::string> WeaponChannelNames()
    {
        return { "Temp_C", "Voltage_V", "Current_A", "Consumption_mAh",
                 "eRPM", "MotorRPM", "WeaponRPM", "TipSpeed_mph", "Power_W" };
    }

    // -------------------------------------------------------------------------
    // Host-side conversion constants (edited live in the UI).
    // -------------------------------------------------------------------------
    struct DriveConfig
    {
        float VoltageScale    = 0.01f;
        float CurrentScale    = 0.01f;
        float ErpmScale       = 100.0f;
        int   Poles           = 14;      // motor poles (14-pole drive motor)
        float GearRatio       = 19.0f;   // motor : wheel reduction
        float SlipFactor      = 0.933f;  // drivetrain slip / efficiency
        float WheelDiameterIn = 3.5f;    // drive wheel diameter (in)
        float MotorKv         = 1400.0f; // rpm/V — used only for predicted no-load RPM
    };

    struct WeaponConfig
    {
        float VoltageScale     = 0.01f;
        float CurrentScale     = 0.01f;
        float ErpmScale        = 100.0f;
        int   Poles            = 6;       // motor poles (6-pole weapon motor)
        float GearRatio        = 4.0f;    // motor : weapon reduction (4:1 pulley)
        float WeaponDiameterIn = 7.874f;  // weapon tip diameter (in)
    };

    // -------------------------------------------------------------------------
    // RawPacket — exactly what came off the wire, pre-conversion.
    // -------------------------------------------------------------------------
    struct RawPacket
    {
        int      id   = -1;  // ESC_RIGHT / ESC_LEFT / ESC_WEAPON
        uint8_t  temp = 0;
        uint16_t voltageRaw  = 0;
        uint16_t currentRaw  = 0;
        uint16_t consumption = 0;
        uint16_t erpmRaw     = 0;
    };

    // -------------------------------------------------------------------------
    // DriveSample — decoded drive ESC values.
    // -------------------------------------------------------------------------
    struct DriveSample
    {
        float tempC = 0, voltageV = 0, currentA = 0, consumption = 0;
        float eRPM = 0, motorRPM = 0, speedMph = 0, powerW = 0;

        std::vector<float> ToChannels() const
        {
            return { tempC, voltageV, currentA, consumption,
                     eRPM, motorRPM, speedMph, powerW };
        }

        static DriveSample Decode(const RawPacket& p, const DriveConfig& cfg)
        {
            DriveSample s;
            s.tempC       = static_cast<float>(p.temp);
            s.voltageV    = p.voltageRaw * cfg.VoltageScale;
            s.currentA    = p.currentRaw * cfg.CurrentScale;
            s.consumption = static_cast<float>(p.consumption);
            s.eRPM        = p.erpmRaw   * cfg.ErpmScale;

            const int   poles = (cfg.Poles      > 1)    ? cfg.Poles      : 2;
            const float gr    = (cfg.GearRatio  != 0.0f) ? cfg.GearRatio  : 1.0f;
            const float slip  = (cfg.SlipFactor != 0.0f) ? cfg.SlipFactor : 1.0f;

            s.motorRPM = s.eRPM / (poles * 0.5f);   // motor RPM = eRPM / (poles/2)
            const float wheelRPM = s.motorRPM / gr / slip;
            const float circIn   = 3.14159265f * cfg.WheelDiameterIn;
            s.speedMph = wheelRPM * circIn / 1056.0f; // 1 mph == 1056 in/min
            s.powerW   = s.voltageV * s.currentA;
            return s;
        }
    };

    // -------------------------------------------------------------------------
    // WeaponSample — decoded weapon ESC values.
    // -------------------------------------------------------------------------
    struct WeaponSample
    {
        float tempC = 0, voltageV = 0, currentA = 0, consumption = 0;
        float eRPM = 0, motorRPM = 0, weaponRPM = 0, tipSpeedMph = 0, powerW = 0;

        std::vector<float> ToChannels() const
        {
            return { tempC, voltageV, currentA, consumption,
                     eRPM, motorRPM, weaponRPM, tipSpeedMph, powerW };
        }

        static WeaponSample Decode(const RawPacket& p, const WeaponConfig& cfg)
        {
            WeaponSample s;
            s.tempC       = static_cast<float>(p.temp);
            s.voltageV    = p.voltageRaw * cfg.VoltageScale;
            s.currentA    = p.currentRaw * cfg.CurrentScale;
            s.consumption = static_cast<float>(p.consumption);
            s.eRPM        = p.erpmRaw   * cfg.ErpmScale;

            const int   poles = (cfg.Poles     > 1)    ? cfg.Poles     : 2;
            const float gr    = (cfg.GearRatio != 0.0f) ? cfg.GearRatio : 1.0f;

            s.motorRPM  = s.eRPM / (poles * 0.5f);   // motor RPM = eRPM / (poles/2)
            s.weaponRPM = s.motorRPM / gr;
            const float circIn = 3.14159265f * cfg.WeaponDiameterIn;
            s.tipSpeedMph = s.weaponRPM * circIn / 1056.0f;
            s.powerW = s.voltageV * s.currentA;
            return s;
        }
    };

    // -------------------------------------------------------------------------
    // XOR checksum over [begin, end) — matches the ESP32 side exactly.
    // -------------------------------------------------------------------------
    inline uint8_t Checksum(const char* begin, const char* end)
    {
        uint8_t crc = 0;
        for (const char* p = begin; p < end; ++p)
            crc ^= static_cast<uint8_t>(*p);
        return crc;
    }

    // -------------------------------------------------------------------------
    // ParseFrame — validate framing + tag + checksum, extract a raw packet.
    // Returns false on any error so '#' heartbeat lines are simply ignored.
    // -------------------------------------------------------------------------
    inline bool ParseFrame(const std::string& line, RawPacket& out)
    {
        if (line.size() < 6 || line.front() != '$')
            return false;

        const size_t star = line.find('*');
        if (star == std::string::npos || star + 3 > line.size())
            return false;

        const char*   payloadBegin = line.data() + 1;
        const char*   payloadEnd   = line.data() + star;
        const uint8_t calc         = Checksum(payloadBegin, payloadEnd);

        unsigned int given = 0;
        if (std::sscanf(line.c_str() + star + 1, "%2x", &given) != 1)
            return false;
        if (static_cast<uint8_t>(given) != calc)
            return false;

        char sideC = 0;
        int  temp = 0, v = 0, i = 0, c = 0, e = 0;
        if (std::sscanf(payloadBegin, "%c,%d,%d,%d,%d,%d",
                        &sideC, &temp, &v, &i, &c, &e) != 6)
            return false;

        const int id = IdFromChar(sideC);
        if (id < 0)
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
