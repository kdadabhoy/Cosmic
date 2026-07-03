#pragma once

// EnvironmentMap.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — EnvironmentMap (image-based lighting)  [S6.3]
 * ============================================================================
 *
 * Owns the full IBL resource set and the bake passes that produce it:
 *
 *   - Environment cube    — a procedural analytic sky (EnvSky.glsl) rendered
 *                           into a mipmapped cubemap. Driven by a sun DIRECTION,
 *                           so S7.1/S7.3 time-of-day just move the sun and rebake.
 *   - Irradiance cube     — cosine-convolved diffuse ambient (IrradianceConvolve).
 *   - Prefilter cube      — GGX-prefiltered specular, one roughness per mip.
 *   - BRDF LUT            — split-sum integration texture (2D, RG in RGBA16F).
 *
 * The sky is the environment source (no committed .hdr needed), so the skybox
 * and the lighting always agree — this is also the S7.1 analytic sky. An equirect
 * .hdr source is a future option; the procedural path ships first.
 *
 * WIRING (app owns one, like PostProcessStack):
 *   env.Init();
 *   env.SetSunDirection(sunToLightVector);   // marks dirty; Bake() rebakes
 *   env.Bake();                              // once, or when the sun moves
 *   ...
 *   env.PushToRenderer();                    // Renderer3D PBR materials get IBL
 *   post.BeginHDR();
 *     env.DrawSkybox(viewProjection);        // background fill, before opaque
 *     Renderer3D::BeginScene(cam); ...; EndScene();
 *
 * Init()/Bake() need a live GL context; Shutdown() before context teardown.
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
	class TextureCube;
	class FrameBuffer;
	class Shader;
	class Mesh;

	/**
	 * @brief Per-frame parameters for the DETAILED per-pixel sky background pass
	 * (SkyDetail.glsl / doc 10 F7). The baked EnvSky cube stays the IBL source
	 * (lighting); this re-evaluates the same sky analytically at full resolution
	 * and adds what a low-res cube cannot hold: a limb-darkened sun disc, a hashed
	 * star field with twinkle, a milky-way band, and a moon whose PHASE falls out
	 * of lighting its visible hemisphere by the real sun. All values default to the
	 * shipped-look day palette (moon/stars contribute only at night, gated by the
	 * shader's own day/night ramp). App policy owns the scenario values.
	 */
	struct SkyDetailDesc
	{
		float     SkyIntensity      = 1.0f;            // overall HDR multiplier
		float     SunDiscIntensity  = 40.0f;           // HDR disc radiance (bloom does the rest)
		float     SunAngularRadius  = 0.00465f;        // radians (~0.53° real sun)
		glm::vec3 MoonDirection{ 0.0f, 1.0f, 0.0f };   // direction TO the moon
		float     MoonIntensity     = 0.0f;            // 0 = no moon disc
		float     MoonAngularRadius = 0.0087f;         // radians (slightly oversized reads better)
		float     StarIntensity     = 1.0f;
		float     StarDensity       = 90.0f;           // candidate stars per cube-face axis
		float     MilkyWayIntensity = 0.35f;
		glm::vec3 MilkyWayDir{ 0.36f, 0.48f, 0.80f };  // normal of the galactic band's great circle
		float     Time              = 0.0f;            // seconds (star twinkle)
	};

	class COSMIC_API EnvironmentMap
	{
	public:
		EnvironmentMap() = default;
		~EnvironmentMap();

		// Owns GPU resources with an explicit Init/Shutdown lifecycle — copying
		// would alias that ownership, so it's disabled (same rule as PostProcessStack).
		EnvironmentMap(const EnvironmentMap&)            = delete;
		EnvironmentMap& operator=(const EnvironmentMap&) = delete;

		/** Load shaders, allocate the cubes + BRDF LUT, and bake the LUT (once). */
		void Init();
		/** Release all GPU resources (call while the GL context is current). */
		void Shutdown();
		bool IsInitialized() const { return m_Initialized; }

		/** Sun direction TO the light (normalized internally). Marks the bake dirty. */
		void SetSunDirection(const glm::vec3& toSun);
		/** Overall sky HDR brightness (marks dirty). */
		void SetSkyIntensity(float intensity);

		/**
		 * Enable the night tier of the BAKED sky (EnvSky.glsl): the cube darkens
		 * through twilight as the sun sets and a moon-glow term takes over so the
		 * IBL convolution yields cool moon-lit ambient. Default off keeps the bake
		 * byte-identical to the shipped S7 output. Marks the bake dirty.
		 */
		void SetNightSky(bool enabled);
		/** Moon direction (TO the moon, normalized internally) + intensity fed to the
		 *  night bake's moon-glow term. Marks dirty. (The crisp moon DISC lives in the
		 *  per-pixel SkyDetail pass, not the bake.) */
		void SetMoon(const glm::vec3& toMoon, float intensity);

		/** (Re)bake environment → irradiance → prefilter. No-op if not dirty. */
		void Bake();
		/** Force the next Bake() to run even if the sun hasn't moved. */
		void MarkDirty() { m_Dirty = true; }

		/** Feed the IBL handles into Renderer3D so PBR materials sample them. */
		void PushToRenderer() const;

		/**
		 * Draw the environment as the scene background. Call INTO the bound HDR
		 * target before opaque geometry — it fills every pixel (depth test/write
		 * off) and the scene draws over it. Reads the binding-1 camera UBO.
		 */
		void DrawSkybox(const glm::mat4& viewProjection);

		/**
		 * Draw the DETAILED analytic sky (SkyDetail.glsl) as the scene background
		 * INSTEAD of the baked cube — same draw shape as DrawSkybox (fullscreen
		 * triangle at the far plane, depth test/write off) but re-evaluated per
		 * pixel: a crisp limb-darkened sun disc, hashed stars with twinkle, a
		 * milky-way band and a phased moon. Uses the stored env sun direction; the
		 * rest comes from `sky`. Reads the binding-1 camera UBO. The shader is
		 * lazy-loaded on first call (most scenes never enable it).
		 */
		void DrawSkyboxDetailed(const glm::mat4& viewProjection, const SkyDetailDesc& sky);

		uint32_t GetIrradianceID() const;
		uint32_t GetPrefilterID() const;
		uint32_t GetBrdfLutID() const;
		float    GetPrefilterMaxLod() const { return m_PrefilterMaxLod; }

	private:
		void RenderCubeFaces(const Ref<Shader>& shader, const Ref<TextureCube>& target, uint32_t mip);

		Ref<TextureCube> m_EnvCube;      // procedural sky, mipmapped
		Ref<TextureCube> m_Irradiance;   // diffuse irradiance
		Ref<TextureCube> m_Prefilter;    // prefiltered specular (mip = roughness)
		Ref<FrameBuffer> m_BrdfLut;      // 2D RG(BA16F) LUT

		Ref<Shader> m_EnvSkyShader;
		Ref<Shader> m_IrradianceShader;
		Ref<Shader> m_PrefilterShader;
		Ref<Shader> m_BrdfShader;
		Ref<Shader> m_SkyboxShader;
		Ref<Shader> m_SkyDetailShader;   // SkyDetail.glsl (lazy — detailed sky background, F7)

		Ref<Mesh>   m_Cube;              // unit box for cube-render passes

		glm::vec3 m_SunDir{ -0.4f, 1.0f, -0.3f };   // direction TO the sun
		float     m_SkyIntensity   = 1.0f;
		float     m_PrefilterMaxLod = 0.0f;
		bool      m_Dirty          = true;
		bool      m_Initialized    = false;

		// --- Night tier of the bake (F7) — defaults keep the bake byte-identical ---
		bool      m_NightSky       = false;         // EnvSky u_NightSky
		glm::vec3 m_MoonDir{ 0.0f, 1.0f, 0.0f };    // direction TO the moon
		float     m_MoonIntensity  = 0.0f;          // EnvSky u_MoonIntensity
	};
}
