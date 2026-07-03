#pragma once

// Frustum.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — view frustum (S12.1-lite / doc 10 F5)  [pure, header-only]
 * ============================================================================
 *
 * Six world-space clip planes extracted from a view-projection matrix by the
 * Gribb–Hartmann method (planes derived from the matrix rows, normals pointing
 * INWARD). Used for coarse per-object culling before an instanced draw: the app
 * culls once per frame against the MAIN camera frustum (inflate by an object
 * radius to keep near-offscreen shadow casters), packs the survivors into an
 * InstanceSet, and draws that set in every pass (F5 culling policy).
 *
 * A point is inside when dot(plane, vec4(p, 1)) >= 0 for all six planes.
 *
 * Pure math (glm only, no GPU types) — unit-tested headless in
 * tests/test_frustum.cpp. Follows the header-only-math convention of
 * water/GerstnerWave.h (no COSMIC_API on a fully-inline utility).
 * ============================================================================
 */

#include <glm/glm.hpp>

#include <cmath>

namespace Cosmic
{
	struct Frustum
	{
		// Left, Right, Bottom, Top, Near, Far. Each = (a, b, c, d) for the plane
		// a x + b y + c z + d = 0 with (a, b, c) the INWARD-pointing unit normal.
		glm::vec4 Planes[6];

		/**
		 * @brief Extract the six frustum planes from a view-projection matrix.
		 * glm matrices are column-major, so matrix row i is
		 * (m[0][i], m[1][i], m[2][i], m[3][i]). OpenGL clip space (z in [-1, 1]),
		 * so near = row3 + row2 and far = row3 - row2.
		 */
		static Frustum FromViewProjection(const glm::mat4& m)
		{
			const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
			const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
			const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
			const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);

			Frustum f;
			f.Planes[0] = r3 + r0;   // left
			f.Planes[1] = r3 - r0;   // right
			f.Planes[2] = r3 + r1;   // bottom
			f.Planes[3] = r3 - r1;   // top
			f.Planes[4] = r3 + r2;   // near
			f.Planes[5] = r3 - r2;   // far

			// Normalize so IntersectsSphere's signed distance is metric.
			for (glm::vec4& p : f.Planes)
			{
				const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
				if (len > 1e-8f)
					p /= len;
			}
			return f;
		}

		/** @brief True unless the AABB [mn, mx] is fully outside some plane
		 *  (conservative: may report a just-outside box as intersecting). */
		bool IntersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const
		{
			for (const glm::vec4& p : Planes)
			{
				// The AABB corner farthest along the plane's inward normal (the
				// "positive vertex"): if even it is behind the plane, the box is out.
				const glm::vec3 pv(p.x >= 0.0f ? mx.x : mn.x,
				                   p.y >= 0.0f ? mx.y : mn.y,
				                   p.z >= 0.0f ? mx.z : mn.z);
				if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0.0f)
					return false;
			}
			return true;
		}

		/** @brief True unless the sphere is fully behind some plane. */
		bool IntersectsSphere(const glm::vec3& center, float radius) const
		{
			for (const glm::vec4& p : Planes)
				if (p.x * center.x + p.y * center.y + p.z * center.z + p.w < -radius)
					return false;
			return true;
		}
	};
}
