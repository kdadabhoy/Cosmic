// test_physics_character.cpp — J6: the character controller (Jolt CharacterVirtual)
// driven through the Scene session. Headless (no GL). Covers the core mechanics the
// WalkController sample relies on: gravity + ground rest, flat walking, wall block
// (no tunnelling at speed), and slope climbing. Slope/step edge cases at the visual
// level are the sample's on-GPU acceptance.

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "physics/CharacterController.h"

#include <glm/glm.hpp>

using namespace Cosmic;

namespace
{
    PhysicsSettings DetSettings()
    {
        PhysicsSettings s;
        s.ThreadCount = 0;
        return s;
    }

    Entity MakeGround(Scene& scene, const glm::vec3& pos, const glm::vec3& half)
    {
        Entity g = scene.CreateEntity("Ground");
        g.GetComponent<TransformComponent>().Position = pos;
        g.AddComponent<RigidBodyComponent>(MotionType::Static);
        g.AddComponent<BoxColliderComponent>().HalfExtents = half;
        return g;
    }

    Entity MakeCharacter(Scene& scene, const glm::vec3& pos)
    {
        Entity c = scene.CreateEntity("Player");
        c.GetComponent<TransformComponent>().Position = pos;
        auto& cc = c.AddComponent<CharacterControllerComponent>();
        cc.Height = 1.8f; cc.Radius = 0.3f; cc.MaxSlopeDeg = 45.0f; cc.StepHeight = 0.35f;
        return c;
    }
}

TEST_SUITE("Physics / character controller (J6)")
{
    TEST_CASE("Rests on the ground under gravity and reports grounded")
    {
        Scene scene;
        MakeGround(scene, { 0, -0.5f, 0 }, { 20, 0.5f, 20 });   // top at y = 0
        Entity player = MakeCharacter(scene, { 0, 1.0f, 0 });   // dropped from 1 m

        PhysicsWorld world; world.Init(DetSettings());
        scene.OnPhysicsStart(world);
        CharacterController* ctrl = scene.GetPhysics()->GetCharacter((entt::entity)player);
        REQUIRE(ctrl != nullptr);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)
        {
            ctrl->Move({ 0, 0, 0 });
            scene.OnPhysicsStep(dt);
        }

        CHECK(ctrl->IsGrounded());
        CHECK(ctrl->GetPosition().y == doctest::Approx(0.0f).epsilon(0.05));   // feet on the ground
        scene.OnPhysicsStop(world);
    }

    TEST_CASE("Walks along flat ground")
    {
        Scene scene;
        MakeGround(scene, { 0, -0.5f, 0 }, { 40, 0.5f, 40 });
        Entity player = MakeCharacter(scene, { 0, 0.1f, 0 });

        PhysicsWorld world; world.Init(DetSettings());
        scene.OnPhysicsStart(world);
        CharacterController* ctrl = scene.GetPhysics()->GetCharacter((entt::entity)player);
        REQUIRE(ctrl != nullptr);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 90; ++i)   // 1.5 s at 2 m/s -> ~3 m
        {
            ctrl->Move({ 2.0f, 0, 0 });
            scene.OnPhysicsStep(dt);
        }
        CHECK(ctrl->GetPosition().x > 2.0f);
        scene.OnPhysicsStop(world);
    }

    TEST_CASE("Blocked by a wall — no tunnelling at speed")
    {
        Scene scene;
        MakeGround(scene, { 0, -0.5f, 0 }, { 40, 0.5f, 40 });
        MakeGround(scene, { 3.0f, 2.0f, 0 }, { 0.5f, 2.5f, 5 });   // wall, near face at x = 2.5
        Entity player = MakeCharacter(scene, { 0, 0.1f, 0 });

        PhysicsWorld world; world.Init(DetSettings());
        scene.OnPhysicsStart(world);
        CharacterController* ctrl = scene.GetPhysics()->GetCharacter((entt::entity)player);
        REQUIRE(ctrl != nullptr);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)   // charge the wall fast
        {
            ctrl->Move({ 12.0f, 0, 0 });
            scene.OnPhysicsStep(dt);
        }
        // Stopped on the near side (radius 0.3 => ~2.2), never punched through to x > 3.
        CHECK(ctrl->GetPosition().x < 2.5f);
        CHECK(ctrl->GetPosition().x > 1.0f);   // but it did travel toward the wall
        scene.OnPhysicsStop(world);
    }

    TEST_CASE("Climbs a gentle slope")
    {
        Scene scene;

        // A wide 25-degree ramp centred at the origin. Rotating +25 deg about Z makes
        // the +X end of the top surface the high end, so walking +X goes uphill (25 <
        // the 45-degree MaxSlope, so it is walkable and the character won't slide).
        Entity ramp = scene.CreateEntity("Ramp");
        auto& rt = ramp.GetComponent<TransformComponent>();
        rt.Position = { 0.0f, 0.0f, 0.0f };
        rt.Rotation = { 0.0f, 0.0f, 25.0f };
        ramp.AddComponent<RigidBodyComponent>(MotionType::Static);
        ramp.AddComponent<BoxColliderComponent>().HalfExtents = { 12.0f, 0.5f, 5.0f };

        Entity player = MakeCharacter(scene, { 0, 2.0f, 0 });   // drop onto the ramp near x = 0

        PhysicsWorld world; world.Init(DetSettings());
        scene.OnPhysicsStart(world);
        CharacterController* ctrl = scene.GetPhysics()->GetCharacter((entt::entity)player);
        REQUIRE(ctrl != nullptr);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 60; ++i)   // settle onto the ramp surface
        {
            ctrl->Move({ 0, 0, 0 });
            scene.OnPhysicsStep(dt);
        }
        const float startY = ctrl->GetPosition().y;
        const float startX = ctrl->GetPosition().x;

        for (int i = 0; i < 240; ++i)   // 4 s walking up
        {
            ctrl->Move({ 3.0f, 0, 0 });
            scene.OnPhysicsStep(dt);
        }
        // Gained height by climbing and advanced up-ramp.
        CHECK(ctrl->GetPosition().y > startY + 0.3f);
        CHECK(ctrl->GetPosition().x > startX + 1.0f);
        scene.OnPhysicsStop(world);
    }
}
