#pragma once
// scripting/ScriptableEntity.h
//
// ============================================================================
// Cosmic scripting — the native C++ script base class (Phase 13 / E11).
// ============================================================================
//
// A script is a real C++ class deriving from ScriptableEntity, compiled into the
// project's game DLL and registered with CS_SCRIPT (see ModuleMacros.h). One
// entity carries a NativeScriptComponent naming the class; at Play the ScriptHost
// resolves the name -> factory, constructs an instance, injects the owning entity
// + scene, pushes the reflected field values saved with the scene, and drives the
// lifecycle callbacks below. Edit mode has no instances (scripts do not run in the
// editor) — the field values live in NativeScriptComponent until Play (§2.4).
//
// GL-free and header-only: user scripts include this (via <Cosmic.h>) and get the
// full engine API through GetScene()/GetEntity(). The lifecycle is:
//
//     OnCreate()   after every entity of the scene exists (all scripts constructed)
//     OnStart()    first Play frame, after every OnCreate has run
//     OnUpdate(ts) variable timestep
//     OnFixedUpdate(dt)  sim-grade fixed timestep
//     OnEvent(e)   input/application events forwarded while Play is live
//     OnDestroy()  on Stop, before the runtime scene is torn down
// ============================================================================

#include "core/Core.h"
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <entt/entt.hpp>

namespace Cosmic
{
    class Event;

    // ------------------------------------------------------------------------
    // ITelemetrySink (E20) — a generic seam for script-emitted telemetry.
    //
    // A host (the Starforge editor's telemetry panel, or any sim harness) may
    // implement this and hand it to the ScriptHost; scripts then push named
    // scalar channels through ScriptableEntity::Telemetry().Push("name", value)
    // and the host routes them into its store. The engine stays name-agnostic —
    // there are no editor/Starforge types here, and the default (no sink) makes
    // Push a cheap no-op, so shipped apps are unaffected.
    // ------------------------------------------------------------------------
    class ITelemetrySink
    {
    public:
        virtual ~ITelemetrySink() = default;

        /** @brief Record one named scalar for `source` during the current step. */
        virtual void Push(entt::entity source, const char* channel, float value) = 0;
    };

    class COSMIC_API ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        // ---- injected accessors (valid from OnCreate onward) ----------------

        /** @brief The owning entity handle. Components via GetComponent<T>(). */
        Entity GetEntity() const { return Entity(m_Handle, m_Scene); }

        /** @brief The scene this script lives in (spawn/destroy/find-by-uuid). */
        Scene& GetScene()  const { return *m_Scene; }

        template<typename T>
        T& GetComponent() { return GetEntity().GetComponent<T>(); }

        template<typename T>
        const T& GetComponent() const { return GetEntity().GetComponent<T>(); }

        template<typename T>
        bool HasComponent() const { return m_Scene && m_Scene->GetRegistry().all_of<T>(m_Handle); }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) { return GetEntity().AddComponent<T>(std::forward<Args>(args)...); }

    protected:
        // ---- telemetry passthrough (E20) ------------------------------------
        // A thin handle bound to this script's entity. Push a named scalar to the
        // host's telemetry store: Telemetry().Push("thrust_N", value). No-op when
        // no sink is installed (the default outside a recording host).
        struct TelemetryProxy
        {
            ITelemetrySink* Sink = nullptr;
            entt::entity    Source = entt::null;
            void Push(const char* channel, float value) const
            {
                if (Sink) Sink->Push(Source, channel, value);
            }
        };
        TelemetryProxy Telemetry() const { return { m_TelemetrySink, m_Handle }; }


        // Override the ones you need — all default to no-ops.
        virtual void OnCreate() {}
        virtual void OnStart() {}
        virtual void OnUpdate(float ts) { (void)ts; }
        virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }
        virtual void OnEvent(Event& e) { (void)e; }
        virtual void OnDestroy() {}

    private:
        friend class ScriptHost;   // injects m_Scene/m_Handle/m_TelemetrySink + drives callbacks
        entt::entity    m_Handle{ entt::null };
        Scene*          m_Scene = nullptr;
        ITelemetrySink* m_TelemetrySink = nullptr;   // null unless a host installs one
    };
}
