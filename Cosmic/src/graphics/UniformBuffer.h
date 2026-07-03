#pragma once

// UniformBuffer.h
// Last Modified: 7/2/2026

/**
 * General Description:
 *
 * UniformBuffer is the engine's abstraction over a GPU uniform buffer object
 * (UBO) — a shared block of constants bound to a GLSL binding index and read by
 * any shader that declares a matching `layout(std140, binding = N)` block.
 * Introduced for lighting v1 (S4.5): binding 0 is reserved engine-wide for the
 * scene lights block.
 *
 * std140 CAVEAT (baked into every UBO struct): never place a bare `vec3` in a
 * std140 block — its padding silently offsets everything after it. Pack as
 * `vec4` and use `.w` for a scalar. See Renderer3D's GpuLightsBlock.
 *
 * Factory pattern (RendererAPI-dispatched, like Shader/Texture2D).
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0)
 *    Pre:  offset + size <= the size the buffer was created with.
 *    Post: Uploads `size` bytes at `offset` into the buffer (glBufferSubData).
 *
 * 2. static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding)
 *    Pre:  size > 0; binding is the GLSL binding index this UBO feeds.
 *    Post: Allocates the buffer and binds it to `binding` (glBindBufferBase).
 */

#include "core/Core.h"
#include <cstdint>

namespace Cosmic
{
	class COSMIC_API UniformBuffer
	{
	public:
		virtual ~UniformBuffer() = default;

		virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;

		static Ref<UniformBuffer> Create(uint32_t size, uint32_t binding);
	};
}
