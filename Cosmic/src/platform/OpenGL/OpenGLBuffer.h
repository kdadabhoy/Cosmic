#pragma once

// OpenGLBuffer.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * OpenGLBuffer.h contains the concrete implementations of the VertexBuffer and
 * IndexBuffer interfaces for the OpenGL graphics backend. These classes manage
 * the allocation, deallocation, and data streaming of GPU-side memory.
 * 
 * The OpenGLVertexBuffer supports both static geometry and dynamic batch rendering
 * through the use of GL_STATIC_DRAW and GL_DYNAMIC_DRAW hints. The
 * OpenGLIndexBuffer manages the topology data (indices) required for efficient
 * indexed draw calls.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLVertexBuffer(uint32_t size)
 * Pre:  None.
 * Post: An empty GPU vertex buffer is allocated with the GL_DYNAMIC_DRAW hint.
 * 
 * 2. OpenGLVertexBuffer(float* vertices, uint32_t size)
 * Pre:  'vertices' points to a valid array of geometric data.
 * Post: A GPU vertex buffer is allocated and populated with GL_STATIC_DRAW.
 *
 * 3. void Bind() / void Unbind()
 * Pre:  The buffer has been successfully initialized.
 * Post: The buffer is bound to its respective target (ARRAY or ELEMENT_ARRAY).
 * 
 * 4. void SetData(const void* data, uint32_t size)
 * Pre:  The buffer was created as a dynamic buffer (size-only constructor).
 * Post: GPU memory is updated with the provided data using glBufferSubData.
 * 
 * 5. OpenGLIndexBuffer(uint32_t* indices, uint32_t count)
 * Pre:  'indices' contains valid connectivity data.
 * Post: A GPU index buffer is populated. Count is stored for draw calls.
 */

#include "graphics/Buffer.h"

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	// Vertex Buffer Implementation
	/////////////////////////////////////////////////////////////////////////////////

	class OpenGLVertexBuffer : public VertexBuffer
	{
	public:
		OpenGLVertexBuffer(uint32_t size);
		OpenGLVertexBuffer(float* vertices, uint32_t size);
		virtual ~OpenGLVertexBuffer();


		virtual void	Bind() const override;
		virtual void	Unbind() const override;

		virtual void	SetData(const void* data, uint32_t size) override;
				
		virtual const	BufferLayout& GetLayout() const override				{ return m_Layout; }
		virtual void	SetLayout(const BufferLayout& layout) override			{ m_Layout = layout; }

	private:
		uint32_t			m_RendererID;
		BufferLayout		m_Layout;
	};

	/////////////////////////////////////////////////////////////////////////////////
	// Index Buffer Implementation
	/////////////////////////////////////////////////////////////////////////////////

	class OpenGLIndexBuffer : public IndexBuffer
	{
	public:
		OpenGLIndexBuffer(uint32_t* indices, uint32_t count);
		virtual ~OpenGLIndexBuffer();

		virtual void		Bind() const override;
		virtual void		Unbind() const override;

		virtual uint32_t	GetCount() const override			{ return m_Count; }

	private:
		uint32_t		m_RendererID;
		uint32_t		m_Count;
	};
}