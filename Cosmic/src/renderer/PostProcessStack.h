#pragma once

// PostProcessStack.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — PostProcessStack (S6 HDR + post-processing)
 * ============================================================================
 *
 * The load-bearing piece of the S6 "visual realism core": the 3D scene renders
 * into a FLOAT (RGBA16F) target so overbright values survive, then a chain of
 * fullscreen passes resolves it to the LDR target the UI composites on top of
 * (doc 05 contract rule 7).
 *
 * Passes owned here:
 *   S6.1  HDR scene target + ACES tonemap + exposure  (Tonemap.glsl)
 *   S6.5  SSAO (reconstruct-from-depth) + blur         (Ssao/SsaoBlur.glsl)
 *   S6.6  Bloom (soft-knee threshold + Gaussian chain) (BloomPrefilter/BloomBlur)
 *   S6.7  FXAA (final LDR edge blend)                  (Fxaa.glsl)
 *   S7.2  Height fog (folded into the tonemap)
 *   S10.3 Sun shafts: shadow-map raymarch god rays     (GodRays.glsl)
 *         [tier 1 — the froxel fog grid is the documented follow-up]
 *   S10.5 Heat-haze: a distortion field the app renders (e.g. distortion
 *         particles) that displaces the tonemap's scene fetch
 *
 * FRAME SHAPE (app-side; mirrors the S3.1 FPV-inset rebind pattern):
 *
 *   post.SetViewportSize(w, h);
 *   post.BeginHDR({0.1f,0.1f,0.1f,1});   // bind + clear the HDR scene FBO
 *       ... draw the whole 3D world ...
 *   post.RenderEffects(projectionMatrix);// SSAO + bloom (reads the scene target)
 *   appViewportFbo->Bind();              // re-bind the LDR target
 *   RenderCommand::SetViewport(0,0,w,h);
 *   post.Composite(exposure);            // tonemap (+AO +bloom) -> LDR, then FXAA
 *   // ... 2D / UI overlay draws in LDR ...
 *
 * SSAO + bloom are enabled per-instance (SetSSAOEnabled / SetBloomEnabled); with
 * both off, Composite is exactly the S6.1 single-pass tonemap. Init() needs a
 * live GL context; Shutdown() before context teardown.
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{
	class FrameBuffer;
	class Shader;
	class Texture2D;

	class COSMIC_API PostProcessStack
	{
	public:
		PostProcessStack() = default;
		~PostProcessStack();

		// Owns GPU resources with an explicit Init/Shutdown lifecycle — copying
		// would alias that ownership (two owners, one Shutdown), so it's disabled.
		PostProcessStack(const PostProcessStack&)            = delete;
		PostProcessStack& operator=(const PostProcessStack&) = delete;

		void Init(uint32_t width, uint32_t height);
		void Shutdown();
		bool IsInitialized() const { return m_Initialized; }

		void SetViewportSize(uint32_t width, uint32_t height);

		/** Bind + clear the HDR scene framebuffer and set the viewport to its size. */
		void BeginHDR(const glm::vec4& clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

		const Ref<FrameBuffer>& GetSceneTarget() const { return m_SceneHDR; }

		/**
		 * Run the enabled screen-space effects (SSAO + bloom) from the HDR scene
		 * target. Call after the 3D scene is drawn and before Composite. `projection`
		 * is the perspective projection matrix — SSAO reconstructs view-space
		 * position from the scene depth. No-op for effects that are disabled.
		 */
		void RenderEffects(const glm::mat4& projection);

		/**
		 * Resolve the HDR scene into the currently-bound LDR target: ACES tonemap +
		 * exposure, modulated by SSAO and combined with bloom (both if enabled),
		 * then an optional FXAA pass. The caller binds + sets the viewport on the
		 * LDR target first. Depth test/write are restored to the engine default after.
		 */
		void Composite(float exposure = 1.0f);

		// ---- SSAO (S6.5) ----
		void SetSSAOEnabled(bool enabled) { m_SsaoEnabled = enabled; }
		bool IsSSAOEnabled() const        { return m_SsaoEnabled; }
		void SetSSAOParams(float radius, float bias) { m_SsaoRadius = radius; m_SsaoBias = bias; }

		// ---- Bloom (S6.6) ----
		void SetBloomEnabled(bool enabled) { m_BloomEnabled = enabled; }
		bool IsBloomEnabled() const        { return m_BloomEnabled; }
		void SetBloomParams(float threshold, float knee, float intensity)
		{ m_BloomThreshold = threshold; m_BloomKnee = knee; m_BloomIntensity = intensity; }

		// ---- FXAA (S6.7) ----
		void SetFXAAEnabled(bool enabled) { m_FxaaEnabled = enabled; }
		bool IsFXAAEnabled() const        { return m_FxaaEnabled; }

		// ---- Vignette (Phase 25 / Q5) — folded into the tonemap pass ----
		// Post-tonemap edge darkening toward `color`. Default off ⇒ the tonemap
		// receives u_VignetteAmount 0 and skips the block (byte-identical output).
		void SetVignetteEnabled(bool enabled) { m_VignetteEnabled = enabled; }
		bool IsVignetteEnabled() const        { return m_VignetteEnabled; }
		void SetVignetteParams(float amount, float radius, float feather, const glm::vec3& color)
		{ m_VignetteAmount = amount; m_VignetteRadius = radius; m_VignetteFeather = feather; m_VignetteColor = color; }

		// Output gamma (X2) — the tonemap's linear->sRGB exponent. Default 2.2
		// reproduces the previously-hardcoded curve (byte-identical).
		void SetGamma(float gamma) { m_Gamma = gamma; }

		// ---- Height fog (S7.2) ----
		void SetFogEnabled(bool enabled) { m_FogEnabled = enabled; }
		bool IsFogEnabled() const        { return m_FogEnabled; }
		void SetFogParams(const glm::vec3& color, float density, float heightFalloff, float baseHeight)
		{ m_FogColor = color; m_FogDensity = density; m_FogHeightFalloff = heightFalloff; m_FogBaseHeight = baseHeight; }
		/** Camera for depth-based reconstruction (fog S7.2 + god rays S10.3) —
		 *  set before RenderEffects/Composite when either is on. */
		void SetCamera(const glm::mat4& viewProjection, const glm::vec3& cameraPos)
		{ m_ViewProjection = viewProjection; m_CameraPos = cameraPos; }

		// ---- Sun shafts / god rays (S10.3 tier 1) ----
		void SetGodRaysEnabled(bool enabled) { m_GodRaysEnabled = enabled; }
		bool IsGodRaysEnabled() const        { return m_GodRaysEnabled; }
		void SetGodRaysParams(float intensity, float density)
		{ m_GodRaysIntensity = intensity; m_GodRaysDensity = density; }
		/** The sun's shadow map + light matrix (ShadowMap::GetDepthID / GetLightViewProj)
		 *  and sun state — required inputs; call each frame god rays are enabled.
		 *  Also requires SetCamera (world reconstruction). */
		void SetSunShaftInputs(uint32_t shadowMapID, const glm::mat4& lightViewProj,
		                       const glm::vec3& sunTravelDir, const glm::vec3& sunColor, float sunIntensity)
		{
			m_ShaftShadowMapID = shadowMapID; m_ShaftLightViewProj = lightViewProj;
			m_ShaftSunDir = sunTravelDir; m_ShaftSunColor = sunColor; m_ShaftSunIntensity = sunIntensity;
		}

		// ---- Heat-haze distortion (S10.5) ----
		void SetHeatHazeEnabled(bool enabled) { m_HeatHazeEnabled = enabled; }
		bool IsHeatHazeEnabled() const        { return m_HeatHazeEnabled; }
		void SetHeatHazeStrength(float strength) { m_HeatHazeStrength = strength; }
		/**
		 * Bind + clear the distortion field target (half-res RG offsets in a float
		 * texture). Render distortion sources into it — typically a ParticleEmitter
		 * via RenderDistortion, which accumulates additively — then EndDistortion()
		 * re-binds the previous framebuffer (caller re-asserts its viewport).
		 * Returns false when heat haze is disabled or uninitialized (skip the pass).
		 */
		bool BeginDistortion();
		void EndDistortion();

		// ---- Underwater medium (S9.4-lite / doc 10 F6) ----
		/**
		 * When enabled AND the camera is below `waterlineY`, the tonemap fogs the
		 * frame toward `color` with distance (1/m `density`) and tints it by `tint`
		 * (spectral absorption). Gated by `enabled` (default off = the shipped
		 * output). The app sets the waterline to the primary water surface height.
		 */
		void SetUnderwater(bool enabled, float waterlineY,
		                   const glm::vec3& color, float density, const glm::vec3& tint)
		{
			m_UnderwaterEnabled = enabled; m_WaterlineY = waterlineY;
			m_UnderwaterColor = color; m_UnderwaterDensity = density; m_UnderwaterTint = tint;
		}
		bool IsUnderwaterEnabled() const { return m_UnderwaterEnabled; }
		/** Subnautica-style depth grading: the fog blends from the shallow color toward
		 *  `deepColor` and gets denser as the camera descends past `depthReference` m. */
		void SetUnderwaterGrading(const glm::vec3& deepColor, float depthReference)
		{ m_UnderwaterDeepColor = deepColor; m_UnderwaterDepthRef = depthReference; }
		/** Animated seafloor caustics on submerged geometry (screen-space, in the
		 *  underwater block). `strength` 0 = off. Needs SetTime each frame. */
		void SetUnderwaterCaustics(float strength, float scale)
		{ m_UnderwaterCausticStrength = strength; m_UnderwaterCausticScale = scale; }
		/** Seconds clock for animated post effects (underwater caustics). */
		void SetTime(float seconds) { m_Time = seconds; }

		// ---- Lens flare (S7 / doc 10 F7) ----
		/**
		 * Screen-space lens flare (LensFlare.glsl) drawn ADDITIVELY over the
		 * tonemapped LDR image (after tonemap, before FXAA). `tint` is usually the
		 * sun color. Occlusion is resolved in-shader from the scene depth around the
		 * sun's screen position. Gated by `enabled` (default off = the shipped
		 * output). Also needs SetLensFlareSun each frame + SetCamera (screen-space
		 * projection of the sun).
		 */
		void SetLensFlare(bool enabled, float intensity, const glm::vec3& tint)
		{ m_LensFlareEnabled = enabled; m_LensFlareIntensity = intensity; m_LensFlareTint = tint; }
		bool IsLensFlareEnabled() const { return m_LensFlareEnabled; }
		/** Direction the sunlight TRAVELS (the sun sits opposite). Call each frame the
		 *  flare is enabled; Composite projects the far sun point through the camera. */
		void SetLensFlareSun(const glm::vec3& sunTravelDir) { m_LensFlareSunDir = sunTravelDir; }

		uint32_t GetWidth()  const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		static void DrawFullscreenTriangle();

	private:
		void InitEffects();
		void ResizeEffects();
		void RenderSSAO(const glm::mat4& projection);
		void RenderBloom();
		void RenderGodRays();

		Ref<FrameBuffer> m_SceneHDR;        // {RGBA16F, DEPTH24STENCIL8}
		Ref<Shader>      m_TonemapShader;   // Tonemap.glsl

		uint32_t m_Width  = 0;
		uint32_t m_Height = 0;
		bool     m_Initialized = false;

		// ---- SSAO ----
		Ref<Shader>            m_SsaoShader;
		Ref<Shader>            m_SsaoBlurShader;
		Ref<FrameBuffer>       m_SsaoTarget;       // half-res
		Ref<FrameBuffer>       m_SsaoBlurTarget;   // half-res
		Ref<Texture2D>           m_NoiseTex;       // 4x4 rotation noise
		std::vector<glm::vec3>   m_Kernel;         // hemisphere samples
		std::vector<std::string> m_KernelNames;    // "u_Kernel[i]" built once (no per-frame formatting)
		bool     m_SsaoEnabled = false;
		float    m_SsaoRadius  = 0.5f;
		float    m_SsaoBias    = 0.025f;
		uint32_t m_AoResultID  = 0;                // set by RenderSSAO

		// ---- Bloom ----
		Ref<Shader>      m_BloomPrefilterShader;
		Ref<Shader>      m_BloomBlurShader;
		Ref<FrameBuffer> m_BloomA;                 // half-res ping
		Ref<FrameBuffer> m_BloomB;                 // half-res pong
		bool     m_BloomEnabled   = false;
		float    m_BloomThreshold = 1.0f;
		float    m_BloomKnee      = 0.6f;
		float    m_BloomIntensity = 0.6f;
		uint32_t m_BloomResultID  = 0;             // set by RenderBloom

		// ---- FXAA ----
		Ref<Shader>      m_FxaaShader;
		Ref<FrameBuffer> m_LdrTarget;              // full-res tonemap output when FXAA on
		bool             m_FxaaEnabled = false;

		// ---- Vignette (Q5) ----
		bool      m_VignetteEnabled = false;
		float     m_VignetteAmount  = 0.35f;
		float     m_VignetteRadius  = 0.9f;
		float     m_VignetteFeather = 0.4f;
		glm::vec3 m_VignetteColor{ 0.0f };
		float     m_Gamma           = 2.2f;   // X2 — tonemap output gamma

		// ---- Height fog (S7.2) ----
		bool      m_FogEnabled       = false;
		glm::vec3 m_FogColor{ 0.70f, 0.80f, 0.92f };
		float     m_FogDensity       = 0.02f;
		float     m_FogHeightFalloff = 0.12f;
		float     m_FogBaseHeight    = 0.0f;
		glm::mat4 m_ViewProjection{ 1.0f };
		glm::vec3 m_CameraPos{ 0.0f };

		// ---- Sun shafts (S10.3 tier 1) ----
		Ref<Shader>      m_GodRaysShader;
		Ref<FrameBuffer> m_ShaftTarget;             // half-res
		bool      m_GodRaysEnabled   = false;
		float     m_GodRaysIntensity = 0.6f;
		float     m_GodRaysDensity   = 0.04f;
		uint32_t  m_ShaftShadowMapID = 0;
		glm::mat4 m_ShaftLightViewProj{ 1.0f };
		glm::vec3 m_ShaftSunDir{ 0.0f, -1.0f, 0.0f };
		glm::vec3 m_ShaftSunColor{ 1.0f };
		float     m_ShaftSunIntensity = 1.0f;
		uint32_t  m_ShaftResultID    = 0;           // set by RenderGodRays

		// ---- Heat-haze distortion (S10.5) ----
		Ref<FrameBuffer> m_DistortTarget;           // half-res RG offset field
		bool     m_HeatHazeEnabled  = false;
		float    m_HeatHazeStrength = 0.02f;
		bool     m_DistortionWritten = false;       // a field was rendered this frame
		bool     m_InDistortion      = false;
		uint32_t m_DistortPrevFbo    = 0;

		// ---- Underwater medium (S9.4-lite / doc 10 F6 + Phase 11 Layer 2) ----
		bool      m_UnderwaterEnabled = false;
		float     m_WaterlineY        = 0.0f;
		glm::vec3 m_UnderwaterColor{ 0.05f, 0.18f, 0.22f };
		float     m_UnderwaterDensity = 0.08f;
		glm::vec3 m_UnderwaterTint{ 0.55f, 0.75f, 0.90f };
		glm::vec3 m_UnderwaterDeepColor{ 0.02f, 0.05f, 0.12f };   // deep grade target
		float     m_UnderwaterDepthRef  = 40.0f;                  // camera depth to reach deep
		float     m_UnderwaterCausticStrength = 0.0f;             // 0 = no seafloor caustics
		float     m_UnderwaterCausticScale    = 0.15f;
		float     m_Time = 0.0f;                                  // animated-effect clock

		// ---- Lens flare (S7 / doc 10 F7) ----
		Ref<Shader> m_LensFlareShader;               // LensFlare.glsl
		bool        m_LensFlareEnabled   = false;
		float       m_LensFlareIntensity = 0.35f;
		glm::vec3   m_LensFlareTint{ 1.0f };
		glm::vec3   m_LensFlareSunDir{ 0.0f, -1.0f, 0.0f };   // direction the sun light TRAVELS
	};
}
