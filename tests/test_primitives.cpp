// test_primitives.cpp — E15 parametric primitives, CPU geometry (no GL).
//
// The GL factories (Mesh::CreateBox/...) upload to the GPU and can't run
// headless, so E15 splits geometry generation into pure Mesh::Build* functions
// returning MeshData. These tests assert vertex/index counts, local bounds and
// normals on that CPU data — the "save/reload rebuilds identical geometry"
// acceptance line, including torus normals.
//
// doctest::Approx.epsilon is RELATIVE; world-coordinate checks use ABSOLUTE
// tolerances via .epsilon on small magnitudes only where safe, or Near() below.

#include "doctest.h"
#include "graphics/Mesh.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

using Cosmic::Mesh;
using Cosmic::MeshData;
using Cosmic::MeshVertex;

namespace
{
    struct AABB { glm::vec3 Min, Max; };

    AABB Bounds(const MeshData& d)
    {
        REQUIRE_FALSE(d.Vertices.empty());
        AABB b{ d.Vertices[0].Position, d.Vertices[0].Position };
        for (const MeshVertex& v : d.Vertices)
        {
            b.Min = glm::min(b.Min, v.Position);
            b.Max = glm::max(b.Max, v.Position);
        }
        return b;
    }

    bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::length(a - b) <= eps;
    }

    // Every index must be in range and the index count a multiple of 3.
    void CheckTopology(const MeshData& d)
    {
        CHECK(d.Indices.size() % 3 == 0);
        for (uint32_t i : d.Indices)
            CHECK(i < d.Vertices.size());
    }

    void CheckUnitNormals(const MeshData& d)
    {
        for (const MeshVertex& v : d.Vertices)
            CHECK(Near(glm::length(v.Normal), 1.0f, 1e-3f));
    }
}

TEST_SUITE("Primitives (E15)")
{
    TEST_CASE("Box: 24 verts, 36 indices, bounds = half extents")
    {
        const glm::vec3 size{ 2.0f, 4.0f, 6.0f };
        MeshData d = Mesh::BuildBox(size);

        CHECK(d.Vertices.size() == 24);   // 4 per face, faces don't share verts
        CHECK(d.Indices.size() == 36);    // 2 triangles per face
        CheckTopology(d);
        CheckUnitNormals(d);

        AABB b = Bounds(d);
        CHECK(Near(b.Min, -0.5f * size));
        CHECK(Near(b.Max,  0.5f * size));
    }

    TEST_CASE("Plane: 4 verts, +Y normal, correct extents")
    {
        MeshData d = Mesh::BuildPlane(3.0f, 5.0f);
        CHECK(d.Vertices.size() == 4);
        CHECK(d.Indices.size() == 6);
        CheckTopology(d);

        AABB b = Bounds(d);
        CHECK(Near(b.Min, glm::vec3{ -1.5f, 0.0f, -2.5f }));
        CHECK(Near(b.Max, glm::vec3{  1.5f, 0.0f,  2.5f }));
        for (const MeshVertex& v : d.Vertices)
            CHECK(Near(v.Normal, glm::vec3{ 0.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("UV sphere: (rings+1)*(segments+1) verts, radial normals")
    {
        const float r = 1.5f;
        const uint32_t rings = 12, segs = 20;
        MeshData d = Mesh::BuildUVSphere(r, rings, segs);

        CHECK(d.Vertices.size() == (rings + 1) * (segs + 1));
        CheckTopology(d);
        CheckUnitNormals(d);

        for (const MeshVertex& v : d.Vertices)
        {
            CHECK(Near(glm::length(v.Position), r, 1e-3f));      // on the sphere
            CHECK(Near(v.Normal, v.Position / r, 1e-3f));        // normal points out
        }

        AABB b = Bounds(d);
        CHECK(Near(b.Min, glm::vec3(-r), 1e-3f));
        CHECK(Near(b.Max, glm::vec3( r), 1e-3f));
    }

    TEST_CASE("Cylinder / Cone: height bounds and radial extent")
    {
        MeshData cyl = Mesh::BuildCylinder(0.5f, 3.0f, 24);
        CheckTopology(cyl);
        CheckUnitNormals(cyl);
        AABB cb = Bounds(cyl);
        CHECK(Near(cb.Min.y, -1.5f, 1e-4f));
        CHECK(Near(cb.Max.y,  1.5f, 1e-4f));
        CHECK(Near(cb.Max.x,  0.5f, 1e-3f));

        MeshData cone = Mesh::BuildCone(0.75f, 2.0f, 24);
        CheckTopology(cone);
        AABB nb = Bounds(cone);
        CHECK(Near(nb.Min.y, -1.0f, 1e-4f));    // base
        CHECK(Near(nb.Max.y,  1.0f, 1e-4f));    // apex
        CHECK(Near(nb.Max.x,  0.75f, 1e-3f));
    }

    TEST_CASE("Torus: vert count, bounds, and normals point out from the tube")
    {
        const float R = 0.8f, r = 0.25f;
        const uint32_t segs = 32, sides = 16;
        MeshData d = Mesh::BuildTorus(R, r, segs, sides);

        CHECK(d.Vertices.size() == (segs + 1) * (sides + 1));
        CheckTopology(d);
        CheckUnitNormals(d);

        AABB b = Bounds(d);
        CHECK(Near(b.Max.x,  R + r, 1e-3f));    // outer radial extent
        CHECK(Near(b.Min.x, -(R + r), 1e-3f));
        CHECK(Near(b.Max.y,  r, 1e-3f));        // tube half-height
        CHECK(Near(b.Min.y, -r, 1e-3f));

        // Each normal must equal the unit vector from the nearest point on the
        // centre circle (radius R in the XZ plane) to the surface point.
        for (const MeshVertex& v : d.Vertices)
        {
            const glm::vec3 radial{ v.Position.x, 0.0f, v.Position.z };
            REQUIRE(glm::length(radial) > 1e-4f);
            const glm::vec3 tubeCenter = R * glm::normalize(radial);
            const glm::vec3 expected   = glm::normalize(v.Position - tubeCenter);
            CHECK(Near(v.Normal, expected, 2e-3f));
        }
    }

    TEST_CASE("Rebuild is deterministic (same params -> identical geometry)")
    {
        MeshData a = Mesh::BuildTorus(0.6f, 0.2f, 24, 12);
        MeshData b = Mesh::BuildTorus(0.6f, 0.2f, 24, 12);
        REQUIRE(a.Vertices.size() == b.Vertices.size());
        REQUIRE(a.Indices.size() == b.Indices.size());
        for (size_t i = 0; i < a.Vertices.size(); ++i)
        {
            CHECK(Near(a.Vertices[i].Position, b.Vertices[i].Position, 0.0f));
            CHECK(Near(a.Vertices[i].Normal,   b.Vertices[i].Normal,   0.0f));
        }
    }

    TEST_CASE("Degenerate subdivisions are clamped, not crashing")
    {
        MeshData s = Mesh::BuildUVSphere(1.0f, 0, 0);   // clamped to >= 3
        CHECK(s.Vertices.size() == (3 + 1) * (3 + 1));
        CheckTopology(s);

        MeshData t = Mesh::BuildTorus(1.0f, 0.3f, 1, 1); // clamped to >= 3
        CHECK(t.Vertices.size() == (3 + 1) * (3 + 1));
        CheckTopology(t);
    }
}
