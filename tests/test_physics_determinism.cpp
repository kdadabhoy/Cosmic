// test_physics_determinism.cpp — J9 phase determinism proof. Builds the DoD scene
// (a ground, a stack of dynamic boxes, and a character walking a fixed path) and
// runs it twice through the Scene physics session; the two runs must produce
// bit-identical transforms. Headless (Jolt never touches GL); single-threaded so
// the result is stable run-to-run (J1's cross-platform-deterministic flag +
// deterministic job order).

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "physics/CharacterController.h"

#include <glm/glm.hpp>
#include <vector>

using namespace Cosmic;

namespace
{
    struct Snapshot { std::vector<glm::vec3> pos; std::vector<glm::quat> rot; };

    Snapshot RunOnce()
    {
        Scene scene;

        // Ground.
        Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<TransformComponent>().Position = { 0, -0.5f, 0 };
        ground.AddComponent<RigidBodyComponent>(MotionType::Static);
        ground.AddComponent<BoxColliderComponent>().HalfExtents = { 30, 0.5f, 30 };

        // A stack of 8 dynamic boxes (a slight X jitter to make the settle non-trivial).
        for (int i = 0; i < 8; ++i)
        {
            Entity b = scene.CreateEntity("Box");
            b.GetComponent<TransformComponent>().Position = { 0.02f * (i % 2), 0.5f + i * 1.05f, 0 };
            b.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.2f;
            b.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
        }

        // A character that will walk a fixed path.
        Entity player = scene.CreateEntity("Player");
        player.GetComponent<TransformComponent>().Position = { 6, 0.1f, 6 };
        player.AddComponent<CharacterControllerComponent>();

        PhysicsWorld world;
        PhysicsSettings ps; ps.ThreadCount = 0;   // single-threaded => reproducible
        world.Init(ps);
        scene.OnPhysicsStart(world);
        CharacterController* ctrl = scene.GetPhysics()->GetCharacter((entt::entity)player);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i)
        {
            if (ctrl) ctrl->Move({ 2.0f, 0, i < 150 ? 0.0f : 1.5f });   // a deterministic path
            scene.OnPhysicsStep(dt);
        }

        Snapshot s;
        for (auto e : scene.GetRegistry().view<TransformComponent, RigidBodyComponent>())
        {
            const auto& t = scene.GetRegistry().get<TransformComponent>(e);
            s.pos.push_back(t.Position);
            s.rot.push_back(t.RotationQuat);
        }
        if (ctrl) s.pos.push_back(ctrl->GetPosition());

        scene.OnPhysicsStop(world);
        return s;
    }
}

TEST_SUITE("Physics / determinism proof (J9)")
{
    TEST_CASE("Two identical runs of the DoD scene are bit-for-bit identical")
    {
        Snapshot a = RunOnce();
        Snapshot b = RunOnce();

        REQUIRE(a.pos.size() == b.pos.size());
        REQUIRE(a.rot.size() == b.rot.size());
        REQUIRE(a.pos.size() >= 9u);   // 8 boxes + character (ground is static, no writeback anyway)

        for (size_t i = 0; i < a.pos.size(); ++i)
        {
            CHECK(a.pos[i].x == b.pos[i].x);
            CHECK(a.pos[i].y == b.pos[i].y);
            CHECK(a.pos[i].z == b.pos[i].z);
        }
        for (size_t i = 0; i < a.rot.size(); ++i)
        {
            CHECK(a.rot[i].x == b.rot[i].x);
            CHECK(a.rot[i].y == b.rot[i].y);
            CHECK(a.rot[i].z == b.rot[i].z);
            CHECK(a.rot[i].w == b.rot[i].w);
        }
    }
}
