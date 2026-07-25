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
 *
 * THE 2D CONFIGURATION (Phase 29 / W6). 2D does NOT get a second compositor: it
 * flows through this same class, so the pass contract in
 * docs/design/frame-lifecycle.md §5 is preserved verbatim on both engines. What
 * survives under COSMIC_2D_ONLY is the spine —
 *
 *   BeginHDR (PassOpaqueHDR) -> sprites + 2D lights via DrawTransparent
 *   (PassTransparents) -> tonemap/FXAA/bloom/vignette (PassPostAndComposite)
 *   -> DrawOverlay2D for canvas UI
 *
 * — and what fences out is everything that needs a 3D subsystem: the light
 * gather, terrain/water/particles/ribbons, the sky + IBL environment, the shadow
 * and coverage depth passes, the planar reflection, the routed opaque submit and
 * the ScenePicker selection outline. See §7.6 of the plan doc for the table.
 * ============================================================================
 */

#include "core/Core.h"
#ifndef COSMIC_2D_ONLY
#include "renderer/Renderer3D.h"       // SceneRenderDesc holds Renderer3D::SceneLightsDesc by value
#include "renderer/EnvironmentMap.h"   // owned by value
#endif
#include "renderer/PostProcessStack.h" // owned by value (the 2D post chain too)
#ifndef COSMIC_2D_ONLY
#include "renderer/ShadowMap.h"        // owned by value
#endif

#include <glm/glm.hpp>
#include <entt/entt.hpp>               // K12 — SceneRenderDesc::SelectedEntities handles

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
	class ScenePicker;             // K12 — the outline pass's id-mask renderer
	class Shader;
	class CoverageCapture;
	class FrameBuffer;             // X7 — RenderToTexture target
	struct EnvironmentComponent;   // E4 — ApplyEnvironment maps it into a desc

	/**
	 * @brief Which pass a DrawOpaque invocation is servicing. Switches on this
	 * MUST keep a default: arm. TopDownDepth (F8) is the snow-coverage top-down
	 * depth capture — like ShadowDepth but routed to a CoverageCapture.
	 */
	enum class ScenePass : uint8_t { ShadowDepth = 0, Reflection, Main, TopDownDepth };

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

		bool IsDepthOnly() const { return Pass == ScenePass::ShadowDepth || Pass == ScenePass::TopDownDepth; }

#ifndef COSMIC_2D_ONLY
		// W6 — every submit verb routes to Renderer3D / ShadowMap / CoverageCapture,
		// none of which exist in the 2D configuration. The context ITSELF stays: it
		// is what DrawTransparent receives, and 2D reads ViewProjection off it to
		// draw sprites.
		void DrawMesh (const Ref<Mesh>& mesh, const glm::mat4& transform,
		               const glm::vec4& color, int entityID = -1) const;
		void DrawMesh (const Ref<Mesh>& mesh, const glm::mat4& transform,
		               const Ref<Material>& material, int entityID = -1) const;
		void DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID = -1) const;

		// Per-submesh LIT submit (M5): one material slot's index range through the
		// Renderer3D queue. Depth passes never call this — the Scene draws the
		// whole mesh once for shadow/coverage (materials don't affect depth), so a
		// multi-material caster stays byte-identical to a single-material one.
		// `material` null ⇒ the Lambert `color` path for that range.
		void DrawMeshRange(const Ref<Mesh>& mesh, const glm::mat4& transform,
		                   const Ref<Material>& material, const glm::vec4& color,
		                   uint32_t indexOffset, uint32_t indexCount, int entityID = -1) const;

		// Skinned submit (Phase 20 / A2): Reflection/Main -> Renderer3D::
		// DrawMeshSkinned (the PBRSkinned twin); ShadowDepth -> ShadowMap::
		// DrawCasterSkinned (deforming shadows); TopDownDepth -> the bind pose.
		void DrawMeshSkinned(const Ref<Mesh>& mesh, const glm::mat4& transform,
		                     const Ref<Material>& material,
		                     const glm::mat4* palette, uint32_t jointCount, int entityID = -1) const;

		// Instanced submit (F5): Reflection/Main → Renderer3D::DrawMeshInstanced;
		// ShadowDepth → ShadowMap::DrawCasterInstanced (material/entityID ignored).
		void DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
		                       const Ref<InstanceSet>& instances, uint32_t count, int entityID = -1) const;
#endif   // COSMIC_2D_ONLY

	private:
		friend class SceneRenderer;
#ifndef COSMIC_2D_ONLY
		ShadowMap*       m_Shadow   = nullptr;   // ShadowDepth routing target
		CoverageCapture* m_Coverage = nullptr;   // TopDownDepth routing target (F8)
