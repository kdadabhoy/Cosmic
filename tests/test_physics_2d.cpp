// test_physics_2d.cpp — W3: physics behaves correctly for a 2D game.
//
// Phase 29 decision 3 keeps Jolt on BOTH engine branches because physics is
// dimension-agnostic: a 2D game uses the same RigidBody + Box/Sphere/Capsule
// colliders, confined to the XY plane. This file is the proof for that claim, and
// the regression net for the 2D engine's physics once engine-2d is cut in W8.
//
//   - a body dropped in XY falls along -Y and lands on the ground
//   - the depth axis holds over 600 fixed steps (10 s): the contact solver leaves a
//     sub-millimetre to millimetre offset at the landing, and then it STOPS — the
//     drift is bounded and is not a rate. The body never tips out of the plane
//   - a 2D trigger volume raises OnTriggerEnter and OnTriggerExit exactly once
//   - two runs with ThreadCount = 0 are bit-identical
//
// Headless (Jolt never touches GL). World-coordinate tolerances are ABSOLUTE —
// doctest's Approx epsilon is relative. The per-step comparisons are aggregated to
// a first-mismatch index rather than one CHECK per sample, so a failure names the
// step it happened on instead of burying it in thousands of passes.

#include "doctest.h"

#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Cosmic;

namespace
{
    constexpr float kDt      = 1.0f / 60.0f;
    constexpr int   kSteps   = 600;            // 10 s
    constexpr float kShoveVx = 3.0f;           // the sliding case's initial X velocity

    PhysicsSettings Settings2D()
    {
        PhysicsSettings s;
        s.ThreadCount = 0;                     // single-threaded => bit-stable run to run
        return s;                              // gravity stays the default (0, -9.81, 0)
    }

    struct Sample { glm::vec3 Pos; glm::quat Rot; };

    struct Run
    {
        std::vector<Sample> Steps;
        float MaxAbsZ    = 0.0f;   // worst |z| at ANY step, not just the last: a
        float MaxOutOfPlaneQ = 0.0f;   // drift that cancels out still has to fail
    };

    // The 2D playfield, authored the way a 2D scene is — everything at z = 0: a
    // wide, thin ground slab whose top face is at y = 0, and one dynamic box above
    // it, optionally shoved along +X the way character input drives a body. Runs
    // `kSteps` fixed steps and records the box's pose after each one.
    Run RunPlayfield(float initialVx)
    {
        Scene scene;

        Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<TransformComponent>().Position = { 0.0f, -0.5f, 0.0f };
        ground.AddComponent<RigidBodyComponent>(MotionType::Static);
        ground.AddComponent<BoxColliderComponent>().HalfExtents = { 40.0f, 0.5f, 40.0f };

        Entity box = scene.CreateEntity("Player");
        box.GetComponent<TransformComponent>().Position = { -2.0f, 6.0f, 0.0f };
        auto& rb = box.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
        rb.Restitution = 0.0f;                 // no bounce: a platformer landing
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };

        PhysicsWorld world;
        world.Init(Settings2D());
        scene.OnPhysicsStart(world);

        const PhysicsBody body = scene.GetPhysics()->GetBody((entt::entity)box);
        REQUIRE(body.IsValid());
        world.SetLinearVelocity(body, { initialVx, 0.0f, 0.0f });

        Run r;
        r.Steps.reserve(kSteps);
        for (int i = 0; i < kSteps; ++i)
        {
            scene.OnPhysicsStep(kDt);
            const auto& t = box.GetComponent<TransformComponent>();
            r.Steps.push_back({ t.Position, t.RotationQuat });
            r.MaxAbsZ = std::max(r.MaxAbsZ, std::abs(t.Position.z));
            // A quaternion that only spins about z has x == y == 0; anything else is
            // the body tipping out of the XY plane.
            r.MaxOutOfPlaneQ = std::max(r.MaxOutOfPlaneQ,
                                        std::max(std::abs(t.RotationQuat.x), std::abs(t.RotationQuat.y)));
        }

