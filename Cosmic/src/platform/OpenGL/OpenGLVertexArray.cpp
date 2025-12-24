#include "platform/opengl/OpenGLVertexArray.h"
#include <glad/glad.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

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

	/////////////////////////////////////////////////////////////////////////////////

	OpenGLVertexArray::~OpenGLVertexArray()
	{
		glDeleteVertexArrays(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void OpenGLVertexArray::AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
	{
		// Ensure the VAO is bound before modifying its state
		glBindVertexArray(m_RendererID);
		vertexBuffer->Bind();

		const auto& layout = vertexBuffer->GetLayout();
		uint32_t index = 0;

		for (const auto& element : layout)
		{
			glEnableVertexAttribArray(index);

			glVertexAttribPointer(
				index,                                    // Attribute index
				element.GetComponentCount(),              // Count (e.g. 3 for Float3)
				ShaderDataTypeToOpenGLBaseType(element.Type), // GL_FLOAT, etc.
				element.Normalized ? GL_TRUE : GL_FALSE,  // Normalized?
				layout.GetStride(),                       // Stride (Total size of vertex)
				(const void*)(uintptr_t)element.Offset    // Offset (Where this data starts)
			);

			index++;
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