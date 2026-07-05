// scripting/ModuleRegistry.cpp — the process-wide script/component registry
// singleton (Phase 13 / E11). See ModuleRegistry.h.

#include "scripting/ModuleRegistry.h"

#include <algorithm>

namespace Cosmic
{
    ModuleRegistry& ModuleRegistry::Get()
    {
        // Constructed once and intentionally leaked — it must outlive every game
        // DLL that might touch it during static teardown (same rationale as
        // Reflect::GetRegistry). No engine types register here at first use;
        // modules populate it explicitly via CosmicModule_Register.
        static ModuleRegistry* s_Instance = new ModuleRegistry();
        return *s_Instance;
    }

    void ModuleRegistry::NoteComponent(entt::id_type typeId, const std::string& name)
    {
        for (auto& c : m_Components)
            if (c.Id == typeId) { c.Module = m_CurrentModule; c.Name = name; return; }
        m_Components.push_back({ typeId, name, m_CurrentModule });
    }

    const ScriptDescriptor* ModuleRegistry::FindScript(const std::string& name) const
    {
        auto it = m_Scripts.find(name);
        return it == m_Scripts.end() ? nullptr : &it->second;
    }

    std::vector<std::string> ModuleRegistry::ScriptNames() const
    {
        std::vector<std::string> out;
        out.reserve(m_Scripts.size());
        for (const auto& [name, desc] : m_Scripts)
            out.push_back(name);
        return out;
    }

    std::vector<std::string> ModuleRegistry::ScriptNames(const std::string& module) const
    {
        std::vector<std::string> out;
        for (const auto& [name, desc] : m_Scripts)
            if (desc.Module == module)
                out.push_back(name);
        return out;
    }

    const SystemDescriptor* ModuleRegistry::FindSystem(const std::string& name) const
    {
        auto it = m_Systems.find(name);
        return it == m_Systems.end() ? nullptr : &it->second;
    }

    std::vector<std::string> ModuleRegistry::SystemNames() const
    {
        std::vector<std::string> out;
        out.reserve(m_Systems.size());
        for (const auto& [name, desc] : m_Systems)
            out.push_back(name);
        return out;
    }

    std::vector<std::string> ModuleRegistry::SystemNames(const std::string& module) const
    {
        std::vector<std::string> out;
        for (const auto& [name, desc] : m_Systems)
            if (desc.Module == module)
                out.push_back(name);
        return out;
    }

    std::vector<entt::id_type> ModuleRegistry::ComponentTypeIds(const std::string& module) const
    {
        std::vector<entt::id_type> out;
        for (const auto& c : m_Components)
            if (c.Module == module)
                out.push_back(c.Id);
        return out;
    }

    void ModuleRegistry::UnregisterModule(const std::string& module)
    {
        for (auto it = m_Scripts.begin(); it != m_Scripts.end(); )
        {
            if (it->second.Module == module) it = m_Scripts.erase(it);
            else                             ++it;
        }
        for (auto it = m_Systems.begin(); it != m_Systems.end(); )   // H9
        {
            if (it->second.Module == module) it = m_Systems.erase(it);
            else                             ++it;
        }
        m_Components.erase(
            std::remove_if(m_Components.begin(), m_Components.end(),
                           [&](const ComponentNote& c) { return c.Module == module; }),
            m_Components.end());
    }
}
