// Phase 11 / F1 — FlyCameraController movement math. Pure vector math, headless
// (no GL context, no window, no Input backend): the controller factors its motion
// into static helpers (DirectionFromYawPitch / ComputeWishVelocity /
// IntegrateMotion / ClampAboveGround) plus the SetPose pitch clamp, all testable
// without a live Application.
//
// Absolute tolerances throughout — doctest::Approx.epsilon is RELATIVE and would
// misbehave near zero components (Phase 10 lesson).

#include <doctest.h>

#include "camera/FlyCameraController.h"

#include <glm/glm.hpp>
#include <cmath>

using Cosmic::FlyCameraController;

namespace
{
	bool Vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
	{
		return std::abs(a.x - b.x) <= eps
		    && std::abs(a.y - b.y) <= eps
		    && std::abs(a.z - b.z) <= eps;
	}
}

// ---------------------------------------------------------------------------
// DirectionFromYawPitch — look vector conventions
// ---------------------------------------------------------------------------

TEST_CASE("DirectionFromYawPitch: yaw 0 / pitch 0 looks down -Z")
{
	CHECK(Vec3Near(FlyCameraController::DirectionFromYawPitch(0.0f, 0.0f), { 0.0f, 0.0f, -1.0f }));
}

TEST_CASE("DirectionFromYawPitch: yaw turns toward +X, pitch tilts toward +Y")
{
	// Yaw +90° looks down +X (right of the -Z home direction).
	CHECK(Vec3Near(FlyCameraController::DirectionFromYawPitch(90.0f, 0.0f), { 1.0f, 0.0f, 0.0f }));
	// Positive pitch looks up (+Y); the result stays unit length.
	const glm::vec3 up45 = FlyCameraController::DirectionFromYawPitch(0.0f, 45.0f);
	CHECK(up45.y == doctest::Approx(std::sin(glm::radians(45.0f))).epsilon(0.001));
	CHECK(up45.z < 0.0f);                                   // still facing forward (-Z)
	CHECK(glm::length(up45) == doctest::Approx(1.0f).epsilon(0.001));
}

// ---------------------------------------------------------------------------
// SetPose — pitch clamp
// ---------------------------------------------------------------------------

TEST_CASE("SetPose: pitch clamps shy of the poles at +/-89 degrees")
{
	FlyCameraController fly(16.0f / 9.0f);

	fly.SetPose({ 1.0f, 2.0f, 3.0f }, 30.0f, 200.0f);   // way past the pole
	CHECK(fly.GetPitch() == doctest::Approx(89.0f));
	CHECK(fly.GetYaw()   == doctest::Approx(30.0f));
	CHECK(Vec3Near(fly.GetPosition(), { 1.0f, 2.0f, 3.0f }));

	fly.SetPose({ 0.0f, 0.0f, 0.0f }, 0.0f, -137.0f);
	CHECK(fly.GetPitch() == doctest::Approx(-89.0f));
}

// ---------------------------------------------------------------------------
// ComputeWishVelocity — direction assembly + normalisation
// ---------------------------------------------------------------------------

TEST_CASE("ComputeWishVelocity: single key follows its axis, scaled by speed")
{
	const glm::vec3 fwd{ 0.0f, 0.0f, -1.0f };
	const glm::vec3 right{ 1.0f, 0.0f, 0.0f };

	// Forward only.
	CHECK(Vec3Near(FlyCameraController::ComputeWishVelocity(true, false, false, false, false, false,
	                                                        fwd, right, 10.0f),
	               { 0.0f, 0.0f, -10.0f }));
	// Right strafe only.
	CHECK(Vec3Near(FlyCameraController::ComputeWishVelocity(false, false, false, true, false, false,
	                                                        fwd, right, 4.0f),
	               { 4.0f, 0.0f, 0.0f }));
	// Ascend only (world +Y regardless of look).
	CHECK(Vec3Near(FlyCameraController::ComputeWishVelocity(false, false, false, false, true, false,
	                                                        fwd, right, 3.0f),
	               { 0.0f, 3.0f, 0.0f }));
}

TEST_CASE("ComputeWishVelocity: opposing keys cancel to zero; combos stay speed-normalised")
{
	const glm::vec3 fwd{ 0.0f, 0.0f, -1.0f };
	const glm::vec3 right{ 1.0f, 0.0f, 0.0f };

	// Forward + back cancel.
	CHECK(Vec3Near(FlyCameraController::ComputeWishVelocity(true, true, false, false, false, false,
	                                                        fwd, right, 10.0f),
	               { 0.0f, 0.0f, 0.0f }));
	// Nothing pressed -> zero.
	CHECK(Vec3Near(FlyCameraController::ComputeWishVelocity(false, false, false, false, false, false,
	                                                        fwd, right, 10.0f),
	               { 0.0f, 0.0f, 0.0f }));
	// Forward + right is a diagonal but still at |speed|.
	const glm::vec3 diag = FlyCameraController::ComputeWishVelocity(true, false, false, true, false, false,
	                                                                fwd, right, 8.0f);
	CHECK(glm::length(diag) == doctest::Approx(8.0f).epsilon(0.001));
}

// ---------------------------------------------------------------------------
// IntegrateMotion — smoothing + Euler step
// ---------------------------------------------------------------------------

TEST_CASE("IntegrateMotion: zero smoothing snaps velocity to the wish and integrates")
{
	FlyCameraController::Motion start;   // pos 0, vel 0
	const glm::vec3 wish{ 0.0f, 0.0f, -10.0f };

	const auto out = FlyCameraController::IntegrateMotion(start, wish, /*smoothing*/ 0.0f, /*ts*/ 0.1f);
	CHECK(Vec3Near(out.Velocity, wish));                     // raw: velocity == wish
	CHECK(Vec3Near(out.Position, { 0.0f, 0.0f, -1.0f }));    // 10 m/s * 0.1 s
}

TEST_CASE("IntegrateMotion: positive smoothing only partially approaches the wish")
{
	FlyCameraController::Motion start;
	const glm::vec3 wish{ 5.0f, 0.0f, 0.0f };

	const auto out = FlyCameraController::IntegrateMotion(start, wish, /*smoothing*/ 12.0f, /*ts*/ 0.1f);
	const float blend = 1.0f - std::exp(-12.0f * 0.1f);
	CHECK(out.Velocity.x == doctest::Approx(5.0f * blend).epsilon(0.001));
	CHECK(out.Velocity.x > 0.0f);
	CHECK(out.Velocity.x < 5.0f);                            // not there yet
}

// ---------------------------------------------------------------------------
// ClampAboveGround — the ground-probe clamp
// ---------------------------------------------------------------------------

TEST_CASE("ClampAboveGround: raises a sunken position to ground + clearance")
{
	const glm::vec3 sunk{ 3.0f, -5.0f, 7.0f };
	const glm::vec3 clamped = FlyCameraController::ClampAboveGround(sunk, /*groundY*/ 10.0f, /*clearance*/ 1.5f);
	CHECK(clamped.y == doctest::Approx(11.5f));              // 10 + 1.5
	CHECK(clamped.x == doctest::Approx(3.0f));               // x/z untouched
	CHECK(clamped.z == doctest::Approx(7.0f));
}

TEST_CASE("ClampAboveGround: leaves a position already above the clearance alone")
{
	const glm::vec3 high{ 0.0f, 100.0f, 0.0f };
	const glm::vec3 clamped = FlyCameraController::ClampAboveGround(high, /*groundY*/ 10.0f, /*clearance*/ 1.5f);
	CHECK(clamped.y == doctest::Approx(100.0f));
}
