// test_frustum.cpp — Frustum extraction + culling tests (doc 10 F5).
// Headless (no GL): pure math on a perspective view-projection.

#include "doctest.h"
#include "math/Frustum.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

TEST_SUITE("Frustum (F5 culling)")
{
	// Camera at (0,0,5) looking down -Z at the origin; 60° vertical FOV, square
	// aspect, near 0.1, far 100. Visible z runs from ~4.9 (near) to -95 (far).
	static Cosmic::Frustum MakeFrustum()
	{
		const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
		const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
		                                   glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		return Cosmic::Frustum::FromViewProjection(proj * view);
	}

	static bool AabbAt(const Cosmic::Frustum& f, glm::vec3 center, float half)
	{
		return f.IntersectsAABB(center - glm::vec3(half), center + glm::vec3(half));
	}

	TEST_CASE("unit cube at the origin is inside")
	{
		const Cosmic::Frustum f = MakeFrustum();
		CHECK(AabbAt(f, { 0.0f, 0.0f, 0.0f }, 0.5f));
		// A point at the origin (sphere radius 0) is inside too — validates the
		// plane sign convention (all dot(plane, origin) = plane.w must be >= 0).
		CHECK(f.IntersectsSphere({ 0.0f, 0.0f, 0.0f }, 0.0f));
	}

	TEST_CASE("cubes outside each face are culled")
	{
		const Cosmic::Frustum f = MakeFrustum();
		CHECK_FALSE(AabbAt(f, {   0.0f,   0.0f,  50.0f }, 0.5f));   // behind the camera (near)
		CHECK_FALSE(AabbAt(f, {   0.0f,   0.0f,-500.0f }, 0.5f));   // beyond the far plane
		CHECK_FALSE(AabbAt(f, {-500.0f,   0.0f,   0.0f }, 0.5f));   // far left
		CHECK_FALSE(AabbAt(f, { 500.0f,   0.0f,   0.0f }, 0.5f));   // far right
		CHECK_FALSE(AabbAt(f, {   0.0f, 500.0f,   0.0f }, 0.5f));   // far above
		CHECK_FALSE(AabbAt(f, {   0.0f,-500.0f,   0.0f }, 0.5f));   // far below
	}

	TEST_CASE("a box enclosing the whole frustum intersects")
	{
		const Cosmic::Frustum f = MakeFrustum();
		CHECK(f.IntersectsAABB(glm::vec3(-1000.0f), glm::vec3(1000.0f)));
	}

	TEST_CASE("a box straddling the near plane intersects")
	{
		const Cosmic::Frustum f = MakeFrustum();
		// Centered just in front of the near plane, spanning across it.
		CHECK(AabbAt(f, { 0.0f, 0.0f, 4.0f }, 1.5f));
	}

	TEST_CASE("sphere variants: inside, outside, straddling")
	{
		const Cosmic::Frustum f = MakeFrustum();

		CHECK(f.IntersectsSphere({ 0.0f, 0.0f, -50.0f }, 1.0f));    // well inside
		CHECK(f.IntersectsSphere({ 0.0f, 0.0f,   0.0f }, 0.5f));    // at origin

		CHECK_FALSE(f.IntersectsSphere({ 0.0f, 0.0f,  50.0f }, 1.0f));    // behind camera
		CHECK_FALSE(f.IntersectsSphere({ 0.0f, 0.0f,-500.0f }, 100.0f));  // beyond far by a margin

		// Big sphere centered far past the far plane but reaching back into it.
		CHECK(f.IntersectsSphere({ 0.0f, 0.0f, -500.0f }, 450.0f));

		// A point just outside the right edge becomes an intersection once the
		// sphere radius reaches back across the plane.
		const glm::vec3 rightOut(4.0f, 0.0f, 0.0f);   // z=0 half-width ~2.9, so x=4 is outside
		CHECK_FALSE(f.IntersectsSphere(rightOut, 0.1f));
		CHECK(f.IntersectsSphere(rightOut, 2.0f));
	}

	TEST_CASE("extracted planes carry unit normals")
	{
		const Cosmic::Frustum f = MakeFrustum();
		for (const glm::vec4& p : f.Planes)
		{
			const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
			CHECK(len == doctest::Approx(1.0f).epsilon(1e-4));
		}
	}
}
