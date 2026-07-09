#pragma once
// voxel/VoxelMesher.h
//
// ============================================================================
// Cosmic voxel worlds — chunk -> MeshData meshing (Phase 18 / V2).
// ============================================================================
//
// Turns one 32^3 chunk of a VoxelVolume into a GL-free MeshData (graphics/Mesh.h,
// the E15 geometry type — reused, not reinvented) so a JobSystem worker can build
// it off the main thread and the main thread uploads it with Mesh::Create.
//
// Two render modes:
//   Culled — one quad per exposed voxel face, each face carrying its own atlas
//            tile UV rect (texture-correct for image atlases).
//   Greedy — coplanar same-block faces merge into larger quads (far fewer verts).
//            Merged quads STRETCH one tile rect across the run, so greedy is
//            texture-correct only for solid-colour tiles (the procedural atlas
//            v1 uses); an image atlas that needs per-tile repeat should use
//            Culled. This is the documented greedy+atlas tradeoff.
//
// A face is emitted between a solid voxel and a neighbour that does not occlude
// it (Air, or a transparent block). Neighbour lookups cross chunk borders through
// the volume, so seams between chunks are correct. Positions are WORLD-VOXEL
// coordinates (absolute voxel units); the volume's Origin + VoxelSize become the
// single draw transform shared by every chunk.
//
// Collision variant: BuildCollision produces greedy geometry merged by solidity
// only (no per-block tile split, no UVs) — the minimal triangle soup Jolt's
// static MeshShape wants (V5).
//
// Everything here is PURE and headless-tested (no GL, no volume mutation).
// ============================================================================

#include "core/Core.h"
#include "graphics/Mesh.h"   // MeshData / MeshVertex

#include <glm/glm.hpp>
#include <cstdint>

namespace Cosmic
{
    class VoxelVolume;
    class BlockPalette;

    enum class VoxelMeshMode { Culled, Greedy };

    class COSMIC_API VoxelMesher
    {
    public:
        /** @brief Build a render mesh for one chunk. Returns empty MeshData for an
         *  all-air chunk. Positions are absolute voxel coordinates. */
        static MeshData BuildChunk(const VoxelVolume& volume, const glm::ivec3& chunk,
                                   const BlockPalette& palette,
                                   VoxelMeshMode mode = VoxelMeshMode::Greedy);

        /** @brief Build a collision mesh for one chunk: greedy, merged by solidity
         *  only, positions/indices only (normals filled, UVs zero). Empty for
         *  all-air. */
        static MeshData BuildCollision(const VoxelVolume& volume, const glm::ivec3& chunk,
                                       const BlockPalette& palette);

        /** @brief Cheap per-vertex ambient occlusion level (0 = darkest .. 3 = lit)
         *  from the three neighbouring voxels sharing a face corner — the classic
         *  "0-3 AO" (side1, side2, corner all solid => 0). Pure + unit-tested.
         *  NOTE: not yet baked into the render mesh (the shared MeshVertex layout
         *  carries no colour channel); kept here so a future voxel shader with a
         *  vertex-colour attribute can fold it in. */
        static int VertexAO(bool side1, bool side2, bool corner)
        {
            if (side1 && side2) return 0;         // both edge-neighbours solid: fully occluded
            return 3 - (int(side1) + int(side2) + int(corner));
        }
    };
}
