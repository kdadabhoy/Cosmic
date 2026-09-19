#pragma once

// SceneRenderer.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — SceneRenderer (engine-owned frame orchestration)  [F2]
 * ============================================================================
 *
 * The engine-owned frame sequence every host drives through the same call —
 * PlayerLayer, Starforge and the render tests alike. A world declares WHAT to
 * render by filling a SceneRenderDesc (camera, clock, settings and the
 * DrawTransparent / DrawOverlay2D callbacks); the SceneRenderer owns HOW (the
 * pass order + every render-state contract of docs/design/frame-lifecycle.md
 * §5):
 *
 *   BeginHDR (PassOpaqueHDR) -> sprites + 2D lights via DrawTransparent
 *   (PassTransparents) -> tonemap/FXAA/bloom/vignette (PassPostAndComposite)
 *   -> DrawOverlay2D for canvas UI
 *
 * OWNED GPU SUBSYSTEM: the PostProcessStack (HDR target + SSAO/bloom/FXAA/fog/
 * vignette/heat-haze/underwater/lens-flare). Init() needs a live GL context;
 * Shutdown() before context teardown.
 *
 * WIRING (host-side, per frame — see PlayerLayer / StarforgeApp):
 *
 *   renderer.SetViewportSize(w, h);
 *   SceneRenderDesc desc;
 *   scene.BuildRenderDesc(camera, dt, desc);          // camera + clock
 *   if (auto* env = scene.FindEnvironment())
 *       renderer.ApplyEnvironment(*env, desc);        // exposure + post toggles
 *   desc.DrawTransparent = [&](const SceneDrawContext& c)
 *       { scene.OnRenderSprites(c.ViewProjection, w, h); scene.OnRender2DLights(c.ViewProjection, w, h); };
 *   // PRE: the final LDR (viewport) FBO is bound.
 *   renderer.Render(desc);
 *   // POST: the same FBO is re-bound, viewport (0,0,w,h), depth ON/ON,
 *   //       cull None, blend Alpha (engine defaults restored).
 *
 * History: F2 (Phase 11) promoted the multi-pass 3D frame apps used to
 * copy-paste from Engine3DDemo — shadow depth -> planar reflection -> opaque
 * HDR -> transparents (water/particles/ribbons) -> post + composite — into this
 * class, which also owned the EnvironmentMap (IBL + skybox) and the ShadowMap
 * (the directional sun map). Phase 29 / W6 fenced everything that needed a 3D
 * subsystem (the light gather, terrain/water/particles/ribbons, the sky + IBL
 * environment, the shadow and coverage depth passes, the planar reflection,
 * the routed opaque submit via Renderer3D and the ScenePicker selection
 * outline) behind COSMIC_2D_ONLY so that 2D would not get a second compositor,
 * and AP-05 purged that half. The spine above is what always ran on the 2D
 * engine, unchanged; the 3D-shaped remnants (ScenePass's depth/reflection
 * values, the sky/IBL/shadow settings) stay for source compatibility.
 * ============================================================================
 */

#include "core/Core.h"
#include "renderer/PostProcessStack.h" // owned by value (the 2D post chain too)

#include <glm/glm.hpp>
#include <entt/entt.hpp>               // K12 — SceneRenderDesc::SelectedEntities handles

#include <cstdint>
#include <functional>
#include <vector>

namespace Cosmic
{
	class Camera;
	class Scene;
	class FrameBuffer;             // X7 — RenderToTexture target
	struct EnvironmentComponent;   // E4 — ApplyEnvironment maps it into a desc

	/**
	 * @brief Which pass a draw callback is servicing. Switches on this MUST keep
	 * a default: arm. Only Main is handed out on this trunk (PassTransparents);
	 * ShadowDepth / Reflection / TopDownDepth are the 3D pass set the enum was
	 * born with (History: the sun-shadow, planar-reflection and snow-coverage
	 * depth passes) and stay for source compatibility.
	 */
	enum class ScenePass : uint8_t { ShadowDepth = 0, Reflection, Main, TopDownDepth };

	/**
	 * @brief Handed to SceneRenderDesc::DrawTransparent once per frame: Pass ==
	 * Main, the main camera's view-projection and eye. The 2D sprite path draws
	 * from it with the HDR target still bound.
	 */
	class COSMIC_API SceneDrawContext
	{
	public:
		ScenePass Pass = ScenePass::Main;
		glm::mat4 ViewProjection{ 1.0f };   // lightVP / mirrored-oblique VP / camera VP
		glm::vec3 EyePosition{ 0.0f };      // this pass's eye (mirrored under Reflection)
		glm::vec3 CameraPosition{ 0.0f };   // ALWAYS the real camera — LOD decisions use this

		bool IsDepthOnly() const { return Pass == ScenePass::ShadowDepth || Pass == ScenePass::TopDownDepth; }

	private:
		friend class SceneRenderer;
	};

	/**
	 * @brief Per-frame feature toggles + tuning. Defaults keep the cheap path;
	 * app policy owns the scenario values (nothing scenario-shaped in the engine).
	 */
	struct SceneRendererSettings
	{
		// Skybox / IBL / Shadows / WaterReflections are 3D-era toggles kept so hosts
		// and EnvironmentComponent can still name them; nothing reads them here.
		bool Skybox = true; bool IBL = true; bool Shadows = true; bool WaterReflections = true;
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

		float TimeSeconds = 0.0f; float Exposure = 1.0f;
		SceneRendererSettings Settings;

		Scene* EcsScene = nullptr;                            // Main only (not Reflection)

		// DeltaTime is NOT fenced (§7.6 left the call at implementation): it is the
		// frame delta, not a coverage parameter. BuildRenderDesc writes it on both
		// engines and the render-test fixtures set it, so a 2D build has to be able
		// to name it — only the coverage pass above ever READS it.
		float            DeltaTime           = 0.0f;   // seconds since last frame

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

		void Init(uint32_t width, uint32_t height);
		void Shutdown();
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

		PostProcessStack& GetPostStack()   { return m_Post; }

		/**
		 * @brief Map a scene's EnvironmentComponent (E4) into a SceneRenderDesc:
		 * writes the exposure and the fog / post-chain toggles into desc.Settings.
		 * The editor/PlayerLayer call this each frame before Render() for the
		 * scene's single "Environment" entity. Generic verb — no editor or
		 * Starforge concepts leak in. The component's sun and sky fields are
		 * carried for scene compatibility; they have nowhere to land in a 2D frame.
		 */
		void ApplyEnvironment(const EnvironmentComponent& env, SceneRenderDesc& desc);

	private:
		// One method per pass = F3's GPU-zone hook points.
		void PassOpaqueHDR(const SceneRenderDesc& desc);
		void PassTransparents(const SceneRenderDesc& desc);
		void PassPostAndComposite(const SceneRenderDesc& desc);
		PostProcessStack m_Post;

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
