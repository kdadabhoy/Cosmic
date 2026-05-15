#include <glad/glad.h>
#include "platform/opengl/OpenGLVertexArray.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ShaderDataTypeToOpenGLBaseType
	 * * INTERNAL HELPER: Maps the engine's abstract ShaderDataType to the
	 * corresponding OpenGL fundamental type (GL_FLOAT, GL_INT, etc.).
	 * This is critical for the glVertexAttribPointer call to understand
	 * the bit-depth and format of the raw memory.
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

	/**
	 * OpenGLVertexArray Constructor
	 * * Generates a unique Vertex Array Object (VAO) ID on the GPU.
	 */
	OpenGLVertexArray::OpenGLVertexArray()
	{
		glGenVertexArrays(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLVertexArray Destructor
	 * * Deletes the VAO resource from GPU memory to prevent VRAM leaks.
	 */
	OpenGLVertexArray::~OpenGLVertexArray()
	{
		glDeleteVertexArrays(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Activates this VAO in the OpenGL state machine. Subsequent buffer
	 * operations and draw calls will refer to the configuration stored here.
	 */
	void OpenGLVertexArray::Bind() const
	{
		glBindVertexArray(m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Unbind
	 * * Resets the current VAO binding to 0, preventing accidental modification
	 * of this state by other parts of the engine.
	 */
	void OpenGLVertexArray::Unbind() const
	{
		glBindVertexArray(0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * AddVertexBuffer
	 * * The "Logic Hub" of the VertexArray.
	 * * 1. Binds the VAO and the target VertexBuffer.
	 * 2. Iterates through the VertexBuffer's Layout (Metadata).
	 * 3. Calls glVertexAttribPointer for every element (Position, Color, UV, etc.).
	 * This "records" the memory offsets and strides into the VAO, so that
	 * the engine doesn't have to re-specify the layout every frame.
	 */
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
				index,                                        // Attribute index
				element.GetComponentCount(),                  // Count (e.g. 3 for Float3)
				ShaderDataTypeToOpenGLBaseType(element.Type), // GL_FLOAT, etc.
				element.Normalized ? GL_TRUE : GL_FALSE,      // Normalized?
				layout.GetStride(),                           // Stride (Total size of vertex)
				(const void*)(uintptr_t)element.Offset        // Offset (Where this data starts)
			);

			index++;
		}

		m_VertexBuffers.push_back(vertexBuffer);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetIndexBuffer
	 * * Links an IndexBuffer to the VAO.
	 * * Note: OpenGL VAOs store the GL_ELEMENT_ARRAY_BUFFER binding, meaning
	 * that simply binding this VAO in the future automatically binds this
	 * specific IndexBuffer for the next draw call.
	 */
	void OpenGLVertexArray::SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
	{
		glBindVertexArray(m_RendererID);
		indexBuffer->Bind();

		m_IndexBuffer = indexBuffer;
	}

	/////////////////////////////////////////////////////////////////////////////////
}