// Phase 8 / S5 — CAD navigation + ViewCube math. Pure matrix/geometry, headless
// (no GL context, no window): OrbitCameraController pose math (SnapView / Frame)
// and NavigationCube::PickFaceFromViewProjection face selection.

#include <doctest.h>

#include "camera/OrbitCameraController.h"
#include "camera/NavigationCube.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using Cosmic::OrbitCameraController;
using Cosmic::NavigationCube;
using Cosmic::ViewPreset;

namespace
{
	bool Vec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f)
	{
		return glm::length(a - b) <= eps;
	}
}

// ---------------------------------------------------------------------------
// S5.2 — SnapView orientations
// ---------------------------------------------------------------------------

TEST_CASE("SnapView: Front puts the camera on +Z looking -Z")
{
	OrbitCameraController orbit(16.0f / 9.0f);
	orbit.SetTarget({ 0.0f, 0.0f, 0.0f });
	orbit.SetDistance(10.0f);

	orbit.SnapView(ViewPreset::Front, /*animate*/ false);

	CHECK(orbit.GetYaw()   == doctest::Approx(0.0f));
	CHECK(orbit.GetPitch() == doctest::Approx(0.0f));
	// Camera sits on the +Z side of the target and looks back toward -Z.
	CHECK(Vec3Near(orbit.GetCamera().GetPosition(), { 0.0f, 0.0f, 10.0f }));
	CHECK(Vec3Near(orbit.GetCamera().GetForward(),  { 0.0f, 0.0f, -1.0f }));
}

TEST_CASE("SnapView: Right / Left / Back place the camera on the expected axis")
{
	OrbitCameraController orbit(1.0f);
	orbit.SetTarget({ 0.0f, 0.0f, 0.0f });
	orbit.SetDistance(5.0f);

	orbit.SnapView(ViewPreset::Right, false);
	CHECK(orbit.GetYaw() == doctest::Approx(90.0f));
	CHECK(Vec3Near(orbit.GetCamera().GetPosition(), { 5.0f, 0.0f, 0.0f }));

	orbit.SnapView(ViewPreset::Left, false);
	CHECK(orbit.GetYaw() == doctest::Approx(-90.0f));
	CHECK(Vec3Near(orbit.GetCamera().GetPosition(), { -5.0f, 0.0f, 0.0f }));

	orbit.SnapView(ViewPreset::Back, false);
	CHECK(orbit.GetYaw() == doctest::Approx(180.0f));
	CHECK(Vec3Near(orbit.GetCamera().GetPosition(), { 0.0f, 0.0f, -5.0f }));
}

TEST_CASE("SnapView: Top / Bottom look down / up the Y axis (shy of the poles)")
{
	OrbitCameraController orbit(1.0f);
	orbit.SetTarget({ 0.0f, 0.0f, 0.0f });
	orbit.SetDistance(4.0f);

	orbit.SnapView(ViewPreset::Top, false);
	CHECK(orbit.GetPitch() == doctest::Approx(89.0f));
	CHECK(orbit.GetCamera().GetPosition().y > 3.9f);          // ~4 (sin 89°)
	CHECK(orbit.GetCamera().GetForward().y   < -0.99f);       // looking down

	orbit.SnapView(ViewPreset::Bottom, false);
	CHECK(orbit.GetPitch() == doctest::Approx(-89.0f));
	CHECK(orbit.GetCamera().GetPosition().y < -3.9f);
	CHECK(orbit.GetCamera().GetForward().y   >  0.99f);       // looking up
}

// ---------------------------------------------------------------------------
// S5.2 — Frame-to-fit
// ---------------------------------------------------------------------------

TEST_CASE("FrameSphere: recenters on the sphere and fits ~70% of the view height")
{
	OrbitCameraController orbit(16.0f / 9.0f);
	orbit.SetYawPitch(35.0f, 20.0f);   // framing keeps the current orientation

	const glm::vec3 center{ 3.0f, 1.0f, -2.0f };
	const float     radius = 2.5f;
	orbit.FrameSphere(center, radius, /*animate*/ false);

	CHECK(Vec3Near(orbit.GetTarget(), center));
	CHECK(orbit.GetYaw()   == doctest::Approx(35.0f));   // orientation unchanged
	CHECK(orbit.GetPitch() == doctest::Approx(20.0f));

	// Distance fits the sphere to 70% of the half-height: d = r / (0.7·tan(fovY/2)).
	const float fovY = orbit.GetCamera().GetFovY();
	const float expected = radius / (0.7f * std::tan(glm::radians(fovY * 0.5f)));
	CHECK(orbit.GetDistance() == doctest::Approx(expected).epsilon(0.001));
}

TEST_CASE("FrameBounds: frames the bounding sphere of an AABB")
{
	OrbitCameraController orbit(1.0f);

	const glm::vec3 mn{ -1.0f, -2.0f, -3.0f };
	const glm::vec3 mx{  1.0f,  2.0f,  3.0f };
	orbit.FrameBounds(mn, mx, false);

	// Target = box center; distance matches framing that same sphere.
	CHECK(Vec3Near(orbit.GetTarget(), 0.5f * (mn + mx)));

	const float radius   = 0.5f * glm::length(mx - mn);
	const float fovY     = orbit.GetCamera().GetFovY();
	const float expected = radius / (0.7f * std::tan(glm::radians(fovY * 0.5f)));
	CHECK(orbit.GetDistance() == doctest::Approx(expected).epsilon(0.001));
}

