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
	class InstanceSet;
	class StorageBuffer;   // A2 — skinned-caster palette SSBO

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

		/**
		 * @brief Instanced shadow caster (S12.3-lite / doc 10 F5): draws `count`
		 * copies of `mesh` (clamped to the InstanceSet's uploaded count) from the
		 * SAME per-instance SSBO PBRInstanced.glsl renders — a scattered forest
		 * shadows itself in one draw. Binds its own ShadowDepthInstanced.glsl, so
		 * a following non-instanced DrawCaster re-binds the plain depth shader
		 * (both restore their own program state). Call inside a depth pass.
		 */
		void DrawCasterInstanced(const Ref<Mesh>& mesh, const Ref<InstanceSet>& instances, uint32_t count);

		/**
		 * @brief Skinned shadow caster (Phase 20 / A2): the caster's joint
		 * palette uploads into this map's own binding-10 SSBO (base 0) and the
		 * mesh draws with ShadowDepthSkinned.glsl, so an animated character's
		 * shadow deforms with it. Same program-state contract as the instanced
		 * caster: a following DrawCaster re-binds the plain depth shader. Call
		 * inside a depth pass.
		 */
		void DrawCasterSkinned(const Ref<Mesh>& mesh, const glm::mat4& transform,
		                       const glm::mat4* palette, uint32_t jointCount);

		void EndDepthPass();

		/** Register the shadow map + light matrix with Renderer3D (lit materials sample it). */
		void PushToRenderer(float bias = 0.0015f) const;

		const glm::mat4& GetLightViewProj() const { return m_LightViewProj; }
		uint32_t         GetDepthID() const;
		uint32_t         GetSize() const { return m_Size; }

	private:
		Ref<FrameBuffer> m_Fbo;                   // depth-only
		Ref<Shader>      m_DepthShader;           // ShadowDepth.glsl
		Ref<Shader>      m_DepthInstancedShader;  // ShadowDepthInstanced.glsl (lazy; F5)
		Ref<Shader>      m_DepthSkinnedShader;    // ShadowDepthSkinned.glsl (lazy; A2)
		Ref<StorageBuffer> m_SkinSsbo;            // per-caster palette (binding 10; lazy; A2)
		uint32_t         m_SkinSsboCapacity = 0;  // matrices
		glm::mat4        m_LightViewProj{ 1.0f };
		uint32_t         m_Size = 2048;
		bool             m_Initialized = false;
	};
}
