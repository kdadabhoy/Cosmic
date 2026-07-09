// test_scripthost.cpp — native C++ script host lifecycle (Phase 13 / E11).
// Headless: registers a script IN-EXE (no game DLL), drives the full lifecycle,
// checks field push/pull, a scripted Transform move, unknown-class safety, and a
// scene-serializer round-trip of the script's field overrides.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "reflect/TypeRegistry.h"

using namespace Cosmic;

namespace
{
    // A test script: pushes its owning entity along +X by Speed each variable tick
    // and along +Y each fixed tick. Static flags observe the lifecycle (single
    // instance per case). Fields are public so CS_FIELD can reflect them.
    class MoverScript : public ScriptableEntity
    {
    public:
        float Speed = 1.0f;
        int   Ticks = 0;

        static inline bool s_Created   = false;
        static inline bool s_Started   = false;
        static inline bool s_Destroyed = false;
        static void ResetFlags() { s_Created = s_Started = s_Destroyed = false; }

    protected:
        void OnCreate()  override { s_Created = true; }
        void OnStart()   override { s_Started = true; }
        void OnUpdate(float ts) override
        {
            ++Ticks;
            GetComponent<TransformComponent>().Position.x += Speed * ts;
        }
        void OnFixedUpdate(float dt) override
        {
            GetComponent<TransformComponent>().Position.y += dt;
            // E20 telemetry seam — no-op unless a host installed a sink.
            Telemetry().Push("posY", GetComponent<TransformComponent>().Position.y);
        }
        void OnDestroy() override { s_Destroyed = true; }
    };

