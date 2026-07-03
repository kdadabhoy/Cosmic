#pragma once

// viperfc/TelemetrySchema.h
//
// ============================================================================
// Viper telemetry / log schema — the P0 contract, now living in viper-fc
// (plan doc 04 §1: "one header shared by SimHal → DataRecorder, TeensyHal →
// SD + downlink, and the ground-station decode"). Portable: no STL containers
// here beyond what a desktop consumer opts into via VIPERFC_DESKTOP.
// ============================================================================
//
// The `fc` entity channel ORDER below is the wire/log layout. SimHal records
// exactly TelemetrySnapshot::Write() into it; any decoder indexes by the same
// list. Change BOTH together or not at all.
// ============================================================================

#include "viperfc/Math.h"

#include <cstdint>

#if !defined(VIPERFC_NO_STL)
	#include <string>
	#include <vector>
#endif

namespace viperfc
{
	// Mode machine (doc 04 §2.4). Numeric values are recorded/telemetered —
	// append only, never renumber.
	enum class FlightMode : int32_t
	{
		Idle = 0, Hover, Transition, Cruise, Orbit, Rtl, Failsafe,
	};

	// Transition sub-state (recorded alongside mode; §2.4.2 instrumentation).
	enum class TransitionPhase : int32_t
	{
		None = 0, Accel, Blend, CruiseLocked, Decel, Flare,
	};

	// Failsafe/alert events the supervisor raises. The sim maps these to audio
	// alerts + UI banners; the real bird maps them to downlink messages.
	enum class FcAlert : int32_t
	{
		None = 0,
		HoverBudgetWarn,   // 80% of the 3–5 min cumulative hover cap
		HoverBudgetHit,    // cap reached — forced out of hover
		BatteryLow,        // per-cell warning threshold
		BatteryCritical,   // force land
		BatteryReserve,    // Wh reserve fraction -> RTL
		LinkLost,          // heartbeat timeout -> RTL
		GeofenceAlt,       // 400 ft AGL breach
		GeofenceRadius,    // horizontal fence breach
		EnvelopeAlpha,     // stall-guard breach in cruise-side flight
		GpsLost,
		ModeChange,        // informational chime
	};

	// One snapshot of FC internals per control step — the estimator's report
	// card lives in the estimate-vs-truth overlay this feeds (plan §3).
	struct TelemetrySnapshot
	{
		FlightMode      mode      = FlightMode::Idle;
		TransitionPhase phase     = TransitionPhase::None;
		float           blend     = 0.0f;   // 0 = pure hover mixer, 1 = pure cruise

		// Estimator state
		Quat  attEst{};
		Vec3  posEst{};
		Vec3  velEst{};
		float airspeedEst = 0.0f;
		float altAglEst   = 0.0f;

		// Controller outputs
		float motor[2] = { 0, 0 };
		float servo[2] = { 0, 0 };
		float attErrRad = 0.0f;    // |attitude error| fed to the rate loop

		// Energy accounting (§2.5)
		float vbat_V        = 0.0f;
		float ibat_A        = 0.0f;
		float power_W       = 0.0f;
		float energyUsed_wh = 0.0f;
		float hoverElapsed_s = 0.0f;   // cumulative hover-budget clock

		// Failsafe
		FcAlert lastAlert   = FcAlert::None;
		int32_t failsafeFlags = 0;     // bitmask of active FcAlert conditions

		bool armed = false;
	};

	// fc-entity channel count — keep in sync with the list + writer below.
	constexpr int kFcChannelCount = 24;

	// Row writer: one float per channel, IN CHANNEL ORDER. `out` must hold
	// kFcChannelCount floats. Shared by SimHal (DataRecorder) and TeensyHal
	// (SD/downlink) so the layouts can never diverge.
	inline void WriteFcRow(const TelemetrySnapshot& t, float* out)
	{
		float r, p, y;
		ToEulerZYX(t.attEst, r, p, y);
		int i = 0;
		out[i++] = static_cast<float>(t.mode);
		out[i++] = static_cast<float>(t.phase);
		out[i++] = t.blend;
		out[i++] = Deg(r);
		out[i++] = Deg(p);
		out[i++] = Deg(y);
		out[i++] = t.posEst.x;
		out[i++] = t.posEst.y;
		out[i++] = t.posEst.z;
		out[i++] = t.velEst.x;
		out[i++] = t.velEst.y;
		out[i++] = t.velEst.z;
		out[i++] = t.airspeedEst;
		out[i++] = t.altAglEst;
		out[i++] = t.motor[0];
		out[i++] = t.motor[1];
		out[i++] = t.servo[0];
		out[i++] = t.servo[1];
		out[i++] = Deg(t.attErrRad);
		out[i++] = t.vbat_V;
		out[i++] = t.power_W;
		out[i++] = t.energyUsed_wh;
		out[i++] = t.hoverElapsed_s;
		out[i++] = static_cast<float>(t.failsafeFlags);
	}

#if !defined(VIPERFC_NO_STL)
	// Channel names for desktop consumers (DataRecorder registration, decoders).
	inline std::vector<std::string> FcChannelNames()
	{
		return {
			"mode", "phase", "blend",
			"roll_est_deg", "pitch_est_deg", "yaw_est_deg",
			"pos_n_est", "pos_e_est", "pos_d_est",
			"vel_n_est", "vel_e_est", "vel_d_est",
			"airspeed_est", "alt_agl_est",
			"motor_r", "motor_l", "servo_r", "servo_l",
			"att_err_deg",
			"vbat_v", "power_w", "energy_wh", "hover_budget_used_s",
			"failsafe_flags",
		};
	}
#endif

	inline const char* ModeName(FlightMode m)
	{
		switch (m)
		{
		case FlightMode::Idle:       return "IDLE";
		case FlightMode::Hover:      return "HOVER";
		case FlightMode::Transition: return "TRANSITION";
		case FlightMode::Cruise:     return "CRUISE";
		case FlightMode::Orbit:      return "ORBIT";
		case FlightMode::Rtl:        return "RTL";
		case FlightMode::Failsafe:   return "FAILSAFE";
		}
		return "?";
	}

	inline const char* PhaseName(TransitionPhase p)
	{
		switch (p)
		{
		case TransitionPhase::None:         return "-";
		case TransitionPhase::Accel:        return "ACCEL";
		case TransitionPhase::Blend:        return "BLEND";
		case TransitionPhase::CruiseLocked: return "CRUISE";
		case TransitionPhase::Decel:        return "DECEL";
		case TransitionPhase::Flare:        return "FLARE";
		}
		return "?";
	}

	inline const char* AlertName(FcAlert a)
	{
		switch (a)
		{
		case FcAlert::None:            return "none";
		case FcAlert::HoverBudgetWarn: return "HOVER BUDGET 80%";
		case FcAlert::HoverBudgetHit:  return "HOVER BUDGET EXCEEDED";
		case FcAlert::BatteryLow:      return "BATTERY LOW";
		case FcAlert::BatteryCritical: return "BATTERY CRITICAL";
		case FcAlert::BatteryReserve:  return "BATTERY RESERVE -> RTL";
		case FcAlert::LinkLost:        return "LINK LOST -> RTL";
		case FcAlert::GeofenceAlt:     return "GEOFENCE 400ft";
		case FcAlert::GeofenceRadius:  return "GEOFENCE RADIUS";
		case FcAlert::EnvelopeAlpha:   return "ENVELOPE (ALPHA)";
		case FcAlert::GpsLost:         return "GPS LOST";
		case FcAlert::ModeChange:      return "mode change";
		}
		return "?";
	}
}
