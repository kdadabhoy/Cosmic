// voxel/VoxelGenerator.cpp — deterministic heightmap + cave chunk generation
// (Phase 18 / V6). Pure aside from writing the target chunk; headless-tested.

#include "voxel/VoxelGenerator.h"
#include "voxel/VoxelVolume.h"
#include "math/Noise.h"

#include <cmath>
#include <functional>

namespace Cosmic
{
    namespace
    {
        template<typename T>
        void HashCombine(std::size_t& seed, const T& v)
        {
            seed ^= std::hash<T>{}(v) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        }
    }

    void VoxelGenerator::GenerateChunk(VoxelVolume& volume, const glm::ivec3& chunk,
                                       const VoxelGeneratorRecipe& r)
    {
        constexpr int N = VoxelVolume::kChunkSize;
        const glm::ivec3 base = VoxelVolume::ChunkMinVoxel(chunk);

        // Two independent noise fields, both seeded off the recipe seed so a rerun
        // is bit-identical.
        const Noise height(r.Seed);
        const Noise cave(r.Seed ^ 0x5bd1e995u);

        std::vector<uint16_t>& blocks = volume.EmplaceChunk(chunk);

        const int octaves = r.Octaves < 1 ? 1 : r.Octaves;

        for (int lz = 0; lz < N; ++lz)
        {
            for (int lx = 0; lx < N; ++lx)
            {
                const int wx = base.x + lx;
                const int wz = base.z + lz;

                // Signed [-1,1] terrain sample.
                float n;
                if (r.Ridged)
                    n = height.Ridged2D(wx * r.Frequency, wz * r.Frequency, octaves, r.Lacunarity, r.Gain) * 2.0f - 1.0f;
                else
                    n = height.Fbm2D(wx * r.Frequency, wz * r.Frequency, octaves, r.Lacunarity, r.Gain);

                const float surfaceF = r.SurfaceLevel + r.Amplitude * n;
                const int   surfaceH = (int)std::floor(surfaceF);
                const bool  shore    = surfaceF <= r.SandLevel;

                for (int ly = 0; ly < N; ++ly)
                {
                    const int wy = base.y + ly;
                    if (wy > surfaceH)
                        continue;   // air above the surface

                    uint16_t block;
                    if (wy == surfaceH)      block = shore ? r.SandBlock : r.GrassBlock;
                    else if (wy > surfaceH - r.DirtDepth) block = shore ? r.SandBlock : r.DirtBlock;
                    else                     block = r.StoneBlock;

                    // Carve caves below the immediate surface.
                    if (r.CaveThreshold > 0.0f && wy < surfaceH - 1)
                    {
                        const float c = cave.Fbm3D(wx * r.CaveFrequency, wy * r.CaveFrequency,
                                                   wz * r.CaveFrequency, 3);
                        if (std::fabs(c) > (1.0f - r.CaveThreshold))
                            continue;   // hollow
                    }

                    blocks[VoxelVolume::LocalIndex(lx, ly, lz)] = block;
                }
            }
        }

        volume.MarkChunkDirty(chunk);
    }

    std::size_t VoxelGenerator::Signature(const VoxelGeneratorRecipe& r)
    {
        std::size_t s = 0;
        HashCombine(s, r.Seed);
        HashCombine(s, r.SurfaceLevel);
        HashCombine(s, r.Amplitude);
        HashCombine(s, r.Frequency);
        HashCombine(s, r.Octaves);
        HashCombine(s, r.Lacunarity);
        HashCombine(s, r.Gain);
        HashCombine(s, r.Ridged);
        HashCombine(s, r.CaveThreshold);
        HashCombine(s, r.CaveFrequency);
        HashCombine(s, r.DirtDepth);
        HashCombine(s, r.SandLevel);
        HashCombine(s, r.GrassBlock);
        HashCombine(s, r.DirtBlock);
        HashCombine(s, r.StoneBlock);
        HashCombine(s, r.SandBlock);
        return s;
    }
}
