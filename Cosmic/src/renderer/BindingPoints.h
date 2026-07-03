#pragma once

// BindingPoints.h
// Last Modified: 7/2/2026

/**
 * ============================================================================
 * COSMIC ENGINE — GPU buffer binding-point registry
 * ============================================================================
 *
 * Single source of truth for every UBO/SSBO binding index the engine reserves.
 * GLSL cannot consume these constants — shaders hardcode the number in their
 * `layout(..., binding = N)` — so each entry names the shader-side owner and
 * any new block MUST claim its slot here first. This registry is also the seed
 * for a future backend's descriptor-set layout (doc 05 §0 / S13.2).
 *
 * Rules:
 *  - One owner per slot. Engine systems allocate from the top of this file;
 *    apps use the App* slots (the engine never binds those).
 *  - UBO and SSBO indices are separate namespaces in GL — overlap between the
 *    two tables is fine; overlap within one is not.
 * ============================================================================
 */

#include <cstdint>

namespace Cosmic::Bindings
{
	// ------------------------------------------------------------------
	// UBO (std140) binding points
	// ------------------------------------------------------------------

	/** Renderer3D scene-lights block — GpuLightsBlock ↔ `LightsBlock` in
	 *  MeshLit.glsl (and any future lit shader). Uploaded by Renderer3D::SetLights. */
	constexpr uint32_t LightsUbo = 0;

	/** Per-frame camera block (S6.2) — GpuCameraBlock ↔ `CameraBlock` (instance
	 *  name `u_Camera`) in every 3D shader. View-projection + camera position;
	 *  time/viewport get added with their first consumer (SSAO/fog). Uploaded by
	 *  Renderer3D::BeginScene, replacing the old per-draw loose u_ViewProjection /
	 *  u_CameraPos uniforms. */
	constexpr uint32_t CameraUbo = 1;

	// ------------------------------------------------------------------
	// SSBO (std430) binding points
	// ------------------------------------------------------------------

	/** App/demo-owned scratch slot (e.g. the Engine3DDemo compute-particle pool).
	 *  The engine never binds SSBOs here; future engine systems (S9 FFT water,
	 *  S10 GPU particles) will claim numbered slots above the app range. */
	constexpr uint32_t AppSsbo0 = 0;
}
