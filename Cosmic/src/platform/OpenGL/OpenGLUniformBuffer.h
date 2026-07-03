#pragma once

// OpenGLUniformBuffer.h
// Concrete OpenGL implementation of the UniformBuffer interface (S4.5).

#include "graphics/UniformBuffer.h"

namespace Cosmic
{
	class OpenGLUniformBuffer : public UniformBuffer
	{
	public:
		OpenGLUniformBuffer(uint32_t size, uint32_t binding);
		virtual ~OpenGLUniformBuffer();

		virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;

	private:
		uint32_t m_RendererID = 0;
	};
}
