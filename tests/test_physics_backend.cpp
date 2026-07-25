// test_physics_backend.cpp — W3: the pluggable physics backend seam.
//
// This file is BOTH the test and the reference example the docs point at. It
// implements a complete third-party IPhysicsBackend (a small AABB integrator),
// registers it the way an app would, selects it with PhysicsSettings::Backend,
// and then drives it through the whole engine stack that a real game uses:
//
//     PhysicsWorld -> ScenePhysics -> ScriptHost contact dispatch
//
// Nothing in Scene, ScenePhysics, the components, the serializer or the scripts
// knows the backend changed — that is the property under test. TinyPhysics keeps
// its own instance/step counters and the tests assert against them, so a run that
// silently fell back to Jolt cannot pass: Jolt does not increment them.
//
// Also covers the registry itself: Register/Has/Names/SetDefault/Default/Create,
// and PhysicsWorld::Init's "unknown name -> log + fall back to null" policy.

#include "doctest.h"

#include "physics/PhysicsBackend.h"
#include "physics/PhysicsWorld.h"
#include "physics/ScenePhysics.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Cosmic;

namespace
{
    // ========================================================================
    // TinyPhysics — a complete, minimal IPhysicsBackend.
    //
    // Semi-implicit Euler on axis-aligned boxes: dynamic bodies fall, static and
    // trigger bodies do not move, and a dynamic/non-dynamic overlap either resolves
    // (push out along the smallest penetration axis, kill that axis' velocity) or,
    // for a sensor, reports and is ignored. Enter/exit events are the set delta
    // between steps. Everything is deterministic and ignores ThreadCount, which the
    // contract allows as long as it is stated — as it is, here.
    //
    // Under 150 lines is the point: this is the floor for "write your own physics".
    // ========================================================================
    class TinyPhysics final : public IPhysicsBackend
    {
    public:
        // Counters the test reads to prove this backend really ran.
        static inline int s_Instances = 0;
        static inline int s_Steps     = 0;
        static void ResetCounters() { s_Instances = 0; s_Steps = 0; }

        TinyPhysics() { ++s_Instances; }

        const char* Name() const override { return "tiny"; }

        // ---- lifecycle ------------------------------------------------------
        void Init(const PhysicsSettings& settings) override
        {
            m_Gravity = settings.Gravity;
            m_Bodies.clear();
            m_Pairs.clear();
            m_Events.clear();
            m_Init = true;
        }
        void Shutdown() override { m_Init = false; m_Bodies.clear(); m_Pairs.clear(); m_Events.clear(); }
        bool IsInitialized() const override { return m_Init; }

        void Step(float dt) override
        {
            if (!m_Init || dt <= 0.0f) return;
            ++s_Steps;

            for (Body& b : m_Bodies)
            {
                if (!b.Alive || b.Motion != MotionType::Dynamic) continue;
                b.Velocity += m_Gravity * b.GravityFactor * dt;
                b.Position += b.Velocity * dt;
            }

            // Contacts: every dynamic body against every non-dynamic one.
            std::unordered_set<uint64_t> nowTouching;
            for (uint32_t i = 0; i < (uint32_t)m_Bodies.size(); ++i)
            {
                if (!m_Bodies[i].Alive || m_Bodies[i].Motion != MotionType::Dynamic) continue;
                for (uint32_t j = 0; j < (uint32_t)m_Bodies.size(); ++j)
                {
                    if (i == j || !m_Bodies[j].Alive || m_Bodies[j].Motion == MotionType::Dynamic) continue;

                    glm::vec3 push;
                    if (!Overlap(m_Bodies[i], m_Bodies[j], push)) continue;

                    nowTouching.insert(PairKey(i, j));
                    if (m_Bodies[j].IsTrigger) continue;   // sensors apply no response

                    m_Bodies[i].Position += push;
                    if (push.x != 0.0f) m_Bodies[i].Velocity.x = 0.0f;
                    if (push.y != 0.0f) m_Bodies[i].Velocity.y = 0.0f;
                    if (push.z != 0.0f) m_Bodies[i].Velocity.z = 0.0f;
                }
            }

            for (uint64_t key : nowTouching)
                if (!m_Pairs.count(key))
                    Emit(key, /*enter*/ true);
            for (uint64_t key : m_Pairs)
                if (!nowTouching.count(key))
                    Emit(key, /*enter*/ false);
            m_Pairs = std::move(nowTouching);
        }

