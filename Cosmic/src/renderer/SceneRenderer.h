#pragma once

// SceneRenderer.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — SceneRenderer (engine-owned frame orchestration)  [F2]
 * ============================================================================
 *
 * The orchestration promotion named as doc 05 §5 S6.1(d)'s "S12-adjacent
 * follow-up" and pulled into Phase 11 (docs/plans/10-phase11-frontier-plan.md,
 * F2). It owns the multi-pass frame sequence that apps otherwise copy-paste from
 * Engine3DDemo (~450 lines): shadow depth → planar reflection → opaque HDR →
 * transparents (water/particles/ribbons) → post + composite. A world declares
 * WHAT to render by filling a SceneRenderDesc and providing DrawOpaque; the
 * SceneRenderer owns HOW (the pass order + all render-state contracts).
 *
 * OWNED GPU SUBSYSTEMS (one each, like the app used to own): EnvironmentMap
 * (IBL + skybox — the app drives the sun policy through GetEnvironment()),
 * PostProcessStack (HDR target + SSAO/bloom/FXAA/fog/god-rays/heat-haze), and
 * ShadowMap (the directional sun map). Init() needs a live GL context;
 * Shutdown() before context teardown (it also clears the Renderer3D IBL/shadow
 * registration).
 *
 * WIRING (app-side, per frame — see FrontierApp / IslandWorld):
 *
 *   renderer.SetViewportSize(w, h);
 *   renderer.GetEnvironment().SetSunDirection(toSun);   // sun policy is APP's
 *   SceneRenderDesc desc;
 *   desc.SetCamera(camera);
 *   desc.Lights = ...; desc.TerrainSystem = &terrain; desc.WaterBodies = {...};
 *   desc.DrawOpaque = [&](const SceneDrawContext& c){ c.DrawMesh(...); };
 *   // PRE: the final LDR (viewport) FBO is bound.
 *   renderer.Render(desc);
 *   // POST: the same FBO is re-bound, viewport (0,0,w,h), depth ON/ON,
 *   //       cull None, blend Alpha (engine defaults restored).
 *
 * Engine3DDemo is deliberately NOT migrated — it stays the low-level acceptance
 * rig that proves the primitives this class sequences.
 * ============================================================================
 */

