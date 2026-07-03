// ComposableDynamics.cpp — see header. Full-envelope tailsitter 6DOF.

#include "sim/ComposableDynamics.h"

#include <algorithm>
#include <cmath>

namespace Viper
{
	namespace
	{
		constexpr float kRho = 1.225f;

		// Translation state packed for E11 RK4 (closed under + and scalar *).
		struct PV
		{
			glm::vec3 p{ 0.0f };
			glm::vec3 v{ 0.0f };
			PV operator+(const PV& o) const { return { p + o.p, v + o.v }; }
			PV operator*(float s)     const { return { p * s, v * s }; }
		};

		// Smoothstep blend factor: 0 in the linear regime, 1 fully stalled.
		float StallBlend(float alphaAbs, float center, float width)
		{
			const float t = std::clamp((alphaAbs - (center - width)) / (2.0f * width), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}

	void ComposableDynamics::AccumulateForces(const glm::vec3& posNed, const glm::vec3& velNed,
	                                          const glm::quat& att, const glm::vec3& omegaBody,
	                                          glm::vec3& accelNed, glm::vec3& alphaBody) const
	{
		const BodyParams& P = m_Params;
		const float m = std::max(P.mass_kg, 1e-3f);

		glm::vec3 forceBody{ 0.0f };    // body-frame force sum (rotated at the end)
		glm::vec3 torque{ 0.0f };       // body-frame moments
		glm::vec3 forceNedExtra{ 0.0f };// world-frame terms (gravity, ground)

		// ------------------------------------------------------------------
		// 1. Gravity (world) — NED +D is down.
		// ------------------------------------------------------------------
		forceNedExtra.z += m * Cosmic::Math::GravityMss;

		// ------------------------------------------------------------------
		// 2. Motors: lagged thrusts along +X body; differential -> yaw moment
		//    about body Z (motors at ±motor_arm_y).
		// ------------------------------------------------------------------
		const float tR = m_MotorThrust[0];
		const float tL = m_MotorThrust[1];
		const float thrustTotal = tR + tL;
		forceBody.x += thrustTotal;
		// r x F: right motor (+y arm) with thrust +x gives -z moment.
		torque.z += (tL - tR) * P.motor_arm_y;

		// ------------------------------------------------------------------
		// 3. Aero: full-envelope whole-wing model in the airflow frame.
		// ------------------------------------------------------------------
		const glm::vec3 windNed = m_WindField.Sample(m_SimTime);
		const glm::vec3 velAirNed = velNed - windNed;
		const glm::quat attInv = glm::conjugate(att);
		const glm::vec3 vb = attInv * velAirNed;        // body-frame airflow (u, v, w)
		const float V = glm::length(vb);

		const float b = std::sqrt(P.wing_area * P.aspect_ratio);   // span
		const float c = P.wing_area / std::max(b, 1e-3f);          // mean chord

		// Elevon deflections (rad): servo[0] = right, servo[1] = left.
		const float deR = std::clamp(m_LastU.servo[0], -1.0f, 1.0f) * P.elevon_max_rad;
		const float deL = std::clamp(m_LastU.servo[1], -1.0f, 1.0f) * P.elevon_max_rad;
		const float deSym  = 0.5f * (deR + deL);     // pitch
		const float deDiff = 0.5f * (deR - deL);     // roll

		// Prop-wash dynamic pressure over the elevons (momentum theory ΔP =
		// T / 2A) — THE term that gives control authority at zero airspeed.
		const float qFree = 0.5f * kRho * V * V;
		const float qWash = qFree + std::max(thrustTotal, 0.0f) / (2.0f * std::max(P.disc_area_m2, 1e-3f));

		if (V > 0.25f)
		{
			const float u = vb.x, v = vb.y, w = vb.z;
			const float alpha = std::atan2(w, std::max(u, 0.05f * V));  // guarded at α→±90°
			const float beta  = std::asin(std::clamp(v / V, -1.0f, 1.0f));

			// Full-envelope lift/drag: linear polar blended into flat plate.
			const float sigma = StallBlend(std::fabs(alpha), P.stall_alpha, P.stall_width);
			const float clLin = P.cl_alpha * alpha + P.cl_alpha * deSym * 0.35f;  // elevon lift share
			const float clFp  = 2.0f * std::sin(alpha) * std::cos(alpha);
			const float cl    = (1.0f - sigma) * clLin + sigma * clFp;

			const float cdLin = P.cd0 + cl * cl /
				(3.14159265f * P.oswald_e * std::max(P.aspect_ratio, 0.5f));
			const float cdFp  = P.cd0 + 2.0f * std::sin(alpha) * std::sin(alpha);
			const float cd    = (1.0f - sigma) * cdLin + sigma * cdFp;

			// Wind axes -> body: drag along -v̂_air, lift ⊥ in the symmetry plane.
			const glm::vec3 dragDir = -vb / V;
			const glm::vec3 liftDir{ std::sin(alpha), 0.0f, -std::cos(alpha) };

			const float qS = qFree * P.wing_area;
			glm::vec3 aeroForce = qS * (cl * liftDir + cd * dragDir);
			aeroForce.y += qS * P.cy_beta * beta;
			forceBody += aeroForce;

			// CG offset: aero forces act at the AC, cg_offset_x ahead of it.
			if (std::fabs(P.cg_offset_x) > 1e-6f)
				torque += glm::cross(glm::vec3{ -P.cg_offset_x, 0.0f, 0.0f }, aeroForce);

			// Static + damping moments (nondimensional rates p̂ = p·b/2V etc.).
			const float twoV = 2.0f * V;
			torque.x += qS * b * (P.cl_p * omegaBody.x * b / twoV);
			torque.y += qS * c * (P.cm_alpha * alpha + P.cm_q * omegaBody.y * c / twoV);
			torque.z += qS * b * (P.cn_beta * beta + P.cn_r * omegaBody.z * b / twoV);
		}

		// Elevon control moments use the WASH pressure — alive at V = 0.
		{
			const float qSw = qWash * P.wing_area;
			torque.y += qSw * c * P.cm_de * deSym;
			torque.x += qSw * b * P.cl_de * deDiff;
		}

		// Prop-wash rate damping (keeps hover from ringing; dimensional).
		torque -= P.wash_damping * omegaBody *
			(0.4f + 0.6f * std::clamp(thrustTotal / (0.5f * P.motor_max_thrust_n * 2.0f), 0.0f, 1.0f)) * 30.0f;

		// ------------------------------------------------------------------
		// 4. Ground contact: spring-damper on the lowest point (spherical).
		// ------------------------------------------------------------------
		const float groundD     = -P.ground_agl_m;
		const float lowestD     = posNed.z + P.body_radius;
		const float penetration = lowestD - groundD;
		if (penetration > 0.0f)
		{
			const float velDown = velNed.z;
			float contactUp = P.ground_k * penetration + P.ground_c * std::max(velDown, 0.0f);
			contactUp = std::max(contactUp, 0.0f);
			forceNedExtra.z -= contactUp;

			// Lateral friction bleeds skid; strong angular damping "rests" the
			// airframe on its legs instead of modelling multi-point contact.
			forceNedExtra.x -= 4.0f * m * velNed.x;
			forceNedExtra.y -= 4.0f * m * velNed.y;
			torque -= omegaBody * 0.8f;
		}

		// ------------------------------------------------------------------
		// Sum up: accel in NED, angular accel in body.
		// ------------------------------------------------------------------
		accelNed = (att * forceBody + forceNedExtra) / m;

		const glm::vec3 I = glm::max(m_Params.inertia, glm::vec3(1e-4f));
		const glm::vec3 Iw{ I.x * omegaBody.x, I.y * omegaBody.y, I.z * omegaBody.z };
		const glm::vec3 gyroTorque = glm::cross(omegaBody, Iw);
		alphaBody = (torque - gyroTorque) / I;
	}

	void ComposableDynamics::Integrate(float h)
	{
		// Motor lag: first-order toward commanded thrust (linear cmd->thrust
		// until bench maps arrive as E13 tables — plan §2.1).
		for (int i = 0; i < 2; ++i)
		{
			const float cmd = m_MotorOut[i] ? 0.0f : std::clamp(m_LastU.motor[i], 0.0f, 1.0f);
			const float target = cmd * m_Params.motor_max_thrust_n;
			const float tau = std::max(m_Params.motor_tau_s, 1e-3f);
			m_MotorThrust[i] += (target - m_MotorThrust[i]) * (1.0f - std::exp(-h / tau));
		}

		// Rotational state: semi-implicit at the substep rate (480 Hz).
		glm::vec3 accelNed, alphaBody;
		AccumulateForces(m_State.posNed, m_State.velNed, m_State.attNed, m_State.omegaBody,
		                 accelNed, alphaBody);
		m_State.omegaBody += alphaBody * h;
		m_State.attNed = Cosmic::Math::IntegrateBodyRate(m_State.attNed, m_State.omegaBody, h);

		// Translation: E11 RK4 over (pos, vel) with attitude/rates frozen for
		// the substep (they advance above at the same rate).
		PV s{ m_State.posNed, m_State.velNed };
		auto deriv = [this](const PV& st, float /*t*/) -> PV
		{
			glm::vec3 a, dummy;
			AccumulateForces(st.p, st.v, m_State.attNed, m_State.omegaBody, a, dummy);
			return { st.v, a };
		};
		s = Cosmic::IntegrateRK4(s, deriv, 0.0f, h);

		m_State.posNed = s.p;
		m_State.velNed = s.v;
		m_LastAccelNed = accelNed;
		m_SimTime += h;

		// Derived truth: airspeed + alpha/beta relative to the wind.
		const glm::vec3 airRel = glm::conjugate(m_State.attNed) *
			(m_State.velNed - m_WindField.Sample(m_SimTime));
		m_State.airspeed = glm::length(airRel);
		if (m_State.airspeed > 0.25f)
		{
			m_State.alpha = std::atan2(airRel.z, std::max(airRel.x, 0.05f * m_State.airspeed));
			m_State.beta  = std::asin(std::clamp(airRel.y / m_State.airspeed, -1.0f, 1.0f));
		}
		else
		{
			m_State.alpha = 0.0f;
			m_State.beta  = 0.0f;
		}
	}

	void ComposableDynamics::Step(const ActuatorFrame& u, float dt)
	{
		m_LastU = u;
		const int substeps = std::max(m_Params.substeps, 1);
		m_Substepper.Run(dt, substeps, [this](float h) { Integrate(h); });
	}
}
