// test_servicehost.cpp — AP-01 V02: the AppService tier (design contract §2) and the
// Data() script proxy (the V06 bullet the prompt routes here).
//
//   U (ordinary suite): in-exe CS_SERVICE registrations — instantiation order
//   (registration, then Order), OnAttach after ALL services are constructed and before
//   the first BindScene, the producer bracket around every callback (nesting-safe),
//   BindScene unsubscribe/subscribe + OnSceneChanged(old, new), 50 scene swaps keeping
//   the same instances and still delivering OnSignal, Destroy in reverse order,
//   Instantiate re-entry destroying first, UnregisterModule stripping services, the
//   PanelRegistry + CS_PANEL call-site record, DispatchEvent, the Data() proxy on
//   ScriptableEntity and SystemScript with and without a bus.
//
//   W: the WO-07 F-LIFETIME pattern extended — AP01ServiceFixture.dll (a real
//   CS_MODULE module: CS_SERVICE + CS_PANEL + CS_SCRIPT) loaded and unloaded 20 times
//   (a) through the REAL Starforge GameModule TU with a DataBus + PanelRegistry the
//   test owns (values, history and producers persist across the reloads), and (b),
//   skipped by default (needs a real Application/window/GL; the runner launches it),
//   through the REAL Application / PlayerLayer path. Both assert OnDetach before
//   FreeLibrary (one shared sequence stamped by the exe and the DLL) and no callback
//   after unload while the same paths still reach exe listeners.

#include <doctest.h>

#include "data/DataBus.h"
#include "scripting/AppService.h"
#include "scripting/ServiceHost.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ScriptHost.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/EventBus.h"
#include "scene/ui/UiComponents.h"
#include "events/ApplicationEvent.h"
#include "core/Application.h"
#include "core/Layer.h"

#include "AP01ServiceReport.h"
#include "WO05NativeWindow.h"
#include "../Projects/Starforge/src/GameModule.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using namespace Cosmic;

namespace
{
    // One shared event log every in-exe service writes to (single-threaded tests).
    std::vector<std::string> g_Log;
    int g_Constructed = 0;

    struct LoggingService : AppService
    {
        std::string Tag;
        int ConstructedAtAttach = -1;
        Scene* SceneAtChange = nullptr;
        explicit LoggingService(std::string tag) : Tag(std::move(tag)) { ++g_Constructed; g_Log.push_back("ctor:" + Tag); }
        ~LoggingService() override { g_Log.push_back("dtor:" + Tag); }
    protected:
        void OnAttach(AppContext& ctx) override
        {
            ConstructedAtAttach = g_Constructed;
            g_Log.push_back("attach:" + Tag);
            (void)ctx;
        }
        void OnDetach() override { g_Log.push_back("detach:" + Tag); }
        void OnUpdate(float) override
        {
            g_Log.push_back("update:" + Tag);
            Bus().Set(Tag + ".out", 1.0);
        }
        void OnFixedUpdate(float) override
        {
            g_Log.push_back("fixed:" + Tag);
            Bus().Set(Tag + ".fixed", 1.0);
        }
        void OnSignal(const std::string& signal, Entity) override
        {
            g_Log.push_back("signal:" + Tag + ":" + signal);
            Bus().Set(Tag + ".sig", 1.0);
        }
        void OnSceneChanged(Scene* oldScene, Scene* newScene) override
        {
            g_Log.push_back("scene:" + Tag);
            SceneAtChange = newScene;
            CHECK(Context().ActiveScene == newScene);
            (void)oldScene;
        }
        void OnEvent(Event&) override { g_Log.push_back("event:" + Tag); }
    };

    struct SvcA : LoggingService
    {
        static inline SvcA* s_Instance = nullptr;
        SvcA() : LoggingService("A") { s_Instance = this; }
    protected:
        void OnAttach(AppContext& ctx) override
        {
            LoggingService::OnAttach(ctx);
            CS_PANEL("PanelA", [](const UiRect& r) { g_Log.push_back("drawA:" + std::to_string((int)r.Width())); });
        }
        void OnUpdate(float ts) override
        {
            LoggingService::OnUpdate(ts);
            // Emit a scene signal from INSIDE the tick: the fan-out to OnSignal runs
            // nested in this bracket and must restore it on return.
            if (Context().ActiveScene) Context().ActiveScene->Events().Emit("from_a", Entity());
            Bus().Set("A.after", 1.0);
        }
    };
    struct SvcB : LoggingService
    {
        static inline SvcB* s_Instance = nullptr;
        SvcB() : LoggingService("B") { s_Instance = this; }
    };
    struct SvcC : LoggingService
    {
        SvcC() : LoggingService("C") {}
    };
    struct SvcOther : LoggingService
    {
        SvcOther() : LoggingService("O") {}
    };