    // Register MoverScript into the process-wide ModuleRegistry via the DSL.
    void RegisterMover()
    {
        ModuleRegistry::Get().BeginModule("test");
        CS_SCRIPT(MoverScript)
            CS_FIELD(Speed).Range(0.0f, 10.0f)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    Entity MakeScripted(Scene& scene, const std::string& className, float speedOverride = -1.0f)
    {
        Entity e = scene.CreateEntity("Mover");
        auto& nsc = e.AddComponent<NativeScriptComponent>();
        nsc.ClassName = className;
        if (speedOverride >= 0.0f)
            nsc.Fields["Speed"] = Reflect::FieldValue{ speedOverride };
        return e;
    }
}

TEST_CASE("E11: a registered script instantiates, ticks, and moves its entity")
{
    MoverScript::ResetFlags();
    RegisterMover();

    Ref<Scene> scene = Scene::Create();
    Entity e = MakeScripted(*scene, "MoverScript", /*speedOverride=*/2.0f);

    ScriptHost host;
    host.Instantiate(*scene);

    CHECK(MoverScript::s_Created);
    CHECK(MoverScript::s_Started);
    CHECK(host.LiveCount() == 1);

    // The override value was pushed into the fresh instance.
    auto* inst = static_cast<MoverScript*>(e.GetComponent<NativeScriptComponent>().Instance);
    REQUIRE(inst != nullptr);
    CHECK(inst->Speed == doctest::Approx(2.0f));

    // Variable tick moves +X by Speed*ts; fixed tick moves +Y by dt.
    host.Tick(0.5f);
    host.FixedTick(0.1f);
    const auto& tf = e.GetComponent<TransformComponent>();
    CHECK(tf.Position.x == doctest::Approx(1.0f));    // 2.0 * 0.5
    CHECK(tf.Position.y == doctest::Approx(0.1f));
    CHECK(inst->Ticks == 1);

    host.Destroy();
    CHECK(MoverScript::s_Destroyed);
    CHECK(host.LiveCount() == 0);
    CHECK(e.GetComponent<NativeScriptComponent>().Instance == nullptr);
}

TEST_CASE("E11: field pull reads the live instance back into the component")
{
    RegisterMover();
    Ref<Scene> scene = Scene::Create();
    Entity e = MakeScripted(*scene, "MoverScript");

    ScriptHost host;
    host.Instantiate(*scene);

    auto& nsc = e.GetComponent<NativeScriptComponent>();
    auto* inst = static_cast<MoverScript*>(nsc.Instance);
    REQUIRE(inst != nullptr);
    inst->Speed = 7.5f;

    const ScriptDescriptor* desc = ModuleRegistry::Get().FindScript("MoverScript");
    REQUIRE(desc != nullptr);
    ScriptHost::PullFields(*desc, inst, nsc);

    CHECK(std::get<float>(nsc.Fields.at("Speed")) == doctest::Approx(7.5f));
    host.Destroy();
}

TEST_CASE("E11: an unknown script class is inert, not a crash")
{
    Ref<Scene> scene = Scene::Create();
    Entity e = MakeScripted(*scene, "NoSuchScript");

    ScriptHost host;
    host.Instantiate(*scene);          // must not throw / crash
    CHECK(host.LiveCount() == 0);
    CHECK(e.GetComponent<NativeScriptComponent>().Instance == nullptr);

    host.Tick(0.016f);                 // ticking an inert host is a no-op
    host.Destroy();
}

TEST_CASE("E20: script telemetry pushes route to an installed sink (and no-op without one)")
{
    RegisterMover();

    Ref<Scene> scene = Scene::Create();
    Entity e = MakeScripted(*scene, "MoverScript");

    // A capturing sink records (entity, channel, value) triples.
    struct CaptureSink : ITelemetrySink
    {
        struct Hit { entt::entity src; std::string ch; float v; };
        std::vector<Hit> hits;
        void Push(entt::entity src, const char* ch, float v) override
        {
            hits.push_back({ src, ch, v });
        }
    } sink;

    ScriptHost host;
    host.SetTelemetrySink(&sink);
    host.Instantiate(*scene);

    host.FixedTick(0.1f);   // MoverScript pushes "posY" == Position.y (== 0.1)
    host.FixedTick(0.1f);   // == 0.2

    REQUIRE(sink.hits.size() == 2);
    CHECK(sink.hits[0].ch == "posY");
    CHECK(sink.hits[0].src == (entt::entity)e);
    CHECK(sink.hits[0].v == doctest::Approx(0.1f));
    CHECK(sink.hits[1].v == doctest::Approx(0.2f));

    host.Destroy();

    // Without a sink the same script ticks are a harmless no-op (default state).
    ScriptHost host2;
    host2.Instantiate(*scene);
    host2.FixedTick(0.1f);   // must not crash / dereference a null sink
    host2.Destroy();
}

TEST_CASE("E11: NativeScript is a reflected component and its fields round-trip through JSON")
{
    RegisterMover();

    Ref<Scene> scene = Scene::Create();
    Entity e = MakeScripted(*scene, "MoverScript", /*speedOverride=*/3.25f);

    // It shows up as a registered component on the entity.
    bool sawScript = false;
    for (const auto* d : Reflect::GetRegistry().ComponentsOf(scene->GetRegistry(), (entt::entity)e))
        if (d->Name == "NativeScript") sawScript = true;
    CHECK(sawScript);

    const std::string text = SceneSerializer::SaveToString(*scene);

    Ref<Scene> loaded = Scene::Create();
    REQUIRE(SceneSerializer::LoadFromString(*loaded, text));

    // Find the reloaded scripted entity and confirm ClassName + the Speed override.
    int found = 0;
    for (auto h : loaded->GetRegistry().view<NativeScriptComponent>())
    {
        const auto& nsc = loaded->GetRegistry().get<NativeScriptComponent>(h);
        CHECK(nsc.ClassName == "MoverScript");
        CHECK(std::get<float>(nsc.Fields.at("Speed")) == doctest::Approx(3.25f));
        ++found;
    }
    CHECK(found == 1);
}

// ---------------------------------------------------------------------------
// U2 — scene signal bus <-> scripts
// ---------------------------------------------------------------------------

namespace
{
    class SignalScript : public ScriptableEntity
    {
    public:
        static inline int         s_Received = 0;
        static inline std::string s_Last;
        static void Reset() { s_Received = 0; s_Last.clear(); }

        void EmitPing() { Signals().Emit("ping"); }   // Signals() is protected -> OK here

    protected:
        void OnSignal(const std::string& sig, Entity) override { ++s_Received; s_Last = sig; }
    };

    void RegisterSignalScript()
    {
        ModuleRegistry::Get().BeginModule("test");
        CS_SCRIPT(SignalScript)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }
}

TEST_CASE("U2: scene signals reach script OnSignal; scripts can emit; route drops on Destroy")
{
    SignalScript::Reset();
    RegisterSignalScript();

    Ref<Scene> scene = Scene::Create();
    Entity e = scene->CreateEntity("S");
    e.AddComponent<NativeScriptComponent>().ClassName = "SignalScript";

    ScriptHost host;
    host.Instantiate(*scene);

    // An external emit (a button, the flow machine) reaches the script.
    scene->Events().Emit("hello", e);
    CHECK(SignalScript::s_Received == 1);
    CHECK(SignalScript::s_Last == "hello");

    // A script emitting broadcasts to every subscriber, itself included.
    auto* inst = static_cast<SignalScript*>(e.GetComponent<NativeScriptComponent>().Instance);
    REQUIRE(inst != nullptr);
    inst->EmitPing();
    CHECK(SignalScript::s_Received == 2);
    CHECK(SignalScript::s_Last == "ping");

    // After Destroy the ConnectAny route is gone — emit cannot reach a dead script.
    host.Destroy();
    scene->Events().Emit("afterlife", e);
    CHECK(SignalScript::s_Received == 2);
}

// ---------------------------------------------------------------------------
// H9 — SystemScript tier: one instance drives a *class* of entities
// ---------------------------------------------------------------------------

namespace
{
    // A system that nudges every matching entity along +X by Step each tick. Its
    // membership is TransformComponent + the "boid" tag. Static observers verify the
    // per-scene single instance + the live membership count.
    class FlockSystem : public SystemScript
    {
    public:
        float Step = 1.0f;

