// scripting/ScriptHost.cpp — per-scene script lifecycle driver (Phase 13 / E11).
// See ScriptHost.h.

#include "scripting/ScriptHost.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ModuleRegistry.h"

#include "scene/Scene.h"
#include "scene/Components.h"
#include "core/Log.h"

#include <algorithm>
#include <span>

namespace Cosmic
{
    ScriptHost::~ScriptHost()
    {
        Destroy();
    }

    void ScriptHost::PushFields(const ScriptDescriptor& desc,
                                const NativeScriptComponent& comp, ScriptableEntity* instance)
    {
        if (!instance)
            return;
        // The script's ScriptableEntity subobject is at offset 0 (single base), so
        // the instance pointer IS the concrete-type address the field thunks expect.
        void* obj = static_cast<void*>(instance);
        for (const auto& f : desc.Fields.Fields)
        {
            auto it = comp.Fields.find(f.Name);
            if (it != comp.Fields.end())
                f.Set(obj, it->second);
        }
    }

    void ScriptHost::PullFields(const ScriptDescriptor& desc,
                                ScriptableEntity* instance, NativeScriptComponent& comp)
    {
        if (!instance)
            return;
        const void* obj = static_cast<const void*>(instance);
        for (const auto& f : desc.Fields.Fields)
            comp.Fields[f.Name] = f.Get(obj);
    }

    void ScriptHost::Instantiate(Scene& scene)
    {
        Destroy();               // idempotent re-entry
        m_Scene = &scene;

        auto& reg = scene.GetRegistry();
        auto& modules = ModuleRegistry::Get();

        // Pass 1 — construct + inject + push fields. Collect for the OnCreate/OnStart
        // sweeps so every entity exists before any callback runs.
        std::vector<entt::entity> created;
        for (auto e : reg.view<NativeScriptComponent>())
        {
            auto& nsc = reg.get<NativeScriptComponent>(e);
            nsc.Instance = nullptr;
            if (nsc.ClassName.empty())
                continue;

            const ScriptDescriptor* desc = modules.FindScript(nsc.ClassName);
            if (!desc || !desc->Factory)
            {
                CS_CORE_WARN("ScriptHost: unknown script class '{0}' — entity kept inert.",
                             nsc.ClassName);
                continue;
            }

            ScriptableEntity* inst = desc->Factory();
            inst->m_Scene         = &scene;
            inst->m_Handle        = e;
            inst->m_TelemetrySink = m_Sink;   // E20 — null unless a host installed one
            nsc.Instance          = inst;
            PushFields(*desc, nsc, inst);

            created.push_back(e);
            m_Live.push_back(e);
        }

        // Pass 2 — OnCreate everyone (all entities/instances now exist).
        for (entt::entity e : created)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    static_cast<ScriptableEntity*>(nsc->Instance)->OnCreate();

        // Pass 3 — OnStart everyone, after every OnCreate.
        for (entt::entity e : created)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    static_cast<ScriptableEntity*>(nsc->Instance)->OnStart();

        // Systems (H9) — resolved AFTER per-entity scripts. One instance per
        // SystemScriptComponent, sorted by descriptor Order (stable). OnCreate all,
        // then OnStart all. Unknown class names warn once and are skipped.
        for (auto e : reg.view<SystemScriptComponent>())
        {
            auto& ssc = reg.get<SystemScriptComponent>(e);
            ssc.Instance = nullptr;
            if (ssc.ClassName.empty())
                continue;

            const SystemDescriptor* desc = modules.FindSystem(ssc.ClassName);
            if (!desc || !desc->Factory)
            {
                CS_CORE_WARN("ScriptHost: unknown system class '{0}' — ignored.", ssc.ClassName);
                continue;
            }

            SystemScript* inst = desc->Factory();
            inst->m_Scene = &scene;
            ssc.Instance  = inst;

            // Push saved reflected overrides (same thunk contract as scripts).
            void* obj = static_cast<void*>(inst);
            for (const auto& f : desc->Fields.Fields)
            {
                auto it = ssc.Fields.find(f.Name);
                if (it != ssc.Fields.end())
                    f.Set(obj, it->second);
            }

            m_Systems.push_back({ inst, desc, e });
        }

        std::stable_sort(m_Systems.begin(), m_Systems.end(),
            [](const LiveSystem& a, const LiveSystem& b) { return a.Desc->Order < b.Desc->Order; });

        for (auto& ls : m_Systems) ls.Instance->OnCreate();
        for (auto& ls : m_Systems) ls.Instance->OnStart();
    }

    // Rebuild a system's membership span from its query (H9). Scratch is reused
    // across ticks; the returned span is valid until the next call.
    std::span<Entity> ScriptHost::BuildMembership(const LiveSystem& ls)
    {
        m_MemberHandles.clear();
        if (ls.Desc && ls.Desc->Collect)
            ls.Desc->Collect(*m_Scene, m_MemberHandles);

        m_MemberEntities.clear();
        m_MemberEntities.reserve(m_MemberHandles.size());
        for (entt::entity h : m_MemberHandles)
            m_MemberEntities.emplace_back(h, m_Scene);

        return std::span<Entity>(m_MemberEntities);
    }

    void ScriptHost::Tick(float ts)
    {
        if (!m_Scene) return;
        auto& reg = m_Scene->GetRegistry();

        // Systems FIRST (H9) — one OnUpdateAll call per system with its live members.
        for (auto& ls : m_Systems)
            if (ls.Instance)
                ls.Instance->OnUpdateAll(BuildMembership(ls), ts);

        for (entt::entity e : m_Live)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    nsc->Instance->OnUpdate(ts);
    }

    void ScriptHost::FixedTick(float fixedDt)
    {
        if (!m_Scene) return;
        auto& reg = m_Scene->GetRegistry();

        for (auto& ls : m_Systems)   // systems first (H9)
            if (ls.Instance)
                ls.Instance->OnFixedUpdateAll(BuildMembership(ls), fixedDt);

        for (entt::entity e : m_Live)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    nsc->Instance->OnFixedUpdate(fixedDt);
    }

    void ScriptHost::DispatchEvent(Event& ev)
    {
        if (!m_Scene) return;
        auto& reg = m_Scene->GetRegistry();
        for (entt::entity e : m_Live)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    nsc->Instance->OnEvent(ev);
    }

    void ScriptHost::Destroy()
    {
        // Systems first (H9) — they were created last; OnDestroy, delete, null holder.
        for (auto& ls : m_Systems)
        {
            if (ls.Instance)
                ls.Instance->OnDestroy();
            delete ls.Instance;
            if (m_Scene && m_Scene->GetRegistry().valid(ls.Holder))
                if (auto* ssc = m_Scene->GetRegistry().try_get<SystemScriptComponent>(ls.Holder))
                    ssc->Instance = nullptr;
        }
        m_Systems.clear();

        if (m_Scene)
        {
            auto& reg = m_Scene->GetRegistry();
            for (entt::entity e : m_Live)
            {
                if (!reg.valid(e)) continue;
                auto* nsc = reg.try_get<NativeScriptComponent>(e);
                if (nsc && nsc->Instance)
                {
                    nsc->Instance->OnDestroy();
                    delete nsc->Instance;
                    nsc->Instance = nullptr;
                }
            }
        }
        m_Live.clear();
        m_Scene = nullptr;
    }
}
