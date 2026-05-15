#pragma once

// OpenGLVertexArray.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * OpenGLVertexArray.h provides the concrete implementation of the VertexArray
 * interface for the OpenGL graphics backend. This class acts as a container
 * on the GPU that stores the state and configuration of geometric data.
 * 
 * It encapsulates the relationship between one or more VertexBuffers (raw data)
 * and their associated BufferLayouts (the "meaning" of the data), as well as a
 * single IndexBuffer for defining topology. By binding this object, the entire
 * state of the vertex attribute pointers is restored, enabling efficient
 * multi-draw calls without redundant state specification.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLVertexArray()
 * Pre:  None.
 * Post: A unique Vertex Array Object (VAO) is generated on the GPU.
 * 
 * 2. void Bind() / void Unbind()
 * Pre:  The VAO has been successfully initialized.
 * Post: The VAO is either made active or inactive in the current OpenGL context.
 * 
 * 3. void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
 * Pre:  The provided VertexBuffer must have a valid BufferLayout attached.
 * Post: The buffer is linked to the VAO, and its layout is used to configure
 * glVertexAttribPointer entries. The buffer is added to the internal vector.
 * 
 * 4. void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
 * Pre:  None.
 * Post: The IndexBuffer is bound to the VAO (GL_ELEMENT_ARRAY_BUFFER target),
 * establishing the draw order for indexed rendering.
 */

#include "graphics/VertexArray.h"

namespace Cosmic
{
	class OpenGLVertexArray : public VertexArray
	{
	public:
		////////////////////////////////
		// Life Cycle
		///////////////////////////////

		OpenGLVertexArray();
		virtual ~OpenGLVertexArray();

		////////////////////////////////
		// Pipeline State
		///////////////////////////////

		void									Bind()   const override;
		void									Unbind() const override;

		////////////////////////////////
		// Attribute Linkage
		///////////////////////////////

		void									AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)	override;
		void									SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)		override;

		////////////////////////////////
		// Accessors
		///////////////////////////////

		const std::vector<Ref<VertexBuffer>>&	GetVertexBuffers() const override		{ return m_VertexBuffers; }
		const Ref<IndexBuffer>&					GetIndexBuffer()  const override		{ return m_IndexBuffer; }


	private:
		uint32_t							m_RendererID;
		std::vector<Ref<VertexBuffer>>		m_VertexBuffers;
		Ref<IndexBuffer>					m_IndexBuffer;
	};
}