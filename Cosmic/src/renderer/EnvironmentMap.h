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

		Ref<Mesh>   m_Cube;              // unit box for cube-render passes

		glm::vec3 m_SunDir{ -0.4f, 1.0f, -0.3f };   // direction TO the sun
		float     m_SkyIntensity   = 1.0f;
		float     m_PrefilterMaxLod = 0.0f;
		bool      m_Dirty          = true;
		bool      m_Initialized    = false;
	};
}
