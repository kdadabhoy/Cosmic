#pragma once
// voxel/VoxelGenerator.h
//
// ============================================================================
// Cosmic voxel worlds — procedural chunk generation (Phase 18 / V6).
// ============================================================================
//
// A deterministic recipe (noise stack -> heightmap terrain + optional caves +
// surface blocks by height) that fills ONE ungenerated chunk. Built on the
// engine Noise toolkit (math/Noise.h) so a given (seed, chunk) always yields the
// same blocks — regenerating with the same seed is byte-identical (headless
// acceptance). The recipe is a plain struct; its fields are FLATTENED onto
// VoxelVolumeComponent (the E18 pattern used by TerrainComponent), and
// BuildVoxelRecipe maps the component back to one of these.
//
// GenerateChunk fills the chunk's block array directly and marks it dirty; it
// touches no other chunk (the streaming layer marks resident neighbours so their
// shared seams re-cull). Pure aside from writing the target chunk — headless.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
    class VoxelVolume;

    struct VoxelGeneratorRecipe
    {
        bool     Enabled = false;        // stream-generate ungenerated chunks in ViewRadius

        uint32_t Seed          = 1337;
        float    SurfaceLevel  = 32.0f;  // average ground height, in VOXELS (world Y)
        float    Amplitude     = 24.0f;  // +/- height variation, in voxels
        float    Frequency     = 0.010f; // noise frequency, per voxel
        int32_t  Octaves       = 5;
        float    Lacunarity    = 2.0f;
        float    Gain          = 0.5f;
        bool     Ridged        = false;  // ridged multifractal (mountains) vs. fBm hills

        float    CaveThreshold = 0.0f;   // 0 = no caves; else carve where 3D noise exceeds it
        float    CaveFrequency = 0.05f;

        int32_t  DirtDepth     = 4;      // voxels of dirt below the grass surface
        float    SandLevel     = -1.0e9f;// surface at/below this height is sand (shores); off by default

        // Block ids into the volume's palette (the ForgeBlocks default: 1..4).
        uint16_t GrassBlock = 1;
        uint16_t DirtBlock  = 2;
        uint16_t StoneBlock = 3;
        uint16_t SandBlock  = 4;
    };

    class COSMIC_API VoxelGenerator
    {
    public:
        /** @brief Fill `chunk` of `volume` from `recipe` (all-air chunk overwritten),
         *  then mark it dirty. Deterministic in (recipe.Seed, chunk). */
        static void GenerateChunk(VoxelVolume& volume, const glm::ivec3& chunk,
                                  const VoxelGeneratorRecipe& recipe);

        /** @brief Parameter-signature hash of the recipe (drives regen on change). */
        static std::size_t Signature(const VoxelGeneratorRecipe& recipe);
    };
}
