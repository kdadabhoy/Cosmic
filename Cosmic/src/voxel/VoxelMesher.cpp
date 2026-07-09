// voxel/VoxelMesher.cpp — culled + greedy chunk meshing (Phase 18 / V2). Pure,
// GL-free, headless-tested.

#include "voxel/VoxelMesher.h"
#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"

#include <array>
#include <vector>

namespace Cosmic
{
    namespace
    {
        constexpr int N = VoxelVolume::kChunkSize;   // 32

        // A face-plane mask cell: which way the face points (+1 / -1 along the
        // slice axis, 0 = no face) and which solid block owns it (for the render
        // tile split; ignored when merging by solidity only).
        struct MaskCell
        {
            int8_t   Sign  = 0;
            uint16_t Block = 0;
        };

        // Append one quad (two CCW triangles) to `md`. Corners are given in an
        // order that is already CCW as seen from the face's outward side; `uv`
        // is the atlas tile rect {u0,v0,u1,v1} mapped onto the four corners.
        void EmitQuad(MeshData& md,
                      const glm::vec3& p0, const glm::vec3& p1,
                      const glm::vec3& p2, const glm::vec3& p3,
                      const glm::vec3& normal, const glm::vec4& uv)
        {
            const uint32_t base = static_cast<uint32_t>(md.Vertices.size());

            MeshVertex v0; v0.Position = p0; v0.Normal = normal; v0.TexCoord = { uv.x, uv.y };
            MeshVertex v1; v1.Position = p1; v1.Normal = normal; v1.TexCoord = { uv.z, uv.y };
            MeshVertex v2; v2.Position = p2; v2.Normal = normal; v2.TexCoord = { uv.z, uv.w };
            MeshVertex v3; v3.Position = p3; v3.Normal = normal; v3.TexCoord = { uv.x, uv.w };
            md.Vertices.push_back(v0);
            md.Vertices.push_back(v1);
            md.Vertices.push_back(v2);
            md.Vertices.push_back(v3);

            md.Indices.push_back(base + 0);
            md.Indices.push_back(base + 1);
            md.Indices.push_back(base + 2);
            md.Indices.push_back(base + 0);
            md.Indices.push_back(base + 2);
            md.Indices.push_back(base + 3);
        }

        // Pick the atlas tile a solid block presents on the face with `normal`.
        uint16_t FaceTile(const BlockPalette& pal, uint16_t block, int axis, int sign)
        {
            const BlockType& bt = pal.Get(block);
            if (axis == 1) return sign > 0 ? bt.TileTop : bt.TileBottom;   // +Y / -Y
            return bt.TileSide;                                            // X / Z sides
        }

