#pragma once
// voxel/VoxelVolume.h
//
// ============================================================================
// Cosmic voxel worlds — the chunk store (Phase 18 / V1).
// ============================================================================
//
// A VoxelVolume is a sparse grid of 32^3 chunks of dense uint16 block ids
// (0 = Air, resolved through a per-volume BlockPalette). It is the AUTHORED
// truth: Get/Set in world VOXEL coordinates (signed, unbounded), lazy chunk
// allocation (an all-air region costs nothing), and a dirty-chunk set that the
// mesher/collision rebuild drains. Editing a voxel on a chunk border also marks
// the neighbouring chunk dirty, because a face's visibility depends on the block
// on the other side of the seam.
//
// PLACEMENT: the volume sits in the world at `Origin` (world position of the min
// corner of voxel (0,0,0)) with `VoxelSize` metres per voxel. RayCast takes a
// WORLD ray and reports the hit voxel + the face normal (for place/break).
//
// SERIALIZATION: `.cvox` is a chunked run-length encoding of the non-air chunks
// (voxel data does NOT live in the scene JSON — the scene stores the sidecar path
// + generator recipe, the E15 "params, not meshes" rule at volume scale).
// Hand-edited chunks serialize; generated-and-untouched chunks re-generate.
//
// GL-free, allocation-only-when-touched, and fully headless-testable. The GPU
// chunk meshes + Jolt collision bodies live OUTSIDE this class (Scene glue, V3/V5)
// so the storage layer never needs a GL/physics context.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
    class BlockPalette;

    // Hash for an integer chunk/voxel coordinate used as a map/set key.
    struct IVec3Hash
    {
        std::size_t operator()(const glm::ivec3& v) const noexcept
        {
            // 64-bit mix of three 21-bit-ish lanes; fine for the coordinate ranges
            // a resident voxel world spans (well within +/-2^20 chunks).
            std::size_t h = 1469598103934665603ull;
            auto mix = [&h](int c) {
                h ^= static_cast<std::size_t>(static_cast<uint32_t>(c));
                h *= 1099511628211ull;
            };
            mix(v.x); mix(v.y); mix(v.z);
            return h;
        }
    };
    struct IVec3Eq
    {
        bool operator()(const glm::ivec3& a, const glm::ivec3& b) const noexcept
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
    };

    // Result of a voxel-grid ray cast (V4). Voxel is the first solid cell hit;
    // Normal is the unit face normal (integer, e.g. {0,1,0}); Place = Voxel+Normal
    // is the empty cell a "place block" edit fills; Prev == Place.
    struct VoxelRayHit
    {
        bool      Hit = false;
        glm::ivec3 Voxel{ 0 };     // the solid cell that was hit
        glm::ivec3 Normal{ 0 };    // face normal (integer axis)
        glm::ivec3 Place{ 0 };     // Voxel + Normal (empty neighbour to place into)
        uint16_t  Block = 0;       // the id at Voxel
        float     Distance = 0.0f; // world metres from the ray origin
        glm::vec3 Point{ 0.0f };   // world hit point on the face
    };

    class COSMIC_API VoxelVolume
    {
    public:
        static constexpr int      kChunkSize   = 32;
        static constexpr int      kChunkSize2  = kChunkSize * kChunkSize;
        static constexpr int      kChunkVolume = kChunkSize * kChunkSize * kChunkSize;

        VoxelVolume() = default;

        template<typename... Args>
        static Ref<VoxelVolume> Create(Args&&... args)
        {
            return std::make_shared<VoxelVolume>(std::forward<Args>(args)...);
        }

        // ---- placement -------------------------------------------------------

        void      SetOrigin(const glm::vec3& o) { m_Origin = o; }
        glm::vec3 GetOrigin() const             { return m_Origin; }
        void      SetVoxelSize(float s)         { m_VoxelSize = s > 1e-4f ? s : 1e-4f; }
        float     GetVoxelSize() const          { return m_VoxelSize; }

        // ---- coordinate math (pure, static, headless-tested) ----------------
        // A world voxel coord maps to a chunk coord by floor-division by 32, and a
        // local [0,31] coord by the low 5 bits. Arithmetic right-shift on two's
        // complement (every mainstream compiler) is floor-div for powers of two,
        // and `& 31` yields the non-negative remainder — so negatives are correct.

        static glm::ivec3 ChunkCoord(int x, int y, int z)
        {
            return { x >> 5, y >> 5, z >> 5 };
        }
        static glm::ivec3 ChunkCoord(const glm::ivec3& v) { return ChunkCoord(v.x, v.y, v.z); }

        static glm::ivec3 LocalCoord(int x, int y, int z)
        {
            return { x & 31, y & 31, z & 31 };
        }
        static int LocalIndex(int lx, int ly, int lz)
        {
            return lx + ly * kChunkSize + lz * kChunkSize2;
        }

        /** @brief World voxel coordinate of a chunk's min corner. */
        static glm::ivec3 ChunkMinVoxel(const glm::ivec3& chunk)
        {
            return chunk * kChunkSize;
        }

        // ---- get / set (world voxel coords) ---------------------------------

        /** @brief Block id at (x,y,z); 0 (Air) if the chunk is not resident. */
        uint16_t Get(int x, int y, int z) const;
        uint16_t Get(const glm::ivec3& v) const { return Get(v.x, v.y, v.z); }

        /** @brief Set (x,y,z) to `block`. Setting Air in a non-resident region is a
         *  no-op; setting a solid block allocates the chunk. Marks the chunk dirty
         *  (and any neighbour sharing the touched face's seam). No-op when the id
         *  is unchanged. */
        void Set(int x, int y, int z, uint16_t block);
        void Set(const glm::ivec3& v, uint16_t block) { Set(v.x, v.y, v.z, block); }

        // ---- world <-> voxel space ------------------------------------------

        /** @brief Voxel coordinate containing world point `p` (floor). */
        glm::ivec3 WorldToVoxel(const glm::vec3& p) const
        {
            const glm::vec3 local = (p - m_Origin) / m_VoxelSize;
            return { (int)std::floor(local.x), (int)std::floor(local.y), (int)std::floor(local.z) };
        }
        /** @brief World position of voxel (x,y,z)'s min corner. */
        glm::vec3 VoxelToWorld(const glm::ivec3& v) const
        {
            return m_Origin + glm::vec3(v) * m_VoxelSize;
        }

        // ---- chunks / dirty tracking ----------------------------------------

        using ChunkMap = std::unordered_map<glm::ivec3, std::vector<uint16_t>, IVec3Hash, IVec3Eq>;

        bool HasChunk(const glm::ivec3& c) const { return m_Chunks.find(c) != m_Chunks.end(); }
        std::size_t ChunkCount() const { return m_Chunks.size(); }

        /** @brief Raw block array of a resident chunk (kChunkVolume ids), or nullptr. */
        const std::vector<uint16_t>* ChunkBlocks(const glm::ivec3& c) const
        {
            auto it = m_Chunks.find(c);
            return it == m_Chunks.end() ? nullptr : &it->second;
        }

        /** @brief Ensure a chunk exists (all-air if new) and return its block array.
         *  Used by generators that fill a chunk in one shot. Does NOT mark dirty —
         *  call MarkChunkDirty afterwards. */
        std::vector<uint16_t>& EmplaceChunk(const glm::ivec3& c);

        void MarkChunkDirty(const glm::ivec3& c) { m_Dirty.insert(c); }
        bool AnyDirty() const { return !m_Dirty.empty(); }

        /** @brief Drop every chunk (and pending dirt). The caller clears any GPU
         *  chunk meshes / generated-flags separately (VoxelRenderData). */
        void Clear() { m_Chunks.clear(); m_Dirty.clear(); }

        /** @brief Move the current dirty-chunk set into `out` and clear it. The
         *  caller (mesher/collision rebuild) consumes the list. */
        void TakeDirtyChunks(std::vector<glm::ivec3>& out);

        /** @brief Visit every resident chunk coordinate (order unspecified). */
        void ForEachChunk(const std::function<void(const glm::ivec3&)>& fn) const
        {
            for (const auto& kv : m_Chunks) fn(kv.first);
        }

        /** @brief Inclusive voxel-space bounds over resident chunks (false if empty). */
        bool ComputeBounds(glm::ivec3& outMin, glm::ivec3& outMax) const;

        // ---- ray cast (DDA, V4) ---------------------------------------------

        /** @brief March a WORLD-space ray through the grid (Amanatides & Woo) and
         *  report the first solid voxel within `maxDistance` metres. `palette`
         *  decides solidity. Pure aside from Get(); safe on an empty region. */
        VoxelRayHit RayCast(const glm::vec3& worldOrigin, const glm::vec3& worldDir,
                            float maxDistance, const BlockPalette& palette) const;

        // ---- `.cvox` serialization (chunked RLE, VoxelVolume.cpp) ------------

        /** @brief Encode the non-air chunks to a byte buffer (deterministic; used
         *  by the disk save + the round-trip test). Origin/VoxelSize are included. */
        void SaveToBuffer(std::vector<uint8_t>& out) const;
        /** @brief Decode a buffer produced by SaveToBuffer. Clears existing chunks
         *  first; every restored chunk is marked dirty. False on malformed input. */
        bool LoadFromBuffer(const std::vector<uint8_t>& in);

        /** @brief Save / load a `.cvox` via the VFS (paths go through FileSystem). */
        bool Save(const std::string& path) const;
        bool Load(const std::string& path);

    private:
        glm::vec3 m_Origin{ 0.0f };
        float     m_VoxelSize = 1.0f;

        ChunkMap m_Chunks;
        std::unordered_set<glm::ivec3, IVec3Hash, IVec3Eq> m_Dirty;
    };
}
