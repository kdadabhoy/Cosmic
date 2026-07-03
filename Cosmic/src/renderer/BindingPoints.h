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
	 *  The engine never binds SSBOs in the app range [0, 7]; engine systems claim
	 *  numbered slots from 8 upward. */
	constexpr uint32_t AppSsbo0 = 0;

	/** Engine GPU-particle pool (S10.1) — ParticleSystem's std430 particle SSBO,
	 *  read/written by ParticleUpdate.glsl (compute) and read by the billboard
	 *  vertex stage. First slot of the engine SSBO range (8+). */
	constexpr uint32_t ParticlesSsbo = 8;

	/** Instanced-mesh pool (S12.3-lite / doc 10 F5) — InstanceSet's std430 array
	 *  of `{ mat4 Model; vec4 Tint; }` (80 bytes/instance), read by
	 *  PBRInstanced.glsl and ShadowDepthInstanced.glsl by gl_InstanceID. Uploaded
	 *  + bound by InstanceSet. */
	constexpr uint32_t InstancesSsbo = 9;

	// ------------------------------------------------------------------
	// Reserved fragment texture units
	// ------------------------------------------------------------------
	// Sampler units the ENGINE binds behind every material draw (Renderer3D
	// injects them after Material::BindFull). Chosen high so a material's own
	// textures (bound from unit 0 upward) never collide; GL guarantees >= 16
	// fragment units. Shaders receive these via their sampler uniforms — the
	// numbers here and the Renderer3D upload are the single source of truth.
	// A future backend maps this table to a per-frame descriptor set (S13.2).

	/** S6.3 IBL — diffuse irradiance cubemap (`u_IrradianceMap`). */
	constexpr uint32_t TexUnitIblIrradiance = 8;
	/** S6.3 IBL — prefiltered specular cubemap (`u_PrefilterMap`). */
	constexpr uint32_t TexUnitIblPrefilter = 9;
	/** S6.3 IBL — split-sum BRDF LUT (`u_BrdfLut`). */
	constexpr uint32_t TexUnitIblBrdfLut = 10;
	/** S6.4 — directional sun shadow map (`u_ShadowMap`). */
	constexpr uint32_t TexUnitShadowMap = 11;

	// F2 SceneRenderer claims NO new slots: it orchestrates the existing
	// Renderer3D / EnvironmentMap / ShadowMap / PostProcessStack passes, which
	// already own every binding above. (F5 instancing claims SSBO 9 above; F8
	// snow claims TexUnitSnowMask = 12.)
}
