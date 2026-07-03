#pragma once

// OpenGLStorageBuffer.h
// Concrete OpenGL implementation of the StorageBuffer interface (S4.7 SSBO).

#include "graphics/StorageBuffer.h"

namespace Cosmic
{
	class OpenGLStorageBuffer : public StorageBuffer
	{
	public:
		OpenGLStorageBuffer(uint32_t size, uint32_t binding);
		virtual ~OpenGLStorageBuffer();

		virtual void SetData(const void* data, uint32_t size, uint32_t offset = 0) override;
		virtual void Bind() override;

	private:
		uint32_t m_RendererID = 0;
		uint32_t m_Binding    = 0;
	};
}
