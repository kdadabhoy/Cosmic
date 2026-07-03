#pragma once

// viperfc/Tecs.h
//
// ============================================================================
// Cruise controller — TECS-lite energy loop + lateral track (doc 04 §2.4.3)
// ============================================================================
//
// Total-energy logic reduced to its useful core for a 1.5 kg wing:
//   throttle  <- total-energy rate   (airspeed error + climb-rate error)
//   pitch     <- energy DISTRIBUTION (trade altitude against airspeed)
//   roll      <- ground-course error (L1-ish proportional track)
// Outputs a full attitude target + throttle for the shared attitude loop, so
// cruise flies through exactly the same inner loop and mixer as hover.
// ============================================================================

#include "viperfc/Params.h"

namespace viperfc
{
	class CruiseControl
	{
	public:
		struct Command
		{
			float airspeedSp = 20.0f;  // m/s
			float altSp      = 50.0f;  // m AGL
			float courseSp   = 0.0f;   // rad, ground course
		};

		struct Output
		{
			Quat  attSp{};
			float throttle = 0.0f;
			float pitchSp  = 0.0f;     // rad (telemetry)
			float rollSp   = 0.0f;     // rad (telemetry)
		};

		Output Update(const Command& cmd, float airspeed, float altAgl, float climbRate,
		              const Vec3& velNed, const FcParams& p) const
		{
			Output out;

			const float vErr    = cmd.airspeedSp - airspeed;
			const float altErr  = cmd.altSp - altAgl;
			const float climbSp = Clampf(altErr * 0.4f, -p.cruise_climb_max, p.cruise_climb_max);

			// Throttle: total energy (speed + climb demand).
			out.throttle = Clampf(
				p.cruise_thr_trim + p.tecs_kv * vErr + p.tecs_kh * (climbSp - climbRate),
				0.0f, 1.0f);

			// Pitch: energy balance — climb for altitude, nose down to regain speed.
			out.pitchSp = Clampf(
				p.tecs_pitch_kh * Clampf(altErr * 0.1f, -1.0f, 1.0f) - p.tecs_pitch_kv * Clampf(vErr * 0.1f, -1.0f, 1.0f),
				-p.tecs_pitch_limit, p.tecs_pitch_limit);

			// Lateral: proportional course-error -> bank.
			const float course = std::atan2(velNed.y, velNed.x);
			out.rollSp = Clampf(p.lat_track_kp * WrapPi(cmd.courseSp - course),
			                    -p.roll_max_rad, p.roll_max_rad);

			// Yaw follows the course (crab handled by the track loop itself).
			out.attSp = FromEulerZYX(out.rollSp, out.pitchSp, cmd.courseSp);
			return out;
		}
	};
}
