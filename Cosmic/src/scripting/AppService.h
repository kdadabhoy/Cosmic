#pragma once
// scripting/AppService.h
//
// ============================================================================
// Cosmic app services — the module-owned logic tier (App Platform / AP-01,
// design contract §2).
// ============================================================================
//
// Where a ScriptableEntity is logic bound to ONE entity, an AppService is logic
// bound to the APP: one instance per run, constructed by the host's ServiceHost
// from the module's CS_SERVICE registrations, given an AppContext (the host-owned
// DataBus, the PanelRegistry, the active scene, the flow) and driven through a
// fixed callback order (see ServiceHost.h and the §2 frame-order table):
//
//     OnAttach(ctx)           once per run, after ALL services are constructed,
//                             before the first BindScene
//     OnUpdate(ts)            every variable tick, BEFORE UiSystem::Update and the flow
//     OnFixedUpdate(dt)       every fixed step, BEFORE ScriptHost::FixedTick
//     OnSignal(name, source)  every scene-bus signal (fanned by the host's ConnectAny)
//     OnSceneChanged(old,new) on every BindScene
//     OnEvent(e)              input/app events while not paused, before scripts
//     OnDetach()              once, before the module unloads / Play stops
//
// The engine ships generic verbs; a service owns the domain logic (a pendulum,
// a rocket, an ESC) and publishes what the screens show through Bus(). A hosted
// ImGui panel is registered from OnAttach with CS_PANEL(name, fn) — the host draws
// it inside an ImGui window sized to the UiHostedPanel element (§4).
//
// GL-free and header-only for the module DLL (the registries live in the engine).
// ============================================================================

#include "core/Core.h"
#include "data/DataBus.h"
#include "scene/Entity.h"                // Entity is passed by value to OnSignal
#include "scripting/ModuleRegistry.h"    // ServiceDescriptor (the registry-side record)

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Cosmic
{
    class Scene;
    class FlowMachine;
    class Event;
    struct UiRect;   // scene/ui/UiComponents.h — the hosted panel's canvas rect

    /**
     * @brief Hosted-panel draw callbacks (D-UI, D-LINKS). Services register a
     * drawer by name from OnAttach (CS_PANEL); the host resolves the scene's
     * UiHostedPanel elements to rects and calls Draw INSIDE an ImGui window sized
     * to that rect (§4). Unknown names are not an error (Draw returns false and the
     * canvas keeps its placeholder). Re-registering a name replaces the drawer.
     */
    class COSMIC_API PanelRegistry
    {
    public:
        using DrawFn = std::function<void(const UiRect& rect)>;   // called INSIDE an ImGui window sized to rect
        struct Source { std::string File; int Line = 0; };

        void Register(const std::string& name, DrawFn fn, const char* file = nullptr, int line = 0);   // re-register replaces
        void Unregister(const std::string& name);
        bool Has(const std::string& name) const;
        bool Draw(const std::string& name, const UiRect& rect) const;   // false when unknown; the host wraps Begin/End
        Source SourceOf(const std::string& name) const;
        std::vector<std::string> Names() const;                         // sorted
        void Clear();

    private:
        struct Entry { DrawFn Fn; Source Src; };
        std::map<std::string, Entry> m_Panels;   // sorted by name
    };

    /** @brief What a service sees. Bus/Panels are the host's (they outlive the
     *  module); ActiveScene/Flow are updated by the ServiceHost. */
    struct AppContext
    {
        DataBus&       Bus;
        PanelRegistry& Panels;
        Scene*         ActiveScene = nullptr;   // the flow's top scene / the played scene; updated by ServiceHost::BindScene
        FlowMachine*   Flow        = nullptr;   // null when the project has no startup flow
        std::string    ProjectName;
        bool           InEditor    = false;     // true under Starforge Play
    };

    class COSMIC_API AppService
    {
    public:
        virtual ~AppService() = default;

    protected:
        virtual void OnAttach(AppContext& ctx) { (void)ctx; }   // once per run, after ALL services are constructed, before the first BindScene
        virtual void OnDetach() {}                              // once, before the module unloads / Play stops
        virtual void OnUpdate(float ts) { (void)ts; }           // every variable tick, BEFORE UiSystem::Update and the flow
        virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }   // every fixed step, BEFORE ScriptHost::FixedTick
        virtual void OnSignal(const std::string& signal, Entity source) { (void)signal; (void)source; }   // every scene-bus signal
        virtual void OnSceneChanged(Scene* oldScene, Scene* newScene) { (void)oldScene; (void)newScene; }
        virtual void OnEvent(Event& e) { (void)e; }             // input/app events while not paused, before scripts

        AppContext&    Context() const { return *m_Ctx; }   // valid from OnAttach to OnDetach
        DataBus&       Bus()     const { return Context().Bus; }
        PanelRegistry& Panels()  const { return Context().Panels; }

    private:
        friend class ServiceHost;   // injects m_Ctx + drives the callbacks
        AppContext* m_Ctx = nullptr;
    };

    // Register a hosted-panel drawer from inside OnAttach; records the call site for "Open source".
    // Expands inside a member function of an AppService subclass (Panels() is a protected member).
    #define CS_PANEL(name, fn) Panels().Register((name), (fn), __FILE__, __LINE__)

    /**
     * @brief The CS_SERVICE chain: `CS_SERVICE(T).Order(n) CS_END;`. Order sequences
     * services with the same registration order (stable). No CS_FIELD support in v1.
     * Defined here (not in ModuleRegistry.h) like SystemBuilder: ModuleRegistry
     * forward-declares it and AddService<T> only instantiates where a module .cpp
     * (via <Cosmic.h> / ModuleMacros.h) sees the full definition.
     */
    template<typename T>
    class ServiceBuilder
    {
    public:
        explicit ServiceBuilder(ServiceDescriptor* desc) : m_Desc(desc) {}
        ServiceBuilder& Order(int n) { if (m_Desc) m_Desc->Order = n; return *this; }
    private:
        ServiceDescriptor* m_Desc = nullptr;
    };
}
