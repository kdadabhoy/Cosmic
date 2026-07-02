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
 * FUTURE (documented slots, not yet implemented):
 *   - DrawMesh(mesh, transform, Ref<Material>) — S4.2 material-driven meshes
 *     via Material::BindFull(); uniform names stay u_Model / u_ViewProjection /
 *     u_CameraPos so Mesh3D.glsl-family shaders keep working.
 *   - WorldToScreen(vec3) for SDF-font labels — S3.
 * ============================================================================
 */

#include "core/Core.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace Cosmic
{
	class PerspectiveCamera;

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
		 * BeginScene captures the camera state for this pass and resets the line
		 * batch. EndScene flushes all batched geometry. The (mat4, vec3) overload
		 * is the camera-agnostic primitive (contract rule 1).
		 */
		static void BeginScene(const glm::mat4& viewProjection, const glm::vec3& cameraPos);
		static void BeginScene(const PerspectiveCamera& camera);   // sugar on the primitive
		static void EndScene();

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

		////////////////////////////////
		// Mesh Submission (Lambert)
		///////////////////////////////

		/**
		 * @brief Draw a mesh with the built-in Lambert shader (one directional
		 * light + ambient floor) and a flat per-draw color.
		 *
		 * Immediate: one GPU draw per call — sim scenes are tens of meshes, so
		 * batching would add complexity for nothing. A material overload
		 * (Ref<Material>) is the planned S4 extension of this exact slot.
		 */
		static void DrawMesh(const Ref<Mesh>& mesh, const glm::mat4& transform, const glm::vec4& color);

		////////////////////////////////
		// Scene Lighting (S2 scope: one directional light)
		///////////////////////////////

		/** @brief Direction the light TRAVELS (normalized internally). Default: (-0.4, -1, -0.25). */
		static void SetLightDirection(const glm::vec3& direction);
		static const glm::vec3& GetLightDirection();

		/** @brief Ambient floor in [0, 1] — the lit level of a face turned away from the light. */
		static void  SetAmbient(float ambient);
		static float GetAmbient();
	};
}
