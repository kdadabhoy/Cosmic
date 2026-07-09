// test_voxel.cpp — Phase 18 voxel worlds, headless (no GL):
//   V1  chunk coord math, get/set across borders, dirty tracking,
//       .cvox chunked-RLE round-trip (byte-identical), .cpal JSON round-trip
//   V2  culled + greedy meshing (fixture counts, greedy<=culled, unit normals,
//       outward winding, cube=6 quads), AO helper
//   V4  DDA ray cast (place/break faces)
//   V6  deterministic generation
// World-space tolerances are ABSOLUTE (doctest::Approx.epsilon is relative).

#include "doctest.h"

#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"
#include "voxel/VoxelMesher.h"
#include "voxel/VoxelGenerator.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <cmath>
#include <vector>

using namespace Cosmic;

namespace
{
    bool NearAbs(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }
}

TEST_SUITE("Voxel V1 — storage / palette / serialization")
{
    TEST_CASE("Chunk coordinate math floors correctly for negatives")
    {
        CHECK(VoxelVolume::ChunkCoord(0, 0, 0)      == glm::ivec3(0, 0, 0));
        CHECK(VoxelVolume::ChunkCoord(31, 0, 0)     == glm::ivec3(0, 0, 0));
        CHECK(VoxelVolume::ChunkCoord(32, 0, 0)     == glm::ivec3(1, 0, 0));
        CHECK(VoxelVolume::ChunkCoord(-1, 0, 0)     == glm::ivec3(-1, 0, 0));
        CHECK(VoxelVolume::ChunkCoord(-32, 0, 0)    == glm::ivec3(-1, 0, 0));
        CHECK(VoxelVolume::ChunkCoord(-33, 0, 0)    == glm::ivec3(-2, 0, 0));

        CHECK(VoxelVolume::LocalCoord(-1, 0, 0)     == glm::ivec3(31, 0, 0));
        CHECK(VoxelVolume::LocalCoord(32, 0, 0)     == glm::ivec3(0, 0, 0));
        CHECK(VoxelVolume::LocalCoord(35, 34, 33)   == glm::ivec3(3, 2, 1));
    }

    TEST_CASE("Get/Set across chunk borders + air no-op")
    {
        VoxelVolume vol;
        CHECK(vol.Get(5, 5, 5) == 0);
        CHECK(vol.ChunkCount() == 0);

        vol.Set(-1, -1, -1, 7);       // negative-corner voxel (its own chunk)
        vol.Set(0, 0, 0, 3);          // straddles into chunk (0,0,0)
        vol.Set(100, 2, 2, 9);        // far chunk

        CHECK(vol.Get(-1, -1, -1) == 7);
        CHECK(vol.Get(0, 0, 0) == 3);
        CHECK(vol.Get(100, 2, 2) == 9);
        CHECK(vol.ChunkCount() == 3);

        // Clearing air in an unpopulated region allocates nothing.
        vol.Set(9999, 9999, 9999, 0);
        CHECK(vol.ChunkCount() == 3);

        // Overwrite + clear.
        vol.Set(0, 0, 0, 0);
        CHECK(vol.Get(0, 0, 0) == 0);
    }

    TEST_CASE("Editing a chunk border marks the neighbour chunk dirty")
    {
        VoxelVolume vol;
        vol.Set(0, 0, 0, 1);           // local (0,0,0) of chunk (0,0,0): 3 negative seams
        std::vector<glm::ivec3> dirty;
        vol.TakeDirtyChunks(dirty);
        // own chunk + 3 negative neighbours (x-1, y-1, z-1)
        CHECK(dirty.size() == 4);
        CHECK(!vol.AnyDirty());        // TakeDirtyChunks cleared it

        vol.Set(15, 15, 15, 1);        // interior voxel: no neighbour seam
        vol.TakeDirtyChunks(dirty);
        CHECK(dirty.size() == 1);
    }

    TEST_CASE(".cvox chunked-RLE round-trip is byte-identical (1M voxels)")
    {
        VoxelVolume vol;
        vol.SetVoxelSize(0.5f);
        vol.SetOrigin({ 3.0f, -2.0f, 11.0f });

        // ~1M voxels: a 100x100x100 patterned block.
        for (int z = 0; z < 100; ++z)
            for (int y = 0; y < 100; ++y)
                for (int x = 0; x < 100; ++x)
                {
                    const uint16_t id = (uint16_t)(((x / 7) + (y / 5) + (z / 3)) % 4);
                    if (id != 0) vol.Set(x, y, z, id);
                }

        std::vector<uint8_t> b1;
        vol.SaveToBuffer(b1);

        VoxelVolume vol2;
        REQUIRE(vol2.LoadFromBuffer(b1));

        // Placement metadata survives.
        CHECK(NearAbs(vol2.GetVoxelSize(), 0.5f));
        CHECK(NearAbs(vol2.GetOrigin().x, 3.0f));
        CHECK(NearAbs(vol2.GetOrigin().z, 11.0f));

        // Sample a few voxels for equality.
        CHECK(vol2.Get(0, 0, 0) == vol.Get(0, 0, 0));
        CHECK(vol2.Get(63, 40, 27) == vol.Get(63, 40, 27));
        CHECK(vol2.Get(99, 99, 99) == vol.Get(99, 99, 99));

        // Byte-identical re-save (deterministic chunk ordering).
        std::vector<uint8_t> b2;
        vol2.SaveToBuffer(b2);
        CHECK(b1 == b2);

        // Loaded chunks come back dirty (need re-mesh).
        CHECK(vol2.AnyDirty());
    }

    TEST_CASE(".cvox rejects a malformed buffer")
    {
        VoxelVolume vol;
        std::vector<uint8_t> junk = { 'N', 'O', 'P', 'E', 1, 2, 3 };
        CHECK_FALSE(vol.LoadFromBuffer(junk));
        std::vector<uint8_t> tooShort = { 'C', 'V', 'O' };
        CHECK_FALSE(vol.LoadFromBuffer(tooShort));
    }

    TEST_CASE("BlockPalette solidity / occlusion / tile UVs")
    {
        auto pal = BlockPalette::CreateDefault();
        CHECK(pal->Count() == 7);                 // Air + 6 blocks
        CHECK(pal->IsAir(0));
        CHECK_FALSE(pal->IsSolid(0));
        CHECK(pal->IsSolid(1));                    // grass
        CHECK(pal->Occludes(3));                   // opaque stone occludes
        CHECK_FALSE(pal->Occludes(0));             // air never occludes

        // Tile 0 spans the top-left cell of the grid.
        const glm::vec4 uv = pal->TileUV(0);
        CHECK(NearAbs(uv.x, 0.0f));
        CHECK(NearAbs(uv.y, 0.0f));
        CHECK(uv.z > 0.0f);
        CHECK(uv.w > 0.0f);
    }

    TEST_CASE(".cpal JSON round-trips")
    {
        auto pal = BlockPalette::CreateDefault();
        const std::string json = pal->ToJson();
        CHECK(json.find("block_palette") != std::string::npos);
        CHECK(json.find("Grass") != std::string::npos);

        BlockPalette loaded;
        REQUIRE(loaded.FromJson(json));
        CHECK(loaded.Count() == pal->Count());
        CHECK(loaded.Get(3).Name == "Stone");
        CHECK(loaded.IsSolid(3));
        CHECK_FALSE(loaded.IsSolid(0));            // id 0 stays Air
        CHECK(loaded.AtlasTilesX() == pal->AtlasTilesX());
    }
}

