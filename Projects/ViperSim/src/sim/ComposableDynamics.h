#pragma once

// ComposableDynamics.h
//
// ============================================================================
// 6DOF rigid-body dynamics with composable forces (plan §2.1 P1b — shipped as
// the primary per the dynamics decision; JSBSim stays a drop-in behind
// IDynamics). P1 shipped gravity + ground contact; P2 added the motor model;
// P4 added the FULL-ENVELOPE aero (α through ±90° — a tailsitter passes α=90°
// in hover) + moments + prop-wash elevon authority + wind gusts.
// ============================================================================
//
// BODY FRAME: FRD — +X out the nose (thrust axis), +Y right wing, +Z belly.
// The tailsitter HOVERS nose-up (body +X = world up, pitch +90°).
//
// Force/moment components (each one term in AccumulateForces):
//   gravity · per-motor first-order-lag thrust (kf·cmd, E13-map-ready) ·
//   full-envelope wing aero (linear polar blended into flat-plate
//   Cl = 2 sinα cosα, Cd = 2 sin²α) · elevon control moments with prop-wash
//   dynamic pressure (T/2A momentum theory — control authority at ZERO
//   airspeed) · rate damping · differential-thrust yaw moment · ground
//   spring-damper on the lowest point · steady wind + E14 gusts.
// ============================================================================

#include "sim/IDynamics.h"
#include "sim/Wind.h"

#include <Cosmic.h>   // math/Integrators.h (E11), math/Spatial.h (E3)

#include <array>

namespace Viper
{
	// Airframe + environment parameters, populated from viper.toml (E10).
	struct BodyParams
	{
		float mass_kg      = 1.49f;
		glm::vec3 inertia  = { 0.02f, 0.05f, 0.06f };  // diagonal, kg·m² (body xx/yy/zz)
		float ground_agl_m = 0.0f;    // ground plane altitude (D = -ground)
		float body_radius  = 0.18f;   // lowest-point offset for contact
		float ground_k     = 4000.0f; // N/m
		float ground_c     = 350.0f;  // N·s/m
		int   substeps     = 8;       // physics substeps per engine fixed tick

		// --- motors (2x, at ±motor_arm_y on the wing, thrust along +X body) ---
		int   motor_count        = 2;
		float motor_max_thrust_n = 14.61f;  // per motor (1490 gf)
		float motor_tau_s        = 0.05f;   // first-order lag
		float motor_arm_y        = 0.35f;   // lateral offset (m)
		float disc_area_m2       = 0.092f;  // TOTAL disc area (prop wash)

		// --- aero (full envelope, whole-wing model) ---------------------------
		float wing_area    = 0.30f;
		float aspect_ratio = 6.0f;
		float oswald_e     = 0.8f;
		float cd0          = 0.035f;
		float cl_alpha     = 4.7f;          // per rad (2πAR/(AR+2))
		float stall_alpha  = 0.21f;         // rad (~12°) — linear->flat-plate blend center
		float stall_width  = 0.06f;         // rad — blend width
		float cm_alpha     = -0.35f;        // static pitch stability
		float cm_q         = -6.0f;         // pitch damping (nondim)
		float cl_p         = -4.5f;         // roll damping
		float cn_beta      = 0.25f;         // weathervane
		float cn_r         = -0.45f;        // yaw damping
		float cy_beta      = -0.30f;        // sideforce
		float cm_de        = -0.55f;        // pitch moment per rad symmetric elevon
		float cl_de        = 0.16f;         // roll moment per rad differential elevon
		float elevon_max_rad = 0.44f;       // ±25° throw (servo cmd ±1)
		float cg_offset_x  = 0.0f;          // m; + = CG forward of the aero center
		// Hover rate damping from prop wash over the surfaces (dimensional).
		glm::vec3 wash_damping = { 0.020f, 0.030f, 0.012f };  // N·m·s per axis
	};

	class ComposableDynamics : public IDynamics
	{
	public:
		explicit ComposableDynamics(const BodyParams& params) : m_Params(params) {}

		void Step(const ActuatorFrame& u, float dt) override;
		const RigidState& GetTruth() const override { return m_State; }
		void SetWind(const WindModel& wind) override
		{
			m_WindField.steadyNed = wind.steadyNed;
			m_WindField.gustSigma = wind.gustSigma;
		}
		void Reset(const RigidState& initial) override
		{
			m_State = initial;
			m_Substepper.Reset();
			m_MotorThrust = { 0.0f, 0.0f };
			m_SimTime = 0.0f;
			m_LastAccelNed = { 0.0f, 0.0f, 0.0f };
		}
		const char* Name() const override { return "Composable (6DOF full-envelope)"; }

		void SetParams(const BodyParams& p) { m_Params = p; }
		const BodyParams& GetParams() const { return m_Params; }
		WindField& Wind() { return m_WindField; }

		float AltitudeAgl() const { return -m_State.posNed.z - m_Params.ground_agl_m; }

		// Truth extras for sensors / HUD.
		const glm::vec3& LastAccelNed() const { return m_LastAccelNed; }   // a (not incl. gravity removal)
		float TotalThrustN() const { return m_MotorThrust[0] + m_MotorThrust[1]; }
		float SimTime() const { return m_SimTime; }
		glm::vec3 CurrentWindNed() const { return m_WindField.Sample(m_SimTime); }

		// Fault injection (doc 04 §2.4.5): motor-out kills one motor's output.
		void SetMotorOut(int index, bool out)
		{
			if (index >= 0 && index < 2) m_MotorOut[index] = out;
		}
		bool MotorOut(int index) const { return index >= 0 && index < 2 && m_MotorOut[index]; }

	private:
		// Force + moment sum for the CURRENT motor thrusts and given kinematic
		// state. Positions/velocities NED; returns accelNed + body angular accel.
		void AccumulateForces(const glm::vec3& posNed, const glm::vec3& velNed,
		                      const glm::quat& att, const glm::vec3& omegaBody,
		                      glm::vec3& accelNed, glm::vec3& alphaBody) const;

		void Integrate(float h);   // one substep of size h

		BodyParams  m_Params;
		RigidState  m_State;
		WindField   m_WindField;
		Cosmic::FixedSubstepper m_Substepper;
		ActuatorFrame m_LastU;

		std::array<float, 2> m_MotorThrust{ { 0.0f, 0.0f } };   // lagged, Newtons
		std::array<bool, 2>  m_MotorOut{ { false, false } };
		glm::vec3 m_LastAccelNed{ 0.0f };
		float m_SimTime = 0.0f;
	};
}