        // ---- bodies ---------------------------------------------------------
        PhysicsBody CreateBody(const BodyDesc& desc) override
        {
            if (!m_Init || desc.Shapes.empty()) return {};

            Body b;
            b.Motion        = desc.Motion;
            b.Position      = desc.Position;
            b.Rotation      = desc.Rotation;
            b.GravityFactor = desc.GravityFactor;
            b.IsTrigger     = desc.IsTrigger;
            b.Entity        = desc.EntityId;
            b.Half          = HalfExtentsOf(desc.Shapes.front());
            m_Bodies.push_back(b);
            return PhysicsBody{ (uint32_t)m_Bodies.size() - 1 };
        }

        void DestroyBody(PhysicsBody body) override
        {
            if (Body* b = Find(body)) b->Alive = false;
        }

        void SetBodyTransform(PhysicsBody body, const glm::vec3& p, const glm::quat& r) override
        {
            if (Body* b = Find(body)) { b->Position = p; b->Rotation = r; b->Velocity = glm::vec3(0.0f); }
        }
        void GetBodyTransform(PhysicsBody body, glm::vec3& outP, glm::quat& outR) const override
        {
            if (const Body* b = Find(body)) { outP = b->Position; outR = b->Rotation; }
        }
        void MoveKinematic(PhysicsBody body, const glm::vec3& p, const glm::quat& r, float dt) override
        {
            Body* b = Find(body);
            if (!b || dt <= 0.0f) return;
            b->Velocity = (p - b->Position) / dt;
            b->Position = p;
            b->Rotation = r;
        }

        void SetLinearVelocity(PhysicsBody body, const glm::vec3& v) override
        { if (Body* b = Find(body)) b->Velocity = v; }
        glm::vec3 GetLinearVelocity(PhysicsBody body) const override
        { const Body* b = Find(body); return b ? b->Velocity : glm::vec3(0.0f); }
        void SetAngularVelocity(PhysicsBody, const glm::vec3&) override {}          // no rotation model
        glm::vec3 GetAngularVelocity(PhysicsBody) const override { return glm::vec3(0.0f); }

        void AddForce(PhysicsBody, const glm::vec3&) override {}                    // no mass model
        void AddImpulse(PhysicsBody body, const glm::vec3& imp) override
        { if (Body* b = Find(body)) b->Velocity += imp; }                           // unit mass
        void AddTorque(PhysicsBody, const glm::vec3&) override {}

        bool IsActive(PhysicsBody body) const override
        { const Body* b = Find(body); return b && b->Alive && b->Motion == MotionType::Dynamic; }
        void Activate(PhysicsBody) override {}

        // ---- queries --------------------------------------------------------
        // A down-ray only (all this test needs); everything else misses.
        std::optional<RayHit> RayCast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance,
                                      uint16_t, uint64_t ignoreEntity) const override
        {
            if (!m_Init || maxDistance <= 0.0f || dir.y >= 0.0f) return std::nullopt;
            std::optional<RayHit> best;
            for (const Body& b : m_Bodies)
            {
                if (!b.Alive || b.IsTrigger) continue;
                if (ignoreEntity && b.Entity == ignoreEntity) continue;
                if (std::abs(origin.x - b.Position.x) > b.Half.x) continue;
                if (std::abs(origin.z - b.Position.z) > b.Half.z) continue;
                const float top  = b.Position.y + b.Half.y;
                const float dist = origin.y - top;
                if (dist < 0.0f || dist > maxDistance) continue;
                if (best && best->Distance <= dist) continue;

                RayHit h;
                h.Hit = true;
                h.EntityId = b.Entity;
                h.Distance = dist;
                h.Point    = { origin.x, top, origin.z };
                h.Normal   = { 0.0f, 1.0f, 0.0f };
                best = h;
            }
            return best;
        }
        std::optional<RayHit> SphereCast(const glm::vec3&, const glm::vec3&, float, float,
                                         uint16_t, uint64_t) const override { return std::nullopt; }
        void OverlapSphere(const glm::vec3&, float, std::vector<uint64_t>& out,
                           uint16_t, uint64_t) const override { out.clear(); }
        void OverlapBox(const glm::vec3&, const glm::vec3&, const glm::quat&,
                        std::vector<uint64_t>& out, uint16_t, uint64_t) const override { out.clear(); }

