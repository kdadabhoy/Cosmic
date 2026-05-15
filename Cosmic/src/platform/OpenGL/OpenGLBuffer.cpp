#include <glad/glad.h>
#include "platform/opengl/OpenGLBuffer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLVertexBuffer Constructor (Dynamic)
	 * * Creates an empty vertex buffer on the GPU with a fixed size.
	 * We use GL_DYNAMIC_DRAW because this buffer is intended to be updated
	 * frequently (every frame) by the Renderer2D batching system.
	 */
	OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size)
	{
		glGenBuffers(1, &m_RendererID);
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLVertexBuffer Constructor (Static)
	 * * Creates and populates a vertex buffer with existing data.
	 * We use GL_STATIC_DRAW as a hint to the driver that this data will not
	 * change often, allowing the GPU to optimize its storage location.
	 */
	OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size)
	{
		glGenBuffers(1, &m_RendererID);
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLVertexBuffer Destructor
	 * * Frees the GPU memory associated with this buffer ID.
	 */
	OpenGLVertexBuffer::~OpenGLVertexBuffer()
	{
		glDeleteBuffers(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Sets this buffer as the active GL_ARRAY_BUFFER in the OpenGL state machine.
	 */
	void OpenGLVertexBuffer::Bind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Unbind
	 * * Resets the current GL_ARRAY_BUFFER binding to 0.
	 */
	void OpenGLVertexBuffer::Unbind() const
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SetData
	 * * Updates a sub-region of the buffer's memory. This is the "streaming"
	 * mechanism used by the Renderer2D to push new vertex batches into
	 * pre-allocated GPU memory without reallocating the entire buffer.
	 */
	void OpenGLVertexBuffer::SetData(const void* data, uint32_t size)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_RendererID);
		glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLIndexBuffer Constructor
	 * * Allocates an Index Buffer (Element Array Buffer).
	 * This buffer stores unsigned integers that point to vertices in the
	 * Vertex Buffer, allowing the GPU to reuse vertex data for multiple triangles.
	 */
	OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
		: m_Count(count)
	{
		glGenBuffers(1, &m_RendererID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OpenGLIndexBuffer Destructor
	 * * Frees the GPU memory associated with this index buffer ID.
	 */
	OpenGLIndexBuffer::~OpenGLIndexBuffer()
	{
		glDeleteBuffers(1, &m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Bind
	 * * Sets this buffer as the active GL_ELEMENT_ARRAY_BUFFER.
	 */
	void OpenGLIndexBuffer::Bind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Unbind
	 * * Resets the current GL_ELEMENT_ARRAY_BUFFER binding to 0.
	 */
	void OpenGLIndexBuffer::Unbind() const
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	/////////////////////////////////////////////////////////////////////////////////
}