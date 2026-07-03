#pragma once

// StorageBuffer.h
// Last Modified: 7/2/2026

/**
 * General Description:
 *
 * StorageBuffer wraps a GPU shader storage buffer object (SSBO) — a large,
 * read-write GPU buffer bound to a std430 binding index and accessed by compute
 * and graphics shaders. It is the storage half of the S4.7 GPU-compute path
 * (FFT water S9, GPU particles S10 build on it). Binding indices are allocated
 * in renderer/BindingPoints.h — claim a slot there before creating a new block.
 *
 * Factory pattern (RendererAPI-dispatched, like Shader/UniformBuffer).
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0)
 *    Pre:  offset + size <= the size the buffer was created with.
 *    Post: Uploads `size` bytes at `offset` (glBufferSubData). Pass data=nullptr
 *          size=0 to skip — allocation happens at Create.
 *
 * 2. virtual void Bind()
 *    Post: Re-issues glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, id).
 *
 * 3. static Ref<StorageBuffer> Create(uint32_t size, uint32_t binding)
 *    Pre:  size > 0; binding is the std430 binding index shaders read/write.
 *    Post: Allocates the buffer and binds it to `binding`.
 */

#include "core/Core.h"
#include <cstdint>

namespace Cosmic
{
	class COSMIC_API StorageBuffer
	{
	public:
		virtual ~StorageBuffer() = default;

		virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) = 0;
		virtual void Bind() = 0;

		static Ref<StorageBuffer> Create(uint32_t size, uint32_t binding);
	};
}
