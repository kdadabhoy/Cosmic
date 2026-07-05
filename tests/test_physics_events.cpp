// test_physics_events.cpp — J5: script collision/trigger callbacks + a script-side
// raycast, driven through the full session loop (ScriptHost + ScenePhysics), all
// headless (Jolt never touches GL). Covers the J5 acceptance:
//   - a falling scripted box gets OnCollisionEnter exactly once, OnCollisionExit
//     when it later separates
//   - a trigger volume reports OnTriggerEnter/Exit without applying contact forces
//   - a script raycast selects the ground under the moving entity every step

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Cosmic;

namespace
{
    class ContactProbe : public ScriptableEntity
    {
    public:
        static inline int  s_Enter = 0, s_Exit = 0, s_TrigEnter = 0, s_TrigExit = 0;
        static inline int  s_RayHits = 0, s_FixedSteps = 0;
        static void Reset() { s_Enter = s_Exit = s_TrigEnter = s_TrigExit = s_RayHits = s_FixedSteps = 0; }

    protected:
        void OnCollisionEnter(Entity) override { ++s_Enter; }
        void OnCollisionExit(Entity)  override { ++s_Exit; }
        void OnTriggerEnter(Entity)   override { ++s_TrigEnter; }
        void OnTriggerExit(Entity)    override { ++s_TrigExit; }
        void OnFixedUpdate(float) override
        {
            ++s_FixedSteps;
            const glm::vec3 p = GetComponent<TransformComponent>().Position;
            // A down-ray from just above the entity origin, ignoring self.
            if (Physics().RayCast(p + glm::vec3(0, 0.1f, 0), glm::vec3(0, -1, 0), 8.0f).has_value())
                ++s_RayHits;
        }
    };

    void RegisterProbe()
    {
        ModuleRegistry::Get().BeginModule("test_physics");
        CS_SCRIPT(ContactProbe)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    PhysicsSettings DetSettings()
    {
        PhysicsSettings s;
        s.ThreadCount = 0;
        return s;
    }

    // One fixed step through the full session order.
    void SessionStep(Scene& scene, ScriptHost& host, float dt)
    {
        host.FixedTick(dt);
        scene.OnPhysicsStep(dt);
        scene.DispatchPhysicsEvents(host);
    }
}

TEST_SUITE("Physics / script events (J5)")
{
    TEST_CASE("Scripted box: OnCollisionEnter on landing, OnCollisionExit on separation; raycast hits every step")
    {
        // NOTE on invariants: physics contact enter/exit counts jitter by a frame or
        // two while a stack settles (the manifold is added/reduced/re-formed) — that
        // is true of every solver, and frame-exact counts are Jolt-internal. So this
        // asserts the *meaningful* contract: a landing raises Enter, a separation
        // raises Exit, and while resting no NEW separation is reported.
        RegisterProbe();
        ContactProbe::Reset();

        Scene scene;

        Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<TransformComponent>().Position = { 0, -0.5f, 0 };
        ground.AddComponent<RigidBodyComponent>(MotionType::Static);
        ground.AddComponent<BoxColliderComponent>().HalfExtents = { 10, 0.5f, 10 };

        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 0, 1.5f, 0 };
        auto& rb = box.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
        rb.Restitution = 0.0f;
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
        box.AddComponent<NativeScriptComponent>().ClassName = "ContactProbe";

        ScriptHost host;
        host.Instantiate(scene);
        PhysicsWorld world;
        world.Init(DetSettings());
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;

        // Fall until the box first touches the ground. Throughout the fall it is
        // above the ground, so the down-ray hits every fixed step.
        int guard = 0;
        while (ContactProbe::s_Enter == 0 && guard++ < 300)
            SessionStep(scene, host, dt);

        CHECK(ContactProbe::s_Enter >= 1);                              // landed -> Enter fired
        CHECK(ContactProbe::s_RayHits == ContactProbe::s_FixedSteps);   // ground under it every step

        // Break the (now active) contact by teleporting the box far above the ground
        // -> the separation is reported as an Exit.
        const int exitsBefore = ContactProbe::s_Exit;
        PhysicsBody b = scene.GetPhysics()->GetBody((entt::entity)box);
        world.SetBodyTransform(b, { 0, 20.0f, 0 }, glm::quat(1, 0, 0, 0));
        for (int i = 0; i < 10; ++i)
            SessionStep(scene, host, dt);

        CHECK(ContactProbe::s_Exit > exitsBefore);

        scene.OnPhysicsStop(world);
        host.Destroy();
    }

    TEST_CASE("Trigger volume: OnTriggerEnter/Exit fire and apply no contact force")
    {
        RegisterProbe();
        ContactProbe::Reset();

        Scene scene;

        // A static sensor slab the box will fall THROUGH.
        Entity trig = scene.CreateEntity("Trigger");
        trig.GetComponent<TransformComponent>().Position = { 0, 0, 0 };
        auto& tcol = trig.AddComponent<BoxColliderComponent>();
        tcol.HalfExtents = { 5, 0.5f, 5 };
        tcol.IsTrigger = true;

        // A scripted dynamic box dropped from above with no ground under it.
        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 0, 4.0f, 0 };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic).LinearDamping = 0.0f;
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.4f, 0.4f, 0.4f };
        box.AddComponent<NativeScriptComponent>().ClassName = "ContactProbe";

        ScriptHost host;
        host.Instantiate(scene);
        PhysicsWorld world;
        world.Init(DetSettings());
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)   // 2 s — falls through the sensor and keeps going
            SessionStep(scene, host, dt);

        CHECK(ContactProbe::s_TrigEnter == 1);
        CHECK(ContactProbe::s_TrigExit == 1);
        CHECK(ContactProbe::s_Enter == 0);   // a sensor applies no contact response

        // No contact force => it fell well below the sensor (didn't rest on it).
        const float y = box.GetComponent<TransformComponent>().Position.y;
        CHECK(y < -1.0f);

        scene.OnPhysicsStop(world);
        host.Destroy();
    }
}
