#pragma once

// scene/WorldSystemRecipes.h
//
// ============================================================================
// Cosmic world-system authoring recipes (Phase 13 / E18).
// ============================================================================
//
// Pure mapping from the reflected recipe fields carried by TerrainComponent /
// WaterComponent / ParticleEmitterComponent (scene/Components.h) onto the
// engine spec structs their factories consume (TerrainSpecification /
// WaterSpecification / ParticleEmitterSpec), plus a parameter-signature hash per
// recipe so Scene::SyncWorldSystems can detect a change without a dirty flag
// (the E15 PrimitiveSignature pattern).
//
// The Build* functions are GL-FREE and VFS-FREE (headless-testable): AssetPath
// fields (heightmap, splat albedos, particle texture) are NOT resolved here —
// the caller resolves them on the main thread after building the spec, since
// AssetLibrary::GetTexture and FileSystem::Resolve are main-thread only. The
// signature hashes DO include the path strings, so changing a path still
// triggers a rebuild.
// ============================================================================

#include "core/Core.h"
#include "scene/Components.h"
#include "scene/Components3D.h"   // W4 — Terrain/Water/ParticleEmitter recipe sources
#include "terrain/Terrain.h"
#include "water/Water.h"
#include "particles/ParticleSystem.h"

#include <cstddef>
#include <cstdint>

namespace Cosmic
{
	// --- Terrain -------------------------------------------------------------

	/** @brief Snap an arbitrary resolution to the nearest VALID terrain
	 *  resolution 32*2^k + 1 in [65, 1025] (Terrain::Create rejects others). */
	COSMIC_API uint32_t ClampTerrainResolution(int32_t resolution);

	/** @brief Recipe -> TerrainSpecification (Resolution clamped). Layer albedo
	 *  Refs are left null and HeightmapPath is copied verbatim — the caller
	 *  resolves VFS/GL assets on the main thread before Terrain::Create. */
	COSMIC_API TerrainSpecification BuildTerrainSpec(const TerrainComponent& c);

	/** @brief Hash of every terrain recipe field (excludes UseRecipe / runtime). */
	COSMIC_API std::size_t TerrainRecipeSignature(const TerrainComponent& c);

	/** @brief Resolve a terrain recipe's VFS/GL assets INTO a built spec (main
	 *  thread only): HeightmapPath -> filesystem path, splat AssetPaths ->
	 *  layer albedo Refs via AssetLibrary. Kept separate from BuildTerrainSpec so
	 *  the latter stays headless-pure; call it after BuildTerrainSpec on the main
	 *  thread, then Terrain::Create off-thread (Create itself is GL-free). */
	COSMIC_API void ResolveTerrainSpecAssets(const TerrainComponent& c, TerrainSpecification& spec);

	// --- Water ---------------------------------------------------------------

	/** @brief Recipe -> WaterSpecification: seeds the wave stack from the Preset
	 *  (water/Presets.h), then overrides center/extent/height/optics. Amplitude
	 *  and Choppiness scale the preset waves. GL-free. */
	COSMIC_API WaterSpecification BuildWaterSpec(const WaterComponent& c);

	/** @brief Hash of every water recipe field (excludes UseRecipe / runtime). */
	COSMIC_API std::size_t WaterRecipeSignature(const WaterComponent& c);

	// --- Particles -----------------------------------------------------------

	/** @brief Recipe -> ParticleEmitterSpec. spec.Texture is left null — the
	 *  caller resolves TexturePath via AssetLibrary::GetTexture on the main
	 *  thread before ParticleEmitter::Create. GL-free. */
	COSMIC_API ParticleEmitterSpec BuildEmitterSpec(const ParticleEmitterComponent& c);

	/** @brief Hash of every emitter recipe field (excludes UseRecipe / runtime). */
	COSMIC_API std::size_t EmitterRecipeSignature(const ParticleEmitterComponent& c);
}
