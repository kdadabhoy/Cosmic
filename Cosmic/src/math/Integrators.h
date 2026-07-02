#pragma once

// Integrators.h
// Last Modified 7/1/2026
//
// E11 (docs/plans/03-simulation-engine-plan.md): generic fixed-step ODE
// integrators over user state types. Header-only, no engine dependencies —
// usable from app DLLs, unit tests, and MCU-portable code alike.
//
// State requirements: any type closed under `state + state` and
// `state * scalar` (float). Plain float and glm::vecN work out of the box;
// structs of vec3s work with user-provided operators.
//
// Quaternions: do NOT naive-RK4 quaternion components. Integrate attitude with
// math/Spatial.h::Integrate (dq = ½·q⊗ω) and RK4 the translational state; the
// two compose cleanly at fixed substeps.
//
// Usage:
//     auto deriv = [](const State& s, float t) -> State { ... };
//     state = Cosmic::IntegrateRK4(state, deriv, t, dt);
//
//     Cosmic::FixedSubstepper sub;                    // engine at 60 Hz,
//     sub.Run(dt, 8, [&](float h) { Step(h); });      // physics at 480 Hz

#include <type_traits>

namespace Cosmic
{
	// =========================================================================
	// Classic 4th-order Runge-Kutta.
	//
	// deriv(state, t) must return d(state)/dt. Global error ~O(h^4).
	// =========================================================================
	template<typename State, typename DerivFn>
	inline State IntegrateRK4(const State& state, DerivFn&& deriv, float t, float dt)
	{
		const State k1 = deriv(state, t);
		const State k2 = deriv(state + k1 * (dt * 0.5f), t + dt * 0.5f);
		const State k3 = deriv(state + k2 * (dt * 0.5f), t + dt * 0.5f);
		const State k4 = deriv(state + k3 * dt,          t + dt);

		return state + (k1 + (k2 + k3) * 2.0f + k4) * (dt / 6.0f);
	}

	// =========================================================================
	// Semi-implicit (symplectic) Euler for second-order systems.
	//
	// Updates velocity from acceleration FIRST, then position from the NEW
	// velocity — the stable workhorse for game/sim physics (energy-bounded for
	// oscillatory systems where explicit Euler blows up).
	//
	// accel(pos, vel, t) must return d(vel)/dt.
	// =========================================================================
	template<typename Pos, typename Vel, typename AccelFn>
	inline void IntegrateSemiImplicitEuler(Pos& pos, Vel& vel, AccelFn&& accel, float t, float dt)
	{
		vel = vel + accel(pos, vel, t) * dt;
		pos = pos + vel * dt;
	}

	// =========================================================================
	// FixedSubstepper — the "N substeps inside OnFixedUpdate" pattern as a verb.
	//
	// Divides a frame delta into N equal substeps and invokes fn(h) N times.
	// Keeps a residual so non-divisible deltas do not drift the sim clock:
	// leftover time carries into the next Run call.
	// =========================================================================
	class FixedSubstepper
	{
	public:
		// step(h): advance the simulation by h seconds. Called `substeps` times
		// per Run with h = dt / substeps (plus carried residual folded in).
		template<typename StepFn>
		void Run(float dt, int substeps, StepFn&& step)
		{
			if (substeps < 1)
				substeps = 1;

			m_Residual += dt;
			const float h = dt / static_cast<float>(substeps);
			if (h <= 0.0f)
				return;

			while (m_Residual >= h)
			{
				step(h);
				m_Residual -= h;
			}
		}

		// Drop any accumulated residual (e.g. on sim Reset).
		void Reset() { m_Residual = 0.0f; }

		float GetResidual() const { return m_Residual; }

	private:
		float m_Residual = 0.0f;
	};
}