        // ---- characters (unsupported by this backend) ------------------------
        CharacterHandle CreateCharacter(const CharacterDesc&) override { return {}; }
        void DestroyCharacter(CharacterHandle) override {}
        void UpdateCharacter(CharacterHandle, const glm::vec3&, float) override {}
        void GetCharacterTransform(CharacterHandle, glm::vec3&, glm::quat&) const override {}
        void SetCharacterPosition(CharacterHandle, const glm::vec3&) override {}
        bool IsCharacterGrounded(CharacterHandle) const override { return false; }
        glm::vec3 GetCharacterGroundNormal(CharacterHandle) const override { return glm::vec3(0, 1, 0); }
        glm::vec3 GetCharacterVelocity(CharacterHandle) const override { return glm::vec3(0.0f); }

        // ---- events / introspection / debug ---------------------------------
        void DrainContactEvents(std::vector<ContactEvent>& out) override
        {
            out.clear();
            out.swap(m_Events);   // the contract: move AND clear
        }
        PhysicsStats GetStatistics() const override
        {
            PhysicsStats s;
            for (const Body& b : m_Bodies)
            {
                if (!b.Alive) continue;
                ++s.BodyCount;
                if (b.Motion == MotionType::Dynamic) ++s.ActiveBodies;
            }
            return s;
        }
        void DebugDraw() const override {}

    private:
        struct Body
        {
            MotionType Motion = MotionType::Static;
            glm::vec3  Position{ 0.0f };
            glm::quat  Rotation{ 1, 0, 0, 0 };
            glm::vec3  Velocity{ 0.0f };
            glm::vec3  Half{ 0.5f };
            float      GravityFactor = 1.0f;
            bool       IsTrigger = false;
            bool       Alive = true;
            uint64_t   Entity = 0;
        };

        static glm::vec3 HalfExtentsOf(const CollisionShapeDesc& d)
        {
            // Boxes and spheres only — the dimension-agnostic subset 2D keeps.
            const glm::vec3 he = (d.Shape == CollisionShapeDesc::Kind::Sphere)
                               ? glm::vec3(d.Radius) : d.HalfExtents;
            return glm::abs(he * d.Scale);
        }

        static uint64_t PairKey(uint32_t a, uint32_t b)
        {
            if (a > b) std::swap(a, b);
            return (uint64_t(a) << 32) | uint64_t(b);
        }

        // AABB overlap; `push` is the minimum translation that separates `a` from `b`.
        static bool Overlap(const Body& a, const Body& b, glm::vec3& push)
        {
            const glm::vec3 d  = a.Position - b.Position;
            const glm::vec3 ov = (a.Half + b.Half) - glm::abs(d);
            if (ov.x <= 0.0f || ov.y <= 0.0f || ov.z <= 0.0f) return false;

            push = glm::vec3(0.0f);
            if (ov.y <= ov.x && ov.y <= ov.z)      push.y = d.y >= 0.0f ? ov.y : -ov.y;
            else if (ov.x <= ov.z)                 push.x = d.x >= 0.0f ? ov.x : -ov.x;
            else                                   push.z = d.z >= 0.0f ? ov.z : -ov.z;
            return true;
        }

        void Emit(uint64_t key, bool enter)
        {
            const Body& a = m_Bodies[(uint32_t)(key >> 32)];
            const Body& b = m_Bodies[(uint32_t)(key & 0xFFFFFFFFull)];
            const bool sensor = a.IsTrigger || b.IsTrigger;

            ContactEvent ev;
            ev.Kind = sensor ? (enter ? ContactKind::TriggerEnter : ContactKind::TriggerExit)
                             : (enter ? ContactKind::CollisionEnter : ContactKind::CollisionExit);
            // For Trigger* events A is the sensor's entity (the documented convention).
            if (sensor && b.IsTrigger) { ev.EntityA = b.Entity; ev.EntityB = a.Entity; }
            else                       { ev.EntityA = a.Entity; ev.EntityB = b.Entity; }
            m_Events.push_back(ev);
        }