TEST_SUITE("Voxel V2 — mesher")
{
    TEST_CASE("Culled fixture: 3 blocks in a row -> 14 quads")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        vol.Set(0, 0, 0, 3);
        vol.Set(1, 0, 0, 3);
        vol.Set(2, 0, 0, 3);

        const MeshData culled = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Culled);
        // 5 + 4 + 5 exposed faces = 14 quads.
        CHECK(culled.Vertices.size() == 14 * 4);
        CHECK(culled.Indices.size()  == 14 * 6);
    }

    TEST_CASE("Greedy merges the row and never exceeds culled vertex count")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        vol.Set(0, 0, 0, 3);
        vol.Set(1, 0, 0, 3);
        vol.Set(2, 0, 0, 3);

        const MeshData culled = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Culled);
        const MeshData greedy = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Greedy);

        // top/bottom/+Z/-Z each merge to one quad; -X and +X are single quads = 6.
        CHECK(greedy.Vertices.size() == 6 * 4);
        CHECK(greedy.Vertices.size() <= culled.Vertices.size());
    }

    TEST_CASE("A solid 32^3 chunk greedy-meshes to exactly 6 quads")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    vol.Set(x, y, z, 3);

        const MeshData greedy = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Greedy);
        CHECK(greedy.Vertices.size() == 6 * 4);
        CHECK(greedy.Indices.size()  == 6 * 6);
    }

    TEST_CASE("An empty chunk produces an empty mesh")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        const MeshData md = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal);
        CHECK(md.Vertices.empty());
        CHECK(md.Indices.empty());
    }

    TEST_CASE("Normals are unit length and triangle winding faces outward")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        vol.Set(5, 5, 5, 3);   // a lone cube: 6 faces

        const MeshData md = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Greedy);
        REQUIRE(md.Vertices.size() == 24);

        for (const MeshVertex& v : md.Vertices)
            CHECK(NearAbs(glm::length(v.Normal), 1.0f));

        // For each triangle, the geometric winding normal must agree with the
        // stored face normal (front faces point outward).
        for (size_t t = 0; t < md.Indices.size(); t += 3)
        {
            const glm::vec3& a = md.Vertices[md.Indices[t + 0]].Position;
            const glm::vec3& b = md.Vertices[md.Indices[t + 1]].Position;
            const glm::vec3& c = md.Vertices[md.Indices[t + 2]].Position;
            const glm::vec3  gn = glm::cross(b - a, c - a);
            const glm::vec3& fn = md.Vertices[md.Indices[t + 0]].Normal;
            CHECK(glm::dot(gn, fn) > 0.0f);
        }
    }

    TEST_CASE("Collision mesh merges across blocks (fewer/equal quads vs render)")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        vol.Set(0, 0, 0, 1);   // grass
        vol.Set(1, 0, 0, 2);   // dirt — different block

        const MeshData render = VoxelMesher::BuildChunk(vol, { 0,0,0 }, *pal, VoxelMeshMode::Greedy);
        const MeshData coll   = VoxelMesher::BuildCollision(vol, { 0,0,0 }, *pal);
        // Render can't merge the two tops (different tiles); collision can.
        CHECK(coll.Vertices.size() <= render.Vertices.size());
        CHECK(coll.Indices.size() % 3 == 0);
    }

    TEST_CASE("VertexAO 0..3 corners")
    {
        CHECK(VoxelMesher::VertexAO(true, true, true)   == 0);
        CHECK(VoxelMesher::VertexAO(true, true, false)  == 0);   // both sides => darkest
        CHECK(VoxelMesher::VertexAO(false, false, false) == 3);  // fully open
        CHECK(VoxelMesher::VertexAO(true, false, false) == 2);
        CHECK(VoxelMesher::VertexAO(false, false, true) == 2);
    }
}