    // Registration order A, B, C with Orders 1, 0, 1 => instantiation B, A, C.
    void RegisterServices()
    {
        auto& reg = ModuleRegistry::Get();
        reg.UnregisterModule("ap01test");
        reg.BeginModule("ap01test");
        CS_SERVICE(SvcA).Order(1) CS_END;
        CS_SERVICE(SvcB).Order(0) CS_END;
        CS_SERVICE(SvcC).Order(1) CS_END;
        reg.EndModule();
    }

    int CountLog(const std::string& prefix)
    {
        int n = 0;
        for (const auto& e : g_Log) if (e.rfind(prefix, 0) == 0) ++n;
        return n;
    }
    int IndexOf(const std::string& entry)
    {
        for (size_t i = 0; i < g_Log.size(); ++i) if (g_Log[i] == entry) return (int)i;
        return -1;
    }

    struct Fixture
    {
        DataBus bus;
        PanelRegistry panels;
        Fixture() { g_Log.clear(); g_Constructed = 0; RegisterServices(); }
        ~Fixture() { ModuleRegistry::Get().UnregisterModule("ap01test"); }
        AppContext Ctx(Scene* scene = nullptr, bool inEditor = false)
        {
            return AppContext{ bus, panels, scene, nullptr, "ap01test", inEditor };
        }
    };
}