// ---------------------------------------------------------------------------
// H1 — Pose-based orbit: no jump on press, pivot stays screen-fixed
// ---------------------------------------------------------------------------

namespace
{
	bool Mat4Identical(const glm::mat4& a, const glm::mat4& b)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (a[c][r] != b[c][r])   // bit-identical: no tolerance
					return false;
		return true;
	}

	// Project a world point to NDC through a view-projection matrix.
	glm::vec3 ToNdc(const glm::mat4& vp, const glm::vec3& world)
	{
		const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
		return glm::vec3(clip) / clip.w;
	}
}

TEST_CASE("OrbitBy: latching a pivot on press does not move the view")
{
	OrbitCameraController orbit(16.0f / 9.0f);
	orbit.SetTarget({ 0.0f, 0.0f, 0.0f });
	orbit.SetDistance(10.0f);
	orbit.SetYawPitch(45.0f, 30.0f);

	const glm::mat4 before = orbit.GetCamera().GetViewMatrix();

	// A pivot 30°+ off the view axis (near the origin but not the target).
	orbit.BeginOrbitAbout({ 2.0f, 1.0f, 3.0f });

	// No rig state changed: the view matrix is bit-identical to before the press.
	CHECK(Mat4Identical(before, orbit.GetCamera().GetViewMatrix()));
}

TEST_CASE("OrbitBy: the pivot re-projects to the same pixel through a 90° orbit")
{
	OrbitCameraController orbit(16.0f / 9.0f);
	orbit.SetTarget({ 0.0f, 0.0f, 0.0f });
	orbit.SetDistance(10.0f);
	orbit.SetYawPitch(45.0f, 30.0f);

	const glm::vec3 pivot{ 2.0f, 1.0f, 3.0f };
	orbit.BeginOrbitAbout(pivot);

	const glm::vec3 ndcBefore = ToNdc(orbit.GetCamera().GetViewProjectionMatrix(), pivot);

	// A full 90° yaw (single exact step) — the pivot's projected NDC must not move.
	orbit.OrbitBy(90.0f, 0.0f);
	const glm::vec3 ndcAfter = ToNdc(orbit.GetCamera().GetViewProjectionMatrix(), pivot);
	CHECK(Vec3Near(ndcBefore, ndcAfter, 1e-3f));

	// The rig actually rotated (yaw advanced by 90°), and the same holds when a
	// pitch component is mixed in over several smaller steps.
	CHECK(orbit.GetYaw() == doctest::Approx(135.0f));

	for (int i = 0; i < 10; ++i)
		orbit.OrbitBy(-4.0f, 3.0f);   // 40° back-yaw + 30° pitch, accumulated
	const glm::vec3 ndcMixed = ToNdc(orbit.GetCamera().GetViewProjectionMatrix(), pivot);
	CHECK(Vec3Near(ndcBefore, ndcMixed, 1e-3f));
}

TEST_CASE("OrbitBy: a classic orbit about the target leaves the target fixed")
{
	OrbitCameraController orbit(1.0f);
	orbit.SetTarget({ 1.0f, 2.0f, -3.0f });
	orbit.SetDistance(8.0f);
	orbit.SetYawPitch(10.0f, 15.0f);

	orbit.BeginOrbitAbout(orbit.GetTarget());   // classic: pivot == target
	orbit.OrbitBy(37.0f, -22.0f);

	// Distance and target are preserved (classic orbit is a pure re-aim about center).
	CHECK(Vec3Near(orbit.GetTarget(), { 1.0f, 2.0f, -3.0f }));
	CHECK(orbit.GetDistance() == doctest::Approx(8.0f));
}

// ---------------------------------------------------------------------------
// S5.3 — ViewCube face picking (pure ray/AABB math)
// ---------------------------------------------------------------------------

TEST_CASE("NavigationCube: a centered click selects the face nearest the camera")
{
	const glm::mat4 proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
	auto vp = [&](const glm::vec3& eye, const glm::vec3& up)
	{
		return proj * glm::lookAt(eye, glm::vec3(0.0f), up);
	};

	ViewPreset p;
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({ 0, 0,  3 }, { 0, 1, 0 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Front);
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({ 0, 0, -3 }, { 0, 1, 0 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Back);
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({  3, 0, 0 }, { 0, 1, 0 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Right);
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({ -3, 0, 0 }, { 0, 1, 0 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Left);
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({ 0,  3, 0 }, { 0, 0, -1 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Top);
	CHECK(NavigationCube::PickFaceFromViewProjection(vp({ 0, -3, 0 }, { 0, 0,  1 }), 0.5f, 0.5f, p)); CHECK(p == ViewPreset::Bottom);
}

TEST_CASE("NavigationCube: a click off the cube misses")
{
	const glm::mat4 proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
	const glm::mat4 vp   = proj * glm::lookAt(glm::vec3(0, 0, 3), glm::vec3(0.0f), glm::vec3(0, 1, 0));

	// u = 0 maps to world x = -1, well outside the ±0.5 cube — the ray misses.
	ViewPreset p = ViewPreset::Iso;
	CHECK(NavigationCube::PickFaceFromViewProjection(vp, 0.0f, 0.5f, p) == false);
}
