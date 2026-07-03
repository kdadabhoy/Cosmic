#pragma once

// telemetry_schema.h
//
// ============================================================================
// Viper telemetry / log schema (P0 deliverable) — sim-side view.
// ============================================================================
//
// The PORTABLE half of the contract (FlightMode, FcAlert, TelemetrySnapshot,
// the fc-entity channel list + row writer) moved into the viper-fc library at
// P2, exactly as the plan intended ("the same file can later move into the
// portable viper-fc library"): see viperfc/TelemetrySchema.h. This header
// keeps the SIM-side pieces — glm-typed truth state, the truth-entity channel
// list, and the sim's actuator struct — and re-exports the shared types so
// existing includes keep working.
//
// Two DataRecorder entities per run (estimate-vs-truth overlay, plan §3):
//   * "truth"   — simulator ground truth        (TruthChannels, glm-side)
//   * "fc"      — flight-computer internals     (viperfc::FcChannelNames)
//   * "sensors" — raw SensorFrame stream        (SensorChannels; feeds the
//                 offline replay-through-FC regression path)
// ============================================================================

#include <viperfc/TelemetrySchema.h>
#include <viperfc/IHal.h>

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <string>
#include <vector>

namespace Viper
{
	// Shared FC types, re-exported under the app namespace.
	using FlightMode = viperfc::FlightMode;
	using FcAlert    = viperfc::FcAlert;

	// --- Actuator command applied to the DYNAMICS (sim side) -----------------
	// Normalized; a dual tailsitter uses motor[0..1] + servo[0..1].
	struct ActuatorFrame
	{
		std::array<float, 4> motor{ { 0, 0, 0, 0 } };  // [0,1]
		std::array<float, 4> servo{ { 0, 0, 0, 0 } };  // [-1,1]
	};

	inline ActuatorFrame FromFc(const viperfc::ActuatorFrame& u)
	{
		ActuatorFrame out;
		for (int i = 0; i < 4; ++i)
		{
			out.motor[i] = u.motor[i];
			out.servo[i] = u.servo[i];
		}
		return out;
	}

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

	// Ground-truth entity channels (12). ORDER IS LOAD-BEARING: ReplayScreen
	// indexes these — extend by APPENDING only.
	inline std::vector<std::string> TruthChannels()
	{
		return {
			"pos_n", "pos_e", "pos_d",
			"vel_n", "vel_e", "vel_d",
			"roll_deg", "pitch_deg", "yaw_deg",
			"airspeed", "alpha_deg", "alt_agl",
		};
	}

	// Raw sensor stream (feeds offline replay-through-FC). Order matches
	// SensorRow() in SimHub.cpp.
	inline std::vector<std::string> SensorChannels()
	{
		return {
			"gyro_x", "gyro_y", "gyro_z",
			"accel_x", "accel_y", "accel_z",
			"mag_x", "mag_y", "mag_z",
			"baro_pa", "pitot_pa",
			"gps_valid",
			"gps_n", "gps_e", "gps_d",
			"gps_vn", "gps_ve", "gps_vd",
			"vbat_v", "ibat_a",
		};
	}

	constexpr int kSensorChannelCount = 20;

	inline void WriteSensorRow(const viperfc::SensorFrame& f, float* out)
	{
		int i = 0;
		out[i++] = f.gyro_rads.x;  out[i++] = f.gyro_rads.y;  out[i++] = f.gyro_rads.z;
		out[i++] = f.accel_mss.x;  out[i++] = f.accel_mss.y;  out[i++] = f.accel_mss.z;
		out[i++] = f.mag_uT.x;     out[i++] = f.mag_uT.y;     out[i++] = f.mag_uT.z;
		out[i++] = f.baro_pa;
		out[i++] = f.airspeed_pa;
		out[i++] = f.gps.valid ? 1.0f : 0.0f;
		out[i++] = f.gps.posNed.x; out[i++] = f.gps.posNed.y; out[i++] = f.gps.posNed.z;
		out[i++] = f.gps.velNed.x; out[i++] = f.gps.velNed.y; out[i++] = f.gps.velNed.z;
		out[i++] = f.vbat_V;
		out[i++] = f.ibat_A;
	}

	inline viperfc::SensorFrame ReadSensorRow(const std::vector<float>& v, float t)
	{
		viperfc::SensorFrame f;
		if (v.size() < kSensorChannelCount)
			return f;
		f.t_us = static_cast<uint64_t>(t * 1e6);
		f.gyro_rads = { v[0], v[1], v[2] };
		f.accel_mss = { v[3], v[4], v[5] };
		f.mag_uT    = { v[6], v[7], v[8] };
		f.baro_pa     = v[9];
		f.airspeed_pa = v[10];
		f.gps.valid   = v[11] > 0.5f;
		f.gps.posNed  = { v[12], v[13], v[14] };
		f.gps.velNed  = { v[15], v[16], v[17] };
		f.gps.sats    = f.gps.valid ? 14u : 0u;
		f.vbat_V = v[18];
		f.ibat_A = v[19];
		return f;
	}
}