TEST_SUITE("AP-01 V02 ServiceHost")
{
    TEST_CASE("V02: instantiation order is registration order stable-sorted by Order; OnAttach after all constructed, before BindScene")
    {
        Fixture f;
        ServiceHost host;
        CHECK_FALSE(host.IsInstantiated());
        CHECK(host.Count() == 0);
        CHECK(host.Names().empty());

        host.Instantiate("ap01test", f.Ctx());
        CHECK(host.IsInstantiated());
        REQUIRE(host.Count() == 3);
        const auto names = host.Names();
        CHECK(names[0] == "SvcB");            // Order 0 first
        CHECK(names[1] == "SvcA");            // Order 1, registered before C
        CHECK(names[2] == "SvcC");

        // Constructed in that order, then attached in that order, after ALL exist.
        REQUIRE(g_Log.size() >= 6);
        CHECK(g_Log[0] == "ctor:B"); CHECK(g_Log[1] == "ctor:A"); CHECK(g_Log[2] == "ctor:C");
        CHECK(g_Log[3] == "attach:B"); CHECK(g_Log[4] == "attach:A"); CHECK(g_Log[5] == "attach:C");
        CHECK(SvcA::s_Instance->ConstructedAtAttach == 3);
        CHECK(SvcB::s_Instance->ConstructedAtAttach == 3);
        CHECK(CountLog("scene:") == 0);       // no BindScene yet

        Ref<Scene> s = Scene::Create();
        host.BindScene(s.get());
        CHECK(CountLog("scene:") == 3);
        CHECK(IndexOf("attach:C") < IndexOf("scene:B"));   // every attach precedes the first scene change

        const ServiceDescriptor* d = host.DescriptorOf("SvcA");
        REQUIRE(d != nullptr);
        CHECK(d->Order == 1);
        CHECK(d->Module == "ap01test");
        CHECK(d->File.find("test_servicehost.cpp") != std::string::npos);
        CHECK(d->Line > 0);
        CHECK(host.DescriptorOf("Nope") == nullptr);
        host.Destroy();
    }

    TEST_CASE("V02: Tick / FixedTick / DispatchSignal bracket the producer per service (nesting-safe)")
    {
        Fixture f;
        Ref<Scene> s = Scene::Create();
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        host.BindScene(s.get());
        f.bus.Set("host.before", 1.0);
        CHECK(f.bus.Producer("host.before").empty());

        host.Tick(0.016f);
        CHECK(f.bus.Producer("A.out") == "SvcA");
        CHECK(f.bus.Producer("B.out") == "SvcB");
        CHECK(f.bus.Producer("C.out") == "SvcC");
        // SvcA emitted "from_a" from inside its tick: the nested fan-out ran under each
        // receiver's own tag and the bracket was restored for SvcA's later write.
        CHECK(f.bus.Producer("B.sig") == "SvcB");
        CHECK(f.bus.Producer("C.sig") == "SvcC");
        CHECK(f.bus.Producer("A.after") == "SvcA");
        CHECK(CountLog("signal:B:from_a") == 1);
        CHECK(CountLog("signal:C:from_a") == 1);
        CHECK(CountLog("signal:A:from_a") == 1);     // the emitter hears its own signal too (scene bus)

        f.bus.Set("host.after", 1.0);
        CHECK(f.bus.Producer("host.after").empty());  // the tag is "" again outside the tick

        host.FixedTick(1.0f / 60.0f);
        CHECK(f.bus.Producer("A.fixed") == "SvcA");
        CHECK(f.bus.Producer("B.fixed") == "SvcB");
        CHECK(CountLog("fixed:") == 3);

        // Order of the update callbacks is the instantiation order.
        const int uB = IndexOf("update:B"), uA = IndexOf("update:A"), uC = IndexOf("update:C");
        CHECK(uB < uA);
        CHECK(uA < uC);
        host.Destroy();
    }

    TEST_CASE("V02: BindScene unsubscribes the old bus, subscribes the new one and calls OnSceneChanged(old, new)")
    {
        Fixture f;
        Ref<Scene> s1 = Scene::Create();
        Ref<Scene> s2 = Scene::Create();
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());

        host.BindScene(s1.get());
        CHECK(s1->Events().TotalListeners() == 1);
        CHECK(CountLog("scene:") == 3);
        CHECK(SvcA::s_Instance->SceneAtChange == s1.get());
        s1->Events().Emit("ping", Entity());
        CHECK(CountLog("signal:A:ping") == 1);
        CHECK(CountLog("signal:B:ping") == 1);
        CHECK(CountLog("signal:C:ping") == 1);

        host.BindScene(s2.get());
        CHECK(s1->Events().TotalListeners() == 0);   // unsubscribed from the old scene
        CHECK(s2->Events().TotalListeners() == 1);
        CHECK(CountLog("scene:") == 6);
        CHECK(SvcA::s_Instance->SceneAtChange == s2.get());
        s1->Events().Emit("stale", Entity());
        CHECK(CountLog("signal:A:stale") == 0);      // the old scene no longer reaches the services
        s2->Events().Emit("fresh", Entity());
        CHECK(CountLog("signal:A:fresh") == 1);

        host.BindScene(s2.get());                    // same scene: a no-op
        CHECK(CountLog("scene:") == 6);
        CHECK(s2->Events().TotalListeners() == 1);

        host.BindScene(nullptr);
        CHECK(s2->Events().TotalListeners() == 0);
        CHECK(CountLog("scene:") == 9);
        CHECK(SvcA::s_Instance->SceneAtChange == nullptr);
        host.Destroy();
    }

    TEST_CASE("V02: 50 scene swaps keep the same instances and still deliver OnSignal")
    {
        Fixture f;
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        SvcA* a = SvcA::s_Instance;
        SvcB* b = SvcB::s_Instance;
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);

        Ref<Scene> current;
        for (int i = 0; i < 50; ++i)
        {
            Ref<Scene> next = Scene::Create();
            host.BindScene(next.get());              // the previous scene is still alive here
            current = next;
        }
        CHECK(g_Constructed == 3);                   // no re-construction across the swaps
        CHECK(SvcA::s_Instance == a);
        CHECK(SvcB::s_Instance == b);
        CHECK(CountLog("ctor:") == 3);
        CHECK(CountLog("scene:") == 150);
        CHECK(current->Events().TotalListeners() == 1);
        current->Events().Emit("late", Entity());
        CHECK(CountLog("signal:A:late") == 1);
        CHECK(CountLog("signal:B:late") == 1);
        CHECK(CountLog("signal:C:late") == 1);
        host.Destroy();
    }

    TEST_CASE("V02: Destroy runs OnDetach in reverse order, deletes, unsubscribes and clears the panels")
    {
        Fixture f;
        Ref<Scene> s = Scene::Create();
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        host.BindScene(s.get());
        CHECK(f.panels.Has("PanelA"));
        const size_t before = g_Log.size();
        host.Destroy();
        REQUIRE(g_Log.size() == before + 6);
        CHECK(g_Log[before + 0] == "detach:C");
        CHECK(g_Log[before + 1] == "detach:A");
        CHECK(g_Log[before + 2] == "detach:B");
        CHECK(g_Log[before + 3] == "dtor:C");
        CHECK(g_Log[before + 4] == "dtor:A");
        CHECK(g_Log[before + 5] == "dtor:B");
        CHECK(s->Events().TotalListeners() == 0);
        CHECK_FALSE(f.panels.Has("PanelA"));
        CHECK(f.panels.Names().empty());
        CHECK_FALSE(host.IsInstantiated());
        CHECK(host.Count() == 0);
        CHECK(f.bus.Producer("x").empty());
        // A second Destroy and ticks on an empty host are harmless no-ops.
        host.Destroy();
        host.Tick(0.1f);
        host.FixedTick(0.1f);
        host.DispatchSignal("nothing", Entity());
        CHECK(g_Log.size() == before + 6);
    }

    TEST_CASE("V02: Instantiate re-entry destroys the previous set first")
    {
        Fixture f;
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        CHECK(host.Count() == 3);
        SvcA* first = SvcA::s_Instance;
        host.Instantiate("ap01test", f.Ctx());
        CHECK(host.Count() == 3);
        CHECK(SvcA::s_Instance != nullptr);
        CHECK(g_Constructed == 6);
        // Every detach/dtor of the first set precedes every ctor of the second set.
        int lastDetach = -1, firstSecondCtor = -1;
        for (size_t i = 0; i < g_Log.size(); ++i)
        {
            if (g_Log[i].rfind("dtor:", 0) == 0 && lastDetach < 0 && i >= 6) lastDetach = (int)i;
        }
        int ctors = 0;
        for (size_t i = 0; i < g_Log.size(); ++i)
            if (g_Log[i].rfind("ctor:", 0) == 0 && ++ctors == 4) { firstSecondCtor = (int)i; break; }
        CHECK(lastDetach >= 0);
        CHECK(firstSecondCtor > lastDetach);
        CHECK(CountLog("detach:") == 3);
        CHECK(CountLog("attach:") == 6);
        (void)first;
        host.Destroy();
        CHECK(CountLog("detach:") == 6);
        CHECK(CountLog("dtor:") == 6);
    }

    TEST_CASE("V02: ModuleRegistry AddService / FindService / ServiceNames and UnregisterModule stripping")
    {
        Fixture f;
        auto& reg = ModuleRegistry::Get();
        reg.BeginModule("ap01other");
        CS_SERVICE(SvcOther) CS_END;
        reg.EndModule();

        const auto mine = reg.ServiceNames("ap01test");
        REQUIRE(mine.size() == 3);
        CHECK(mine[0] == "SvcA"); CHECK(mine[1] == "SvcB"); CHECK(mine[2] == "SvcC");   // registration order
        CHECK(reg.ServiceNames("ap01other").size() == 1);
        CHECK(reg.ServiceNames().size() >= 4);
        const ServiceDescriptor* d = reg.FindService("SvcB");
        REQUIRE(d != nullptr);
        CHECK(d->Module == "ap01test");
        CHECK(d->Order == 0);
        CHECK(d->File.find("test_servicehost.cpp") != std::string::npos);
        CHECK(d->Line > 0);
        REQUIRE(d->Factory);
        AppService* inst = d->Factory();
        CHECK(inst != nullptr);
        delete inst;
        CHECK(reg.FindService("NoSuchService") == nullptr);

        // Re-registering replaces in place (same position, new Order).
        reg.BeginModule("ap01test");
        CS_SERVICE(SvcB).Order(7) CS_END;
        reg.EndModule();
        CHECK(reg.ServiceNames("ap01test").size() == 3);
        CHECK(reg.ServiceNames("ap01test")[1] == "SvcB");
        CHECK(reg.FindService("SvcB")->Order == 7);

        reg.UnregisterModule("ap01test");
        CHECK(reg.FindService("SvcA") == nullptr);
        CHECK(reg.FindService("SvcB") == nullptr);
        CHECK(reg.FindService("SvcC") == nullptr);
        CHECK(reg.ServiceNames("ap01test").empty());
        CHECK(reg.FindService("SvcOther") != nullptr);   // another module's service untouched
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());        // nothing registered any more
        CHECK(host.IsInstantiated());
        CHECK(host.Count() == 0);
        host.Destroy();
        reg.UnregisterModule("ap01other");
        CHECK(reg.FindService("SvcOther") == nullptr);
        RegisterServices();                            // fresh registrations work after a strip
        CHECK(reg.ServiceNames("ap01test").size() == 3);
    }

    TEST_CASE("V02: PanelRegistry — register/replace/draw/source/names/unregister/clear and the CS_PANEL call site")
    {
        PanelRegistry panels;
        int drawn = 0;
        CHECK_FALSE(panels.Has("x"));
        CHECK_FALSE(panels.Draw("x", UiRect{}));
        CHECK(panels.Names().empty());
        CHECK(panels.SourceOf("x").File.empty());
        CHECK(panels.SourceOf("x").Line == 0);

        panels.Register("Zeta", [&](const UiRect&) { drawn += 1; }, "a.cpp", 10);
        panels.Register("Alpha", [&](const UiRect&) { drawn += 10; }, "b.cpp", 20);
        CHECK(panels.Has("Zeta"));
        const auto names = panels.Names();
        REQUIRE(names.size() == 2);
        CHECK(names[0] == "Alpha");                    // sorted
        CHECK(names[1] == "Zeta");
        CHECK(panels.Draw("Zeta", UiRect{}));
        CHECK(drawn == 1);
        CHECK(panels.SourceOf("Alpha").File == "b.cpp");
        CHECK(panels.SourceOf("Alpha").Line == 20);

        panels.Register("Zeta", [&](const UiRect&) { drawn += 100; }, "c.cpp", 30);   // replaces
        CHECK(panels.Names().size() == 2);
        CHECK(panels.Draw("Zeta", UiRect{}));
        CHECK(drawn == 101);
        CHECK(panels.SourceOf("Zeta").File == "c.cpp");
        panels.Unregister("Zeta");
        CHECK_FALSE(panels.Has("Zeta"));
        CHECK_FALSE(panels.Draw("Zeta", UiRect{}));
        panels.Clear();
        CHECK(panels.Names().empty());

        // CS_PANEL from a service's OnAttach records the registration call site.
        Fixture f;
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        REQUIRE(f.panels.Has("PanelA"));
        const PanelRegistry::Source src = f.panels.SourceOf("PanelA");
        CHECK(src.File.find("test_servicehost.cpp") != std::string::npos);
        CHECK(src.Line > 0);
        CHECK(f.panels.Draw("PanelA", UiRect{ { 0.0f, 0.0f }, { 320.0f, 200.0f } }));
        CHECK(CountLog("drawA:320") == 1);
        host.Destroy();
        CHECK_FALSE(f.panels.Has("PanelA"));
    }

    TEST_CASE("V02: DispatchEvent reaches every service in order")
    {
        Fixture f;
        ServiceHost host;
        host.Instantiate("ap01test", f.Ctx());
        WindowResizeEvent e(640, 480);
        host.DispatchEvent(e);
        CHECK(CountLog("event:") == 3);
        CHECK(IndexOf("event:B") < IndexOf("event:A"));
        CHECK(IndexOf("event:A") < IndexOf("event:C"));
        host.Destroy();
    }
}

