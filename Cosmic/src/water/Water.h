#pragma once

// Water.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Water (Tier 1 lake/river surface)  [S9.1 + S9.2]
 * ============================================================================
 *
 * A Gerstner-displaced water plane with the Tier 1 feature set (doc 05 §8):
 * dual scrolling detail normal maps, depth-fade absorption color from the
 * scene depth, refraction via a scene-color grab with distorted UVs, PLANAR
 * reflection (mirrored re-render with an oblique near-plane clip — no new
 * render-state verbs needed), Fresnel blend, sun glint, and shoreline foam
 * from the depth delta. Tier 2 (FFT ocean) is S9.3 and builds on the S4.7
 * compute path later.
 *
 * The wave set is shared with the CPU queries (water/GerstnerWave.h), so
 * SampleHeight (S9.2) matches the rendered surface — the buoyancy contract.
 *
 * WIRING (multi-pass — the app sequences it; see Engine3DDemo):
 *
 *   // 1) reflection: mirrored world into the water's reflection target
 *   glm::mat4 reflVP; glm::vec3 reflCam;
 *   if (water->BeginReflection(view, proj, camPos, reflVP, reflCam)) {
 *       ... redraw the world (sky + terrain + key meshes) with
 *           Renderer3D::BeginScene(reflVP, reflCam) ...
 *       water->EndReflection();          // restores the previous framebuffer
 *       RenderCommand::SetViewport(...); // caller re-asserts its viewport
 *   }
 *   // 2) opaque scene renders into the HDR target as usual
 *   // 3) water draws last, grabbing the scene color for refraction:
 *   water->Render(camPos, time, viewProj, sceneColorID, sceneDepthID, w, h);
 *
 * Render must be called while the target that OWNS sceneColorID/sceneDepthID
 * is bound (it re-binds that target itself after the refraction grab).
 * Create() is CPU-only (headless-safe); GPU resources are lazy like Terrain.
 * ============================================================================
 */

#include "core/Core.h"
#include "water/GerstnerWave.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Cosmic
{
	class Mesh;
	class Shader;
	class Texture2D;
	class FrameBuffer;

	struct WaterSpecification
	{
		glm::vec2 Center{ 0.0f };            // world XZ center of the plane
		glm::vec2 Extent{ 200.0f, 200.0f };  // world size along X and Z
		float     SurfaceHeight = 0.0f;      // world Y of the calm surface
		uint32_t  GridResolution = 129;      // vertices per side of the displaced grid

		/** Wave set (<= 4 uploaded). Empty -> a plausible default 3-wave swell. */
		std::vector<GerstnerWave> Waves;

		// --- Optics (S9.1) ---
		glm::vec3 ShallowColor{ 0.10f, 0.42f, 0.45f };
		glm::vec3 DeepColor{ 0.02f, 0.12f, 0.20f };
		float     DepthFadeDistance  = 5.0f;    // meters of water to reach DeepColor
		float     FoamDepth          = 0.7f;    // shoreline foam under this depth delta
		float     RefractionStrength = 0.035f;  // screen-space UV distortion
		float     ReflectionStrength = 0.05f;   // reflection UV wobble
		float     DetailTiling       = 0.12f;   // detail normal repeats per meter
		float     DetailSpeed        = 0.03f;   // scroll speed, uv/second
		float     DetailStrength     = 0.30f;   // detail normal contribution
		float     SpecularPower      = 240.0f;  // sun glint tightness

		uint32_t  ReflectionResolution = 512;   // reflection RT size (square)
	};

	class COSMIC_API Water
	{
	public:
		/** @brief CPU-only construction (waves resolved, defaults applied). */
		static Ref<Water> Create(const WaterSpecification& spec);

		~Water();

		// GPU resource owner with lazy init — copying would alias ownership.
		Water(const Water&)            = delete;
		Water& operator=(const Water&) = delete;

		////////////////////////////////
		// Reflection pass (S9.1)
		///////////////////////////////

		/**
		 * @brief Start the planar-reflection pass: computes the camera mirrored
		 * about the water plane with an OBLIQUE near plane clipping at the
		 * surface (Lengyel), then binds + clears the reflection target.
		 * @return false when GPU resources are unavailable (skip the pass).
		 * On true, render the world with Renderer3D::BeginScene(outMirroredViewProj,
		 * outMirroredCamPos), then call EndReflection. Winding flips under the
		 * mirror — harmless while the engine cull default is None (S12 revisits).
		 */
		bool BeginReflection(const glm::mat4& view, const glm::mat4& projection,
		                     const glm::vec3& cameraPos,
		                     glm::mat4& outMirroredViewProj, glm::vec3& outMirroredCamPos);

		/** @brief End the reflection pass; re-binds the framebuffer that was
		 *  bound at BeginReflection. The caller re-asserts its viewport. */
		void EndReflection();

		////////////////////////////////
		// Surface draw (S9.1)
		///////////////////////////////

		/**
		 * @brief Draw the water into the CURRENTLY BOUND target (call after all
		 * opaque geometry, inside a Renderer3D scene). First copies `sceneColorID`
		 * into the refraction target (the shader cannot sample the render target
		 * it writes), then re-binds the caller's target and draws the grid.
		 * @param sceneColorID / sceneDepthID  the bound HDR target's attachments.
		 * @param viewportWidth/Height          the bound target's pixel size.
		 */
		void Render(const glm::vec3& cameraPos, float timeSeconds,
		            const glm::mat4& viewProjection,
		            uint32_t sceneColorID, uint32_t sceneDepthID,
		            uint32_t viewportWidth, uint32_t viewportHeight,
		            int entityID = -1);

		////////////////////////////////
		// Queries (S9.2) — pure CPU
		///////////////////////////////

		/** @brief World-space surface height at (x, z) — the buoyancy query. */
		float SampleHeight(float x, float z, float timeSeconds) const;

		/** @brief Unit surface normal at world (x, z). */
		glm::vec3 SampleNormal(float x, float z, float timeSeconds) const;

		const WaterSpecification&        GetSpecification() const { return m_Spec; }
		const std::vector<GerstnerWave>& GetWaves() const         { return m_Waves; }

	public:
		// Public only so Ref<Water> construction works inside Create().
		Water() = default;

	private:
		bool EnsureGpuResources();
		Ref<Texture2D> MakeDetailNormalMap(uint32_t seed) const;

		WaterSpecification        m_Spec;
		std::vector<GerstnerWave> m_Waves;      // resolved set (defaults applied)

		// --- GPU state (lazy) ---
		bool             m_GpuReady = false;
		Ref<Mesh>        m_Grid;
		Ref<Shader>      m_Shader;              // Water.glsl
		Ref<Shader>      m_CopyShader;          // BlitCopy.glsl (refraction grab)
		Ref<Texture2D>   m_DetailA, m_DetailB;  // procedural scrolling normal maps
		Ref<FrameBuffer> m_ReflectionFbo;       // RGBA16F + depth (HDR reflections)
		Ref<FrameBuffer> m_RefractionFbo;       // RGBA16F scene-color copy

		glm::mat4 m_ReflectionViewProj{ 1.0f }; // set by BeginReflection
		bool      m_HasReflection = false;      // a reflection was captured this frame
		bool      m_InReflection  = false;
		uint32_t  m_PrevFramebuffer = 0;        // restored by EndReflection
	};
}
