// test_nav_world.cpp — N1 acceptance. Bakes a hand-authored greybox (two platforms
// joined by a bridge + a disconnected island) through the NavWorld service and
// asserts: a reachable pair yields a path of a known length ±ε; an unreachable pair
// fails cleanly (no crash, Reached=false); the baked mesh serializes + reloads to an
// identical path; and RandomPointAround is deterministic for a fixed seed. Headless —
// Recast/Detour never touch GL, and NavWorld's public header exposes zero Recast types.

#include "doctest.h"

#include "nav/NavWorld.h"

#include <glm/glm.hpp>
#include <vector>

using namespace Cosmic;

namespace
{
    // Append a flat, up-facing (normal +Y => walkable) quad at height `y`.
    void AddQuad(std::vector<float>& v, std::vector<int>& t,
                 float minX, float maxX, float minZ, float maxZ, float y)
    {
        const int base = int(v.size() / 3);
        auto push = [&](float x, float yy, float z) { v.push_back(x); v.push_back(yy); v.push_back(z); };
        push(minX, y, minZ);   // A (base+0)
        push(maxX, y, minZ);   // B (base+1)
        push(maxX, y, maxZ);   // C (base+2)
        push(minX, y, maxZ);   // D (base+3)
        // Up-facing winding (normal +Y): (A,C,B) and (A,D,C).
        t.push_back(base + 0); t.push_back(base + 2); t.push_back(base + 1);
        t.push_back(base + 0); t.push_back(base + 3); t.push_back(base + 2);
    }

    // Greybox: platform L + centered bridge + platform R (all connected), plus a
    // far island with a wide gap (unreachable). Returns the geometry.
    void BuildGreybox(std::vector<float>& v, std::vector<int>& t)
    {
        AddQuad(v, t, -12.0f,  -2.0f, -5.0f, 5.0f, 0.0f);   // platform L
        AddQuad(v, t,  -2.0f,   2.0f, -1.5f, 1.5f, 0.0f);   // bridge (centered on z=0)
        AddQuad(v, t,   2.0f,  12.0f, -5.0f, 5.0f, 0.0f);   // platform R
        AddQuad(v, t,  20.0f,  28.0f, -4.0f, 4.0f, 0.0f);   // island (disconnected)
    }

    NavBuildDesc GreyboxDesc()
    {
        NavBuildDesc d;
        d.CellSize = 0.2f; d.CellHeight = 0.2f;
        d.AgentRadius = 0.4f; d.AgentHeight = 1.8f; d.AgentMaxClimb = 0.4f;
        return d;
    }
}

TEST_SUITE("Nav / NavWorld bake + query (N1)")
{
    TEST_CASE("Bakes a greybox and finds a reachable path of a known length")
    {
        std::vector<float> v; std::vector<int> t;
        BuildGreybox(v, t);

        NavWorld nav;
        NavGeometryInput geom{ v, t };
        std::string err;
        REQUIRE(nav.Build(GreyboxDesc(), geom, &err));
        REQUIRE(nav.IsBuilt());

        // L -> R across the bridge: a roughly straight ~18 m path (from -9 to +9).
        NavPath path = nav.FindPath({ -9, 0, 0 }, { 9, 0, 0 });
        CHECK(path.Reached);
        CHECK_FALSE(path.Partial);
        REQUIRE(path.Corners.size() >= 2u);
        CHECK(path.Length == doctest::Approx(18.0f).epsilon(0.12));   // ±~2 m for corner routing
    }

    TEST_CASE("An unreachable pair fails cleanly")
    {
        std::vector<float> v; std::vector<int> t;
        BuildGreybox(v, t);

        NavWorld nav;
        NavGeometryInput geom{ v, t };
        REQUIRE(nav.Build(GreyboxDesc(), geom));

        // Platform L -> the island: disconnected, so no full path.
        NavPath path = nav.FindPath({ -9, 0, 0 }, { 24, 0, 0 });
        CHECK_FALSE(path.Reached);      // the goal is not reached
        // Clean failure: either a partial path toward the gap or an empty result,
        // but never the goal and never a crash.
        CHECK(path.Length < 30.0f);
    }

    TEST_CASE("Serialize -> Load round-trips to an identical mesh + path")
    {
        std::vector<float> v; std::vector<int> t;
        BuildGreybox(v, t);

        NavWorld a;
        REQUIRE(a.Build(GreyboxDesc(), NavGeometryInput{ v, t }));
        NavMeshData blob = a.Serialize();
        REQUIRE_FALSE(blob.Empty());
        NavPath pathA = a.FindPath({ -9, 0, 0 }, { 9, 0, 0 });

        NavWorld b;
        REQUIRE(b.Load(blob));
        REQUIRE(b.IsBuilt());

        // The reloaded mesh re-serializes to the identical bytes...
        NavMeshData blob2 = b.Serialize();
        REQUIRE(blob.Bytes.size() == blob2.Bytes.size());
        CHECK(blob.Bytes == blob2.Bytes);

        // ...and produces the same path length.
        NavPath pathB = b.FindPath({ -9, 0, 0 }, { 9, 0, 0 });
        CHECK(pathB.Reached);
        CHECK(pathB.Length == doctest::Approx(pathA.Length));
    }

    TEST_CASE("Nearest + random points land on the navmesh, random is seed-deterministic")
    {
        std::vector<float> v; std::vector<int> t;
        BuildGreybox(v, t);
        NavWorld nav;
        REQUIRE(nav.Build(GreyboxDesc(), NavGeometryInput{ v, t }));

        // A point above platform L snaps down onto the mesh (~y 0).
        auto near = nav.NearestPoint({ -6, 3, 0 });
        REQUIRE(near.has_value());
        CHECK(near->y == doctest::Approx(0.0f).epsilon(0.2));

        // Same seed => identical random point (the N4 determinism primitive).
        uint32_t s1 = 12345u, s2 = 12345u;
        auto r1 = nav.RandomPointAround({ 0, 0, 0 }, 6.0f, s1);
        auto r2 = nav.RandomPointAround({ 0, 0, 0 }, 6.0f, s2);
        REQUIRE(r1.has_value());
        REQUIRE(r2.has_value());
        CHECK(r1->x == r2->x);
        CHECK(r1->y == r2->y);
        CHECK(r1->z == r2->z);
    }

    TEST_CASE("Debug triangles are produced for a built mesh")
    {
        std::vector<float> v; std::vector<int> t;
        BuildGreybox(v, t);
        NavWorld nav;
        REQUIRE(nav.Build(GreyboxDesc(), NavGeometryInput{ v, t }));

        std::vector<NavDebugTri> tris;
        nav.GetDebugTriangles(tris);
        CHECK(tris.size() > 0);
    }
}