// ============================================================================
// The Data() script proxy (contract §2, last paragraph; V06 bullet).
// ============================================================================
namespace
{
    class DataScript : public ScriptableEntity
    {
    public:
        static inline int s_Updates = 0;
        static inline bool s_HasBefore = true;
        static inline double s_AgeBefore = 0.0;
        static inline double s_Fallback = 0.0;
        static inline bool s_BoolFallback = false;
        static inline std::string s_StringFallback;
    protected:
        void OnUpdate(float) override
        {
            ++s_Updates;
            s_HasBefore      = Data().Has("script.x");
            s_AgeBefore      = Data().Age("script.x");
            s_Fallback       = Data().GetNumber("missing", 4.5);
            s_BoolFallback   = Data().GetBool("missing", true);
            s_StringFallback = Data().GetString("missing", "fb");
            Data().Set("script.x", Data().GetNumber("script.x", 0.0) + 1.0);
            Data().SetBool("script.flag", true);
            Data().SetString("script.name", "hello");
        }
    };

    class DataSystem : public SystemScript
    {
    protected:
        void OnUpdateAll(std::span<Entity> entities, float) override
        {
            Data().Set("system.n", (double)entities.size());
            Data().Set("system.host", Data().GetNumber("host.value", -1.0));
        }
    };

