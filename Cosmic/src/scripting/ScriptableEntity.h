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
#include "scene/Components.h"            // TagComponent (SystemBuilder::WithTag), H9
#include "scripting/ModuleRegistry.h"    // SystemDescriptor (SystemBuilder), H9

#include <entt/entt.hpp>

#include <span>
#include <vector>
#include <string>
#include <functional>

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

    // ========================================================================
    // SystemScript (H9) — logic bound to a *class* of entities.
    //
    // Where ScriptableEntity is one-instance-per-entity, a SystemScript is
    // one-instance-per-scene whose OnUpdateAll gets the WHOLE matching entity set
    // each tick (the "one physics script drives every airplane" pattern). Register
    // with CS_SYSTEM(T).Requires<Components...>().WithTag("optional") — membership is
    // that query, rebuilt per tick from the live scene (entities may spawn/die).
    // Reflected fields (CS_FIELD) serialize/inspect exactly like a script's. Held by
    // a SystemScriptComponent on any entity; instantiated + ticked by the ScriptHost
    // (systems tick BEFORE per-entity scripts — deterministic).
    // ========================================================================
    class COSMIC_API SystemScript
    {
    public:
        virtual ~SystemScript() = default;

        /** @brief The scene this system runs in (spawn/destroy/find-by-uuid). */
        Scene& GetScene() const { return *m_Scene; }

    protected:
        virtual void OnCreate() {}
        virtual void OnStart() {}
        // Called ONCE per tick with the matching entity set (not per entity). The span
        // is scratch owned by the ScriptHost — valid only for the call; copy handles
        // you keep. Iteration order is the entt view order (not user-sortable in v1).
        virtual void OnUpdateAll(std::span<Entity> entities, float ts) { (void)entities; (void)ts; }
        virtual void OnFixedUpdateAll(std::span<Entity> entities, float fixedDt) { (void)entities; (void)fixedDt; }
        virtual void OnDestroy() {}

    private:
        friend class ScriptHost;   // injects m_Scene + drives callbacks
        Scene* m_Scene = nullptr;
    };

    // ------------------------------------------------------------------------
    // SystemBuilder<T> — the CS_SYSTEM chain: declares membership + reflected
    // fields on a SystemDescriptor. Requires<>() sets the component filter,
    // WithTag() adds an exact TagComponent match, Order() sequences systems, and
    // Field() (via CS_FIELD) attaches reflected overrides. Defined here (not in
    // ModuleRegistry.h) so the membership query can see Scene/Entity/TagComponent;
    // ModuleRegistry forward-declares it and only instantiates AddSystem<T> where
    // the full definition is visible (a module .cpp via <Cosmic.h>).
    // ------------------------------------------------------------------------
    template<typename T>
    class SystemBuilder
    {
    public:
        explicit SystemBuilder(SystemDescriptor* desc) : m_Desc(desc) {}

        template<typename... Comps>
        SystemBuilder& Requires()
        {
            m_Base = [](Scene& s, std::vector<entt::entity>& out)
            {
                for (auto e : s.GetRegistry().view<Comps...>())
                    out.push_back(e);
            };
            Rebuild();
            return *this;
        }

        SystemBuilder& WithTag(const std::string& tag) { m_Tag = tag; Rebuild(); return *this; }
        SystemBuilder& Order(int order) { if (m_Desc) m_Desc->Order = order; return *this; }

        // CS_FIELD entry point — reflected field on the descriptor's field list;
        // returns a ClassBuilder so hint calls + subsequent CS_FIELDs chain.
        template<typename M>
        Reflect::ClassBuilder<T> Field(const char* name, M T::* member)
        {
            Reflect::ClassBuilder<T> cb(&m_Desc->Fields);
            cb.Field(name, member);
            return cb;
        }

    private:
        void Rebuild()
        {
            if (!m_Desc)
                return;
            auto base = m_Base;
            std::string tag = m_Tag;
            m_Desc->Collect = [base, tag](Scene& s, std::vector<entt::entity>& out)
            {
                if (!base)
                    return;
                std::vector<entt::entity> tmp;
                base(s, tmp);
                auto& reg = s.GetRegistry();
                for (entt::entity e : tmp)
                {
                    if (!tag.empty())
                    {
                        auto* tc = reg.try_get<TagComponent>(e);
                        if (!tc || tc->Tag != tag)
                            continue;
                    }
                    out.push_back(e);
                }
            };
        }

        SystemDescriptor* m_Desc = nullptr;
        std::function<void(Scene&, std::vector<entt::entity>&)> m_Base;
        std::string m_Tag;
    };
}
