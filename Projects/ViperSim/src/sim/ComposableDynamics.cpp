// ComposableDynamics.cpp — see header. Drop-test physics (gravity + ground).

#include "sim/ComposableDynamics.h"

#include <algorithm>
#include <cmath>

namespace Viper
{
	// State packed for E11 RK4: position + velocity in NED. Closed under + and
	// scalar * (the requirement Integrators.h documents).
	namespace
	{
		struct PV
		{
			glm::vec3 p{ 0.0f };
			glm::vec3 v{ 0.0f };
			PV operator+(const PV& o) const { return { p + o.p, v + o.v }; }
			PV operator*(float s)     const { return { p * s, v * s }; }
		};
	}

	glm::vec3 ComposableDynamics::AccelNed(const glm::vec3& posNed, const glm::vec3& velNed) const
	{
		// 1. Gravity — NED +D is down, so gravity is +D.
		glm::vec3 accel{ 0.0f, 0.0f, Cosmic::Math::GravityMss };

		// 2. Ground contact: spring-damper on the vehicle's lowest point. The
		//    ground plane sits at D = -ground_agl_m; the lowest point is
		//    body_radius below the CG. Penetration pushes UP (-D).
		const float groundD    = -m_Params.ground_agl_m;
		const float lowestD    = posNed.z + m_Params.body_radius;   // most-positive D
		const float penetration = lowestD - groundD;                // >0 while intruding

		if (penetration > 0.0f)
		{
			const float velDown = velNed.z;   // +D velocity == moving down
			// Spring pushes up; damper opposes downward motion only (no stick on
			// rebound) — clamp so the ground never pulls the body down.
			float contactUp = m_Params.ground_k * penetration + m_Params.ground_c * std::max(velDown, 0.0f);
			contactUp = std::max(contactUp, 0.0f);
			accel.z -= contactUp / std::max(m_Params.mass_kg, 1e-3f);

			// Lateral friction while in contact — bleeds horizontal skid so the
			// box settles instead of sliding forever.
			const float mu = 4.0f;
			accel.x -= mu * velNed.x;
			accel.y -= mu * velNed.y;
		}

		// 3. (P2) motor thrust, (P4) aero panels + wind compose in here.
		return accel;
	}

	void ComposableDynamics::Integrate(float h)
	{
		PV s{ m_State.posNed, m_State.velNed };

		auto deriv = [this](const PV& st, float /*t*/) -> PV
		{
			return { st.v, AccelNed(st.p, st.v) };
		};

		s = Cosmic::IntegrateRK4(s, deriv, 0.0f, h);

		m_State.posNed = s.p;
		m_State.velNed = s.v;

		// Attitude: no aerodynamic/torque model yet, so a free-falling body
		// holds its orientation. The integration path is wired for P2 (body
		// rates will come from the moment model).
		m_State.attNed = Cosmic::Math::IntegrateBodyRate(m_State.attNed, m_State.omegaBody, h);

		// Derived truth: airspeed relative to steady wind (alpha/beta at P4).
		const glm::vec3 airRel = m_State.velNed - m_Wind.steadyNed;
		m_State.airspeed = glm::length(airRel);
	}

	void ComposableDynamics::Step(const ActuatorFrame& u, float dt)
	{
		m_LastU = u;

		// Fixed-substep the frame delta (engine rate × substeps) via E11.
		const int substeps = std::max(m_Params.substeps, 1);
		m_Substepper.Run(dt, substeps, [this](float h) { Integrate(h); });
	}
}