    void RegisterDataScripts()
    {
        auto& reg = ModuleRegistry::Get();
        reg.BeginModule("ap01data");
        CS_SCRIPT(DataScript) CS_END;
        CS_SYSTEM(DataSystem).Requires<TransformComponent>() CS_END;
        reg.EndModule();
    }
}

TEST_SUITE("AP-01 V06 Data proxy")
{
    TEST_CASE("V06: Data() no-ops / defaults without a bus, reads and writes with one, on scripts and systems")
    {
        RegisterDataScripts();
        Ref<Scene> scene = Scene::Create();
        Entity e = scene->CreateEntity("Probe");
        e.AddComponent<NativeScriptComponent>().ClassName = "DataScript";
        Entity sys = scene->CreateEntity("System");
        sys.AddComponent<SystemScriptComponent>().ClassName = "DataSystem";

        // No bus installed: every call is a no-op or the fallback.
        {
            ScriptHost host;
            host.Instantiate(*scene);
            host.Tick(0.016f);
            CHECK(DataScript::s_Updates == 1);
            CHECK_FALSE(DataScript::s_HasBefore);
            CHECK(std::isinf(DataScript::s_AgeBefore));
            CHECK(DataScript::s_Fallback == 4.5);
            CHECK(DataScript::s_BoolFallback == true);
            CHECK(DataScript::s_StringFallback == "fb");
            host.Destroy();
        }

        // A bus installed BEFORE Instantiate: reads see the host's values, writes land.
        DataBus bus;
        bus.Set("host.value", 42.0);
        {
            ScriptHost host;
            host.SetDataBus(&bus);
            host.Instantiate(*scene);
            host.Tick(0.016f);
            CHECK_FALSE(DataScript::s_HasBefore);          // first tick: not written yet
            CHECK(bus.GetNumber("script.x") == 1.0);
            CHECK(bus.GetBool("script.flag") == true);
            CHECK(bus.GetString("script.name") == "hello");
            CHECK(bus.GetNumber("system.n") == 2.0);       // both entities carry a Transform
            CHECK(bus.GetNumber("system.host") == 42.0);   // the system read a host value
            bus.Advance(0.5);
            host.Tick(0.016f);
            CHECK(DataScript::s_HasBefore);
            CHECK(DataScript::s_AgeBefore == 0.5);
            CHECK(bus.GetNumber("script.x") == 2.0);
            CHECK(DataScript::s_Fallback == 4.5);          // still missing on the bus
            host.Destroy();
        }

        // Detaching the bus (nullptr) before a fresh Instantiate makes the proxy inert again.
        {
            ScriptHost host;
            host.SetDataBus(&bus);
            host.SetDataBus(nullptr);
            host.Instantiate(*scene);
            host.Tick(0.016f);
            CHECK(bus.GetNumber("script.x") == 2.0);       // unchanged
            CHECK_FALSE(DataScript::s_HasBefore);
            host.Destroy();
        }
        ModuleRegistry::Get().UnregisterModule("ap01data");
    }
}

