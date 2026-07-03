#pragma once

// viperfc/Orbit.h
//
// ============================================================================
// Orbit / loiter-on-ROI — the signature mode (doc 04 §2.4.4)
// ============================================================================
//
// Circular path around a ground point with wind-drift compensation: the loop
// controls GROUND course (from GPS velocity), so steady wind is rejected by
// construction — the aircraft crabs automatically. v1 camera pointing is
// "fixed camera + aircraft pointing": banking around the circle holds the ROI
// in frame; the HUD's ROI-in-frame error quantifies how well.
//
// Output is a CruiseControl::Command — orbit IS cruise flight with a course
// law, so it reuses the whole TECS + lateral stack.
// ============================================================================

#include "viperfc/Tecs.h"

namespace viperfc
{
	class OrbitControl
	{
	public:
		// roiNed: the ground point; pos: vehicle position (NED, same origin).
		// clockwise: orbit direction viewed from above.
		CruiseControl::Command Update(const Vec3& roiNed, const Vec3& pos,
		                              float altSpAgl, bool clockwise,
		                              const FcParams& p) const
		{
			const float relN = pos.x - roiNed.x;
			const float relE = pos.y - roiNed.y;
			const float r    = std::sqrt(relN * relN + relE * relE);
			const float bearing = std::atan2(relE, relN);   // ROI -> vehicle bearing

			// Tangent course for the requested direction; radial error steers
			// back onto the circle (clamped so far-out captures spiral in).
			const float side = clockwise ? 1.0f : -1.0f;
			const float radialCorr = Clampf(p.orbit_radial_kp * (r - p.orbit_radius_m),
			                                -Rad(60.0f), Rad(60.0f));

			CruiseControl::Command cmd;
			cmd.courseSp   = WrapPi(bearing + side * (kPi * 0.5f) + side * radialCorr);
			cmd.airspeedSp = p.orbit_airspeed;
			cmd.altSp      = altSpAgl;
			return cmd;
		}

		// HUD metric: angle between the camera boresight and the line of sight
		// to the ROI (rad). v1 fixed camera looks out the BELLY (+Z body) — in a
		// banked orbit that face points inside the circle at the ROI.
		static float RoiInFrameError(const Vec3& roiNed, const Vec3& pos, const Quat& att)
		{
			const Vec3 los = Normalized(roiNed - pos);
			const Vec3 boresight = Rotate(att, { 0, 0, 1 });   // body +Z in world
			const float c = Clampf(Dot(los, boresight), -1.0f, 1.0f);
			return std::acos(c);
		}
	};
}
