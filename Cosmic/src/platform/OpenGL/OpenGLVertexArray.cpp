#include <glad/glad.h>
#include "platform/opengl/OpenGLVertexArray.h"
#include "platform/opengl/OpenGLContext.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ShaderDataTypeToOpenGLBaseType
	 *
	 * INTERNAL HELPER: Maps the engine's abstract ShaderDataType to the
	 * corresponding OpenGL fundamental type (GL_FLOAT, GL_INT, etc.).
	 */
	static GLenum ShaderDataTypeToOpenGLBaseType(ShaderDataType type)
	{
		switch (type)
		{
		case ShaderDataType::Float:    return GL_FLOAT;
		case ShaderDataType::Float2:   return GL_FLOAT;
		case ShaderDataType::Float3:   return GL_FLOAT;
		case ShaderDataType::Float4:   return GL_FLOAT;
		case ShaderDataType::Mat3:     return GL_FLOAT;
		case ShaderDataType::Mat4:     return GL_FLOAT;
		case ShaderDataType::Int:      return GL_INT;
		case ShaderDataType::Int2:     return GL_INT;
		case ShaderDataType::Int3:     return GL_INT;
		case ShaderDataType::Int4:     return GL_INT;
		case ShaderDataType::Bool:     return GL_BOOL;
		}

		return 0;
	}

	/////////////////////////////////////////////////////////////////////////////////

	OpenGLVertexArray::OpenGLVertexArray()
	{
		glGenVertexArrays(1, &m_RendererID);
	}

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		// Skip the GL delete if the context is already gone (abort/teardown order) —
		// see OpenGLContext::HasCurrentContext(). The driver reclaims the VAO with the
		// context, so this leaks nothing.
		if (OpenGLContext::HasCurrentContext())
			glDeleteVertexArrays(1, &m_RendererID);
	}

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * AddVertexBuffer
	 *
	 * Robustly loops over buffer elements, dynamically appending attribute locations
	 * sequentially to support multiple VBO layouts, while processing per-instance hardware divisors.
	 */
	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
	{
		// Ensure the VAO is bound before modifying its state
		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();

		const auto& layout = vertexBuffer->GetLayout();

		// DYNAMIC FIX: Calculate the starting layout location index based on 
		// how many attributes have already been registered by previously added VBOs.
		uint32_t currentAttributeIndex = 0;
		for (const auto& existingBuffer : m_VertexBuffers)
		{
			currentAttributeIndex += (uint32_t)existingBuffer->GetLayout().GetElements().size();
		}

		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(currentAttributeIndex);

			glVertexAttribPointer(
				currentAttributeIndex,                        // Contextually safe attribute index
				element.GetComponentCount(),                  // Count (e.g. 3 for Float3)
				ShaderDataTypeToOpenGLBaseType(element.Type), // GL_FLOAT, etc.
				element.Normalized ? GL_TRUE : GL_FALSE,      // Normalized?
				layout.GetStride(),                           // Stride (Total size of vertex)
				(const void*)(uintptr_t)element.Offset        // Offset (Where this data starts)
			);

			// HARDWARE DIVISOR STATE MANAGEMENT FIX:
			// Explicitly configure or clear the instancing divisor flag state bound natively 
			// within this VAO allocation scope to completely isolate batching vs instanced rendering passes.
			if (element.Instanced)
			{
				glVertexAttribDivisor(currentAttributeIndex, 1); // Advances once per instance
			}
			else
			{
				glVertexAttribDivisor(currentAttributeIndex, 0); // Advances once per vertex (Restores core default)
			}

			currentAttributeIndex++;
		}

		m_VertexBuffers.push_back(vertexBuffer);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

	/////////////////////////////////////////////////////////////////////////////////
}