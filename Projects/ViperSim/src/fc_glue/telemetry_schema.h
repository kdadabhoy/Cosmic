#pragma once

// telemetry_schema.h
//
// ============================================================================
// Viper telemetry / log schema (P0 deliverable).
// ============================================================================
//
// Playbook §2.3: "define the telemetry packet format + log schema early —
// cheap early, painful to retrofit." This ONE header is the contract shared by:
//   * SimHal   -> DataRecorder channel registration (this sim)
//   * TeensyHal -> SD card + downlink (future firmware)
//   * ground-station decode (future)
//
// It is intentionally dependency-light (only <cstdint> + glm for vectors) so
// the same file can later move into the portable viper-fc library and compile
// on the Teensy. The frame structs mirror the plan's §1 sketch.
//
// Two DataRecorder ENTITIES are registered per run so replay can overlay
// estimate-vs-truth (the estimator's report card, plan §3):
//   * "truth"  — the simulator's ground-truth rigid-body state
//   * "fc"     — flight-computer internals (mode, mixer, energy) — stubbed
//                until viper-fc exists (P2); channels reserved here now.
// ============================================================================

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>   // glm::quat (attitude)
#include <array>
#include <string>
#include <vector>

namespace Viper
{
	// --- Sensor sample fed INTO the flight computer (SimHal fills this) ------
	struct GpsFix
	{
		bool      valid = false;
		glm::vec3 posNed{ 0.0f };   // meters, N/E/D
		glm::vec3 velNed{ 0.0f };   // m/s
		uint32_t  sats = 0;
	};

	struct SensorFrame
	{
		uint64_t  t_us = 0;
		glm::vec3 gyro_rads{ 0.0f };    // body angular rate
		glm::vec3 accel_mss{ 0.0f };    // body specific force
		glm::vec3 mag_uT{ 0.0f };
		float     baro_pa      = 101325.0f;
		float     airspeed_pa  = 0.0f;  // pitot differential (unreliable < ~5 m/s)
		GpsFix    gps;
		float     vbat_V = 0.0f;
		float     ibat_A = 0.0f;
	};

	// --- Actuator command OUT of the flight computer -------------------------
	// Normalized; a dual-motor tailsitter uses motor[0..1] + servo[0..1].
	struct ActuatorFrame
	{
		std::array<float, 4> motor{ { 0, 0, 0, 0 } };  // [0,1]
		std::array<float, 4> servo{ { 0, 0, 0, 0 } };  // [-1,1]
	};

	// --- Ground-truth rigid-body state (IDynamics::GetTruth) -----------------
	struct RigidState
	{
		glm::vec3 posNed{ 0.0f };                 // N, E, D  (D positive down)
		glm::vec3 velNed{ 0.0f };                 // m/s in NED
		glm::quat attNed{ 1.0f, 0.0f, 0.0f, 0.0f }; // body -> NED
		glm::vec3 omegaBody{ 0.0f };              // rad/s, body axes
		float     airspeed = 0.0f;                // m/s
		float     alpha    = 0.0f;                // angle of attack, rad
		float     beta     = 0.0f;                // sideslip, rad
	};

	// =========================================================================
	// Channel schema — the column layout each DataRecorder entity is
	// registered with. Kept as free functions so SimHal and any future decoder
	// share ONE source of truth for column order.
	// =========================================================================

	// Ground-truth entity channels (12).
	inline std::vector<std::string> TruthChannels()
	{
		return {
			"pos_n", "pos_e", "pos_d",
			"vel_n", "vel_e", "vel_d",
			"roll_deg", "pitch_deg", "yaw_deg",
			"airspeed", "alpha_deg", "alt_agl",
		};
	}

	// Flight-computer entity channels (reserved for P2; recorded as zeros now).
	inline std::vector<std::string> FcChannels()
	{
		return {
			"mode", "energy_wh", "hover_budget_s",
			"mix0", "mix1", "mix2", "mix3",
			"att_err_deg", "vbat_v",
		};
	}

	// Enum mirrors the plan's mode machine (§2.4). Kept here so both the sim
	// and viper-fc agree on the numeric encoding written to telemetry.
	enum class FlightMode : int32_t
	{
		Idle = 0, Hover, Transition, Cruise, Orbit, Rtl, Failsafe,
	};
}
