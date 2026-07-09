// voxel/VoxelVolume.cpp — chunk store, DDA ray cast, and `.cvox` chunked-RLE
// serialization (Phase 18 / V1 + V4). GL-free, headless-tested.

#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // Get / Set
    // ------------------------------------------------------------------------

    uint16_t VoxelVolume::Get(int x, int y, int z) const
    {
        const glm::ivec3 cc = ChunkCoord(x, y, z);
        auto it = m_Chunks.find(cc);
        if (it == m_Chunks.end())
            return 0;   // Air
        const glm::ivec3 l = LocalCoord(x, y, z);
        return it->second[LocalIndex(l.x, l.y, l.z)];
    }

    std::vector<uint16_t>& VoxelVolume::EmplaceChunk(const glm::ivec3& c)
    {
        auto it = m_Chunks.find(c);
        if (it == m_Chunks.end())
            it = m_Chunks.emplace(c, std::vector<uint16_t>(kChunkVolume, 0)).first;
        return it->second;
    }

    void VoxelVolume::Set(int x, int y, int z, uint16_t block)
    {
        const glm::ivec3 cc = ChunkCoord(x, y, z);
        const glm::ivec3 l  = LocalCoord(x, y, z);
        const int idx = LocalIndex(l.x, l.y, l.z);

        auto it = m_Chunks.find(cc);
        if (it == m_Chunks.end())
        {
            if (block == 0)
                return;   // clearing air in an empty region — nothing to do
            it = m_Chunks.emplace(cc, std::vector<uint16_t>(kChunkVolume, 0)).first;
        }

        if (it->second[idx] == block)
            return;       // no change

        it->second[idx] = block;
        m_Dirty.insert(cc);

        // A face on a chunk seam is shared with the neighbour chunk: its
        // visibility flips when the block on either side changes, so rebuild it too.
        if (l.x == 0)               m_Dirty.insert(cc + glm::ivec3(-1, 0, 0));
        if (l.x == kChunkSize - 1)  m_Dirty.insert(cc + glm::ivec3( 1, 0, 0));
        if (l.y == 0)               m_Dirty.insert(cc + glm::ivec3(0, -1, 0));
        if (l.y == kChunkSize - 1)  m_Dirty.insert(cc + glm::ivec3(0,  1, 0));
        if (l.z == 0)               m_Dirty.insert(cc + glm::ivec3(0, 0, -1));
        if (l.z == kChunkSize - 1)  m_Dirty.insert(cc + glm::ivec3(0, 0,  1));
    }

    void VoxelVolume::TakeDirtyChunks(std::vector<glm::ivec3>& out)
    {
        out.assign(m_Dirty.begin(), m_Dirty.end());
        m_Dirty.clear();
    }

    bool VoxelVolume::ComputeBounds(glm::ivec3& outMin, glm::ivec3& outMax) const
    {
        if (m_Chunks.empty())
            return false;
        const int lo = std::numeric_limits<int>::min(), hi = std::numeric_limits<int>::max();
        outMin = { hi, hi, hi };
        outMax = { lo, lo, lo };
        for (const auto& kv : m_Chunks)
        {
            const glm::ivec3 mn = ChunkMinVoxel(kv.first);
            const glm::ivec3 mx = mn + glm::ivec3(kChunkSize - 1);
            outMin = glm::min(outMin, mn);
            outMax = glm::max(outMax, mx);
        }
        return true;
    }

    // ------------------------------------------------------------------------
    // Ray cast — Amanatides & Woo voxel traversal in world space.
    // ------------------------------------------------------------------------

    VoxelRayHit VoxelVolume::RayCast(const glm::vec3& worldOrigin, const glm::vec3& worldDir,
                                     float maxDistance, const BlockPalette& palette) const
    {
        VoxelRayHit hit;

        const float dlen = glm::length(worldDir);
        if (dlen < 1e-8f || maxDistance <= 0.0f)
            return hit;
        const glm::vec3 dir = worldDir / dlen;

        // Work in voxel space (origin-shifted, unit = one voxel) so the DDA steps
        // are integer cells; convert the travelled parameter back to metres at the end.
        const glm::vec3 p = (worldOrigin - m_Origin) / m_VoxelSize;
        const glm::vec3 d = dir;   // direction is scale-invariant

        glm::ivec3 voxel{ (int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z) };

        const glm::ivec3 step{
            d.x > 0 ? 1 : (d.x < 0 ? -1 : 0),
            d.y > 0 ? 1 : (d.y < 0 ? -1 : 0),
            d.z > 0 ? 1 : (d.z < 0 ? -1 : 0)
        };

        const float INF = std::numeric_limits<float>::infinity();
        glm::vec3 tMax{ INF, INF, INF };   // param (voxel units) to next cell boundary
        glm::vec3 tDelta{ INF, INF, INF }; // param per full voxel crossed

        auto axisInit = [&](int a)
        {
            if (step[a] == 0)
                return;
            tDelta[a] = std::abs(1.0f / d[a]);
            const float cellBoundary = (float)voxel[a] + (step[a] > 0 ? 1.0f : 0.0f);
            tMax[a] = (cellBoundary - p[a]) / d[a];
        };
        axisInit(0); axisInit(1); axisInit(2);

        // Max travel in VOXEL units (params above are in voxel units since |d| == 1).
        const float maxT = maxDistance / m_VoxelSize;

        // If the very first cell is already solid, that's the hit (origin inside a block).
        if (palette.IsSolid(Get(voxel)))
        {
            hit.Hit = true;
            hit.Voxel = voxel;
            hit.Block = Get(voxel);
            hit.Normal = { 0, 0, 0 };
            hit.Place = voxel;
            hit.Distance = 0.0f;
            hit.Point = worldOrigin;
            return hit;
        }

        float t = 0.0f;
        // A generous cap on iterations guards against a degenerate direction.
        const int maxSteps = (int)(maxT * 3.0f) + 8;
        for (int i = 0; i < maxSteps; ++i)
        {
            // Advance to the nearest cell boundary, recording which face we cross.
            int axis;
            if (tMax.x < tMax.y) axis = (tMax.x < tMax.z) ? 0 : 2;
            else                 axis = (tMax.y < tMax.z) ? 1 : 2;

            t = tMax[axis];
            if (t > maxT)
                break;

            voxel[axis] += step[axis];
            tMax[axis]  += tDelta[axis];

            const uint16_t id = Get(voxel);
            if (palette.IsSolid(id))
            {
                hit.Hit = true;
                hit.Voxel = voxel;
                hit.Block = id;
                hit.Normal = { 0, 0, 0 };
                hit.Normal[axis] = -step[axis];   // face we entered through
                hit.Place = voxel + hit.Normal;
                hit.Distance = t * m_VoxelSize;
                hit.Point = worldOrigin + dir * hit.Distance;
                return hit;
            }
        }
        return hit;
    }

    // ------------------------------------------------------------------------
    // `.cvox` chunked RLE
    //
    //   [0..3]  magic  'C''V''O''X'
    //   [4]     version (1)
    //   float   VoxelSize
    //   float3  Origin
    //   u32     chunkCount
    //   per chunk:
    //     i32 x, y, z
    //     u32 runCount
    //     runCount * { u16 value, u32 length }   (lengths sum to kChunkVolume)
    // ------------------------------------------------------------------------

    namespace
    {
        template<typename T>
        void PutPod(std::vector<uint8_t>& b, const T& v)
        {
            const auto* p = reinterpret_cast<const uint8_t*>(&v);
            b.insert(b.end(), p, p + sizeof(T));
        }
        template<typename T>
        bool GetPod(const std::vector<uint8_t>& b, size_t& off, T& v)
        {
            if (off + sizeof(T) > b.size())
                return false;
            std::memcpy(&v, b.data() + off, sizeof(T));
            off += sizeof(T);
            return true;
        }
    }

    void VoxelVolume::SaveToBuffer(std::vector<uint8_t>& out) const
    {
        out.clear();
        out.push_back('C'); out.push_back('V'); out.push_back('O'); out.push_back('X');
        out.push_back(1);   // version
        PutPod(out, m_VoxelSize);
        PutPod(out, m_Origin.x); PutPod(out, m_Origin.y); PutPod(out, m_Origin.z);
        PutPod(out, static_cast<uint32_t>(m_Chunks.size()));

        // Emit chunks in a deterministic (sorted) order so a save->load->save
        // round-trip is byte-identical regardless of hash-map iteration order.
        std::vector<glm::ivec3> coords;
        coords.reserve(m_Chunks.size());
        for (const auto& kv : m_Chunks)
            coords.push_back(kv.first);
        std::sort(coords.begin(), coords.end(), [](const glm::ivec3& a, const glm::ivec3& b)
        {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });

        for (const glm::ivec3& coord : coords)
        {
            PutPod(out, static_cast<int32_t>(coord.x));
            PutPod(out, static_cast<int32_t>(coord.y));
            PutPod(out, static_cast<int32_t>(coord.z));

            const std::vector<uint16_t>& blocks = m_Chunks.at(coord);

            // Reserve the run-count slot, then RLE the chunk in index order.
            const size_t runCountPos = out.size();
            PutPod(out, static_cast<uint32_t>(0));
            uint32_t runs = 0;

            size_t i = 0;
            while (i < blocks.size())
            {
                const uint16_t value = blocks[i];
                uint32_t length = 1;
                while (i + length < blocks.size() && blocks[i + length] == value)
                    ++length;
                PutPod(out, value);
                PutPod(out, length);
                ++runs;
                i += length;
            }
            std::memcpy(out.data() + runCountPos, &runs, sizeof(uint32_t));
        }
    }

    bool VoxelVolume::LoadFromBuffer(const std::vector<uint8_t>& in)
    {
        size_t off = 0;
        if (in.size() < 5 || in[0] != 'C' || in[1] != 'V' || in[2] != 'O' || in[3] != 'X')
            return false;
        off = 4;
        uint8_t version = in[off++];
        if (version != 1)
            return false;

        float voxelSize = 1.0f;
        glm::vec3 origin{ 0.0f };
        uint32_t chunkCount = 0;
        if (!GetPod(in, off, voxelSize)) return false;
        if (!GetPod(in, off, origin.x) || !GetPod(in, off, origin.y) || !GetPod(in, off, origin.z)) return false;
        if (!GetPod(in, off, chunkCount)) return false;

        m_Chunks.clear();
        m_Dirty.clear();
        m_VoxelSize = voxelSize > 1e-4f ? voxelSize : 1e-4f;
        m_Origin = origin;

        for (uint32_t c = 0; c < chunkCount; ++c)
        {
            int32_t cx, cy, cz;
            if (!GetPod(in, off, cx) || !GetPod(in, off, cy) || !GetPod(in, off, cz)) return false;
            uint32_t runCount = 0;
            if (!GetPod(in, off, runCount)) return false;

            std::vector<uint16_t> blocks;
            blocks.reserve(kChunkVolume);
            for (uint32_t r = 0; r < runCount; ++r)
            {
                uint16_t value; uint32_t length;
                if (!GetPod(in, off, value) || !GetPod(in, off, length)) return false;
                if (blocks.size() + length > (size_t)kChunkVolume) return false;
                blocks.insert(blocks.end(), length, value);
            }
            if (blocks.size() != (size_t)kChunkVolume)
                return false;

            const glm::ivec3 coord{ cx, cy, cz };
            m_Chunks[coord] = std::move(blocks);
            m_Dirty.insert(coord);
        }
        return true;
    }

    bool VoxelVolume::Save(const std::string& path) const
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(resolved).parent_path(), ec);

        std::vector<uint8_t> buf;
        SaveToBuffer(buf);

        std::ofstream os(std::filesystem::u8path(resolved), std::ios::binary | std::ios::trunc);
        if (!os)
        {
            CS_CORE_ERROR("VoxelVolume::Save — cannot open '{}'", resolved);
            return false;
        }
        os.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
        return static_cast<bool>(os);
    }

    bool VoxelVolume::Load(const std::string& path)
    {
        const std::string resolved = FileSystem::Resolve(path);
        std::ifstream is(std::filesystem::u8path(resolved), std::ios::binary);
        if (!is)
            return false;
        std::vector<uint8_t> buf((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
        return LoadFromBuffer(buf);
    }
}
