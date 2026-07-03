#pragma once

// CoverageCapture.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — CoverageCapture (top-down accumulation mask)  [S11.1 / F8]
 * ============================================================================
 *
 * A GENERIC top-down coverage-accumulation system — snow build-up is its first
 * use, but nothing here is snow-shaped (rust, moss, dust, wetness all fit). It
 * owns:
 *
 *   - a depth-only FBO rendered from a top-down ORTHOGRAPHIC camera over a world
 *     XZ rect (the "capture volume"), producing the scene's TOP-SURFACE height
 *     per column, and
 *   - an RGBA16F ping-pong pair holding the accumulated mask: R = coverage
 *     [0,1], G = the encoded top-surface world-Y (so material shaders can reject
 *     receivers below the covered surface — sheltered floors stay bare).
 *
 * FRAME SHAPE (driven by SceneRenderer from a SceneRenderDesc, or by hand):
 *
 *   coverage.BeginDepthCapture();
 *     coverage.DrawCaster(mesh, transform);           // every occluder
 *     coverage.DrawCasterInstanced(mesh, set, count); // scattered occluders
 *     terrain->RenderDepth(coverage.GetCaptureViewProj(), cameraPos);
 *   coverage.EndDepthCapture();                        // restores the prior FBO
 *   coverage.UpdateCoverage(dt, accumPerSec, meltPerSec);  // advance the mask
 *   ...
 *   Renderer3D::SnowDesc snow; ...; coverage.FillSnowDesc(snow);  // rect + decode
 *   Renderer3D::SetSnow(snow);                          // material draws sample it
 *
 * The depth capture reuses the ShadowDepth / ShadowDepthInstanced shaders with a
 * top-down ortho matrix (no new depth shaders); UpdateCoverage runs SnowAccum.glsl
 * as a fullscreen pass. Init()/passes need a live GL context; Shutdown() before
 * context teardown.
 *
 * NOTE (documented deviation): the mask target is RGBA16F (only RG used), because
 * the engine's FramebufferTextureFormat set has no dedicated RG16F — functionally
 * identical (both color-renderable float), one more channel of VRAM.
 * ============================================================================
 */

#include "core/Core.h"
#include "renderer/Renderer3D.h"   // Renderer3D::SnowDesc (FillSnowDesc target)

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
	class FrameBuffer;
	class Shader;
	class Mesh;
	class InstanceSet;

	class COSMIC_API CoverageCapture
	{
	public:
		CoverageCapture() = default;
		~CoverageCapture();

		// Owns GPU resources with an explicit Init/Shutdown lifecycle — copying
		// would alias that ownership, so it's disabled (engine GPU-owner rule).
		CoverageCapture(const CoverageCapture&)            = delete;
		CoverageCapture& operator=(const CoverageCapture&) = delete;

		/**
		 * @brief Allocate the capture volume. `resolution` sizes both the depth
		 * capture and the mask (a power-of-two like 512/1024 reads best). The volume
		 * is the world XZ square [worldMin, worldMin + worldSize] spanning
		 * [worldYMin, worldYMax] vertically (the ortho looks straight down over it).
		 */
		void Init(uint32_t resolution, const glm::vec2& worldMin, float worldSize,
		          float worldYMin, float worldYMax);
		void Shutdown();
		bool IsInitialized() const { return m_Initialized; }

		/** @brief Bind the depth FBO + the ortho viewport and bind ShadowDepth.glsl.
		 *  Draw occluders with DrawCaster* between this and EndDepthCapture. */
		void BeginDepthCapture();
		void DrawCaster(const Ref<Mesh>& mesh, const glm::mat4& transform);
		void DrawCasterInstanced(const Ref<Mesh>& mesh, const Ref<InstanceSet>& instances, uint32_t count);
		/** @brief Restore the framebuffer bound before BeginDepthCapture. The CALLER
		 *  re-asserts its own viewport afterward. */
		void EndDepthCapture();

		/**
		 * @brief Advance the coverage mask by `dt`: a SnowAccum.glsl fullscreen pass
		 * into the write target, reading the read target + the fresh depth capture,
		 * then swap. Coverage rises at `accumPerSec` and melts at `meltPerSec`
		 * (net = accum - melt). Depth test/write are toggled + restored.
		 */
		void UpdateCoverage(float dt, float accumPerSec, float meltPerSec);

		/** @brief The current (latest-written) mask texture — RG = coverage + encoded Y. */
		uint32_t GetMaskTextureID() const;
		/** @brief The top-down ortho view-projection (feed terrain->RenderDepth). */
		const glm::mat4& GetCaptureViewProj() const { return m_CaptureVP; }

		/** @brief Fill the mask-related fields of a SnowDesc (texture id, world rect,
		 *  Y-decode). The caller sets the look fields (Amount/Line/Color/…). */
		void FillSnowDesc(Renderer3D::SnowDesc& snow) const;

	private:
		void RebuildCaptureMatrix();

		Ref<FrameBuffer> m_DepthFbo;             // depth-only top-down capture
		Ref<FrameBuffer> m_MaskA;                // RGBA16F (latest mask; material samples this)
		Ref<FrameBuffer> m_MaskB;                // RGBA16F (write target this update)
		Ref<Shader>      m_DepthShader;          // ShadowDepth.glsl
		Ref<Shader>      m_DepthInstancedShader; // ShadowDepthInstanced.glsl (lazy)
		Ref<Shader>      m_AccumShader;          // SnowAccum.glsl

		glm::mat4 m_CaptureVP{ 1.0f };           // ortho * view (top-down)
		glm::vec2 m_WorldMin{ 0.0f };
		float     m_WorldSize = 1.0f;
		float     m_WorldYMin = 0.0f;
		float     m_WorldYMax = 1.0f;
		uint32_t  m_Resolution = 0;

		bool     m_FirstFrame   = true;          // bootstrap: ignore the ping-pong read
		bool     m_Initialized  = false;
		bool     m_InCapture    = false;
		uint32_t m_PrevFbo      = 0;             // saved during BeginDepthCapture
	};
}
