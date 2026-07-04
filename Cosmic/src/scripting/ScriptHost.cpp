// scripting/ScriptHost.cpp — per-scene script lifecycle driver (Phase 13 / E11).
// See ScriptHost.h.

#include "scripting/ScriptHost.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ModuleRegistry.h"

#include "scene/Scene.h"
#include "scene/Components.h"
#include "core/Log.h"

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
            inst->m_Scene  = &scene;
            inst->m_Handle = e;
            nsc.Instance   = inst;
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
    }

    void ScriptHost::Tick(float ts)
    {
        if (!m_Scene) return;
        auto& reg = m_Scene->GetRegistry();
        for (entt::entity e : m_Live)
            if (reg.valid(e))
                if (auto* nsc = reg.try_get<NativeScriptComponent>(e); nsc && nsc->Instance)
                    nsc->Instance->OnUpdate(ts);
    }

    void ScriptHost::FixedTick(float fixedDt)
    {
        if (!m_Scene) return;
        auto& reg = m_Scene->GetRegistry();
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
