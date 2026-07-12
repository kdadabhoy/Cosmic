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

#include <string>
#include <vector>

namespace Cosmic
{
	// One resolved GPU timing zone (S12.5 profiler, doc 10 F3). Depth = zone
	// nesting level (0 = top-level) so a HUD can indent nested zones.
	struct GpuZoneResult
	{
		std::string Name;
		float       Milliseconds = 0.0f;
		uint32_t    Depth        = 0;
	};

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

		// S4.7 GPU-compute verbs. Engine enums (no GL tokens leak out); the platform
		// layer translates. GpuBarrier is a bitmask of memory-barrier scopes.
		enum class GpuBarrier : uint32_t
		{
			VertexAttribArray = 1u << 0,
			ShaderStorage     = 1u << 1,
			ShaderImage       = 1u << 2,
			All               = 0xFFFFFFFFu
		};

		enum class PrimitiveTopology { Points, Lines, Triangles };

		// Face-culling modes for SetCullMode. None matches the engine default
		// (culling disabled — 2D sprites may flip winding via FlipX/FlipY).
		enum class CullMode { None = 0, Back, Front };

		// Blend equations for SetBlendMode (S10 — particles/effects). Alpha is the
		// engine default configured at Init (src-alpha over); Additive is the
		// emissive/particle accumulate mode (src-alpha, one); Off disables blending
		// (opaque passes that must not read the destination).
		enum class BlendMode { Alpha = 0, Additive, Off };

		// Triangle rasterization fill for SetPolygonMode (R8 — wireframe view
		// modes). Fill is the engine default from Init(); Line rasterizes triangle
		// edges only (debug/wireframe passes).
		enum class PolygonMode { Fill = 0, Line };

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

		// Face culling. Defaults to None at Init() (2D quads must render with
		// either winding). Same restore contract as the depth verbs: passes that
		// enable Back/Front culling (shadow maps, opaque 3D) must restore None
		// before handing the frame back.
		virtual void SetCullMode(CullMode mode) = 0;

		// Blending. Defaults to Alpha at Init() (Renderer2D depends on it). Same
		// restore contract: passes that switch to Additive/Off (GPU particles,
		// opaque post passes) must restore Alpha before their scope ends.
		virtual void SetBlendMode(BlendMode mode) = 0;

		// Triangle fill mode (R8). Defaults to Fill at Init(). Same restore
		// contract as the other state verbs: a pass that switches to Line
		// (wireframe debug view) must restore Fill before its scope ends —
		// fullscreen post/composite triangles rasterized as lines are a blank
		// frame, not an error the driver reports.
		virtual void SetPolygonMode(PolygonMode mode) = 0;

		////////////////////////////////
		// Submission Commands
		///////////////////////////////

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

		virtual void DrawIndexedInstanced(const Ref<VertexArray>& vertexArray, uint32_t indexCount, uint32_t instanceCount) = 0;

		////////////////////////////////
		// Compute & Attribute-less Draw (S4.7)
		///////////////////////////////

		// Dispatch a compute shader over an x*y*z grid of work groups (the bound
		// program must be a compute program).
		virtual void DispatchCompute(uint32_t x, uint32_t y, uint32_t z) = 0;

		// Insert a GPU memory barrier so compute writes are visible to the given
		// consumers before the next draw/dispatch reads them. Named GpuMemoryBarrier
		// (not MemoryBarrier) because <winnt.h> defines MemoryBarrier as a macro —
		// the plain name would break any TU that includes windows.h.
		virtual void GpuMemoryBarrier(GpuBarrier bits) = 0;

		// Attribute-less array draw (e.g. points read from an SSBO by gl_VertexID).
		// Core GL requires a bound VAO, so the platform layer binds a private empty one.
		virtual void DrawArrays(PrimitiveTopology topology, uint32_t first, uint32_t count) = 0;

		////////////////////////////////
		// Texture Binding (S6 — post-process passes)
		///////////////////////////////

		// Bind a raw 2D texture handle (e.g. a FrameBuffer color/depth attachment
		// rendererID) to a sampler unit. The post-process stack samples FBO
		// attachments that are not wrapped in a Ref<Texture2D>, so this is the
		// §0-rule-1 verb for that — no gl* in feature code. A future backend
		// translates it to a descriptor/image bind.
		virtual void BindTextureSlot(uint32_t slot, uint32_t rendererID) = 0;

		// Same as BindTextureSlot but for a CUBEMAP handle (S6.3 IBL) — binds to
		// the GL_TEXTURE_CUBE_MAP target of the unit. The samplerCube uniform must
		// be set to the same `slot`.
		virtual void BindTextureCubeSlot(uint32_t slot, uint32_t rendererID) = 0;

		// Query / restore the bound draw framebuffer handle. The post stack (S6.7)
		// tonemaps into an intermediate then rebinds the caller's target for the
		// final FXAA pass; a future backend tracks the bound target itself.
		virtual uint32_t GetBoundFramebuffer() const = 0;
		virtual void     BindFramebufferHandle(uint32_t id) = 0;

		////////////////////////////////
		// GPU Timing (S12.5 profiler — doc 10 F3)
		///////////////////////////////

		// Scoped GPU timer zones. Zones may nest; the backend records a timestamp
		// pair per zone (GL_TIMESTAMP queries, which — unlike GL_TIME_ELAPSED — do
		// not have a no-nest restriction). Results are read a few frames late so
		// the GPU is never stalled waiting on a query. GpuFrameMark is called once
		// per frame and both closes the just-recorded frame and resolves the oldest
		// ready one. A None/DirectX backend leaves these as no-ops.
		virtual void BeginGpuZone(const char* name) = 0;
		virtual void EndGpuZone() = 0;
		virtual void GpuFrameMark() = 0;
		virtual const std::vector<GpuZoneResult>& GetGpuZoneResults() const = 0;

		////////////////////////////////
		// Global API Accessor
		///////////////////////////////

		inline static API GetAPI() { return s_API; }

	private:
		static API s_API;
	};

	// Bitmask operators for GpuBarrier (enum class needs explicit ones).
	inline RendererAPI::GpuBarrier operator|(RendererAPI::GpuBarrier a, RendererAPI::GpuBarrier b)
	{
		return static_cast<RendererAPI::GpuBarrier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
	}
	inline uint32_t operator&(RendererAPI::GpuBarrier a, RendererAPI::GpuBarrier b)
	{
		return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
	}
}