        scene.OnPhysicsStop(world);
        return r;
    }

    // First index at which two runs disagree on any component, or -1 when identical.
    int FirstMismatch(const Run& a, const Run& b)
    {
        const size_t n = std::min(a.Steps.size(), b.Steps.size());
        for (size_t i = 0; i < n; ++i)
        {
            const Sample& x = a.Steps[i];
            const Sample& y = b.Steps[i];
            if (x.Pos.x != y.Pos.x || x.Pos.y != y.Pos.y || x.Pos.z != y.Pos.z ||
                x.Rot.x != y.Rot.x || x.Rot.y != y.Rot.y || x.Rot.z != y.Rot.z || x.Rot.w != y.Rot.w)
                return (int)i;
        }
        return -1;
    }

    // ------------------------------------------------------------------------
    // Trigger probe (2D sensor volume).
    // ------------------------------------------------------------------------
    class Probe2D : public ScriptableEntity
    {
    public:
        static inline int s_TrigEnter = 0, s_TrigExit = 0, s_Enter = 0;
        static void Reset() { s_TrigEnter = s_TrigExit = s_Enter = 0; }

    protected:
        void OnTriggerEnter(Entity)   override { ++s_TrigEnter; }
        void OnTriggerExit(Entity)    override { ++s_TrigExit; }
        void OnCollisionEnter(Entity) override { ++s_Enter; }
    };

    void RegisterProbe2D()
    {
        ModuleRegistry::Get().BeginModule("test_physics_2d");
        CS_SCRIPT(Probe2D)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }
}

