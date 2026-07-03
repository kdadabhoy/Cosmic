#pragma once

// viperfc/wire/HilProtocol.h
//
// ============================================================================
// HIL wire contract (doc 04 §1 integration modes, P6)
// ============================================================================
//
// Payloads carried inside the engine's COBS+CRC16 framing (Cosmic
// serial/Framing.h — deliberately freestanding so THIS side compiles on the
// Teensy too). One byte of packet id, then a packed little-endian struct.
// Both ends are little-endian (x64 PC, Cortex-M7 Teensy) — no byte swapping.
//
//   PC  -> FC : SensorPacket (sim-clock timestamped), HeartbeatPacket, ParamPacket
//   FC  -> PC : ActuatorPacket (echoes the sensor timestamp -> latency on
//               screen), StatusPacket (mode/blend/alerts at a low rate)
//
// ≥2 Mbaud USB-CDC: a 97-byte sensor packet at 240 Hz is ~2% of the link.
// ============================================================================

#include <stdint.h>
#include <string.h>

namespace viperfc
{
namespace wire
{
	enum PacketId : uint8_t
	{
		SensorPacketId    = 0x01,   // PC -> FC
		ActuatorPacketId  = 0x02,   // FC -> PC
		HeartbeatPacketId = 0x03,   // PC -> FC (GCS link-alive; kill = link-loss test)
		StatusPacketId    = 0x04,   // FC -> PC
		CommandPacketId   = 0x05,   // PC -> FC (arm/mode/setpoints)
	};

#pragma pack(push, 1)

	struct SensorPacket
	{
		uint8_t  id = SensorPacketId;
		uint64_t t_us = 0;              // SIM clock — echoed back for latency
		float    gyro[3]  = { 0, 0, 0 };
		float    accel[3] = { 0, 0, 0 };
		float    mag[3]   = { 0, 0, 0 };
		float    baro_pa = 101325.0f;
		float    airspeed_pa = 0.0f;
		uint8_t  gps_valid = 0;
		float    gps_pos[3] = { 0, 0, 0 };
		float    gps_vel[3] = { 0, 0, 0 };
		uint32_t gps_sats = 0;
		float    vbat_V = 0.0f;
		float    ibat_A = 0.0f;
	};

	struct ActuatorPacket
	{
		uint8_t  id = ActuatorPacketId;
		uint64_t t_echo_us = 0;         // t_us of the SensorPacket this answers
		float    motor[4] = { 0, 0, 0, 0 };
		float    servo[4] = { 0, 0, 0, 0 };
	};

	struct HeartbeatPacket
	{
		uint8_t id = HeartbeatPacketId;
	};

	struct StatusPacket
	{
		uint8_t id = StatusPacketId;
		int32_t mode = 0;               // FlightMode
		int32_t phase = 0;              // TransitionPhase
		float   blend = 0.0f;
		int32_t failsafeFlags = 0;
		float   hoverElapsed_s = 0.0f;
		float   energyUsed_wh = 0.0f;
		uint8_t armed = 0;
	};

	struct CommandPacket
	{
		uint8_t id = CommandPacketId;
		uint8_t arm = 0;                // 0 = no change, 1 = arm, 2 = disarm
		int32_t requestMode = -1;       // FlightMode, -1 = no request
		float   velCmd[3] = { 0, 0, 0 };
		float   yawRateCmd = 0.0f;
		float   roi[3] = { 0, 0, 0 };
	};

#pragma pack(pop)

	// memcpy codecs: payload is exactly sizeof(T). Returns payload size, or 0
	// if the buffer is the wrong size / wrong id.
	template<typename T>
	inline size_t EncodePacket(const T& pkt, uint8_t* out, size_t cap)
	{
		if (cap < sizeof(T)) return 0;
		memcpy(out, &pkt, sizeof(T));
		return sizeof(T);
	}

	template<typename T>
	inline bool DecodePacket(const uint8_t* payload, size_t len, uint8_t expectedId, T& out)
	{
		if (len != sizeof(T) || payload[0] != expectedId) return false;
		memcpy(&out, payload, sizeof(T));
		return true;
	}
}
}