#include "core/Core.h"
#include "renderer/Renderer3D.h"       // SceneRenderDesc holds Renderer3D::SceneLightsDesc by value
#include "renderer/EnvironmentMap.h"   // owned by value
#include "renderer/PostProcessStack.h" // owned by value
#include "renderer/ShadowMap.h"        // owned by value

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace Cosmic
{
	class Camera;
	class Mesh;
	class Model;
	class Material;
	class InstanceSet;
	class Terrain;
	class Water;
	class ParticleEmitter;
	class RibbonEmitter;
	class Scene;

	/**
	 * @brief Which pass a DrawOpaque invocation is servicing. Switches on this
	 * MUST keep a default: arm — F8 (snow coverage) appends a top-down depth pass.
	 */
	enum class ScenePass : uint8_t { ShadowDepth = 0, Reflection, Main };

	/**
	 * @brief Handed to SceneRenderDesc::DrawOpaque once per pass. The submit verbs
	 * route by Pass: Reflection/Main → Renderer3D (a scene is live), ShadowDepth →
	 * ShadowMap::DrawCaster (material/color/entityID are ignored there). Direct
	 * Renderer3D::Draw* calls from the callback are legal in Reflection/Main and a
	 * bug in ShadowDepth (no scene camera is set for a depth-only pass).
	 */
	class COSMIC_API SceneDrawContext
	{
	public:
		ScenePass Pass = ScenePass::Main;
		glm::mat4 ViewProjection{ 1.0f };   // lightVP / mirrored-oblique VP / camera VP
		glm::vec3 EyePosition{ 0.0f };      // this pass's eye (mirrored under Reflection)
		glm::vec3 CameraPosition{ 0.0f };   // ALWAYS the real camera — LOD decisions use this

		bool IsDepthOnly() const { return Pass == ScenePass::ShadowDepth; }

		void DrawMesh (const Ref<Mesh>& mesh, const glm::mat4& transform,
		               const glm::vec4& color, int entityID = -1) const;
		void DrawMesh (const Ref<Mesh>& mesh, const glm::mat4& transform,
		               const Ref<Material>& material, int entityID = -1) const;
		void DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID = -1) const;

		// Instanced submit (F5): Reflection/Main → Renderer3D::DrawMeshInstanced;
		// ShadowDepth → ShadowMap::DrawCasterInstanced (material/entityID ignored).
		void DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
		                       const Ref<InstanceSet>& instances, uint32_t count, int entityID = -1) const;

	private:
		friend class SceneRenderer;
		ShadowMap* m_Shadow = nullptr;      // ShadowDepth routing target
	};

	/**
	 * @brief Per-frame feature toggles + tuning. Defaults keep the cheap path;
	 * app policy owns the scenario values (nothing scenario-shaped in the engine).
	 */
	struct SceneRendererSettings
	{
		bool Skybox = true; bool IBL = true; bool Shadows = true; bool WaterReflections = true;
		bool TerrainCastsShadows = true;                      // consumed from F4
		glm::vec4 ClearColor{ 0.1f, 0.1f, 0.1f, 1.0f };
		glm::vec3 ShadowCenter{ 0.0f }; float ShadowRadius = 50.0f; float ShadowBias = 0.0015f;
		bool SSAO = false;  float SsaoRadius = 0.5f, SsaoBias = 0.025f;
		bool Bloom = false; float BloomThreshold = 1.0f, BloomKnee = 0.6f, BloomIntensity = 0.6f;
		bool FXAA = true;
		bool Fog = false;   glm::vec3 FogColor{ 0.70f, 0.80f, 0.92f };
		                    float FogDensity = 0.02f, FogHeightFalloff = 0.12f, FogBaseHeight = 0.0f;
		bool GodRays = false; float GodRaysIntensity = 0.6f, GodRaysDensity = 0.04f; // auto-off when !Shadows
		bool HeatHaze = false; float HeatHazeStrength = 0.02f;
		// Underwater medium (F6): tonemap fogs + tints the frame when the camera is
		// below UnderwaterY. The app sets UnderwaterY to the primary water surface.
		bool Underwater = false; float UnderwaterY = 0.0f;
		glm::vec3 UnderwaterColor{ 0.05f, 0.18f, 0.22f };
		float     UnderwaterDensity = 0.08f;
		glm::vec3 UnderwaterTint{ 0.55f, 0.75f, 0.90f };
		// F7 appends detailed-sky + lens-flare fields.
	};

	/**
	 * @brief The full description of one frame handed to SceneRenderer::Render.
	 * The app fills camera + lights + content lists + the draw callbacks.
	 */
	struct COSMIC_API SceneRenderDesc
	{
		glm::mat4 View{ 1.0f }; glm::mat4 Projection{ 1.0f }; glm::vec3 CameraPosition{ 0.0f };
		void SetCamera(const Camera& camera);                 // sugar filling the three above

		Renderer3D::SceneLightsDesc Lights;
		float TimeSeconds = 0.0f; float Exposure = 1.0f;
		SceneRendererSettings Settings;

		Terrain* TerrainSystem = nullptr;                     // Reflection + Main (+ shadow via F4)
		std::vector<Water*>           WaterBodies;            // app submits far -> near
		int                           PrimaryReflectionWater = 0;   // index; -1 = IBL-only for all
		std::vector<ParticleEmitter*> Emitters;
		std::vector<RibbonEmitter*>   Ribbons;
		std::vector<ParticleEmitter*> DistortionEmitters;     // heat-haze field writers
		Scene* EcsScene = nullptr;                            // Main only (not Reflection)

		std::function<void(const SceneDrawContext&)> DrawOpaque;
		std::function<void(const SceneDrawContext&)> DrawTransparent;  // HDR still bound, after water/particles
		std::function<void()>                        DrawOverlay2D;    // after Composite (LDR bound)
	};

	class COSMIC_API SceneRenderer
	{
	public:
		SceneRenderer() = default;
		~SceneRenderer();

		// Owns GPU subsystems with an explicit Init/Shutdown lifecycle — copying
		// would alias that ownership, so it's disabled (same rule as the members).
		SceneRenderer(const SceneRenderer&)            = delete;
		SceneRenderer& operator=(const SceneRenderer&) = delete;

		void Init(uint32_t width, uint32_t height, uint32_t shadowMapSize = 2048);
		void Shutdown();                          // also Renderer3D::ClearIBL/ClearShadow
		bool IsInitialized() const { return m_Initialized; }
		void SetViewportSize(uint32_t width, uint32_t height);

		/**
		 * @brief Render one frame. PRE: the final LDR (viewport) FBO is bound.
		 * POST: that same FBO is re-bound, the viewport is (0,0,w,h), and the
		 * engine render-state defaults are restored (depth ON/ON, cull None,
		 * blend Alpha). Re-entrant calls (Render inside a draw callback) are a bug
		 * and are refused.
		 */
		void Render(const SceneRenderDesc& desc);

		EnvironmentMap&   GetEnvironment() { return m_Environment; }   // app drives the sun policy
		PostProcessStack& GetPostStack()   { return m_Post; }
		ShadowMap&        GetShadowMap()   { return m_Shadow; }

	private:
		// One method per pass = F3's GPU-zone hook points.
		void PassShadow(const SceneRenderDesc& desc);
		void PassReflection(const SceneRenderDesc& desc);
		void PassOpaqueHDR(const SceneRenderDesc& desc);
		void PassTransparents(const SceneRenderDesc& desc);
		void PassPostAndComposite(const SceneRenderDesc& desc);

		EnvironmentMap   m_Environment;
		PostProcessStack m_Post;
		ShadowMap        m_Shadow;

		uint32_t m_Width  = 0;
		uint32_t m_Height = 0;
		bool     m_Initialized = false;
		bool     m_InRender    = false;   // reentrancy guard

		// Per-frame scratch (valid only for the duration of one Render call).
		uint32_t  m_FinalFbo = 0;
		glm::mat4 m_ViewProj{ 1.0f };
		glm::mat4 m_InvViewProj{ 1.0f };
	};
}
