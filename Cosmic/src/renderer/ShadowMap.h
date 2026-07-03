#pragma once

// ShadowMap.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — ShadowMap (directional sun shadows)  [S6.4]
 * ============================================================================
 *
 * A single directional shadow map: a depth-only framebuffer the scene renders
 * into from the sun's point of view, then PBR.glsl / MeshLit.glsl sample with
 * 3x3 PCF to darken shadowed surfaces. This is the "single 2k map + PCF" first
 * tier; 3-split cascaded shadow maps (CSM) with texel-snapping are the next step
 * (documented deferral — the API here is CSM-ready via SetLight's fitted matrix).
 *
 * WIRING (app owns one, like PostProcessStack / EnvironmentMap):
 *   shadow.Init();
 *   shadow.SetLight(sunTravelDir, sceneCenter, sceneRadius);  // fit the frustum
 *   shadow.BeginDepthPass();
 *     shadow.DrawCaster(mesh, transform);   // every shadow caster
 *   shadow.EndDepthPass();                   // restores render state + FBO
 *   shadow.PushToRenderer(bias);             // lit materials now sample it
 *   ... main lit pass ...
 *
 * Front-face culling during the depth pass reduces peter-panning; a slope-scaled
 * bias in the lit shader fights acne. Init()/passes need a live GL context.
 * ============================================================================
 */

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
	class FrameBuffer;
	class Shader;
	class Mesh;

	class COSMIC_API ShadowMap
	{
	public:
		ShadowMap() = default;
		~ShadowMap();

		// Owns GPU resources with an explicit Init/Shutdown lifecycle — copying
		// would alias that ownership, so it's disabled (same rule as PostProcessStack).
		ShadowMap(const ShadowMap&)            = delete;
		ShadowMap& operator=(const ShadowMap&) = delete;

		void Init(uint32_t size = 2048);
		void Shutdown();
		bool IsInitialized() const { return m_Initialized; }

		/**
		 * Fit the sun's ortho frustum around a world-space bounding sphere.
		 * @param sunTravelDir direction the sun light TRAVELS (same convention as the lights UBO).
		 */
		void SetLight(const glm::vec3& sunTravelDir, const glm::vec3& center, float radius);

		void BeginDepthPass();
		void DrawCaster(const Ref<Mesh>& mesh, const glm::mat4& transform);
		void EndDepthPass();

		/** Register the shadow map + light matrix with Renderer3D (lit materials sample it). */
		void PushToRenderer(float bias = 0.0015f) const;

		const glm::mat4& GetLightViewProj() const { return m_LightViewProj; }
		uint32_t         GetDepthID() const;
		uint32_t         GetSize() const { return m_Size; }

	private:
		Ref<FrameBuffer> m_Fbo;          // depth-only
		Ref<Shader>      m_DepthShader;  // ShadowDepth.glsl
		glm::mat4        m_LightViewProj{ 1.0f };
		uint32_t         m_Size = 2048;
		bool             m_Initialized = false;
	};
}
