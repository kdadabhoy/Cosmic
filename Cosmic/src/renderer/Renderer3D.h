#pragma once

// Renderer3D.h
// Last Modified: 7/1/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Renderer3D (sim-grade 3D viewport renderer)
 * ============================================================================
 *
 * The 3D sibling of Renderer2D: batched debug/engineering line drawing (grid,
 * axes, wire boxes, trajectories) plus immediate-mode mesh submission with
 * Lambert shading. Built for simulator viewports first, but every design
 * decision follows doc 05's FORWARD-COMPATIBILITY CONTRACT so the same API
 * grows into the full 3D engine tier (materials, lighting v1, glTF) without
 * a rewrite:
 *
 *   1. CAMERA-AGNOSTIC CORE — BeginScene(const glm::mat4& viewProjection,
 *      const glm::vec3& cameraPos) is the primitive; the PerspectiveCamera
 *      overload is sugar. Any future camera type works without touching this.
 *   2. Render-state changes go through RenderCommand's generic verbs
 *      (SetDepthTest / SetDepthWrite) — never raw GL calls.
 *   3. Meshes are first-class Ref<Mesh> GPU resources (graphics/Mesh.h) with
 *      the canonical vertex layout (position, normal, uv).
 *   4. Transforms are glm::mat4 — attitude quaternions (math/Spatial.h) are
 *      converted upstream, so ECS integration later is a call-site change.
 *   5. NO 2D REGRESSIONS — Renderer3D restores any state Renderer2D depends
 *      on; both coexist in one frame (3D world + 2D overlays).
 *
 * USAGE (inside a layer's OnUpdate, viewport FBO already bound + cleared):
 *
 *   Renderer3D::BeginScene(m_Orbit.GetCamera());
 *   Renderer3D::DrawGrid(20.0f, 1.0f, gridColor);
 *   Renderer3D::DrawAxes(glm::mat4(1.0f), 1.0f);
 *   Renderer3D::DrawMesh(m_Fuselage, transform, { 0.8f, 0.2f, 0.2f, 1.0f });
 *   Renderer3D::EndScene();
 *
 *   // 2D overlay afterwards, exactly as usual:
 *   Renderer2D::BeginScene(m_UiCamera); ... Renderer2D::EndScene();
 *
 * Complete the 3D scene before starting a 2D pass (or vice versa) — the two
 * renderers keep separate view-projection state by design (mirrors the
 * Renderer/Renderer2D separation documented in Renderer.h).
 *
 * SHIPPED EXTENSIONS (Phase 7 / S4): DrawMesh(mesh, transform, Ref<Material>)
 * material path (S4.2), DrawModel + glTF import (S4.4b), SetLights + the
 * std140 lights UBO (S4.5, binding: Bindings::LightsUbo), and per-draw
 * entity IDs on every submission verb for MRT picking (S4.6).
 *
 * RENDER QUEUE (Phase 12 / S12.1–S12.3) — mesh submission is DEFERRED:
 * DrawMesh/DrawModel record into a per-scene queue; Flush()/EndScene() sorts
 * and executes it. What that means for callers:
 *
 *   - FRUSTUM CULLING (S12.1): submissions whose world AABB (mesh local bounds
 *     x transform) misses the pass frustum are dropped at submit — free wins
 *     for every caller. SetFrustumCullingEnabled(false) opts out (e.g. a
 *     vertex-displacing custom shader whose motion can exceed the mesh AABB).
 *   - SORTING (S12.2): opaques execute grouped by shader -> material -> mesh,
 *     then front-to-back; TRANSPARENT materials (Material::SetTransparent)
 *     draw after all opaques, back-to-front, with depth writes off (test on)
 *     under the default Alpha blend. Submission order within one scene is NO
 *     LONGER the draw order.
 *   - VALUE CAPTURE: transform/color/entityID are captured per call; the
 *     MATERIAL is captured by reference and its values are read at flush —
 *     mutating one material between draws no longer gives per-draw variation
 *     (use Material::Clone per variant; see Engine3DDemo's PBR grid). Scene
 *     state (lights, IBL/shadow/snow sets) is likewise read at flush.
 *   - STATE ISLANDS: a caller that must draw under custom render state calls
 *     Flush() while its state is applied, then restores (engine defaults are
 *     the flush-time contract). Prefer SetTransparent — it removes the manual
 *     depth-write juggling entirely.
 *   - AUTO-INSTANCING (S12.3): runs of >= 4 identical (mesh, material) opaque
 *     submissions with entityID == -1 whose material registered an instancing
 *     twin (Material::SetInstancingShader, e.g. PBRInstanced.glsl) collapse
 *     into one hardware-instanced draw. Statistics reports the win.
 *
 * FUTURE (documented slots, not yet implemented):
 *   - WorldToScreen(vec3) for SDF-font labels — S3.5.
 * ============================================================================
 */

#include "core/Core.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace Cosmic
{
	class Camera;
	class PerspectiveCamera;
	class Material;
	class Model;
	class Shader;
	class InstanceSet;

	class COSMIC_API Renderer3D
	{
	public:
		/**
		 * SYSTEM LIFECYCLE
		 * Called by Renderer::Init/Shutdown alongside Renderer2D — client code
		 * never calls these.
		 */
		static void Init();
		static void Shutdown();

		/**
		 * SCENE BOUNDARIES
		 * BeginScene captures the camera state for this pass, extracts the pass
		 * frustum (S12.1), and resets the line batch + mesh queue. EndScene
		 * flushes the mesh queue (sorted — see header note) and then the batched
		 * lines. The (mat4, vec3) overload is the camera-agnostic primitive
		 * (contract rule 1).
		 */
		static void BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPos);
		static void BeginScene(const PerspectiveCamera& camera);   // sugar on the primitive
		static void BeginScene(const Camera& camera);              // camera-agnostic sugar (S4.1)
		static void EndScene();

		/**
		 * @brief Execute + clear the queued mesh submissions NOW, under the
		 * CURRENT render state (S12.2 state-island escape hatch — see header
		 * note). Opaques draw sorted/auto-instanced first, then transparents
		 * back-to-front with depth writes off (restored to the engine default ON
		 * afterwards). EndScene calls this automatically; mid-scene calls are
		 * only needed around custom render state.
		 */
		static void Flush();

		////////////////////////////////
		// Batched Line Primitives
		///////////////////////////////

		/** @brief World-space line segment (batched; flushed at EndScene). */
		static void DrawLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color);

		/** @brief Connected line strip through `count` points (count-1 segments). */
		static void DrawPolyline(const glm::vec3* points, size_t count, const glm::vec4& color);
		static void DrawPolyline(const std::vector<glm::vec3>& points, const glm::vec4& color);

		/**
		 * @brief Ground-reference grid on the XZ plane (y = 0), centered on the
		 * origin, spanning ±extent with a line every `step`.
		 */
		static void DrawGrid(float extent, float step, const glm::vec4& color);

		/**
		 * @brief Grid with emphasized major lines every `majorEvery` steps
		 * (axis-through-origin lines use the major color too).
		 */
		static void DrawGrid(float extent, float step,
		                     const glm::vec4& minorColor, const glm::vec4& majorColor,
		                     int majorEvery = 5);

		/**
		 * @brief RGB axis tripod at a transform: +X red, +Y green, +Z blue,
		 * each `size` long. Pass a model matrix to visualize any object frame.
		 */
		static void DrawAxes(const glm::mat4& transform, float size = 1.0f);

		/** @brief Unit cube (±0.5) wireframe under `transform` — AABBs, bounds, zones. */
		static void DrawWireBox(const glm::mat4& transform, const glm::vec4& color);

		/**
		 * @brief Infinite editor grid (K10): a single fullscreen-triangle draw
		 * whose fragment shader ray-casts the pixel onto the y = Height plane —
		 * no extent, decade cell switching with distance, fwidth-antialiased
		 * lines, axis highlighting, and a camera-height-scaled distance fade.
		 * FadeDistance 0 = auto. Draws IMMEDIATELY (not queued) with depth test
		 * ON / depth write OFF (restored) under the default alpha blend, so
		 * scene geometry occludes it and it occludes nothing. Call between
		 * BeginScene/EndScene. Default-off by nature — nothing draws unless an
		 * app calls it (DrawGrid is untouched; compat gate holds).
		 */
		struct InfiniteGridDesc
		{
			float     Height = 0.0f;
			glm::vec4 MinorColor{ 0.30f, 0.32f, 0.36f, 0.55f };
			glm::vec4 MajorColor{ 0.45f, 0.47f, 0.52f, 0.85f };
			glm::vec4 AxisXColor{ 0.86f, 0.24f, 0.24f, 0.95f };   // z == 0 line
			glm::vec4 AxisZColor{ 0.25f, 0.45f, 0.90f, 0.95f };   // x == 0 line
			float     FadeDistance = 0.0f;                        // 0 = auto
		};
		static void DrawInfiniteGrid(const InfiniteGridDesc& desc = {});

		////////////////////////////////
		// Mesh Submission (Lambert)
		///////////////////////////////

		/**
		 * @brief Draw a mesh with the built-in Lambert shader (one directional
		 * light + ambient floor) and a flat per-draw color.
		 *
		 * Queued (S12.2): recorded now — frustum-tested (S12.1), then sorted and
		 * executed at Flush()/EndScene(). The material overload below is the
		 * custom-shader path.
		 */
		// indexOffset/indexCount (M5): draw only one submesh range (elements) of
		// the mesh. Default 0/0 ⇒ the whole mesh, byte-identical to before. The
		// per-submesh path submits one queue entry per slot so sort/cull still
		// apply; ranged draws never auto-instance.
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color,
		                     int entityID = -1, uint32_t indexOffset = 0, uint32_t indexCount = 0);

		/**
		 * @brief Draw a mesh with a custom Material (S4.2 material-driven path).
		 *
		 * The material owns its shader and per-material uniforms (u_Color,
		 * textures, tiling, …); the engine layers the scene/per-draw convention
		 * uniforms on top so any Material shader lights and transforms correctly:
		 *
		 *   u_ViewProjection (mat4) u_Model (mat4) u_NormalMatrix (mat3)
		 *   u_CameraPos (vec3)      u_LightDir (vec3) u_Ambient (float)
		 *
		 * Convention uniforms are uploaded AFTER the material binds, so they always
		 * win; a shader opts in by declaring the ones it uses (undeclared uniforms
		 * are silently ignored — OpenGLShader caches location -1). u_Color stays
		 * material-owned: this path never sets it.
		 */
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform,
		                     const Ref<Material>& material, int entityID = -1,
		                     uint32_t indexOffset = 0, uint32_t indexCount = 0);

		/**
		 * @brief Draw every part of an imported Model (S4.4b) under `transform`,
		 * each tinted by its glTF base color via the Lambert color path. Part
		 * geometry already has its glTF world transform baked in — `transform`
		 * places the whole model.
		 */
		static void DrawModel(const Ref<Model>& model, const glm::mat4& transform, int entityID = -1);

		/**
		 * @brief Skinned mesh draw (Phase 20 / A2). `palette` holds `jointCount`
		 * skinning matrices (Skeleton::ComputePalette's output for this pose) —
		 * COPIED into the frame's palette staging, uploaded once into the SSBO at
		 * Bindings::SkinningSsbo by Flush, and consumed by the material's SKINNED
		 * twin (Material::SetSkinnedShader, e.g. PBRSkinned.glsl) at this draw's
		 * u_SkinBase. Falls back to a static bind-pose draw when the material has
		 * no twin or the palette is empty (the compat default). Queued like every
		 * DrawMesh; never auto-instanced. Call between BeginScene/EndScene.
		 */
		static void DrawMeshSkinned(const Ref<Mesh>& mesh, const glm::mat4& transform,
		                            const Ref<Material>& material,
		                            const glm::mat4* palette, uint32_t jointCount, int entityID = -1);

		/**
		 * @brief Hardware-instanced mesh draw (S12.3-lite / doc 10 F5). Draws
		 * `count` copies of `mesh` (clamped to the InstanceSet's uploaded count),
		 * reading per-instance `{ mat4 Model; vec4 Tint }` from the SSBO bound at
		 * Bindings::InstancesSsbo. The material's shader MUST be the instanced
		 * variant (PBRInstanced.glsl) — there is no per-draw u_Model/u_NormalMatrix;
		 * the SSBO replaces them. The scene IBL/shadow set is applied as usual.
		 * Call between BeginScene/EndScene.
		 */
		static void DrawMeshInstanced(const Ref<Mesh>& mesh, const Ref<Material>& material,
		                              const Ref<InstanceSet>& instances, uint32_t count, int entityID = -1);

		////////////////////////////////
		// Frustum culling (S12.1)
		///////////////////////////////

		/**
		 * @brief Enable/disable the per-submission frustum test (default ON).
		 * Sticky global — not per scene. Turn OFF only for content whose shader
		 * moves vertices beyond the mesh's static AABB. Explicit-instanced draws
		 * (DrawMeshInstanced) are never engine-culled: the app culls those once
		 * against the MAIN frustum per the F5 policy.
		 */
		static void SetFrustumCullingEnabled(bool enabled);
		static bool IsFrustumCullingEnabled();

		////////////////////////////////
		// Telemetry (S12 — mirrors Renderer2D::Statistics)
		///////////////////////////////

		/**
		 * @brief Frame counters. Apps call ResetStats() once per frame (before
		 * the first pass) and read GetStats() when presenting — counters
		 * accumulate across every scene/pass in between, exactly like
		 * Renderer2D's. The cull + auto-instance counters are the S12.1/S12.3
		 * acceptance evidence.
		 */
		struct Statistics
		{
			uint32_t DrawCalls          = 0;  // GPU mesh draws issued (indexed + instanced)
			uint32_t MeshesSubmitted    = 0;  // DrawMesh / DrawModel-part submissions accepted
			uint32_t MeshesCulled       = 0;  // dropped by the frustum test (S12.1)
			uint32_t MeshesDrawn        = 0;  // single-draw submissions executed
			uint32_t AutoInstanceBatches = 0; // queue runs collapsed to one instanced draw (S12.3)
			uint32_t AutoInstancedMeshes = 0; // submissions drawn through those batches
			uint32_t ExplicitInstanceDraws = 0; // DrawMeshInstanced calls (F5 path)
			uint32_t ExplicitInstances     = 0; // instances drawn by those calls
		};

		static void       ResetStats();
		static Statistics GetStats();

		////////////////////////////////
		// Scene Lighting (S2 scope: one directional light)
		///////////////////////////////

		/** @brief Direction the light TRAVELS (normalized internally). Default: (-0.4, -1, -0.25).
		 *  As of H3 this also patches the sun fields of the LightsBlock UBO so the cheap Mesh3D
		 *  color path (which now reads that block, like MeshLit) stays coherent for callers that
		 *  use only this legacy setter (Engine3DDemo). Prefer SetLights for full scene lighting. */
		static void SetLightDirection(const glm::vec3& direction);
		static const glm::vec3& GetLightDirection();

		/** @brief Ambient floor in [0, 1] — the lit level of a face turned away from the light.
		 *  Also mirrored into the LightsBlock UBO (H3; see SetLightDirection). */
		static void  SetAmbient(float ambient);
		static float GetAmbient();

		////////////////////////////////
		// Lighting v1 (S4.5: sun + <= kMaxPointLights point lights, lights UBO)
		///////////////////////////////

		/** @brief Point-light capacity of the GPU lights block. Mirrored by the
		 *  literal 16s in MeshLit.glsl's LightsBlock arrays — change both together. */
		static constexpr uint32_t kMaxPointLights = 16;

		/** @brief One point light for the S4.5 lights block. */
		struct PointLightDesc
		{
			glm::vec3 Position{ 0.0f };
			float     Radius = 10.0f;
			glm::vec3 Color{ 1.0f };
			float     Intensity = 1.0f;
		};

		/** @brief The full scene lighting description uploaded by SetLights. */
		struct SceneLightsDesc
		{
			glm::vec3 SunDirection{ -0.4f, -1.0f, -0.3f };   // direction the sun light TRAVELS
			glm::vec3 SunColor{ 1.0f };
			float     SunIntensity = 1.0f;
			float     Ambient      = 0.25f;
			std::vector<PointLightDesc> Points;              // first kMaxPointLights uploaded
		};

		/**
		 * @brief Pack + upload the scene lights into the lights UBO immediately
		 * (binding: Bindings::LightsUbo). Shaders that declare the std140
		 * LightsBlock (see MeshLit.glsl) read it; the legacy Lambert path
		 * (Mesh3D.glsl) ignores it. Points beyond kMaxPointLights are dropped
		 * with a once-per-run warning.
		 */
		static void SetLights(const SceneLightsDesc& lights);

		////////////////////////////////
		// Image-based lighting (S6.3)
		///////////////////////////////

		/**
		 * @brief Register the IBL cubemaps + BRDF LUT (from renderer/EnvironmentMap)
		 * so every PBR material DrawMesh binds them (to reserved units) and enables
		 * its IBL ambient term. Pass renderer IDs, not Refs, to keep Renderer3D
		 * decoupled from EnvironmentMap. ClearIBL() disables it again.
		 */
		static void SetIBL(uint32_t irradianceCubeID, uint32_t prefilterCubeID,
		                   uint32_t brdfLutID, float prefilterMaxLod);
		static void ClearIBL();

		////////////////////////////////
		// Directional shadows (S6.4)
		///////////////////////////////

		/**
		 * @brief Register the sun's shadow map + light matrix (from renderer/ShadowMap)
		 * so every lit material DrawMesh samples it (PBR / MeshLit PCF-shadow the sun
		 * term). Pass the depth-texture renderer ID, not a Ref. ClearShadow() disables it.
		 */
		static void SetShadow(uint32_t shadowMapID, const glm::mat4& lightViewProj, float bias);
		static void ClearShadow();

		////////////////////////////////
		// Snow overlay (S11.1 / doc 10 F8)
		///////////////////////////////

		/**
		 * @brief Scene-wide snow overlay parameters (S11.1). Pushed by ApplySceneBindings
		 * to every PBR / PBRInstanced / Terrain draw (the `u_Snow*` uniforms). App
		 * policy owns the values — nothing scenario-shaped lives in the engine.
		 *
		 * With no mask texture (MaskTextureID == 0) coverage is UNIFORM: snow appears
		 * on up-facing surfaces above `Line`. A CoverageCapture mask (RG = coverage +
		 * encoded top-surface Y) restricts it to accumulated, sky-exposed columns so
		 * sheltered floors stay bare. `OverlayAmount` drives Terrain's mask-gated snow
		 * BELOW the snow line (blizzard build-up); it is a no-op on non-terrain shaders.
		 */
		struct SnowDesc
		{
			float     Amount     = 1.0f;   // global coverage scale [0,1]
			float     Line       = 30.0f;  // world Y where snow fades in
			float     BlendHalf  = 6.0f;   // altitude blend half-width (m)
			float     SlopeSharp  = 3.0f;  // N·up exponent (higher = flatter-only)
			glm::vec3 Color{ 0.93f, 0.95f, 0.98f };
			float     Sparkle    = 0.5f;   // micro-glint strength (0 = off)

			uint32_t  MaskTextureID = 0;   // 0 = uniform coverage (no mask)
			glm::vec2 MaskWorldMin{ 0.0f };     // world XZ min corner of the mask rect
			glm::vec2 MaskWorldInvSize{ 0.0f }; // 1 / world size (x, z)
			glm::vec2 MaskYDecode{ 0.0f };      // worldY = g * x + y
			float     MaskYTolerance = 0.5f;    // receiver-vs-top tolerance (m)

			float     OverlayAmount = 0.0f;     // terrain: mask-driven snow below the line
		};

		/** @brief Enable the scene-wide snow overlay with `desc`. Active until ClearSnow. */
		static void SetSnow(const SnowDesc& desc);
		/** @brief Disable the snow overlay (every draw goes back to byte-identical output). */
		static void ClearSnow();

		/**
		 * @brief Bind the scene-level lighting resources (S6.3 IBL set + S6.4
		 * shadow map, reserved units per renderer/BindingPoints.h) and set the
		 * convention uniforms (u_IrradianceMap/u_PrefilterMap/u_BrdfLut/u_HasIBL,
		 * u_ShadowMap/u_LightViewProj/u_ShadowBias/u_HasShadow) on `shader`.
		 *
		 * DrawMesh's material path calls this automatically; engine systems that
		 * draw with their OWN shaders (terrain S8, water S9) call it right after
		 * binding. Sampler units are assigned even when a resource is inactive so
		 * no sampler is left aliasing another sampler TYPE on unit 0 — that alias
		 * is a draw-time INVALID_OPERATION on strict GL drivers.
		 */
		static void ApplySceneBindings(const Ref<Shader>& shader);
	};
}
