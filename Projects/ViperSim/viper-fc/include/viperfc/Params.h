#pragma once

// viperfc/Params.h
//
// ============================================================================
// viper-fc — every tunable in one aggregate (versioned gains, playbook §7)
// ============================================================================
//
// Defaults are the design-point values from the Viper doc set (Proposal v0.3 /
// Cost & Weight Tracker) — a missing viper.toml still yields a flyable vehicle.
// ViperSim overwrites these from `[fc.*]` tables at load (and live from the
// Tuning screen); the Teensy build compiles the defaults and later reads its
// own stored config. Plain aggregate: no heap, no ctors, memcpy-safe.
// ============================================================================

#include "viperfc/Math.h"

namespace viperfc
{
	struct FcParams
	{
		// --- physical (mirrors viper.toml [airframe]/[motors]/[battery]) -----
		float mass_kg          = 1.49f;
		float wing_area_m2     = 0.30f;
		float max_thrust_N     = 14.61f;   // PER MOTOR (1490 gf) — 2 motors total
		float motor_count      = 2.0f;
		float batt_capacity_wh = 100.0f;
		float batt_cells       = 4.0f;
		float batt_usable_frac = 0.85f;

		// --- loop rates -------------------------------------------------------
		float fc_rate_hz = 240.0f;         // control step rate (sim + Teensy)

		// --- estimator (complementary) ----------------------------------------
		float est_acc_gain  = 1.5f;        // tilt correction (1/s)
		float est_mag_gain  = 0.3f;        // yaw correction (1/s)
		float est_gps_pos_gain = 2.0f;     // position blend (1/s)
		float est_gps_vel_gain = 3.0f;     // velocity blend (1/s)
		float est_baro_gain    = 1.2f;     // altitude blend (1/s)

		// --- attitude loop ------------------------------------------------------
		float att_kp          = 7.0f;      // attitude error (rad) -> rate sp (rad/s)
		float rate_max_rads   = 3.5f;      // rate setpoint clamp
		float rate_kp_x = 0.11f, rate_ki_x = 0.06f, rate_kd_x = 0.0035f;  // roll (body x)
		float rate_kp_y = 0.13f, rate_ki_y = 0.07f, rate_kd_y = 0.0045f;  // pitch (body y)
		float rate_kp_z = 0.16f, rate_ki_z = 0.08f, rate_kd_z = 0.0f;     // yaw  (body z)
		float rate_i_limit    = 0.25f;     // integrator clamp (normalized torque)

		// --- hover position/velocity loop --------------------------------------
		float pos_kp          = 0.95f;     // pos err (m) -> vel sp (m/s)
		float vel_kp          = 2.2f;      // vel err (m/s) -> accel sp (m/s^2)
		float vel_ki          = 0.45f;
		float vel_i_limit     = 2.5f;      // m/s^2
		float vel_max_xy      = 6.0f;      // m/s
		float vel_max_up      = 3.0f;      // m/s climb
		float vel_max_dn      = 1.5f;      // m/s descend
		float tilt_max_rad    = 0.61f;     // ~35 deg thrust-vector tilt — a broadside
		                                   // 5 m/s wind on 0.3 m^2 needs ~32 deg
		float land_speed_ms   = 0.7f;      // vertical-land descent rate

		// --- hover mixer ---------------------------------------------------------
		float mix_yaw_gain    = 0.30f;     // differential-thrust authority (body z)
		float mix_elevon_gain = 1.0f;      // torque cmd -> servo deflection scale
		float motor_idle      = 0.05f;     // keep props spinning while armed

		// --- transition (doc 04 §2.4.2 — THE deliverable) ------------------------
		float trans_v_blend_lo = 8.0f;     // m/s: blend starts (≈ stall)
		float trans_v_blend_hi = 14.0f;    // m/s: blend complete
		float trans_accel_pitch_rate = Rad(35.0f);  // pitch-over schedule, rad/s
		float trans_accel_thr  = 0.85f;    // throttle during ACCEL
		float trans_decel_pitch_rate = Rad(45.0f);  // pitch-up (flare) rate
		float trans_timeout_s  = 12.0f;    // abort → revert if blend never completes

		// --- cruise (TECS-lite + lateral track) ----------------------------------
		float cruise_airspeed  = 20.0f;    // m/s design cruise
		float cruise_thr_trim  = 0.38f;
		float tecs_kv          = 0.045f;   // airspeed err -> throttle
		float tecs_kh          = 0.030f;   // climb-rate err -> throttle
		float tecs_pitch_kv    = Rad(2.2f);// airspeed err -> pitch (energy balance)
		float tecs_pitch_kh    = Rad(3.0f);// alt err -> pitch
		float tecs_pitch_limit = Rad(18.0f);
		float lat_track_kp     = 1.1f;     // course err (rad) -> roll cmd (rad)
		float roll_max_rad     = Rad(45.0f);
		float cruise_climb_max = 3.0f;     // m/s

		// --- orbit (signature mode) ----------------------------------------------
		float orbit_radius_m   = 120.0f;
		float orbit_airspeed   = 20.0f;    // ~45 mph
		float orbit_radial_kp  = 0.012f;   // radial err (m) -> course correction (rad)

		// --- failsafe supervisor (limits mirror viper.toml [limits]) ---------------
		float geofence_agl_m     = 121.9f; // FAA 400 ft
		float geofence_radius_m  = 400.0f; // horizontal fence from home
		float hover_budget_s     = 300.0f; // 3–5 min cumulative cap — enforced
		float hover_warn_frac    = 0.8f;   // warning at 80% budget
		float batt_low_v_cell    = 3.5f;   // warning
		float batt_crit_v_cell   = 3.3f;   // force land
		float batt_v_qualify_s   = 2.0f;   // cellV must stay below threshold this long
		                                   // (IR sag under transient load is not a
		                                   //  low pack — ArduPilot/PX4 do the same)
		float batt_low_wh_frac   = 0.25f;  // reserve fraction -> RTL
		float link_timeout_s     = 1.5f;   // no heartbeat -> RTL
		float envelope_alpha_max = Rad(70.0f);  // cruise-side stall guard
		float rtl_altitude_agl   = 40.0f;  // climb/hold altitude for return leg
		float rtl_capture_m      = 8.0f;   // home-capture radius -> vertical land
	};
}