// ============================================================================
// W — the fixture DLL through the real GameModule and PlayerLayer paths.
// ============================================================================
namespace
{
    std::string ExeDir()
    {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        return std::filesystem::path(exePath).parent_path().string();
    }

    void PublishReport(AP01ServiceReport* rep)
    {
        char ptr[32];
        std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)rep);
        _putenv_s("COSMIC_AP01_REPORT", rep ? ptr : "");
    }
}

TEST_SUITE("AP-01 V02 ServiceHost")
{
    TEST_CASE("V02 W: 20 GameModule reloads keep the host-owned DataBus, detach before FreeLibrary and never call back after unload")
    {
        AP01ServiceReport rep;
        PublishReport(&rep);
        const std::string dir = ExeDir();
        REQUIRE(std::filesystem::exists(std::filesystem::path(dir) / "AP01ServiceFixture.dll"));

        DataBus bus;              // host-owned: outlives every load of the module
        PanelRegistry panels;
        int hostPing = 0, hostSig = 0;
        bus.Subscribe("host.ping", [&](const std::string&, const DataValue&) { ++hostPing; });
        Ref<Scene> scene = Scene::Create();
        scene->Events().ConnectAny([&](const std::string&, Entity) { ++hostSig; });
        std::vector<DataSample> h;
        auto& reg = ModuleRegistry::Get();

        for (int cycle = 1; cycle <= 20; ++cycle)
        {
            CAPTURE(cycle);
            Starforge::GameModule gm;
            REQUIRE(gm.Load("AP01ServiceFixture", "AP01ServiceFixture", dir));
            CHECK(gm.IsLoaded());
            CHECK(rep.dllLoaded == cycle);
            const ServiceDescriptor* d = reg.FindService("AP01Service");
            REQUIRE(d != nullptr);
            CHECK(d->Module == "AP01ServiceFixture");
            CHECK(d->File.find("AP01ServiceFixture.cpp") != std::string::npos);
            CHECK(d->Line > 0);
            CHECK(reg.FindScript("AP01Script") != nullptr);
            CHECK(rep.constructed == cycle - 1);

            ServiceHost host;
            host.Instantiate("AP01ServiceFixture", AppContext{ bus, panels, nullptr, nullptr, "AP01ServiceFixture", false });
            CHECK(host.Count() == 1);
            CHECK(rep.constructed == cycle);
            CHECK(rep.attached == cycle);
            CHECK(rep.attachInEditor == 0);
            CHECK(rep.attachHadFlow == 0);
            CHECK(rep.reloadsSeen == cycle - 1);              // the previous load's write survived the unload
            CHECK(bus.GetNumber("ap01.reloads") == (double)cycle);
            CHECK(rep.panelRegistered == 1);
            CHECK(panels.Has("AP01Panel"));
            CHECK(panels.SourceOf("AP01Panel").File.find("AP01ServiceFixture.cpp") != std::string::npos);

            host.BindScene(scene.get());
            CHECK(rep.sceneChanges == cycle);
            bus.Set("host.ping", (double)cycle);
            CHECK(rep.pingWhileLive == cycle);
            scene->Events().Emit("ap01.sig", Entity());
            CHECK(rep.signalsWhileLive == cycle);
            for (int i = 0; i < 3; ++i) host.Tick(1.0f / 60.0f);
            CHECK(rep.updates == 3 * cycle);
            CHECK(rep.producerSeen == 3 * cycle);             // the bracket was visible inside OnUpdate
            CHECK(bus.Producer("ap01.tick") == "AP01Service");
            host.FixedTick(1.0f / 60.0f);
            CHECK(rep.fixedUpdates == cycle);
            CHECK(panels.Draw("AP01Panel", UiRect{ { 0.0f, 0.0f }, { 100.0f, 50.0f } }));
            CHECK(rep.panelDraws == cycle);
            CHECK(bus.History("ap01.tick", h) == (size_t)(3 * cycle));   // history accumulates across reloads

            const long long beforeDestroy = ++rep.seq;
            host.Destroy();
            CHECK(rep.detached == cycle);
            CHECK(rep.destroyed == cycle);
            CHECK(rep.seqDetach > beforeDestroy);
            CHECK(rep.seqDeleted > rep.seqDetach);
            CHECK_FALSE(panels.Has("AP01Panel"));
            CHECK_FALSE(host.IsInstantiated());
            CHECK(bus.Producer("ap01.tick") == "AP01Service");   // producers persist

            const long long beforeUnload = ++rep.seq;
            gm.Unload();
            const long long afterUnload = ++rep.seq;
            CHECK_FALSE(gm.IsLoaded());
            CHECK(rep.dllUnloaded == cycle);
            CHECK(rep.seqDllDetach > beforeUnload);            // the image went away inside Unload...
            CHECK(rep.seqDllDetach < afterUnload);
            CHECK(rep.seqDetach < beforeUnload);               // ...and OnDetach ran before it
            CHECK(rep.seqDeleted < beforeUnload);
            CHECK(reg.FindService("AP01Service") == nullptr);  // stripped before FreeLibrary
            CHECK(reg.ServiceNames("AP01ServiceFixture").empty());
            CHECK(reg.FindScript("AP01Script") == nullptr);

            // No callback after unload — while the same paths still reach exe listeners.
            bus.Set("host.ping", -1.0);
            scene->Events().Emit("ap01.sig", Entity());
            CHECK(rep.pingAfterUnload == 0);
            CHECK(rep.signalsAfterUnload == 0);
            CHECK(hostPing == 2 * cycle);
            CHECK(hostSig == 2 * cycle);
            CHECK(bus.GetNumber("ap01.reloads") == (double)cycle);   // values survive the unload
        }
        CHECK(bus.GetNumber("ap01.reloads") == 20.0);
        CHECK(bus.History("ap01.tick", h) == 60);
        CHECK(rep.attached == 20);
        CHECK(rep.detached == 20);
        CHECK(rep.destroyed == 20);
        CHECK(rep.dllLoaded == 20);
        CHECK(rep.dllUnloaded == 20);
        PublishReport(nullptr);
        std::printf("AP01 V02 W GameModule: reloads=20 updates=%d producerSeen=%d pingAfterUnload=%d signalsAfterUnload=%d\n",
                    rep.updates.load(), rep.producerSeen.load(), rep.pingAfterUnload.load(), rep.signalsAfterUnload.load());
    }
}

