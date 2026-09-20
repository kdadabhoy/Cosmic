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

    ServiceDescriptor& ModuleRegistry::ServiceSlot(const std::string& name)
    {
        for (ServiceDescriptor& d : m_Services)
            if (d.Name == name) return d;      // re-register: replace in place
        m_Services.push_back(ServiceDescriptor{});
        return m_Services.back();
    }

    const ServiceDescriptor* ModuleRegistry::FindService(const std::string& name) const
    {
        for (const ServiceDescriptor& d : m_Services)
            if (d.Name == name) return &d;
        return nullptr;
    }

    std::vector<std::string> ModuleRegistry::ServiceNames() const
    {
        std::vector<std::string> out;
        out.reserve(m_Services.size());
        for (const ServiceDescriptor& d : m_Services)
            out.push_back(d.Name);
        return out;
    }

    std::vector<std::string> ModuleRegistry::ServiceNames(const std::string& module) const
    {
        std::vector<std::string> out;
        for (const ServiceDescriptor& d : m_Services)
            if (d.Module == module)
                out.push_back(d.Name);
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
        // WO-07 / KI-29: the module's component descriptors in the Reflect
        // registry hold std::functions (Add/Has/Get/Remove/Copy + every field
        // Read/Write) whose code is IN the module DLL. Remove them now — the
        // caller unloads the DLL right after — so no stale thunk can be called
        // (or destroyed) once the image is unmapped, and a scene loaded while the
        // module is absent keeps the block as an opaque block. The next load
        // registers a fresh descriptor.
        for (const auto& c : m_Components)
            if (c.Module == module)
                Reflect::GetRegistry().Remove(c.Id);

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
        // AP-01: the module's services go exactly like its scripts — their factories
        // are code in the module DLL. Live instances were destroyed by the host's
        // ServiceHost::Destroy() before it called this.
        m_Services.erase(
            std::remove_if(m_Services.begin(), m_Services.end(),
                           [&](const ServiceDescriptor& d) { return d.Module == module; }),
            m_Services.end());
        m_Components.erase(
            std::remove_if(m_Components.begin(), m_Components.end(),
                           [&](const ComponentNote& c) { return c.Module == module; }),
            m_Components.end());
    }
}