        Body* Find(PhysicsBody h)
        { return h.IsValid() && h.Id < m_Bodies.size() ? &m_Bodies[h.Id] : nullptr; }
        const Body* Find(PhysicsBody h) const
        { return h.IsValid() && h.Id < m_Bodies.size() ? &m_Bodies[h.Id] : nullptr; }

        std::vector<Body>          m_Bodies;
        std::unordered_set<uint64_t> m_Pairs;
        std::vector<ContactEvent>  m_Events;
        glm::vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
        bool m_Init = false;
    };

    // ------------------------------------------------------------------------
    // A script that just counts what the backend reported.
    // ------------------------------------------------------------------------
    class BackendProbe : public ScriptableEntity
    {
    public:
        static inline int s_Enter = 0, s_Exit = 0, s_TrigEnter = 0, s_TrigExit = 0, s_Rays = 0;
        static void Reset() { s_Enter = s_Exit = s_TrigEnter = s_TrigExit = s_Rays = 0; }

    protected:
        void OnCollisionEnter(Entity) override { ++s_Enter; }
        void OnCollisionExit(Entity)  override { ++s_Exit; }
        void OnTriggerEnter(Entity)   override { ++s_TrigEnter; }
        void OnTriggerExit(Entity)    override { ++s_TrigExit; }
        void OnFixedUpdate(float) override
        {
            const glm::vec3 p = GetComponent<TransformComponent>().Position;
            if (Physics().RayCast(p, glm::vec3(0, -1, 0), 20.0f).has_value())
                ++s_Rays;
        }
    };

    void RegisterProbe()
    {
        ModuleRegistry::Get().BeginModule("test_physics_backend");
        CS_SCRIPT(BackendProbe)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    // The whole third-party integration, exactly as §6.3 documents it.
    void RegisterTinyBackend()
    {
        PhysicsBackendRegistry::Register("tiny", [] { return std::make_unique<TinyPhysics>(); });
    }

    void SessionStep(Scene& scene, ScriptHost& host, float dt)
    {
        host.FixedTick(dt);
        scene.OnPhysicsStep(dt);
        scene.DispatchPhysicsEvents(host);
    }

    bool Contains(const std::vector<std::string>& v, const char* s)
    {
        return std::find(v.begin(), v.end(), s) != v.end();
    }
}

