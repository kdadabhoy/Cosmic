#pragma once

// ComposableDynamics.h
//
// 6DOF rigid-body dynamics with composable forces (plan §2.1 P1b — the
// fallback, kept ready and shipped now for the P1 drop test). Translation is
// integrated with the engine's E11 RK4 through a FixedSubstepper; attitude via
// Spatial.h quaternion body-rate integration.
//
// At P1 the only forces are GRAVITY + GROUND CONTACT (a spring-damper on the
// vehicle's lowest point), which is all a drop test needs. Motor thrust, aero
// panels, and wind are stubbed hooks that P2/P4 fill in — the composition
// points are already here so growth is additive, not a rewrite.

#include "sim/IDynamics.h"

#include <Cosmic.h>   // math/Integrators.h (E11), math/Spatial.h (E3)

namespace Viper
{
	// Airframe + environment parameters, populated from viper.toml (E10).
	struct BodyParams
	{
		float mass_kg      = 1.49f;
		glm::vec3 inertia  = { 0.02f, 0.05f, 0.06f };  // diagonal, kg·m²
		float ground_agl_m = 0.0f;    // ground plane altitude (D = -ground)
		float body_radius  = 0.18f;   // lowest-point offset for contact
		// Ground contact spring-damper (per meter of penetration).
		float ground_k     = 4000.0f; // N/m
		float ground_c     = 350.0f;  // N·s/m
		int   substeps     = 8;
	};

	class ComposableDynamics : public IDynamics
	{
	public:
		explicit ComposableDynamics(const BodyParams& params) : m_Params(params) {}

		void Step(const ActuatorFrame& u, float dt) override;
		const RigidState& GetTruth() const override { return m_State; }
		void SetWind(const WindModel& wind) override { m_Wind = wind; }
		void Reset(const RigidState& initial) override { m_State = initial; m_Substepper.Reset(); }
		const char* Name() const override { return "Composable (6DOF)"; }

		void SetParams(const BodyParams& p) { m_Params = p; }
		const BodyParams& GetParams() const { return m_Params; }

		// Altitude above ground (positive up) — convenience for HUD/telemetry.
		float AltitudeAgl() const { return -m_State.posNed.z - m_Params.ground_agl_m; }

	private:
		// Net specific force (accel) in NED for the current state — the RK4 rhs.
		glm::vec3 AccelNed(const glm::vec3& posNed, const glm::vec3& velNed) const;

		void Integrate(float h);   // one substep of size h

		BodyParams  m_Params;
		RigidState  m_State;
		WindModel   m_Wind;
		Cosmic::FixedSubstepper m_Substepper;
		ActuatorFrame m_LastU;     // reserved for P2 motor forces
	};
}