        // The shared greedy-slice builder. allowMerge=false forces 1x1 quads
        // (the Culled mode); mergeByBlock=false merges across different solid
        // blocks (the collision variant); withUV=false zeroes UVs.
        MeshData Build(const VoxelVolume& volume, const glm::ivec3& chunk,
                       const BlockPalette& palette,
                       bool allowMerge, bool mergeByBlock, bool withUV)
        {
            MeshData md;
            const glm::ivec3 base = VoxelVolume::ChunkMinVoxel(chunk);

            std::array<glm::ivec3, 3> e = { glm::ivec3(1,0,0), glm::ivec3(0,1,0), glm::ivec3(0,0,1) };
            std::vector<MaskCell> mask((size_t)N * N);

            for (int d = 0; d < 3; ++d)
            {
                const int u = (d + 1) % 3;
                const int v = (d + 2) % 3;

                // Slices are the N+1 planes cutting the chunk perpendicular to d.
                for (int slice = 0; slice <= N; ++slice)
                {
                    // Build the face mask for this plane.
                    for (int j = 0; j < N; ++j)
                    {
                        for (int i = 0; i < N; ++i)
                        {
                            glm::ivec3 a(0); a[d] = slice - 1; a[u] = i; a[v] = j;   // negative-side cell
                            glm::ivec3 b(0); b[d] = slice;     b[u] = i; b[v] = j;   // positive-side cell

                            const uint16_t blockNeg = volume.Get(base + a);
                            const uint16_t blockPos = volume.Get(base + b);
                            const bool negSolid = palette.IsSolid(blockNeg);
                            const bool posSolid = palette.IsSolid(blockPos);

                            MaskCell cell;
                            if (negSolid && !palette.Occludes(blockPos))
                            {
                                cell.Sign  = +1;                 // face points +d (out of the negative cell)
                                cell.Block = blockNeg;
                            }
                            else if (posSolid && !palette.Occludes(blockNeg))
                            {
                                cell.Sign  = -1;                 // face points -d
                                cell.Block = blockPos;
                            }
                            mask[(size_t)j * N + i] = cell;
                        }
                    }

                    // Greedy-merge equal cells into rectangles and emit quads.
                    auto eq = [&](const MaskCell& c1, const MaskCell& c2)
                    {
                        if (c1.Sign == 0 || c2.Sign == 0) return false;
                        if (c1.Sign != c2.Sign) return false;
                        return !mergeByBlock || c1.Block == c2.Block;
                    };

                    for (int j = 0; j < N; ++j)
                    {
                        for (int i = 0; i < N; )
                        {
                            const MaskCell c = mask[(size_t)j * N + i];
                            if (c.Sign == 0) { ++i; continue; }

                            int w = 1;
                            if (allowMerge)
                                while (i + w < N && eq(mask[(size_t)j * N + i + w], c)) ++w;

                            int h = 1;
                            if (allowMerge)
                            {
                                bool grow = true;
                                while (j + h < N && grow)
                                {
                                    for (int k = 0; k < w; ++k)
                                        if (!eq(mask[(size_t)(j + h) * N + i + k], c)) { grow = false; break; }
                                    if (grow) ++h;
                                }
                            }

                            // Quad geometry in absolute voxel space.
                            glm::vec3 org(0.0f);
                            org[d] = (float)slice;
                            org[u] = (float)i;
                            org[v] = (float)j;
                            const glm::vec3 P = glm::vec3(base) + org;

                            glm::vec3 du(0.0f); du[u] = (float)w;
                            glm::vec3 dv(0.0f); dv[v] = (float)h;

                            const glm::vec3 normal = glm::vec3(e[d]) * (float)c.Sign;

                            glm::vec4 uv{ 0.0f, 0.0f, 0.0f, 0.0f };
                            if (withUV)
                                uv = palette.TileUV(FaceTile(palette, c.Block, d, c.Sign));

                            if (c.Sign > 0)   // outward = +d : winding P, P+du, P+du+dv, P+dv
                                EmitQuad(md, P, P + du, P + du + dv, P + dv, normal, uv);
                            else              // outward = -d : reversed
                                EmitQuad(md, P, P + dv, P + du + dv, P + du, normal, uv);

                            // Consume the merged rectangle.
                            for (int hh = 0; hh < h; ++hh)
                                for (int ww = 0; ww < w; ++ww)
                                    mask[(size_t)(j + hh) * N + i + ww].Sign = 0;

                            i += w;
                        }
                    }
                }
            }
            return md;
        }
    }

    MeshData VoxelMesher::BuildChunk(const VoxelVolume& volume, const glm::ivec3& chunk,
                                     const BlockPalette& palette, VoxelMeshMode mode)
    {
        const bool greedy = (mode == VoxelMeshMode::Greedy);
        return Build(volume, chunk, palette,
                     /*allowMerge*/ greedy, /*mergeByBlock*/ true, /*withUV*/ true);
    }

    MeshData VoxelMesher::BuildCollision(const VoxelVolume& volume, const glm::ivec3& chunk,
                                         const BlockPalette& palette)
    {
        return Build(volume, chunk, palette,
                     /*allowMerge*/ true, /*mergeByBlock*/ false, /*withUV*/ false);
    }
}
