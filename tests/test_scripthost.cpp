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
