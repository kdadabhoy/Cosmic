#pragma once
// IFrameClock.h
//
// ============================================================================
// The time-source seam for Application's frame clock (WO-10, 2D stability campaign)
// ============================================================================
//
// Application::RenderSingleFrame samples ONE wall-clock value per frame and
// derives everything else from it: the raw frame delta, the spiral-of-death
// clamp, the fixed-step accumulator drain (OnFixedUpdate), the scaled variable
// delta (UpdateLayerTime + OnUpdate) and the unscaled uptime (GetAbsoluteTime).
// That arithmetic is the production scheduler, so it is exactly what the clock
// acceptance cases (N01/N02) must drive — but it originally had no headless
// entry point: the only time source was glfwGetTime().
//
// This interface extracts ONLY that one sample. Nothing about scheduling policy
// lives here: the accumulator loop, the clamp, pause, the time scale and the
// layer dispatch stay in Application, untouched, and run identically over the
// default clock and over an injected one. A test injects a scripted clock
// through Application's test-only constructor overload (the same discipline as
// the WO-04 serial transport seam: the fake feeds the input, the real state
// machine runs). Nothing test-only lives in the engine or ships in a package —
// the fake clock lives under tests/.
//
// The default is GlfwFrameClock: glfwGetTime(), the source the engine has
// always used, sampled exactly where it always was.
//
// Spec of record: docs/plans/2d-stability-2026-09-16/evidence/WO-10/report.md
// ============================================================================

#include "core/Core.h"

namespace Cosmic
{
	// The frame time source. Now() is called once by Application::Run to seed the
	// frame clock and once at the top of every RenderSingleFrame; it returns
	// seconds on a monotonic timeline whose origin is the implementation's own
	// (glfwGetTime counts from GLFW init). Application only ever uses the
	// DIFFERENCE between two consecutive samples, so the origin is irrelevant to
	// scheduling — which is precisely what an injected clock varies (N02: origins
	// of 0, 2 h and 24 h with sub-frame deltas).
	class COSMIC_API IFrameClock
	{
	public:
		virtual ~IFrameClock() = default;

		// Current time in seconds. Must be non-decreasing between calls.
		virtual double Now() = 0;
	};

	// The shipping clock: glfwGetTime(), verbatim.
	class COSMIC_API GlfwFrameClock final : public IFrameClock
	{
	public:
		double Now() override;
	};
}
