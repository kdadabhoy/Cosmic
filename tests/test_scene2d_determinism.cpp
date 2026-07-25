// test_scene2d_determinism.cpp — the 2D stack's determinism proof (Phase 29 W2 / §9.2).
//
// test_physics_determinism.cpp proves the 3D physics session is bit-identical
// run-to-run. This is its 2D counterpart, and it covers the WHOLE 2D play loop
// rather than physics alone: native scripts, the flow machine's variables and
// timers, sprite animation clocks, the XY-constrained physics session, and the
// painter list the frame is drawn from.
//
// Headless (no GL) and single-threaded (PhysicsSettings::ThreadCount = 0), so
// the only thing that could make two runs differ is the engine itself. That
// makes this the net under W5's Scene.cpp -> Scene3D.cpp split: any ordering or
// state that leaks out of the 2D half shows up here as a mismatch.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/FlowMachine.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "reflect/TypeRegistry.h"

#include <glm/glm.hpp>
#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    // A deterministic 2D "gameplay" script: walks its entity along a fixed
    // lissajous path in XY (no randomness, no wall clock) and retimes its own
    // sprite ZOrder from the path, so the painter list changes over the run.
    class Wanderer2D : public ScriptableEntity
    {
    public:
        float Rate  = 1.0f;
        float Phase = 0.0f;

    protected:
        void OnUpdate(float ts) override
        {
            m_T += ts * Rate;
            auto& t = GetComponent<TransformComponent>();
            // A closed-form path — no accumulation of the transform itself, so
            // the only float history is m_T.
            t.Position.x = 3.0f * std::sin(m_T + Phase);
            t.Position.y = 2.0f * std::sin(2.0f * m_T + Phase);
            if (HasComponent<SpriteRendererComponent>())
                GetComponent<SpriteRendererComponent>().ZOrder = (int32_t)(t.Position.y * 4.0f);
        }

    private:
        float m_T = 0.0f;
    };

    void RegisterWanderer()
    {
        ModuleRegistry::Get().BeginModule("test2d");
        CS_SCRIPT(Wanderer2D)
            CS_FIELD(Rate).Range(0.0f, 10.0f)
            CS_FIELD(Phase)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    // A two-state flow with a timer and a variable, driven off the same clock as
    // the scene. Its scene loader is a no-op (the flow's own scene switching is
    // covered by test_flowmachine); what matters here is that its timers and
    // variables land on the same values every run.
    const char* kFlow = R"({
      "cosmic_flow": 1,
      "start": "Playing",
      "variables": [ { "name": "score", "type": "number", "default": 0 } ],
      "states": [
        { "name": "Playing",
          "transitions": [ { "on": "timer:1.5", "to": "Scored" } ] },
        { "name": "Scored",
          "onEnter": [ { "setVar": { "var": "score", "value": 7 } } ],
          "transitions": [ { "on": "timer:1.0", "to": "Playing" } ] }
      ]
    })";

    // Everything observable about one run, captured bit-exactly.
    struct Snapshot
    {
        std::vector<glm::vec3> Positions;      // every transform, registry order
        std::vector<float>     AnimElapsed;
        std::vector<uint32_t>  DrawOrder;      // BuildSpriteDrawList entity ids
        std::vector<int32_t>   DrawZ;
        std::string            FlowState;
        double                 FlowScore = 0.0;
    };

    Snapshot RunOnce()
    {
        RegisterWanderer();

        Ref<Scene> scene = Scene::Create();

        // --- sprites + a scripted wanderer each ------------------------------
        for (int i = 0; i < 6; ++i)
        {
            Entity e = scene->CreateEntity("wanderer" + std::to_string(i));
            e.GetComponent<TransformComponent>().Position = { (float)i, 0.0f, 0.0f };
            auto& sr = e.AddComponent<SpriteRendererComponent>();
            sr.ZOrder = i;
            sr.YSort  = (i % 2) == 0;
            auto& nsc = e.AddComponent<NativeScriptComponent>();
            nsc.ClassName = "Wanderer2D";
            nsc.Fields["Rate"]  = Reflect::FieldValue{ 0.5f + 0.25f * (float)i };
            nsc.Fields["Phase"] = Reflect::FieldValue{ 0.37f * (float)i };
        }

        // --- a tilemap that interleaves with them ----------------------------
        {
            Entity map = scene->CreateEntity("map");
            map.GetComponent<TransformComponent>().Position = { -8.0f, -6.0f, 0.0f };
            auto& tm = map.AddComponent<TilemapComponent>();
            tm.GridW = 16; tm.GridH = 16; tm.ZOrder = -2;
            tm.EnsureCells();
            for (size_t c = 0; c < tm.Cells.size(); ++c)
                tm.Cells[c] = (uint16_t)(c % 5);
        }

        // --- a flipbook clock ------------------------------------------------
        {
            Entity anim = scene->CreateEntity("flipbook");
            anim.AddComponent<SpriteRendererComponent>().ZOrder = 20;
            auto& sa = anim.AddComponent<SpriteAnimationComponent>();
            sa.Frames = 6; sa.FPS = 12.0f; sa.Loop = true;
        }

        // --- 2D physics: boxes falling onto a floor in the XY plane -----------
        // Gravity is -Y and nothing pushes along Z, so this is the 2D case even
        // though the solver is the shared 3D one (Phase 29 decision 3 keeps Jolt
        // on both engines). The assertion here is reproducibility, not planarity.
        {
            Entity floor = scene->CreateEntity("floor");
            floor.GetComponent<TransformComponent>().Position = { 0.0f, -4.0f, 0.0f };
            floor.AddComponent<RigidBodyComponent>(MotionType::Static);
            floor.AddComponent<BoxColliderComponent>().HalfExtents = { 20.0f, 0.5f, 1.0f };

            for (int i = 0; i < 4; ++i)
            {
                Entity b = scene->CreateEntity("box" + std::to_string(i));
                b.GetComponent<TransformComponent>().Position = { -1.5f + i * 1.01f, 2.0f + i, 0.0f };
                b.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.15f;
                b.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
                b.AddComponent<SpriteRendererComponent>().ZOrder = 5 + i;
            }
        }

        // --- start the session -----------------------------------------------
        ScriptHost host;
        host.Instantiate(*scene);

        PhysicsWorld world;
        PhysicsSettings ps;
        ps.ThreadCount = 0;                       // single-threaded => reproducible
        world.Init(ps);
        scene->OnPhysicsStart(world);

        FlowAsset flow;
        std::string err;
        REQUIRE(FlowAsset::LoadFromString(flow, kFlow, &err));
        FlowMachine machine;
        machine.SetSceneLoader([](const std::string&) -> Ref<Scene> { return nullptr; });
        machine.Start(flow);

        // --- the loop: 180 fixed 60 Hz frames --------------------------------
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 180; ++i)
        {
            host.Tick(dt);
            scene->UpdateSpriteAnimations(dt);
            machine.OnUpdate(dt);
            scene->OnPhysicsStep(dt);
        }

        // --- capture -----------------------------------------------------------
        Snapshot s;
        for (auto e : scene->GetRegistry().view<TransformComponent>())
            s.Positions.push_back(scene->GetRegistry().get<TransformComponent>(e).Position);
        for (auto e : scene->GetRegistry().view<SpriteAnimationComponent>())
            s.AnimElapsed.push_back(scene->GetRegistry().get<SpriteAnimationComponent>(e).Elapsed);
        for (const auto& item : scene->BuildSpriteDrawList())
        {
            s.DrawOrder.push_back((uint32_t)item.E);
            s.DrawZ.push_back(item.Z);
        }
        s.FlowState = machine.CurrentState();
        if (machine.HasVar("score"))
            s.FlowScore = machine.GetVar("score").Number;

        machine.Stop();
        scene->OnPhysicsStop(world);
        host.Destroy();
        return s;
    }
}