        static inline int s_Created   = 0;
        static inline int s_LastCount = -1;
        static void Reset() { s_Created = 0; s_LastCount = -1; }

    protected:
        void OnCreate() override { ++s_Created; }
        void OnUpdateAll(std::span<Entity> ents, float ts) override
        {
            s_LastCount = (int)ents.size();
            for (Entity e : ents)
                e.GetComponent<TransformComponent>().Position.x += Step * ts;
        }
    };

    void RegisterFlock()
    {
        ModuleRegistry::Get().BeginModule("test");
        CS_SYSTEM(FlockSystem).Requires<TransformComponent>().WithTag("boid")
            CS_FIELD(Step).Range(0.0f, 10.0f)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    // Attach a SystemScriptComponent naming `className` to a fresh holder entity.
    Entity MakeSystemHolder(Scene& scene, const std::string& className, float stepOverride = -1.0f)
    {
        Entity e = scene.CreateEntity("Systems");
        auto& ssc = e.AddComponent<SystemScriptComponent>();
        ssc.ClassName = className;
        if (stepOverride >= 0.0f)
            ssc.Fields["Step"] = Reflect::FieldValue{ stepOverride };
        return e;
    }
}

TEST_CASE("H9: a system ticks once with its whole matching set and moves them all")
{
    FlockSystem::Reset();
    RegisterFlock();

    Ref<Scene> scene = Scene::Create();
    MakeSystemHolder(*scene, "FlockSystem", /*stepOverride=*/2.0f);

    std::vector<Entity> boids;
    for (int i = 0; i < 10; ++i)
        boids.push_back(scene->CreateEntity("boid"));       // tag == "boid"
    Entity other = scene->CreateEntity("other");            // NOT a boid

    ScriptHost host;
    host.Instantiate(*scene);
    CHECK(FlockSystem::s_Created == 1);                     // one instance per scene

    host.Tick(0.5f);
    CHECK(FlockSystem::s_LastCount == 10);                  // the whole set, one call
    for (Entity b : boids)
        CHECK(b.GetComponent<TransformComponent>().Position.x == doctest::Approx(1.0f));   // 2.0*0.5
    CHECK(other.GetComponent<TransformComponent>().Position.x == doctest::Approx(0.0f));   // untouched

    host.Destroy();
}

TEST_CASE("H9: membership is rebuilt each tick as entities spawn and die")
{
    FlockSystem::Reset();
    RegisterFlock();

    Ref<Scene> scene = Scene::Create();
    MakeSystemHolder(*scene, "FlockSystem");

    std::vector<Entity> boids;
    for (int i = 0; i < 5; ++i)
        boids.push_back(scene->CreateEntity("boid"));

    ScriptHost host;
    host.Instantiate(*scene);

    host.Tick(0.1f);
    CHECK(FlockSystem::s_LastCount == 5);

    scene->DestroyEntity(boids[0]);
    scene->DestroyEntity(boids[1]);
    scene->CreateEntity("boid");            // +1

    host.Tick(0.1f);
    CHECK(FlockSystem::s_LastCount == 4);   // 5 - 2 + 1

    host.Destroy();
}

TEST_CASE("H9: SystemScript fields round-trip through the scene serializer")
{
    RegisterFlock();

    Ref<Scene> scene = Scene::Create();
    MakeSystemHolder(*scene, "FlockSystem", /*stepOverride=*/3.25f);

    const std::string text = SceneSerializer::SaveToString(*scene);

    Ref<Scene> loaded = Scene::Create();
    REQUIRE(SceneSerializer::LoadFromString(*loaded, text));

    int found = 0;
    for (auto h : loaded->GetRegistry().view<SystemScriptComponent>())
    {
        const auto& ssc = loaded->GetRegistry().get<SystemScriptComponent>(h);
        CHECK(ssc.ClassName == "FlockSystem");
        CHECK(std::get<float>(ssc.Fields.at("Step")) == doctest::Approx(3.25f));
        ++found;
    }
    CHECK(found == 1);
}
