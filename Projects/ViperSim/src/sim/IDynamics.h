#pragma once

// IDynamics.h
//
// ============================================================================
// IDynamics — the app-owned dynamics abstraction (plan §2.1).
// ============================================================================
//
// Everything downstream (SimHal sensor sampling, screens, DataRecorder) sees
// ONLY this interface, so the JSBSim-vs-hand-rolled outcome never ripples into
// the rest of ViperSim. Two implementations plug in behind it:
//
//   * ComposableDynamics — 6DOF rigid body, composable forces, integrated with
//     the engine's E11 RK4/substepper. Shipped now (drives the P1 drop test);
//     the plan's designated fallback, kept ready. Grows into full-envelope
//     aero at P4.
//   * JsbsimDynamics — the timeboxed P1 spike (see docs/DYNAMICS_DECISION.md).
//     Not built in-tree; swapping it in is a factory change, nothing else.
//
// Contract: Step() advances truth by dt using the given actuator command;
// GetTruth() returns the current ground-truth state; SetWind()/Reset() are
// scenario setup.
// ============================================================================

#include "fc_glue/telemetry_schema.h"

#include <glm/glm.hpp>

namespace Viper
{
	// Steady wind + a simple gust amplitude (Dryden-ish turbulence arrives at
	// P3 via the engine's E14 noise; for the drop test wind is unused).
	struct WindModel
	{
		glm::vec3 steadyNed{ 0.0f };  // m/s
		float     gustSigma = 0.0f;   // m/s, 1-sigma turbulence (P3)
	};

	class IDynamics
	{
	public:
		virtual ~IDynamics() = default;

		// Advance ground truth by dt seconds under the given actuator command.
		virtual void Step(const ActuatorFrame& u, float dt) = 0;

		// Current ground-truth rigid-body state.
		virtual const RigidState& GetTruth() const = 0;

		// Scenario setup.
		virtual void SetWind(const WindModel& wind) = 0;
		virtual void Reset(const RigidState& initial) = 0;

		// Human-readable name for the UI ("Composable (6DOF)" / "JSBSim").
		virtual const char* Name() const = 0;
	};
}