TEST_SUITE("Physics / 2D plane (W3)")
{
    TEST_CASE("Gravity pulls along -Y and the body lands on the ground")
    {
        const Run r = RunPlayfield(kShoveVx);
        REQUIRE(r.Steps.size() == (size_t)kSteps);

        // It fell (started at y = 6) ...
        CHECK(r.Steps.front().Pos.y < 6.0f);

        // ... monotonically while airborne — free fall, no sideways lift.
        int nonDescending = 0;
        for (int i = 1; i < 20; ++i)
            if (r.Steps[i].Pos.y >= r.Steps[i - 1].Pos.y) ++nonDescending;
        CHECK(nonDescending == 0);

        // ... and came to rest on the slab, its lower face on the top face (y = 0).
        const Sample& rest = r.Steps.back();
        CHECK(std::abs(rest.Pos.y - 0.5f) < 0.02f);

        // The X shove actually moved it, so this is a genuine XY trajectory and not
        // a body that simply dropped straight down.
        CHECK(rest.Pos.x > r.Steps.front().Pos.x);
    }

    TEST_CASE("The depth axis never drifts over 600 steps")
    {
        // The authored z is 0 and nothing in the scene pushes along z. A 2D game
        // relies on this: sprites are placed by z-order, and a body that crept along
        // z would silently reorder the scene.
        //
        // It is NOT exactly zero, and pretending otherwise would make this test a
        // lie. Contacts are solved by sequential impulses over a 4-point manifold
        // whose iteration order is not z-symmetric, so a little of each normal and
        // friction impulse lands on z. That is true of every such solver. What
        // matters for a 2D game is that the offset is (a) sub-millimetre-to-
        // millimetre and (b) NOT a rate — it happens at the landing and then stops.
        // Both are asserted below, and the numbers in the comments are MEASURED on
        // this build, not assumed.
        //
        // Bounds are set an order of magnitude above the measurement so a Jolt
        // version bump does not turn this red, while a body that genuinely wandered
        // off the plane — centimetres per second — still fails loudly.

        const Run still = RunPlayfield(0.0f);     // falls and rests: measured 0.59 mm
        const Run slid  = RunPlayfield(kShoveVx); // + 10 s of sliding: measured 2.05 mm

        MESSAGE("drop-only: max |z| = " << still.MaxAbsZ << " m, max out-of-plane |q| = " << still.MaxOutOfPlaneQ);
        MESSAGE("sliding:   max |z| = " << slid.MaxAbsZ  << " m, max out-of-plane |q| = " << slid.MaxOutOfPlaneQ);

        CHECK_MESSAGE(still.MaxAbsZ < 5e-3f, "a body that only fell and rested drifted ",
                      still.MaxAbsZ, " m in z");
        CHECK_MESSAGE(slid.MaxAbsZ < 2e-2f, "a body that slid for ", kSteps, " steps drifted ",
                      slid.MaxAbsZ, " m in z");

        // Not a rate: the box comes to rest well before halfway, so the second half
        // of the run must add nothing. This is the assertion that would actually
        // catch "z is leaking every step".
        for (const Run* r : { &still, &slid })
        {
            const float halfway = std::abs(r->Steps[(size_t)kSteps / 2].Pos.z);
            const float ending  = std::abs(r->Steps.back().Pos.z);
            CHECK_MESSAGE(std::abs(ending - halfway) < 1e-5f,
                          "z is still moving in the second half of the run: ", halfway, " -> ", ending);
        }

        // Same story for rotation: an XY body may spin about z, but must not tip out
        // of the plane (a quaternion about z has x == y == 0). Measured ~8.7e-5 in
        // both runs; 1e-3 is still only ~0.1 degrees of tilt.
        CHECK_MESSAGE(still.MaxOutOfPlaneQ < 1e-3f, "the dropped body tipped out of the XY plane: ",
                      still.MaxOutOfPlaneQ);
        CHECK_MESSAGE(slid.MaxOutOfPlaneQ < 1e-3f, "the sliding body tipped out of the XY plane: ",
                      slid.MaxOutOfPlaneQ);
    }

    TEST_CASE("A 2D trigger volume raises enter and exit exactly once")
    {
        RegisterProbe2D();
        Probe2D::Reset();

        Scene scene;

        // A sensor band across the playfield at y = 0 — a pickup / kill-plane, the
        // 2D use of a trigger. Thin in z as well: this is an XY volume.
        Entity trig = scene.CreateEntity("Zone");
        trig.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
        auto& tcol = trig.AddComponent<BoxColliderComponent>();
        tcol.HalfExtents = { 6.0f, 0.5f, 1.0f };
        tcol.IsTrigger = true;

        Entity box = scene.CreateEntity("Player");
        box.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic).LinearDamping = 0.0f;
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.4f, 0.4f, 0.4f };
        box.AddComponent<NativeScriptComponent>().ClassName = "Probe2D";

        ScriptHost host;
        host.Instantiate(scene);

        PhysicsWorld world;
        world.Init(Settings2D());
        scene.OnPhysicsStart(world);

        for (int i = 0; i < 180; ++i)   // 3 s: in, through, and well past
        {
            host.FixedTick(kDt);
            scene.OnPhysicsStep(kDt);
            scene.DispatchPhysicsEvents(host);
        }

        CHECK(Probe2D::s_TrigEnter == 1);
        CHECK(Probe2D::s_TrigExit == 1);
        CHECK(Probe2D::s_Enter == 0);                                    // sensors apply no force

        const glm::vec3 p = box.GetComponent<TransformComponent>().Position;
        CHECK(p.y < -1.0f);                                              // it fell through
        CHECK(std::abs(p.z) < 1e-4f);                                    // still no z drift

        scene.OnPhysicsStop(world);
        host.Destroy();
    }

    TEST_CASE("Two runs with ThreadCount = 0 are bit-identical")
    {
        const Run a = RunPlayfield(kShoveVx);
        const Run b = RunPlayfield(kShoveVx);

        REQUIRE(a.Steps.size() == b.Steps.size());

        const int bad = FirstMismatch(a, b);
        CHECK_MESSAGE(bad < 0, "the two runs diverged at step ", bad,
                      ": a = (", a.Steps[(size_t)std::max(bad, 0)].Pos.x, ", ",
                                 a.Steps[(size_t)std::max(bad, 0)].Pos.y, ", ",
                                 a.Steps[(size_t)std::max(bad, 0)].Pos.z, ")  b = (",
                                 b.Steps[(size_t)std::max(bad, 0)].Pos.x, ", ",
                                 b.Steps[(size_t)std::max(bad, 0)].Pos.y, ", ",
                                 b.Steps[(size_t)std::max(bad, 0)].Pos.z, ")");
    }
}
