#pragma once

// viperfc/Mixer.h
//
// ============================================================================
// Control allocation for the dual-motor tailsitter (doc 04 §0/§2.4)
// ============================================================================
//
// Body frame FRD, motors on the wing at ±Y thrusting along +X (nose), two
// elevons in the prop wash. The allocation is IDENTICAL in hover and cruise —
// what changes across transition is the OUTER loops feeding it, so the "mixer
// crossfade" is a crossfade of commands, not of allocation:
//
//   torque.x (roll about nose axis) -> differential elevon
//   torque.y (pitch)                -> symmetric elevon
//   torque.z (yaw about belly axis) -> differential motor thrust
//   thrust   [0,1] collective       -> both motors
//
// In HOVER (nose up): body-x roll = heading spin, body-z yaw = tip over —
// exactly the classic twin tailsitter arrangement.
//
// Saturation policy: attitude authority wins over collective — motors are
// re-centered so the differential command survives clipping (mixer-saturation
// unit test in viper-fc/tests).
// ============================================================================

#include "viperfc/IHal.h"
#include "viperfc/Params.h"

namespace viperfc
{
	struct MixerInputs
	{
		Vec3  torque{};      // normalized [-1, 1] per axis
		float thrust = 0.0f; // normalized collective [0, 1]
	};

	inline void MixTailsitter(const MixerInputs& in, const FcParams& p, ActuatorFrame& out)
	{
		const float tz = Clampf(in.torque.z, -1.0f, 1.0f) * p.mix_yaw_gain;
		float collective = Clampf(in.thrust, 0.0f, 1.0f);

		// Differential thrust: +tz needs LEFT motor faster (see header math).
		float mr = collective - tz;
		float ml = collective + tz;

		// Preserve the differential through saturation by shifting the pair.
		const float over  = (mr > 1.0f ? mr - 1.0f : 0.0f) + (ml > 1.0f ? ml - 1.0f : 0.0f);
		const float under = (mr < p.motor_idle ? p.motor_idle - mr : 0.0f)
		                  + (ml < p.motor_idle ? p.motor_idle - ml : 0.0f);
		mr += under - over;
		ml += under - over;

		out.motor[0] = Clampf(mr, p.motor_idle, 1.0f);   // right
		out.motor[1] = Clampf(ml, p.motor_idle, 1.0f);   // left
		out.motor[2] = out.motor[3] = 0.0f;

		// Elevons: symmetric = pitch, differential = roll.
		const float ty = Clampf(in.torque.y, -1.0f, 1.0f) * p.mix_elevon_gain;
		const float tx = Clampf(in.torque.x, -1.0f, 1.0f) * p.mix_elevon_gain;
		out.servo[0] = Clampf(ty + tx, -1.0f, 1.0f);     // right elevon
		out.servo[1] = Clampf(ty - tx, -1.0f, 1.0f);     // left elevon
		out.servo[2] = out.servo[3] = 0.0f;
	}
}
