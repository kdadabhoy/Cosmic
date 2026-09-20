#pragma once
// scripting/ServiceHost.h
//
// ============================================================================
// Cosmic app services — the per-run service driver (App Platform / AP-01,
// design contract §2).
// ============================================================================
//
// Owned by whoever runs an app (the standalone PlayerLayer; StarforgeApp for
// editor Play) next to the host-owned DataBus and PanelRegistry. Given a module
// name it:
//
//   Instantiate(module, ctx)  constructs every service the module registered
//                             (registration order, stable-sorted by Order), injects
//                             the context, then calls OnAttach in that order.
//                             Idempotent re-entry: Destroy() runs first.
//   BindScene(scene)          re-points the services at a new scene: unsubscribes
//                             the old scene bus, ConnectAny on the new one (fanning
//                             to OnSignal), updates ctx.ActiveScene, then calls
//                             OnSceneChanged(old, new). Call it while the OLD scene
//                             is still alive (the hosts do).
//   Tick / FixedTick          OnUpdate / OnFixedUpdate per service, each bracketed
//                             by Bus.SetProducer(name) ... SetProducer(previous) so
//                             every channel written remembers its producer (D-LINKS).
//   DispatchEvent / DispatchSignal  fan-outs (same producer bracket).
//   Destroy()                 OnDetach in REVERSE order, delete, unsubscribe the
//                             scene bus, Panels.Clear(). Runs BEFORE the module's
//                             UnregisterModule / FreeLibrary (KI-29 discipline: the
//                             instances' vtables are code in the module DLL).
//
// GL-free and headless-testable (register a service in-exe with CS_SERVICE).
// ============================================================================

#include "core/Core.h"
#include "scripting/AppService.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Cosmic
{
    class Scene;
    class Event;

    class COSMIC_API ServiceHost
    {
    public:
        ServiceHost() = default;
        ~ServiceHost();
        ServiceHost(const ServiceHost&) = delete;
        ServiceHost& operator=(const ServiceHost&) = delete;

        // Construct every service registered by `module` (registration order, stable-sorted by Order),
        // inject the context, call OnAttach in that order. Idempotent re-entry: calls Destroy() first.
        void Instantiate(const std::string& module, const AppContext& ctx);
        // Re-point services at a new scene: unsubscribes the old bus, subscribes ConnectAny on the new one
        // (fanning to OnSignal), updates ctx.ActiveScene, calls OnSceneChanged(old, new) on every service.
        // Binding the scene that is already bound is a no-op.
        void BindScene(Scene* scene);
        void Tick(float ts);              // Bus.SetProducer(name) ... OnUpdate ... SetProducer("") per service
        void FixedTick(float fixedDt);    // same bracket around OnFixedUpdate
        void DispatchEvent(Event& e);
        void DispatchSignal(const std::string& signal, Entity source);   // public for tests
        void Destroy();                   // OnDetach in REVERSE order, delete, unsubscribe, Panels.Clear()

        bool   IsInstantiated() const { return m_Instantiated; }
        size_t Count() const { return m_Live.size(); }
        std::vector<std::string> Names() const;   // instantiation order
        const ServiceDescriptor* DescriptorOf(const std::string& name) const;

    private:
        struct Live
        {
            AppService*       Instance = nullptr;
            ServiceDescriptor Desc;       // a copy: the registry entry may be stripped before a query
        };
        // Nesting-safe producer bracket: DispatchSignal may run INSIDE a Tick (a
        // service emits a scene signal from OnUpdate), so the previous tag is
        // restored on exit instead of blindly clearing it.
        struct ProducerScope
        {
            ServiceHost& Host; std::string Previous;
            ProducerScope(ServiceHost& host, const std::string& name);
            ~ProducerScope();
        };

        std::vector<Live>         m_Live;
        std::optional<AppContext> m_Ctx;
        std::string               m_ActiveProducer;   // what this host last told the bus
        Scene*                    m_BoundScene = nullptr;
        uint64_t                  m_BusHandle  = 0;    // EventBus ConnectAny handle on m_BoundScene (0 = none)
        bool                      m_Instantiated = false;
    };
}