TEST_SUITE("Physics / pluggable backend (W3)")
{
    TEST_CASE("Registry: register, list, look up, create, and the default name")
    {
        // Init registers the built-ins; do one so the registry is populated even if
        // this test case runs first.
        { PhysicsWorld w; w.Init(); w.Shutdown(); }

        RegisterTinyBackend();

        CHECK(PhysicsBackendRegistry::Has("tiny"));
        CHECK(PhysicsBackendRegistry::Has("null"));
        CHECK_FALSE(PhysicsBackendRegistry::Has("no-such-backend"));

        const std::vector<std::string> names = PhysicsBackendRegistry::Names();
        CHECK(Contains(names, "tiny"));
        CHECK(Contains(names, "null"));
        CHECK(std::is_sorted(names.begin(), names.end()));

        std::unique_ptr<IPhysicsBackend> made = PhysicsBackendRegistry::Create("tiny");
        REQUIRE(made != nullptr);
        CHECK(std::string(made->Name()) == "tiny");
        CHECK_FALSE(made->IsInitialized());
        CHECK(PhysicsBackendRegistry::Create("no-such-backend") == nullptr);

        // SetDefault is process-wide, so put it back — the other physics suites in
        // this binary run on the default. (COSMIC_WITH_JOLT is PRIVATE to the engine
        // target, so a test cannot name the built-in default; it can only require
        // that there IS one and that it resolves.)
        const std::string previous = PhysicsBackendRegistry::Default();
        CHECK_FALSE(previous.empty());
        CHECK(PhysicsBackendRegistry::Has(previous));

        PhysicsBackendRegistry::SetDefault("tiny");
        CHECK(PhysicsBackendRegistry::Default() == "tiny");

        {   // an empty PhysicsSettings::Backend now resolves to "tiny"
            PhysicsWorld w;
            w.Init();
            CHECK(w.IsInitialized());
            const PhysicsBody b = w.CreateBody([]
            {
                BodyDesc d;
                d.Motion = MotionType::Dynamic;
                d.Shapes.push_back({});
                return d;
            }());
            CHECK(b.IsValid());
            CHECK(w.GetStatistics().BodyCount == 1u);
            w.Shutdown();
        }

        PhysicsBackendRegistry::SetDefault(previous);
        CHECK(PhysicsBackendRegistry::Default() == previous);
    }

    TEST_CASE("A third-party backend drives Scene -> ScenePhysics -> script contacts")
    {
        RegisterTinyBackend();
        RegisterProbe();
        BackendProbe::Reset();
        TinyPhysics::ResetCounters();

        Scene scene;

        // Ground: a static slab whose top face sits exactly at y = 0.
        Entity ground = scene.CreateEntity("Ground");
        ground.GetComponent<TransformComponent>().Position = { 0.0f, -0.5f, 0.0f };
        ground.AddComponent<RigidBodyComponent>(MotionType::Static);
        ground.AddComponent<BoxColliderComponent>().HalfExtents = { 20.0f, 0.5f, 20.0f };

        // A scripted dynamic box dropped from y = 4.
        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 0.0f, 4.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };
        box.AddComponent<NativeScriptComponent>().ClassName = "BackendProbe";

        ScriptHost host;
        host.Instantiate(scene);

        PhysicsWorld world;
        PhysicsSettings ps;
        ps.Backend     = "tiny";       // <- the whole opt-in
        ps.ThreadCount = 0;
        world.Init(ps);
        REQUIRE(world.IsInitialized());

        scene.OnPhysicsStart(world);
        REQUIRE(scene.GetPhysics() != nullptr);
        CHECK(scene.GetPhysics()->GetBody((entt::entity)box).IsValid());

        // TinyPhysics was constructed and is the thing stepping.
        CHECK(TinyPhysics::s_Instances >= 1);
        const int stepsBefore = TinyPhysics::s_Steps;

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 180; ++i)
            SessionStep(scene, host, dt);

        CHECK(TinyPhysics::s_Steps == stepsBefore + 180);

        // TinyPhysics pushes the penetration out completely, so the box rests with
        // its lower face exactly ON the ground's top face: y == its half extent.
        // Tolerances are ABSOLUTE — doctest's Approx epsilon is relative.
        const glm::vec3 rest = box.GetComponent<TransformComponent>().Position;
        CHECK(std::abs(rest.y - 0.5f) < 1e-5f);
        CHECK(std::abs(rest.x) < 1e-6f);
        CHECK(std::abs(rest.z) < 1e-6f);

        // The landing was reported once, through the backend's own event queue and
        // out to the script via ScenePhysics::DispatchEvents.
        CHECK(BackendProbe::s_Enter == 1);
        CHECK(BackendProbe::s_Exit == 0);
        CHECK(BackendProbe::s_TrigEnter == 0);

        // ...and the script's raycast reached the backend too.
        CHECK(BackendProbe::s_Rays > 0);

        // Separation raises Exit — through PhysicsWorld's unchanged public API.
        const PhysicsBody body = scene.GetPhysics()->GetBody((entt::entity)box);
        world.SetBodyTransform(body, { 0.0f, 30.0f, 0.0f }, glm::quat(1, 0, 0, 0));
        SessionStep(scene, host, dt);
        CHECK(BackendProbe::s_Exit == 1);

        CHECK(world.GetStatistics().BodyCount == 2u);
        CHECK(world.GetStatistics().ActiveBodies == 1u);

        scene.OnPhysicsStop(world);
        host.Destroy();
    }

    TEST_CASE("A third-party backend's trigger volumes reach OnTriggerEnter/Exit")
    {
        RegisterTinyBackend();
        RegisterProbe();
        BackendProbe::Reset();

        Scene scene;

        Entity trig = scene.CreateEntity("Trigger");
        trig.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
        auto& tcol = trig.AddComponent<BoxColliderComponent>();
        tcol.HalfExtents = { 5.0f, 0.5f, 5.0f };
        tcol.IsTrigger = true;

        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 0.0f, 4.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.4f, 0.4f, 0.4f };
        box.AddComponent<NativeScriptComponent>().ClassName = "BackendProbe";

        ScriptHost host;
        host.Instantiate(scene);

        PhysicsWorld world;
        PhysicsSettings ps;
        ps.Backend = "tiny";
        world.Init(ps);
        scene.OnPhysicsStart(world);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 120; ++i)   // falls all the way through the sensor
            SessionStep(scene, host, dt);

        CHECK(BackendProbe::s_TrigEnter == 1);
        CHECK(BackendProbe::s_TrigExit == 1);
        CHECK(BackendProbe::s_Enter == 0);                                   // a sensor pushes nothing
        CHECK(box.GetComponent<TransformComponent>().Position.y < -1.0f);    // ...so it kept falling

        scene.OnPhysicsStop(world);
        host.Destroy();
    }

    TEST_CASE("An unknown backend name logs and falls back to null instead of crashing")
    {
        Scene scene;

        Entity box = scene.CreateEntity("Box");
        box.GetComponent<TransformComponent>().Position = { 0.0f, 4.0f, 0.0f };
        box.AddComponent<RigidBodyComponent>(MotionType::Dynamic);
        box.AddComponent<BoxColliderComponent>().HalfExtents = { 0.5f, 0.5f, 0.5f };

        PhysicsWorld world;
        PhysicsSettings ps;
        ps.Backend = "definitely-not-registered";
        world.Init(ps);

        // Init succeeded on the null backend rather than leaving a dead world.
        CHECK(world.IsInitialized());

        scene.OnPhysicsStart(world);
        REQUIRE(scene.GetPhysics() != nullptr);
        for (int i = 0; i < 60; ++i)
            scene.OnPhysicsStep(1.0f / 60.0f);

        // Null creates no bodies, so the authored transform is untouched — bit-exact,
        // because nothing ever wrote to it.
        CHECK(box.GetComponent<TransformComponent>().Position.y == 4.0f);
        CHECK(world.GetStatistics().BodyCount == 0u);
        CHECK_FALSE(scene.GetPhysics()->GetBody((entt::entity)box).IsValid());

        std::vector<ContactEvent> events{ ContactEvent{} };
        world.DrainContactEvents(events);
        CHECK(events.empty());                       // cleared even with nothing to report

        std::vector<uint64_t> hits{ 1, 2, 3 };
        world.OverlapSphere({ 0, 0, 0 }, 5.0f, hits);
        CHECK(hits.empty());
        CHECK_FALSE(world.RayCast({ 0, 5, 0 }, { 0, -1, 0 }, 100.0f).has_value());

        scene.OnPhysicsStop(world);
    }

    TEST_CASE("PhysicsWorld tolerates use before Init and after Shutdown")
    {
        PhysicsWorld world;                       // no backend yet
        CHECK_FALSE(world.IsInitialized());
        CHECK_FALSE(world.CreateBody({}).IsValid());
        CHECK(world.GetStatistics().BodyCount == 0u);
        CHECK(world.GetLinearVelocity({}) == glm::vec3(0.0f));
        CHECK_FALSE(world.IsCharacterGrounded({}));
        CHECK(world.GetCharacterGroundNormal({}) == glm::vec3(0, 1, 0));
        world.Step(1.0f / 60.0f);                 // must not crash
        world.DebugDraw();

        // GetBodyTransform leaves its out-params alone when there is nothing to read.
        glm::vec3 p{ 7.0f, 8.0f, 9.0f };
        glm::quat r{ 0.5f, 0.5f, 0.5f, 0.5f };
        world.GetBodyTransform({}, p, r);
        CHECK(p == glm::vec3(7.0f, 8.0f, 9.0f));

        world.Init();
        CHECK(world.IsInitialized());
        world.Shutdown();
        CHECK_FALSE(world.IsInitialized());
        world.Shutdown();                         // idempotent
        CHECK_FALSE(world.CreateBody({}).IsValid());
    }
}