TEST_SUITE("2D scene determinism (W2)")
{
    TEST_CASE("two runs of a scripted 2D scene are bit-for-bit identical")
    {
        const Snapshot a = RunOnce();
        const Snapshot b = RunOnce();

        REQUIRE(a.Positions.size() == b.Positions.size());
        REQUIRE(a.Positions.size() >= 11u);   // 6 wanderers + map + flipbook + floor + 4 boxes
        for (size_t i = 0; i < a.Positions.size(); ++i)
        {
            CHECK(a.Positions[i].x == b.Positions[i].x);
            CHECK(a.Positions[i].y == b.Positions[i].y);
            CHECK(a.Positions[i].z == b.Positions[i].z);
        }

        REQUIRE(a.AnimElapsed.size() == b.AnimElapsed.size());
        for (size_t i = 0; i < a.AnimElapsed.size(); ++i)
            CHECK(a.AnimElapsed[i] == b.AnimElapsed[i]);

        REQUIRE(a.DrawOrder.size() == b.DrawOrder.size());
        REQUIRE(a.DrawOrder.size() >= 11u);
        for (size_t i = 0; i < a.DrawOrder.size(); ++i)
        {
            CHECK(a.DrawOrder[i] == b.DrawOrder[i]);
            CHECK(a.DrawZ[i]     == b.DrawZ[i]);
        }

        CHECK(a.FlowState == b.FlowState);
        CHECK(a.FlowScore == b.FlowScore);
    }

    TEST_CASE("the run actually moved: the snapshot is not a pile of defaults")
    {
        // A determinism test that captured nothing would also be "identical", so
        // pin that the scene really simulated.
        const Snapshot s = RunOnce();

        bool anyMoved = false;
        for (const glm::vec3& p : s.Positions)
            if (p.x != 0.0f || p.y != 0.0f)
                anyMoved = true;
        CHECK(anyMoved);

        for (float e : s.AnimElapsed)
            CHECK(e == doctest::Approx(3.0f));           // 180 * 1/60

        CHECK_FALSE(s.DrawOrder.empty());
        CHECK_FALSE(s.FlowState.empty());
        CHECK(s.FlowScore == 7);                         // the timer fired

        // The scripts reordered the painter list away from its authored order.
        bool anyNegativeZ = false;
        for (int32_t z : s.DrawZ)
            if (z < 0) anyNegativeZ = true;
        CHECK(anyNegativeZ);                             // the tilemap at -2, at least
    }
}
