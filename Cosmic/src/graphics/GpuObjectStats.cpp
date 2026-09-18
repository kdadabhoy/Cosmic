// graphics/GpuObjectStats.cpp — see header.

#include "graphics/GpuObjectStats.h"

#include <atomic>

namespace Cosmic
{
	namespace
	{
		std::atomic<uint32_t> s_Counts[6] = {};

		std::atomic<uint32_t>& Slot(GpuObjectStats::Kind kind)
		{
			return s_Counts[static_cast<int>(kind)];
		}
	}

	GpuObjectCounts GpuObjectStats::Live()
	{
		GpuObjectCounts c;
		c.Framebuffers           = Slot(Kind::Framebuffer).load(std::memory_order_relaxed);
		c.FramebufferAttachments = Slot(Kind::FramebufferAttachment).load(std::memory_order_relaxed);
		c.Textures               = Slot(Kind::Texture).load(std::memory_order_relaxed);
		c.Buffers                = Slot(Kind::Buffer).load(std::memory_order_relaxed);
		c.VertexArrays           = Slot(Kind::VertexArray).load(std::memory_order_relaxed);
		c.Shaders                = Slot(Kind::Shader).load(std::memory_order_relaxed);
		return c;
	}

	void GpuObjectStats::Created(Kind kind, uint32_t n)
	{
		Slot(kind).fetch_add(n, std::memory_order_relaxed);
	}

	void GpuObjectStats::Destroyed(Kind kind, uint32_t n)
	{
		Slot(kind).fetch_sub(n, std::memory_order_relaxed);
	}
}
