#include <glad/glad.h>
#include "platform/opengl/OpenGLStorageBuffer.h"
#include "platform/opengl/OpenGLContext.h"

namespace Cosmic
{
	OpenGLStorageBuffer::OpenGLStorageBuffer(uint32_t size, uint32_t binding)
		: m_Binding(binding)
	{
		glGenBuffers(1, &m_RendererID);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
		glBufferData(GL_SHADER_STORAGE_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_RendererID);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	}

	OpenGLStorageBuffer::~OpenGLStorageBuffer()
	{
		if (OpenGLContext::HasCurrentContext())
			glDeleteBuffers(1, &m_RendererID);
	}

	void OpenGLStorageBuffer::SetData(const void* data, uint32_t size, uint32_t offset)
	{
		if (size == 0)
			return;
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_RendererID);
		glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, size, data);
	}

	void OpenGLStorageBuffer::Bind()
	{
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, m_Binding, m_RendererID);
	}
}
