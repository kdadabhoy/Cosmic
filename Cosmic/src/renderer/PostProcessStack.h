#pragma once

// PostProcessStack.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — PostProcessStack (S6.1 HDR pipeline foundation)
 * ============================================================================
 *
 * The load-bearing piece of the S6 "visual realism core": the 3D scene renders
 * into a FLOAT (RGBA16F) target so overbright values survive, then a fullscreen
 * pass tonemaps to the LDR target the UI composites on top of (doc 05 contract
 * rule 7 — "the 3D scene renders into a float target and tonemaps to the
 * swapchain; 2D/UI composite after tonemap").
 *
 * S6.1 ships one pass — ACES tonemap + exposure (Tonemap.glsl). The class is
 * the home the rest of S6/S7 plug into: bloom (S6.6) inserts a threshold +
 * blur chain over ping-pong HDR buffers before Composite; SSAO (S6.5) and
 * height fog (S7.2) read GetSceneTarget()'s depth; FXAA (S6.7) is a final
 * fullscreen pass. Every one of those is a fullscreen-triangle pass exactly
 * like Composite already is.
 *
 * WIRING (app-side; mirrors the S3.1 FPV-inset / S4.6 pick-pass rebind pattern):
 *
 *   post.SetViewportSize(w, h);          // once per frame; resizes the HDR target
 *   post.BeginHDR({0.1f,0.1f,0.1f,1});   // bind + clear the HDR scene FBO
 *       Renderer3D::BeginScene(camera);  // ... draw the whole 3D world ...
 *       Renderer3D::EndScene();
 *   appViewportFbo->Bind();              // re-bind the LDR target (workspace FBO)
 *   RenderCommand::SetViewport(0,0,w,h);
 *   post.Composite(exposure);            // tonemap HDR -> the bound LDR target
 *   // ... 2D / UI overlay now draws into the LDR target ...
 *
 * OWNERSHIP: an app owns a PostProcessStack instance (like the FPV inset FBO),
 * proves it in Engine3DDemo, and can later be promoted to an engine-global
 * SceneRenderer when every 3D app wants HDR by default (S12-adjacent). Init()
 * needs a live GL context; call Shutdown() before the context tears down.
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
	class FrameBuffer;
	class Shader;

	class COSMIC_API PostProcessStack
	{
	public:
		PostProcessStack() = default;
		~PostProcessStack();   // out-of-line so forward-declared Refs are fine

		/**
		 * Allocate the HDR scene target + load the tonemap shader. Requires a live
		 * GL context. Idempotent — a second call is a no-op.
		 */
		void Init(uint32_t width, uint32_t height);

		/** Release all GPU resources. Call while the GL context is still current. */
		void Shutdown();

		bool IsInitialized() const { return m_Initialized; }

		/** Resize the HDR scene target to match the viewport. No-op if unchanged. */
		void SetViewportSize(uint32_t width, uint32_t height);

		/**
		 * Bind + clear the HDR scene framebuffer and set the viewport to its size.
		 * The caller then renders the 3D scene into it.
		 */
		void BeginHDR(const glm::vec4& clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

		/** The HDR scene target — {RGBA16F, DEPTH24STENCIL8}. Read for depth-based
		 *  post (SSAO/fog) and to sample its color. Null before Init(). */
		const Ref<FrameBuffer>& GetSceneTarget() const { return m_SceneHDR; }

		/**
		 * Run the post chain (S6.1: ACES tonemap + exposure) as a fullscreen pass
		 * INTO the currently-bound framebuffer. The caller binds + sets the viewport
		 * on the LDR target first. Depth test/write are disabled for the pass and
		 * restored to the engine default (ON/ON) after, so Renderer2D is unaffected.
		 */
		void Composite(float exposure = 1.0f);

		uint32_t GetWidth()  const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		/**
		 * Issue a single fullscreen triangle (3 attribute-less verts; the bound
		 * shader positions them from gl_VertexID). Shared by every fullscreen post
		 * pass. The caller binds the shader + inputs first.
		 */
		static void DrawFullscreenTriangle();

	private:
		Ref<FrameBuffer> m_SceneHDR;        // {RGBA16F, DEPTH24STENCIL8}
		Ref<Shader>      m_TonemapShader;   // assets/shaders/Tonemap.glsl

		uint32_t m_Width  = 0;
		uint32_t m_Height = 0;
		bool     m_Initialized = false;
	};
}