namespace
{
    // Drives the real Application through 20 load / unload cycles of the fixture DLL
    // (CreatePluginLayer -> the real PlayerLayer hosts the module's service).
    class AP01ReloadDriver final : public Layer
    {
        AP01ServiceReport& rep;
        std::string dll;
        int frames = 0, cycle = 1, phase = 0, settle = 0;
        int cycles = 20;
    public:
        int completed = 0, orderFailures = 0, failures = 0;
        AP01ReloadDriver(AP01ServiceReport& r, std::string d) : Layer("AP01 reload driver"), rep(r), dll(std::move(d)) {}
        void OnUpdate(float) override
        {
            ++frames;
            auto& app = Application::Get();
            switch (phase)
            {
            case 0:   // the service attached this cycle: let the real PlayerLayer tick it a few frames
                if (rep.attached == cycle) { if (++settle >= 4) { settle = 0; app.TransitionToLauncher(); phase = 1; } }
                break;
            case 1:   // unloaded (FreeLibrary done) and the workspace torn down: check ordering, reload
                if (rep.dllUnloaded == cycle && app.GetWorkspaceLayer() == nullptr)
                {
                    if (!(rep.seqDetach > 0 && rep.seqDetach < rep.seqDeleted && rep.seqDeleted < rep.seqDllDetach)) ++orderFailures;
                    ++completed;
                    if (cycle >= cycles) { phase = 2; WindowCloseEvent e; app.OnEvent(e); }
                    else { ++cycle; app.TransitionFromLauncherToWorkspace(dll); phase = 0; }
                }
                break;
            default: break;
            }
            if (frames > 6000 && phase != 2) { ++failures; phase = 2; WindowCloseEvent e; app.OnEvent(e); }
        }
    };
}

