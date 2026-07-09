#pragma once
// voxel/VoxelRender.h
//
// ============================================================================
// Cosmic voxel worlds — per-volume RUNTIME render state (Phase 18 / V3).
// ============================================================================
//
// VoxelVolume (V1) is GL-free storage; the GPU chunk meshes, the procedural
// atlas texture + material, and the streaming/collision bookkeeping live here so
// the storage layer never needs a GL context. A VoxelVolumeComponent holds one
// of these via a Ref<> (shared, cheap to copy) — it is runtime-only (never
// serialized), rebuilt from the volume + palette on load.
//
// The atlas is a small procedural texture: one solid-colour tile per palette
// tile index (top brighter, bottom darker for cheap face shading), point-sampled
// so greedy-merged quads that stretch one tile show no bleeding. An image atlas
// is a drop-in replacement (set the material's albedo map + use Culled meshing).
// ============================================================================

#include "core/Core.h"
#include "voxel/VoxelVolume.h"     // IVec3Hash / IVec3Eq
#include "voxel/VoxelMesher.h"     // VoxelMeshMode
#include "voxel/VoxelGenerator.h"  // VoxelGeneratorRecipe
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>

namespace Cosmic
{
    class Material;
    class Texture2D;
    class BlockPalette;
    struct VoxelVolumeComponent;   // scene/Components.h (flattened recipe source)

    struct VoxelRenderData
    {
        // Uploaded chunk render meshes (world-voxel-space positions; drawn with the
        // volume's Origin/VoxelSize transform). Absent key = an all-air chunk.
        std::unordered_map<glm::ivec3, Ref<Mesh>, IVec3Hash, IVec3Eq> ChunkMeshes;

        // Chunks the generator has already filled (so streaming won't re-fill them).
        std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq> Generated;

        // Chunks whose static Jolt collision needs a (re)build — populated when a
        // chunk is re-meshed, drained by ScenePhysics during a play session (V5).
        std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq> CollisionDirty;

        Ref<Material>  AtlasMaterial;
        Ref<Texture2D> AtlasTexture;
        std::size_t    AtlasPaletteVersion = static_cast<std::size_t>(-1);

        VoxelMeshMode  Mode = VoxelMeshMode::Greedy;
    };

    // --- helpers (VoxelRender.cpp) -------------------------------------------

    /** @brief Hash of the palette's tiling-relevant state (block count, per-block
     *  face tiles + colours, atlas dims) — the atlas rebuilds when it changes. */
    COSMIC_API std::size_t VoxelPaletteVersion(const BlockPalette& palette);

    /** @brief Build (or rebuild) the procedural atlas texture + PBR material from
     *  the palette into `rd`. Main-thread / GL. */
    COSMIC_API void BuildVoxelAtlas(VoxelRenderData& rd, const BlockPalette& palette);

    /** @brief Map the flattened generator fields on a VoxelVolumeComponent onto a
     *  VoxelGeneratorRecipe (pure). */
    COSMIC_API VoxelGeneratorRecipe BuildVoxelRecipe(const VoxelVolumeComponent& c);
}
