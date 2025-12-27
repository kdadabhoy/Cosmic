#pragma once
#include "core/Core.h"
#include "renderer/RendererAPI.h"
#include "graphics/VertexArray.h"

// RenderCommand is a static utility class that dispatches commands to the specific RendererAPI implementation
// Note: s_RendererAPI is initialized as soon as the program is started by a global function in the .cpp
//       This global function depends on RendererAPI

namespace Cosmic
{
	class RenderCommand
	{
	public:
		// Initialize the graphics API
		inline static void                  Init()												{ s_RendererAPI->Init(); }														// Initialize the graphics API
		inline static void					SetViewport(uint32_t x, uint32_t y, 
														uint32_t width, uint32_t height)		{ s_RendererAPI->SetViewport(x, y, width, height); }							// Set the screen/window viewport size
		inline static void					Clear(float r, float g, float b)					{ s_RendererAPI->SetClearColor({ r, g, b, 1.0f }); s_RendererAPI->Clear(); }	// Clear the screen with a specific color	
		inline static void					DrawIndexed(const Ref<VertexArray>& vertexArray)	{ s_RendererAPI->DrawIndexed(vertexArray); }									// Execute a draw call


	private:
		static RendererAPI*					s_RendererAPI;	// Pointer that holds the current API (OpenGL/whatever) that is currently active
	};

}