TEST_SUITE("Voxel V4 — ray cast")
{
    TEST_CASE("DDA ray hits the near face and reports the placement cell")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;                 // unit voxels, origin 0
        vol.Set(5, 0, 0, 3);             // a block spanning world [5,6] x [0,1] x [0,1]

        // Shoot +X from x=-1 along the block's row.
        const VoxelRayHit hit = vol.RayCast({ -1.0f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.0f }, 20.0f, *pal);
        REQUIRE(hit.Hit);
        CHECK(hit.Voxel == glm::ivec3(5, 0, 0));
        CHECK(hit.Normal == glm::ivec3(-1, 0, 0));   // entered through the -X face
        CHECK(hit.Place  == glm::ivec3(4, 0, 0));    // empty neighbour to place into
        CHECK(hit.Block  == 3);
        CHECK(NearAbs(hit.Distance, 6.0f, 1e-3f));   // from x=-1 to x=5
    }

    TEST_CASE("A ray through empty space misses")
    {
        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        vol.Set(5, 0, 0, 3);
        const VoxelRayHit hit = vol.RayCast({ -1.0f, 10.0f, 10.0f }, { 1.0f, 0.0f, 0.0f }, 20.0f, *pal);
        CHECK_FALSE(hit.Hit);
    }
}

TEST_SUITE("Voxel V6 — generation")
{
    TEST_CASE("Same seed regenerates byte-identically; different seed differs")
    {
        VoxelGeneratorRecipe r;
        r.Enabled = true;
        r.Seed = 2024;
        r.SurfaceLevel = 20.0f;
        r.Amplitude = 12.0f;
        r.Frequency = 0.03f;
        r.CaveThreshold = 0.35f;

        VoxelVolume a, b;
        VoxelGenerator::GenerateChunk(a, { 0, 0, 0 }, r);
        VoxelGenerator::GenerateChunk(b, { 0, 0, 0 }, r);

        std::vector<uint8_t> ba, bb;
        a.SaveToBuffer(ba);
        b.SaveToBuffer(bb);
        CHECK(ba == bb);                 // deterministic

        VoxelGeneratorRecipe r2 = r;
        r2.Seed = 99;
        VoxelVolume c;
        VoxelGenerator::GenerateChunk(c, { 0, 0, 0 }, r2);
        std::vector<uint8_t> bc;
        c.SaveToBuffer(bc);
        CHECK(ba != bc);                 // a different seed yields different terrain

        CHECK(VoxelGenerator::Signature(r) != VoxelGenerator::Signature(r2));
    }

    TEST_CASE("Generated ground is solid below the surface, air far above")
    {
        VoxelGeneratorRecipe r;
        r.Seed = 7;
        r.SurfaceLevel = 16.0f;
        r.Amplitude = 4.0f;      // surface stays within [12,20]
        r.Frequency = 0.02f;
        r.CaveThreshold = 0.0f;  // no caves for a clean check

        auto pal = BlockPalette::CreateDefault();
        VoxelVolume vol;
        VoxelGenerator::GenerateChunk(vol, { 0, 0, 0 }, r);

        CHECK(pal->IsSolid(vol.Get(10, 0, 10)));    // deep -> stone
        CHECK(vol.Get(10, 31, 10) == 0);            // y=31 well above surface -> air
    }
}
