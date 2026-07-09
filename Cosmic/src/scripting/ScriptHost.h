#pragma once
// scripting/ScriptHost.h
//
// ============================================================================
// Cosmic scripting — the per-scene script driver (Phase 13 / E11).
// ============================================================================
//
// Owned by whoever runs a Play session (Starforge's PlayState in the editor, the
// engine PlayerLayer standalone). Given a runtime Scene it:
//
//   Instantiate(scene)  for every entity with a NativeScriptComponent: resolve
//                       ClassName -> factory -> construct -> inject entity+scene ->
//                       push the reflected field values -> OnCreate all, then
//                       OnStart all. Unresolved class names log once and stay inert
//                       (never a crash).
//   Tick(ts)/FixedTick(dt)/DispatchEvent(e)  fan the callback to every live
//                       instance in creation order.
//   Destroy(scene)      OnDestroy each, delete, null the component's Instance.
//
// Field pull-back on Stop is intentionally NOT done — the edit scene is
// authoritative (§E11). The static Push/PullFields helpers are reused by the
// editor to seed a script's default field values when it is first assigned.
//
// GL-free and headless-testable (no DLL needed — register a script in-exe).
// ============================================================================

#include "core/Core.h"
#include "scene/Entity.h"   // std::vector<Entity> membership scratch (H9)

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace Cosmic
{
    class Scene;
    class Event;
    class ScriptableEntity;
    class ITelemetrySink;
    class SystemScript;
    struct ScriptDescriptor;
    struct SystemDescriptor;
    struct NativeScriptComponent;

    class COSMIC_API ScriptHost
    {
    public:
        ScriptHost() = default;
        ~ScriptHost();

        ScriptHost(const ScriptHost&)            = delete;   // owns heap instances
        ScriptHost& operator=(const ScriptHost&) = delete;

        // Telemetry seam (E20): install a generic sink that scripts push to via
        // ScriptableEntity::Telemetry(). Injected into every instance at
        // Instantiate; set before Instantiate to also catch OnCreate/OnStart
        // pushes. Pass nullptr to detach. No-op for scripts that never push.
        void SetTelemetrySink(ITelemetrySink* sink) { m_Sink = sink; }

        // Build + start every script in the scene (OnCreate all, then OnStart all).
        void Instantiate(Scene& scene);

        void Tick(float ts);            // OnUpdate for every live instance
        void FixedTick(float fixedDt);  // OnFixedUpdate
        void DispatchEvent(Event& e);   // OnEvent

        // Physics contact dispatch (J5) — invoke the collision/trigger virtual on
        // the script instance owned by `self` (no-op if it has none). `other` is the
        // counterpart entity. Called by Scene::DispatchPhysicsEvents after the step.
        void DispatchCollisionEnter(entt::entity self, Entity other);
        void DispatchCollisionExit(entt::entity self, Entity other);
        void DispatchTriggerEnter(entt::entity self, Entity other);
        void DispatchTriggerExit(entt::entity self, Entity other);

        // OnDestroy each, delete, null the NativeScriptComponent::Instance pointers.
        void Destroy();

        // Route a scene signal to every live script's OnSignal (U2). Wired to the
        // scene EventBus at Instantiate via ConnectAny; also public so a host can
        // drive it directly in a test.
        void DispatchSignal(const std::string& signal, Entity source);

        bool IsInstantiated() const { return m_Scene != nullptr; }
        size_t LiveCount()    const { return m_Live.size(); }

        // ---- field <-> instance (also used by the editor) -------------------
        // Push saved override values from the component into a fresh instance.
        static void PushFields(const ScriptDescriptor& desc,
                               const NativeScriptComponent& comp, ScriptableEntity* instance);
        // Read the instance's current field values back into the component's map.
        static void PullFields(const ScriptDescriptor& desc,
                               ScriptableEntity* instance, NativeScriptComponent& comp);

    private:
        Scene* m_Scene = nullptr;
        std::vector<entt::entity> m_Live;   // entities with a live instance, creation order
        ITelemetrySink* m_Sink = nullptr;   // E20 — injected into each instance
        uint64_t m_SignalHandle = 0;        // U2 — EventBus ConnectAny handle (0 = none)

        // SystemScript tier (H9): one instance per SystemScriptComponent, resolved
        // after per-entity scripts and ticked BEFORE them (deterministic order).
        struct LiveSystem
        {
            SystemScript*           Instance = nullptr;
            const SystemDescriptor* Desc     = nullptr;
            entt::entity            Holder   = entt::null;   // the component's entity
        };
        std::vector<LiveSystem>   m_Systems;
        std::vector<entt::entity> m_MemberHandles;   // per-tick scratch (membership query)
        std::vector<Entity>       m_MemberEntities;  // per-tick scratch (span handed to the system)

        // Rebuild a system's membership span from its query (scratch reused per tick).
        std::span<Entity> BuildMembership(const LiveSystem& ls);
    };
}
