#pragma once

// InstanceSet.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — InstanceSet (per-instance transform pool)  [S12.3-lite / F5]
 * ============================================================================
 *
 * A thin CPU-side packer + std430 SSBO for hardware-instanced mesh drawing.
 * Renderer3D::DrawMeshInstanced and ShadowMap::DrawCasterInstanced read this
 * pool (binding Bindings::InstancesSsbo = 9) by gl_InstanceID from
 * PBRInstanced.glsl / ShadowDepthInstanced.glsl. One draw scatters a whole
 * forest of trees / field of rocks.
 *
 * The GPU struct is `{ mat4 Model; vec4 Tint; }` — 80 bytes, matching the
 * std430 layout in both instanced shaders (see the static_assert in the .cpp).
 * Tint.rgb multiplies albedo for per-instance variation; Tint.a is reserved.
 *
 * UNIFORM-SCALE ASSUMPTION: the instanced shaders derive normals from
 * mat3(Model), so per-instance transforms should be rigid + uniform scale
 * (trees, rocks, debris). A non-uniform instance scale would skew normals —
 * documented limitation (a per-instance inverse-transpose isn't worth 48
 * bytes/instance here).
 *
 * A value-type GPU owner (no Init/Shutdown): the wrapped StorageBuffer releases
 * on destruction. Create() needs a live GL context; SetInstances re-uploads.
 * ============================================================================
 */

#include "core/Core.h"
#include "graphics/StorageBuffer.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
	class COSMIC_API InstanceSet
	{
	public:
		/** @brief Allocate a pool for up to `capacity` instances (binding 9). */
		static Ref<InstanceSet> Create(uint32_t capacity);

		/**
		 * @brief Pack + upload `count` instances (clamped to the capacity).
		 * @param transforms world model matrices (rigid + uniform scale — see header).
		 * @param tints      per-instance rgb albedo tint; null -> opaque white.
		 */
		void SetInstances(const glm::mat4* transforms, const glm::vec4* tints, uint32_t count);

		/** @brief Re-bind the SSBO at Bindings::InstancesSsbo (before an instanced draw). */
		void Bind() const;

		/** @brief Instances uploaded by the last SetInstances (0 initially). */
		uint32_t GetCount()    const { return m_Count; }
		/** @brief Pool capacity (max instances). */
		uint32_t GetCapacity() const { return m_Capacity; }

	public:
		// Public only so Ref<InstanceSet> construction works inside Create();
		// client code should always go through the factory.
		InstanceSet() = default;

	private:
		Ref<StorageBuffer> m_Buffer;
		uint32_t           m_Capacity = 0;
		uint32_t           m_Count    = 0;
	};
}
