#pragma once

// RendererAPI.h
// Last Modified 5/14/2026


// Abstract Class for back-end... has a flag to pick what graphics API
// Every platform will derive a RendererAPI class from this


/**
 * General Description:
 * 
 * RendererAPI.h defines the abstract blueprint for the Cosmic Engine's low-level
 * graphics commands. It serves as the final gateway between the engine's
 * high-level rendering logic and the hardware-specific drivers (OpenGL, DirectX, etc.).
 * 
 * Every rendering backend must derive from this class and implement its virtual
 * functions to ensure that the engine can switch between different graphics
 * APIs seamlessly without changing the core rendering code.
 * 
 * 
 * Architecture Components:
 * 
 * 1. API Enum: A selection flag (None, OpenGL, DirectX) used by the engine's
 * factory methods to instantiate the correct hardware buffers and shaders.
 * 
 * 2. State Control: Directives for setting the Viewport, Clear Color, and
 * executing the actual Clear command on the GPU.
 * 
 * 3. Primitive Submission: The DrawIndexed and DrawLines methods which act
 * as the final "Execute" command sent to the graphics hardware.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. virtual void Init() = 0
 * Pre:  A valid GraphicsContext has been created.
 * Post: Hardware-specific global states (e.g., Alpha Blending, Depth Testing)
 * are enabled and configured.
 * 
 * 2. virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0
 * Pre:  None.
 * Post: The GPU's rasterization area is mapped to the specified window coordinates.
 * 
 * 3. virtual void Clear() = 0
 * Pre:  SetClearColor() should ideally be called beforehand.
 * Post: The current color and depth buffers are wiped clean.
 * 
 * 4. virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0
 * Pre:  The VertexArray is bound and populated with valid data.
 * Post: The GPU renders geometry based on the index buffer contained in the VertexArray.
 * 
 * 5. static API GetAPI()
 * Pre:  None.
 * Post: Returns the globally selected graphics backend for the current session.
 */

#include "core/Core.h"
#include <glm/glm.hpp>
#include "graphics/VertexArray.h"

namespace Cosmic
{
	class RendererAPI
	{
	public:
		/**
		 * API Selection Enum
		 */
		enum class API
		{
			None = 0, OpenGL = 1, DirectX = 2
		};

	public:
		virtual ~RendererAPI() = default;

		////////////////////////////////
		// Hardware Lifecycle & Config
		///////////////////////////////

		virtual void Init() = 0;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

		////////////////////////////////
		// Buffer Operations
		///////////////////////////////

		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		////////////////////////////////
		// Render State Control
		///////////////////////////////

		// Depth testing/writing toggles. Both default ON at Init(). Renderer3D and
		// future passes MUST restore any state they change — never leave these
		// altered across a Begin/End scope (see the Renderer3D state contract).
		virtual void SetDepthTest(bool enabled) = 0;
		virtual void SetDepthWrite(bool enabled) = 0;

		////////////////////////////////
		// Submission Commands
		///////////////////////////////

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

		virtual void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) = 0;

		////////////////////////////////
		// Global API Accessor
		///////////////////////////////

		inline static API GetAPI() { return s_API; }

	private:
		static API s_API;
	};
}