#endif
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
		// Environment polish (X2): AmbientIntensity scales the PBR ambient/IBL term;
		// Gamma is the tonemap output gamma. Defaults reproduce the shipped frame
		// (1.0 = unscaled ambient; 2.2 = the hardcoded sRGB curve) → byte-identical.
		float AmbientIntensity = 1.0f; float Gamma = 2.2f;
		glm::vec3 ShadowCenter{ 0.0f }; float ShadowRadius = 50.0f; float ShadowBias = 0.0015f;
		bool SSAO = false;  float SsaoRadius = 0.5f, SsaoBias = 0.025f;
		bool Bloom = false; float BloomThreshold = 1.0f, BloomKnee = 0.6f, BloomIntensity = 0.6f;
		bool FXAA = true;
		bool Fog = false;   glm::vec3 FogColor{ 0.70f, 0.80f, 0.92f };
		                    float FogDensity = 0.02f, FogHeightFalloff = 0.12f, FogBaseHeight = 0.0f;
		bool GodRays = false; float GodRaysIntensity = 0.6f, GodRaysDensity = 0.04f; // auto-off when !Shadows
		bool HeatHaze = false; float HeatHazeStrength = 0.02f;
		// Underwater medium (F6 + Phase 11 Layer 2): tonemap fogs + tints the frame
		// when the camera is below UnderwaterY. The app sets UnderwaterY to the primary
		// water surface. Depth grading (deep color + reference depth) darkens/blue-shifts
		// as the camera descends; caustics dance on submerged geometry (0 = off).
		bool Underwater = false; float UnderwaterY = 0.0f;
		glm::vec3 UnderwaterColor{ 0.05f, 0.18f, 0.22f };
		float     UnderwaterDensity = 0.08f;
		glm::vec3 UnderwaterTint{ 0.55f, 0.75f, 0.90f };
		glm::vec3 UnderwaterDeepColor{ 0.02f, 0.05f, 0.12f };
		float     UnderwaterDepthReference  = 40.0f;
		float     UnderwaterCausticStrength = 0.0f;
		float     UnderwaterCausticScale    = 0.15f;
		// Lens flare (F7): additive screen-space flare in the composite LDR stage.
		// The tint is taken from the sun color; the sun screen position is derived
		// from the frame camera. Auto-uses the detailed-sky when DetailedSky is set.
		bool  LensFlare = false; float LensFlareIntensity = 0.35f;
		// Vignette (Q5): post-tonemap edge darkening folded into the tonemap pass.
		// Default off ⇒ byte-identical (the shader skips the block at amount 0).
		bool      Vignette = false; float VignetteAmount = 0.35f, VignetteRadius = 0.9f, VignetteFeather = 0.4f;
		glm::vec3 VignetteColor{ 0.0f };
		// Wireframe (R8): rasterize the geometry passes (opaque + transparents)
		// with PolygonMode::Line and skip the skybox draw (a full-screen cube's 12
		// edges are noise, not information) — the standard editor debug view. Post
		// passes always run with Fill restored. Default off = byte-identical.
		bool Wireframe = false;
		// Selection outline (K12): after the composite, entities listed in
		// SceneRenderDesc::SelectedEntities render into an id-mask FBO (a
		// selection-filtered ScenePicker pass) and a fullscreen edge-detect
		// composites a crisp silhouette ring over the LDR frame. Editor-facing
		// but generic (any host can outline any entity set). Default off =
		// byte-identical; requires desc.EcsScene + a non-empty selection.
		bool      OutlineEnabled = false;
		glm::vec3 OutlineColor{ 1.0f, 0.62f, 0.11f };
		float     OutlineWidthPx = 2.0f;
	};

	/**
	 * @brief The full description of one frame handed to SceneRenderer::Render.
	 * The app fills camera + lights + content lists + the draw callbacks.
	 */
	struct COSMIC_API SceneRenderDesc
	{
		glm::mat4 View{ 1.0f }; glm::mat4 Projection{ 1.0f }; glm::vec3 CameraPosition{ 0.0f };
		void SetCamera(const Camera& camera);                 // sugar filling the three above

#ifndef COSMIC_2D_ONLY
		Renderer3D::SceneLightsDesc Lights;
#endif
		float TimeSeconds = 0.0f; float Exposure = 1.0f;
		SceneRendererSettings Settings;

#ifndef COSMIC_2D_ONLY
		Terrain* TerrainSystem = nullptr;                     // Reflection + Main (+ shadow via F4)
		std::vector<Water*>           WaterBodies;            // app submits far -> near
		int                           PrimaryReflectionWater = 0;   // index; -1 = IBL-only for all
		std::vector<ParticleEmitter*> Emitters;
		std::vector<RibbonEmitter*>   Ribbons;
		std::vector<ParticleEmitter*> DistortionEmitters;     // heat-haze field writers
#endif
		Scene* EcsScene = nullptr;                            // Main only (not Reflection)

#ifndef COSMIC_2D_ONLY
		// F7: when set, the opaque + reflection passes draw the DETAILED per-pixel
		// sky (SkyDetail.glsl) instead of the baked skybox cube. Points at app-owned
		// storage that must outlive the Render() call.
		const SkyDetailDesc* DetailedSky = nullptr;

		// F8: when set, a top-down depth capture runs right after the shadow pass —
		// DrawOpaque is invoked with a ScenePass::TopDownDepth context (routed to the
		// CoverageCapture), plus terrain depth + ECS casters — then the coverage mask
		// advances by DeltaTime. The app feeds the resulting mask into a SnowDesc.
		CoverageCapture* Coverage           = nullptr;
		float            CoverageAccumPerSec = 0.0f;
		float            CoverageMeltPerSec  = 0.0f;
#endif
		// DeltaTime is NOT fenced (§7.6 left the call at implementation): it is the
		// frame delta, not a coverage parameter. BuildRenderDesc writes it on both
		// engines and the render-test fixtures set it, so a 2D build has to be able
		// to name it — only the coverage pass above ever READS it.
		float            DeltaTime           = 0.0f;   // seconds since last frame

#ifndef COSMIC_2D_ONLY
		// K12 — the entities the outline pass silhouettes (only read when
		// Settings.OutlineEnabled and EcsScene are set). Points at caller-owned
		// storage that must outlive the Render() call (the editor's selection).
		const std::vector<entt::entity>* SelectedEntities = nullptr;

		std::function<void(const SceneDrawContext&)> DrawOpaque;
#endif
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

		/**
		 * @brief Render one frame straight into an offscreen `target` framebuffer
		 * instead of the bound viewport (X7 / gap §12.3) — the stable public verb
		 * behind minimaps, security cameras, portals, RTT thumbnails. Binds
		 * `target`, resizes this renderer's post stack to it, runs the normal
		 * Render() (so env/sky/shadows/post all apply), then RE-BINDS whatever
		 * framebuffer was bound on entry — the A4 state-restore contract, so the
		 * main viewport renders identically afterward. Headless / uninitialized /
		 * null-target ⇒ a safe no-op. Use a DEDICATED SceneRenderer sized to the
		 * target to avoid resizing the main post stack each frame. Minimap/
		 * fog-of-war LOGIC stays app-side (this ships only the generic verb).
		 */
		void RenderToTexture(const SceneRenderDesc& desc, const Ref<FrameBuffer>& target);

#ifndef COSMIC_2D_ONLY
		EnvironmentMap&   GetEnvironment() { return m_Environment; }   // app drives the sun policy
#endif
		PostProcessStack& GetPostStack()   { return m_Post; }
#ifndef COSMIC_2D_ONLY
		ShadowMap&        GetShadowMap()   { return m_Shadow; }
#endif

		/**
		 * @brief Map a scene's EnvironmentComponent (E4) into a SceneRenderDesc:
		 * writes exposure + fog + post toggles into desc.Settings, the sun into
		 * desc.Lights, and drives the owned EnvironmentMap's sun direction + sky
		 * intensity. The editor/PlayerLayer call this each frame before Render()
		 * for the scene's single "Environment" entity; Frontier never calls it, so
		 * its explicit desc.Settings path is untouched. Generic verb — no editor or
		 * Starforge concepts leak in.
		 *
		 * In the 2D configuration it maps the exposure + post-chain fields only —
		 * the sun, sky and IBL halves have nowhere to land.
		 */
		void ApplyEnvironment(const EnvironmentComponent& env, SceneRenderDesc& desc);

	private:
		// One method per pass = F3's GPU-zone hook points.
#ifndef COSMIC_2D_ONLY
		void PassShadow(const SceneRenderDesc& desc);
		void PassCoverage(const SceneRenderDesc& desc);   // F8 top-down snow capture
		void PassReflection(const SceneRenderDesc& desc);
#endif
		void PassOpaqueHDR(const SceneRenderDesc& desc);
		void PassTransparents(const SceneRenderDesc& desc);
		void PassPostAndComposite(const SceneRenderDesc& desc);
#ifndef COSMIC_2D_ONLY
		void PassOutline(const SceneRenderDesc& desc);    // K12 (LDR bound, after composite)

		EnvironmentMap   m_Environment;
#endif
		PostProcessStack m_Post;
#ifndef COSMIC_2D_ONLY
		ShadowMap        m_Shadow;

		// K12 — lazily created on the first outlined frame; zero cost while the
		// setting stays off (the compat default). ScenePicker is 3D-only, so the
		// 2D build has no outline path at all — sprites never had one.
		Ref<ScenePicker> m_OutlineMask;
		Ref<Shader>      m_OutlineShader;
		bool             m_OutlineShaderTried = false;
#endif

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
