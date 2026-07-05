// test_physics_scene.cpp — J3/J4: physics components + scene/session integration.
// Headless (Jolt never touches GL). Covers:
//   J3 — physics components round-trip the scene serializer (incl. an empty one)
//   J4 — a ground + falling boxes scene: OnPhysicsStart builds bodies, OnPhysicsStep
//        settles them onto the ground, OnPhysicsStop tears down cleanly
//   J4 — determinism: two identical runs produce identical positions
// World-coordinate tolerances are ABSOLUTE (doctest Approx.epsilon is relative).

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "physics/PhysicsWorld.h"

#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    PhysicsSettings DetSettings()
    {
        PhysicsSettings s;
        s.ThreadCount = 0;   // single-threaded => bit-stable across runs
        return s;
    }
}

TEST_SUITE("Physics / components + scene (J3/J4)")
{
    TEST_CASE("Physics components round-trip the serializer")
    {
        Scene scene;
        Entity e = scene.CreateEntity("Crate");

        auto& rb = e.AddComponent<RigidBodyComponent>();
        rb.Motion = MotionType::Dynamic;
        rb.Mass = 12.5f;
        rb.Friction = 0.33f;
        rb.Restitution = 0.42f;
        rb.CCD = true;
        rb.CollisionCategory = 0x0004;
        rb.CollidesWith = 0x00F0;

        auto& box = e.AddComponent<BoxColliderComponent>();
        box.HalfExtents = { 0.25f, 0.75f, 1.5f };
        box.Offset = { 0.1f, 0.0f, -0.2f };
        box.IsTrigger = true;

        e.AddComponent<TerrainColliderComponent>();   // empty component

        auto& cc = e.AddComponent<CharacterControllerComponent>();
        cc.Height = 2.1f; cc.Radius = 0.4f; cc.StepHeight = 0.5f;

        const std::string saved = SceneSerializer::SaveToString(scene);

        Scene scene2;
        REQUIRE(SceneSerializer::LoadFromString(scene2, saved));

        // Find the reloaded entity (only one).
        auto view = scene2.GetRegistry().view<RigidBodyComponent>();
        REQUIRE(view.size() == 1u);
        entt::entity h = *view.begin();

        const auto& rb2 = scene2.GetRegistry().get<RigidBodyComponent>(h);
        CHECK(rb2.Motion == MotionType::Dynamic);
        CHECK(rb2.Mass == doctest::Approx(12.5f));
        CHECK(rb2.Friction == doctest::Approx(0.33f));
        CHECK(rb2.Restitution == doctest::Approx(0.42f));
        CHECK(rb2.CCD == true);
        CHECK(rb2.CollisionCategory == 0x0004u);
        CHECK(rb2.CollidesWith == 0x00F0u);

        REQUIRE(scene2.GetRegistry().all_of<BoxColliderComponent>(h));
        const auto& box2 = scene2.GetRegistry().get<BoxColliderComponent>(h);
        CHECK(box2.HalfExtents.z == doctest::Approx(1.5f));
        CHECK(box2.Offset.x == doctest::Approx(0.1f));
        CHECK(box2.IsTrigger == true);

        CHECK(scene2.GetRegistry().all_of<TerrainColliderComponent>(h));   // empty comp survived

        REQUIRE(scene2.GetRegistry().all_of<CharacterControllerComponent>(h));
        CHECK(scene2.GetRegistry().get<CharacterControllerComponent>(h).Height == doctest::Approx(2.1f));
    }

    // Build a ground + N dynamic boxes scene programmatically.
    static void BuildDropScene(Scene& scene, int n)
    {
        Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<TransformComponent>().Position = { 0, -0.5f, 0 };
        ground.AddComponent<RigidBodyComponent>(MotionType::Static);
        ground.AddComponent<BoxColliderComponent>().HalfExtents = { 25, 0.5f, 25 };

        for (int i = 0; i < n; ++i)
        {
            Entity b = scene.CreateEntity("Box");
            b.GetComponent<TransformComponent>().Position = { 0, 2.0f + i * 1.2f, 0 };
            auto& rb = b.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
            rb.Restitution = 0.0f;
            b.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
        }
    }

    TEST_CASE("A dropped stack settles onto the ground through the scene session")
    {
        Scene scene;
        BuildDropScene(scene, 5);

        PhysicsWorld world;
        world.Init(DetSettings());
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 480; ++i)
            scene.OnPhysicsStep(dt);

        // Every dynamic box wrote its transform back and is resting above the ground.
        int boxes = 0;
        for (auto e : scene.GetRegistry().view<RigidBodyComponent, TransformComponent>())
        {
            const auto& rb = scene.GetRegistry().get<RigidBodyComponent>(e);
            if (rb.Motion != MotionType::Dynamic) continue;
            ++boxes;
            const auto& tc = scene.GetRegistry().get<TransformComponent>(e);
            CHECK(tc.Position.y > 0.4f);      // above the ground surface (y = 0)
            CHECK(tc.Position.y < 10.0f);     // didn't fly off
            CHECK(tc.UseQuatRotation);        // physics writes the quat slot
        }
        CHECK(boxes == 5);

        scene.OnPhysicsStop(world);
        // After stop the bodies are gone; stepping again is a no-op (no crash).
        scene.OnPhysicsStep(dt);
    }

    TEST_CASE("Two identical runs are deterministic")
    {
        auto run = []()
        {
            Scene scene;
            BuildDropScene(scene, 6);
            PhysicsWorld world;
            world.Init(DetSettings());
            scene.OnPhysicsStart(world);
            const float dt = 1.0f / 60.0f;
            for (int i = 0; i < 300; ++i)
                scene.OnPhysicsStep(dt);

            std::vector<glm::vec3> out;
            for (auto e : scene.GetRegistry().view<RigidBodyComponent, TransformComponent>())
                out.push_back(scene.GetRegistry().get<TransformComponent>(e).Position);
            scene.OnPhysicsStop(world);
            return out;
        };

        std::vector<glm::vec3> a = run();
        std::vector<glm::vec3> b = run();
        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].x == b[i].x);   // bit-exact
            CHECK(a[i].y == b[i].y);
            CHECK(a[i].z == b[i].z);
        }
    }
}
