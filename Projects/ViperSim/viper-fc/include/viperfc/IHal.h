#pragma once

// viperfc/IHal.h
//
// ============================================================================
// viper-fc — the HAL boundary (plan doc 04 §1)
// ============================================================================
//
// Everything outside the flight computer plugs in through this small
// interface: SimHal (inside the ViperSim DLL), TeensyHal (PlatformIO), and the
// HIL bridge all implement it. The frame structs ARE the wire/log contract —
// keep them portable (fixed-width types, no STL, no glm).
// ============================================================================

#include "viperfc/Math.h"

#include <cstdint>

namespace viperfc
{
	struct GpsFix
	{
		bool     valid = false;
		Vec3     posNed{};        // meters from home/origin
		Vec3     velNed{};        // m/s
		uint32_t sats = 0;
	};

	// Sensor sample fed INTO the flight computer each step.
	struct SensorFrame
	{
		uint64_t t_us = 0;
		Vec3     gyro_rads{};      // body angular rate
		Vec3     accel_mss{};      // body specific force (measures -gravity at rest)
		Vec3     mag_uT{};         // body magnetic field
		float    baro_pa     = 101325.0f;
		float    airspeed_pa = 0.0f;   // pitot differential — unreliable < ~5 m/s
		GpsFix   gps;
		float    vbat_V = 0.0f;
		float    ibat_A = 0.0f;
	};

	// Actuator command OUT of the flight computer. Normalized: motors [0,1],
	// servos [-1,1]. The dual-motor tailsitter uses motor[0..1] (right, left)
	// and servo[0..1] (right elevon, left elevon); the extra slots keep the
	// quad-tailsitter fallback wire-compatible.
	struct ActuatorFrame
	{
		float motor[4] = { 0, 0, 0, 0 };
		float servo[4] = { 0, 0, 0, 0 };
	};

	// The hardware abstraction the FC runs against. Implementations:
	//   SimHal    — samples the sim's IDynamics truth through the sensor models
	//   TeensyHal — real drivers on the Teensy 4.1 (firmware/)
	class IHal
	{
	public:
		virtual ~IHal() = default;

		virtual bool     ReadSensors(SensorFrame& out) = 0;
		virtual void     WriteActuators(const ActuatorFrame& cmd) = 0;
		virtual uint64_t TimeUs() = 0;
		virtual void     Log(const char* /*msg*/) {}
	};
}
