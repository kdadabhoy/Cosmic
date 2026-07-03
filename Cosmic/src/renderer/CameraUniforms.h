#pragma once

// CameraUniforms.h
// Last Modified: 7/3/2026

/**
 * ============================================================================
 * COSMIC ENGINE — per-frame camera UBO mirror (S6.2)
 * ============================================================================
 *
 * The std140 C++ mirror of the `CameraBlock` uniform block that every 3D shader
 * reads from (binding: Bindings::CameraUbo = 1). Renderer3D::BeginScene packs +
 * uploads one of these once per pass, REPLACING the old per-draw loose
 * u_ViewProjection / u_CameraPos uniforms (deferred here from S6.1 — see doc 05
 * §5). One upload per BeginScene instead of two setters per DrawMesh, and every
 * lit/mesh/line/particle shader shares one source of camera truth.
 *
 * std140 discipline (identical to GpuLightsBlock): vec4-only after the mat4 — a
 * bare vec3 member would silently misalign everything following it. The GLSL
 * block uses an INSTANCE NAME (`u_Camera`) so members are accessed as
 * `u_Camera.ViewProjection`; this is deliberate — it keeps the literal string
 * "u_ViewProjection" out of the source so OpenGLShader::PreProcess does not
 * inject a colliding loose `uniform mat4 u_ViewProjection;` (the same injection
 * trap that broke the first S6.1 tonemap shader).
 *
 *   GLSL (identical block in every stage/shader that reads it):
 *     layout(std140, binding = 1) uniform CameraBlock {
 *         mat4 ViewProjection;
 *         vec4 CameraPosition;   // xyz = world-space camera position, w reserved
 *     } u_Camera;
 *
 * Time / viewport-size fields are intentionally NOT here yet — they get added
 * (and the static_assert size bumped) alongside their first consumer (SSAO / fog
 * in S6.5 / S7.2), rather than shipping as always-zero dead fields today.
 * ============================================================================
 */

#include <glm/glm.hpp>

namespace Cosmic
{
	struct GpuCameraBlock
	{
		glm::mat4 ViewProjection{ 1.0f };   // 64 bytes
		glm::vec4 CameraPosition{ 0.0f };   // xyz world pos, w reserved (16 bytes)
	};
	static_assert(sizeof(GpuCameraBlock) == 80,
		"GpuCameraBlock must match the std140 CameraBlock (80 bytes).");
}
