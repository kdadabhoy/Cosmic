#pragma once

// VertexArray.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * VertexArray.h defines the interface for Vertex Array Objects (VAO). The
 * VertexArray acts as a "State Manager" for geometry, encapsulating all the
 * necessary data and instructions required for the GPU to render a specific mesh.
 * * It links VertexBuffers (the data) with their respective BufferLayouts
 * (the metadata) and an IndexBuffer (the topology) into a single, high-level
 * graphics object. This reduces the number of state-change calls during
 * the rendering phase, as binding the VAO automatically restores all
 * associated buffer bindings and vertex attribute pointers.
 * 
 * Architecture Components:
 * 
 * 1. VertexArray (Interface): The abstract blueprint for VAOs. Use 'Create'
 * to instantiate platform-specific implementations (e.g., OpenGLVertexArray).
 * 
 * 2. VertexBuffer Linkage: Manages a collection of data buffers. Multiple buffers
 * can be added to a single VAO to support complex vertex schemas.
 * 
 * 3. IndexBuffer Linkage: Owns a single IndexBuffer to define the drawing order
 * of the vertices.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. virtual void Bind()
 * Pre:  The VertexArray has been successfully created.
 * Post: This VAO becomes the active state in the graphics pipeline; all
 * previously linked attributes are ready for draw calls.
 * 
 * 2. virtual void Unbind()
 * Pre:  None.
 * Post: Clears the current VAO binding from the graphics pipeline.
 * 
 * 3. virtual void AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)
 * Pre:  The provided VertexBuffer must have a valid BufferLayout set.
 * Post: The buffer is bound to the VAO, and its layout is used to configure
 * GPU vertex attribute pointers. The VAO takes a reference to the buffer.
 * 
 * 4. virtual void SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)
 * Pre:  None.
 * Post: Links the IndexBuffer to the VAO. This buffer will be used during
 * indexed draw calls (e.g., Renderer::DrawIndexed).
 * 
 * 5. virtual const std::vector<Ref<VertexBuffer>>& GetVertexBuffers()
 * Pre:  None.
 * Post: Returns a list of all VertexBuffers currently linked to this VAO.
 * 
 * 6. virtual const Ref<IndexBuffer>& GetIndexBuffer()
 * Pre:  None.
 * Post: Returns the IndexBuffer associated with this VAO.
 * 
 * 7. static Ref<VertexArray> Create()
 * Pre:  A RendererAPI has been initialized.
 * Post: Returns a reference-counted, platform-specific VertexArray instance.
 */

#include "core/Core.h" 
#include "graphics/Buffer.h"
#include <vector>

namespace Cosmic
{
	class VertexArray
	{
	public:
		////////////////////////////////
		// Destructor
		///////////////////////////////
		virtual	~VertexArray() = default;

		////////////////////////////////
		// State Management
		///////////////////////////////
		virtual void		Bind()   const		= 0;
		virtual void		Unbind() const		= 0;

		////////////////////////////////
		// Resource Linkage
		///////////////////////////////
		virtual void		AddVertexBuffer(const Ref<VertexBuffer>& vertexBuffer)	= 0;
		virtual void		SetIndexBuffer(const Ref<IndexBuffer>& indexBuffer)		= 0;

		////////////////////////////////
		// Accessors
		///////////////////////////////
		virtual const std::vector<Ref<VertexBuffer>>&		GetVertexBuffers() const	= 0;
		virtual const Ref<IndexBuffer>&						GetIndexBuffer()   const	= 0;

		////////////////////////////////
		// Factory Pattern
		///////////////////////////////
		static Ref<VertexArray> Create();
	};
}