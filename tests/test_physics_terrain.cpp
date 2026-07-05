// test_physics_terrain.cpp — J7: TerrainCollider builds a Jolt HeightFieldShape
// from the SAME CPU heightfield the renderer samples. Headless (no GL): a Terrain
// is built on the CPU, wrapped in a TerrainComponent + TerrainColliderComponent,
// and the Jolt collider surface is probed by ray and compared to
// Terrain::SampleHeight at random points (the parity acceptance).
// World-coordinate tolerances are ABSOLUTE (doctest Approx.epsilon is relative).

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "terrain/Terrain.h"

#include <random>
#include <cmath>

using namespace Cosmic;

TEST_SUITE("Physics / terrain collider (J7)")
{
    TEST_CASE("Jolt heightfield matches Terrain::SampleHeight within 2 cm")
    {
        // A gentle rolling terrain (smooth enough that the collider's triangulation
        // and the renderer's agree to well under 2 cm at any interior point).
        TerrainSpecification spec;
        spec.Resolution  = 129;      // 32*2^k + 1
        spec.WorldSize   = 128.0f;   // 1 m sample spacing
        spec.HeightScale = 8.0f;
        spec.BaseHeight  = 0.0f;
        spec.Origin      = { 0.0f, 0.0f };
        spec.Seed        = 7;
        spec.Octaves     = 2;
        spec.Frequency   = 1.0f;     // gentle
        Ref<Terrain> terr = Terrain::Create(spec);
        REQUIRE(terr != nullptr);

        Scene scene;
        Entity e = scene.CreateEntity("Terrain");
        auto& tc = e.AddComponent<TerrainComponent>();
        tc.TerrainAsset = terr;                 // code-set: UseRecipe stays false
        e.AddComponent<TerrainColliderComponent>();

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;
        world.Init(ps);
        scene.OnPhysicsStart(world);

        // The collider drops the far +X/+Z edge row, so probe the interior only.
        const glm::vec2 corner = terr->GetWorldMinCorner();
        const float span = spec.WorldSize - 2.0f;   // stay a metre inside both edges

        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> u(1.0f, span);
        int checked = 0;
        float worst = 0.0f;
        for (int i = 0; i < 100; ++i)
        {
            const float x = corner.x + u(rng);
            const float z = corner.y + u(rng);
            const float expected = terr->SampleHeight(x, z);

            auto hit = world.RayCast({ x, 100.0f, z }, { 0, -1, 0 }, 1000.0f);
            REQUIRE(hit.has_value());
            const float err = std::fabs(hit->Point.y - expected);
            worst = std::max(worst, err);
            CHECK(err < 0.02f);
            ++checked;
        }
        CHECK(checked == 100);
        INFO("worst height error (m): " << worst);

        scene.OnPhysicsStop(world);
    }

    TEST_CASE("A dynamic box comes to rest on the terrain surface")
    {
        TerrainSpecification spec;
        spec.Resolution  = 65;
        spec.WorldSize   = 64.0f;
        spec.HeightScale = 4.0f;
        spec.Origin      = { 0.0f, 0.0f };
        spec.Seed        = 3;
        spec.Octaves     = 2;
        spec.Frequency   = 1.0f;
        Ref<Terrain> terr = Terrain::Create(spec);
        REQUIRE(terr != nullptr);

        Scene scene;
        Entity ground = scene.CreateEntity("Terrain");
        ground.AddComponent<TerrainComponent>().TerrainAsset = terr;
        ground.AddComponent<TerrainColliderComponent>();

        Entity box = scene.CreateEntity("Box");
        const float dropX = 4.0f, dropZ = -3.0f;
        box.GetComponent<TransformComponent>().Position = { dropX, 30.0f, dropZ };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.0f;
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;
        world.Init(ps);
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i)
            scene.OnPhysicsStep(dt);

        // Rests roughly a half-extent above the ground beneath the drop point.
        const float ground_y = terr->SampleHeight(dropX, dropZ);
        const float rest_y = box.GetComponent<TransformComponent>().Position.y;
        CHECK(rest_y > ground_y + 0.2f);
        CHECK(rest_y < ground_y + 1.0f);

        scene.OnPhysicsStop(world);
    }
}
