#pragma once

// graphics/GpuObjectStats.h — live engine-owned GPU object counts.
//
// WO-08 (2D stability, R05) observation probe: how many GPU objects the ENGINE
// currently owns, by class, counted at the platform layer's create/delete sites.
// It answers "did 200 framebuffer resize/create/destroy cycles leave anything
// behind?" with engine bookkeeping rather than driver name reuse, which is what
// the acceptance catalog asks for ("count engine GPU objects, not driver caches").
//
// Pure telemetry: the hooks never change what is created or deleted, the counts
// are never consulted by engine code, and a backend that skips the GL delete
// because the context is already gone still decrements (the object is gone
// either way). Thread-safe (atomics) so a worker-thread upload cannot tear it.

#include "core/Core.h"

#include <cstdint>

namespace Cosmic
{
	struct GpuObjectCounts
	{
		uint32_t Framebuffers           = 0;   // FrameBuffer objects
		uint32_t FramebufferAttachments = 0;   // colour + depth textures a FrameBuffer allocated for itself
		uint32_t Textures               = 0;   // Texture2D objects holding a live GPU name
		uint32_t Buffers                = 0;   // vertex + index buffers
		uint32_t VertexArrays           = 0;
		uint32_t Shaders                = 0;   // linked programs

		uint32_t Total() const
		{
			return Framebuffers + FramebufferAttachments + Textures + Buffers + VertexArrays + Shaders;
		}
		bool operator==(const GpuObjectCounts& o) const
		{
			return Framebuffers == o.Framebuffers && FramebufferAttachments == o.FramebufferAttachments &&
			       Textures == o.Textures && Buffers == o.Buffers &&
			       VertexArrays == o.VertexArrays && Shaders == o.Shaders;
		}
		bool operator!=(const GpuObjectCounts& o) const { return !(*this == o); }
	};

	class COSMIC_API GpuObjectStats
	{
	public:
		enum class Kind { Framebuffer, FramebufferAttachment, Texture, Buffer, VertexArray, Shader };

		// Snapshot of the live counts.
		static GpuObjectCounts Live();

		// Platform-layer hooks (observation only).
		static void Created(Kind kind, uint32_t n = 1);
		static void Destroyed(Kind kind, uint32_t n = 1);
	};
}