TEST_SUITE("AP-01 V02 ServiceHost")
{
    // Skipped by default: needs a real Application (window + GL). The AP-01 runner
    // launches it as an isolated child (ap01-units manifest, V02-W-PLAYER).
    TEST_CASE("V02 W host: 20 PlayerLayer reloads through the real Application detach the service before FreeLibrary" * doctest::skip())
    {
        AP01ServiceReport rep;
        PublishReport(&rep);
        const std::string dll = (std::filesystem::path(ExeDir()) / "AP01ServiceFixture.dll").string();
        REQUIRE(std::filesystem::exists(dll));
        int completed = 0, orderFailures = 0, failures = 0;
        {
            Application app(dll);
            app.GetWindow().SetVSync(false);
            if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
            auto* driver = new AP01ReloadDriver(rep, dll);
            app.PushLayer(driver);
            app.Run();
            completed = driver->completed; orderFailures = driver->orderFailures; failures = driver->failures;
        }
        PublishReport(nullptr);
        CHECK(failures == 0);
        CHECK(completed == 20);
        CHECK(orderFailures == 0);
        CHECK(rep.dllLoaded == 20);
        CHECK(rep.dllUnloaded == 20);
        CHECK(rep.attached == 20);              // the real PlayerLayer instantiated the service every load
        CHECK(rep.detached == 20);
        CHECK(rep.destroyed == 20);
        CHECK(rep.attachInEditor == 0);
        CHECK(rep.attachHadFlow == 0);          // no manifest => no flow
        CHECK(rep.updates >= 60);               // ticked by the real PlayerLayer frame
        CHECK(rep.producerSeen == rep.updates); // inside the PlayerLayer's ServiceHost::Tick bracket
        CHECK(rep.pingAfterUnload == 0);
        CHECK(rep.signalsAfterUnload == 0);
        CHECK(ModuleRegistry::Get().FindService("AP01Service") == nullptr);
        CHECK(ModuleRegistry::Get().FindScript("AP01Script") == nullptr);
        std::printf("AP01 V02 W PlayerLayer: cycles=%d updates=%d producerSeen=%d orderFailures=%d\n",
                    completed, rep.updates.load(), rep.producerSeen.load(), orderFailures);
    }
}
