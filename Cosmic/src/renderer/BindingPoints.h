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

	/** Scene-lights block — `LightsBlock` in MeshLit.glsl (and any future lit
	 *  shader). History: uploaded by the 3D renderer's SetLights (GpuLightsBlock,
	 *  purged in AP-05); reserved while the shader still declares it. */
	constexpr uint32_t LightsUbo = 0;

	/** Per-frame camera block (S6.2) — History: the GpuCameraBlock mirror (renderer/CameraUniforms.h) was removed by AP-Q1 with nothing uploading it;
	 *  ↔ `CameraBlock` (instance name `u_Camera`) in the lit mesh shaders.
	 *  View-projection + camera position. History: the 3D renderer's BeginScene
	 *  uploaded it once per pass, replacing the old per-draw loose
	 *  u_ViewProjection / u_CameraPos uniforms. */
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

	/** Skinning-matrix palette (Phase 20 / A2) — a std430 mat4 array holding
	 *  every skinned draw's joint palette for the frame, read by
	 *  PBRSkinned.glsl at `u_SkinBase + joint`. History: the 3D renderer
	 *  uploaded the queued draws' palettes at Flush and the shadow caster path
	 *  per caster at base 0 (both purged in AP-05). */
	constexpr uint32_t SkinningSsbo = 10;

	// ------------------------------------------------------------------
	// Reserved fragment texture units
	// ------------------------------------------------------------------
	// Sampler units reserved for the ENGINE behind every material draw (History:
	// the 3D renderer injected them after Material::BindFull). Chosen high so a
	// material's own textures (bound from unit 0 upward) never collide; GL
	// guarantees >= 16 fragment units. Shaders receive these via their sampler
	// uniforms — the numbers here are the single source of truth. A future
	// backend maps this table to a per-frame descriptor set (S13.2).

	/** S6.3 IBL — diffuse irradiance cubemap (`u_IrradianceMap`). */
	constexpr uint32_t TexUnitIblIrradiance = 8;
	/** S6.3 IBL — prefiltered specular cubemap (`u_PrefilterMap`). */
	constexpr uint32_t TexUnitIblPrefilter = 9;
	/** S6.3 IBL — split-sum BRDF LUT (`u_BrdfLut`). */
	constexpr uint32_t TexUnitIblBrdfLut = 10;
	// Unit 11 is reserved: History: it carried the S6.4 directional sun shadow
	// map (`u_ShadowMap`, the deleted TexUnitShadowMap) and PBR.glsl /
	// PBRSkinned.glsl / MeshLit.glsl still declare that sampler.

	/** S11.1 (doc 10 F8) — snow coverage mask (`u_SnowMaskMap`; RG = coverage +
	 *  encoded top-surface Y). History: pushed by the 3D renderer's SetSnow via
	 *  ApplySceneBindings to PBR / PBRInstanced / Terrain (purged in AP-05);
	 *  reserved while PBR.glsl still declares the sampler. */
	constexpr uint32_t TexUnitSnowMask = 12;

	/** K12 — the selection-outline id mask (`u_IdMask`, isampler2D): the
	 *  ScenePicker's RED_INTEGER attachment, bound by SceneRenderer::PassOutline
	 *  for the Outline.glsl composite only. */
	constexpr uint32_t TexUnitOutlineMask = 13;

	// F2 SceneRenderer claims NO other slots: it orchestrates the PostProcessStack
	// passes, which own their bindings shader-side. (History: the 3D renderer, the
	// environment map and the shadow map passes owned the rest of the table.)
}
