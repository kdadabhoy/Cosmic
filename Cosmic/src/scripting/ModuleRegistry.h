#pragma once
// scripting/ModuleRegistry.h
//
// ============================================================================
// Cosmic scripting — the script/component factory registry (Phase 13 / E11).
// ============================================================================
//
// A process-wide singleton owned by the ENGINE DLL (like Reflect::GetRegistry):
// game modules register their scripts + custom components into it, and every DLL
// in the process sees the one instance. It stores, per script class:
//   * a factory (name -> heap ScriptableEntity*), used by the ScriptHost at Play;
//   * a reflected field descriptor (the same Reflect machinery components use), so
//     the Inspector/serializer can enumerate and edit a script's fields.
//
// Custom components (CS_COMPONENT) are ordinary reflected entt components — they
// register straight into Reflect::GetRegistry(); the ModuleRegistry only NOTES
// their type ids so a hot reload (E12) can clear their entt storage before
// FreeLibrary. Registrations are bracketed by BeginModule/EndModule so
// UnregisterModule can strip exactly what one module added.
//
// GL-free, headless-testable (register a script in-exe, no DLL needed).
// ============================================================================

#include "reflect/TypeRegistry.h"

#include <entt/entt.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cosmic
{
    class ScriptableEntity;
    class SystemScript;                     // scripting/ScriptableEntity.h (H9)
    class Scene;                            // membership query iterates its registry
    template<typename> class SystemBuilder; // scripting/ScriptableEntity.h (H9)

    // One registered script class: its factory + reflected field list + owner tag.
    struct ScriptDescriptor
    {
        std::string                        Name;
        std::function<ScriptableEntity*()> Factory;
        Reflect::TypeDescriptor            Fields;   // fields only; entt thunks unused
        std::string                        Module;   // owning module ("" = in-exe/engine)
    };

    // One registered SYSTEM class (H9): factory + fields + a membership query. Collect
    // appends the entt handles the system should act on (built per tick from the query
    // declared via SystemBuilder::Requires<>/WithTag). Order sequences multiple systems.
    struct SystemDescriptor
    {
        std::string                    Name;
        std::function<SystemScript*()> Factory;
        Reflect::TypeDescriptor        Fields;
        std::string                    Module;
        int                            Order = 0;
        std::function<void(Scene&, std::vector<entt::entity>&)> Collect;
    };

    class COSMIC_API ModuleRegistry
    {
    public:
        // The one process-wide registry (defined in ModuleRegistry.cpp, engine DLL).
        static ModuleRegistry& Get();

        // ---- module scoping (E12 hot reload) -------------------------------
        // Bracket a module's registrations; new scripts/components are tagged with
        // the current module so UnregisterModule can remove exactly them.
        void BeginModule(const std::string& module) { m_CurrentModule = module; }
        void EndModule()                             { m_CurrentModule.clear(); }
        const std::string& CurrentModule() const     { return m_CurrentModule; }

        // ---- registration ---------------------------------------------------
        // Register a script factory + its field descriptor. Returns a ClassBuilder
        // bound to the descriptor's field list so CS_FIELD(...) chains attach hints.
        // Re-registering the same name overwrites (idempotent reload).
        template<typename T>
        Reflect::ClassBuilder<T> AddScript(const std::string& name)
        {
            ScriptDescriptor& d = m_Scripts[name];
            d.Name    = name;
            d.Module  = m_CurrentModule;
            d.Factory = []() -> ScriptableEntity* { return static_cast<ScriptableEntity*>(new T()); };
            d.Fields.TypeId = entt::type_hash<T>::value();
            d.Fields.Name   = name;
            d.Fields.Fields.clear();   // fresh field list on re-register
            return Reflect::ClassBuilder<T>(&d.Fields);
        }

        // Register a SYSTEM class T (a SystemScript subclass) with a membership query
        // (H9). Returns a SystemBuilder bound to the descriptor so the CS_SYSTEM chain
        // can attach .Requires<...>()/.WithTag(...)/.Order(...) and CS_FIELD(...) hints.
        // Re-registering the same name overwrites (idempotent reload). The body only
        // instantiates where called (a module .cpp that includes the full SystemBuilder
        // via <Cosmic.h>), so ModuleRegistry.h can forward-declare it.
        template<typename T>
        SystemBuilder<T> AddSystem(const std::string& name)
        {
            SystemDescriptor& d = m_Systems[name];
            d.Name    = name;
            d.Module  = m_CurrentModule;
            d.Factory = []() -> SystemScript* { return static_cast<SystemScript*>(new T()); };
            d.Fields.TypeId = entt::type_hash<T>::value();
            d.Fields.Name   = name;
            d.Fields.Fields.clear();
            d.Order   = 0;
            d.Collect = nullptr;
            return SystemBuilder<T>(&d);
        }

        // Note a custom component type a module registered (CS_COMPONENT). The
        // component itself lives in Reflect::GetRegistry(); this only records the
        // type id for storage stripping on unload.
        void NoteComponent(entt::id_type typeId, const std::string& name);

        // ---- queries --------------------------------------------------------
        const ScriptDescriptor* FindScript(const std::string& name) const;
        std::vector<std::string> ScriptNames() const;                        // all
        std::vector<std::string> ScriptNames(const std::string& module) const;

        const SystemDescriptor* FindSystem(const std::string& name) const;   // H9
        std::vector<std::string> SystemNames() const;
        std::vector<std::string> SystemNames(const std::string& module) const;

        // entt type ids of the components a module registered (E12 clears their
        // storage before FreeLibrary so no dangling vtables remain).
        std::vector<entt::id_type> ComponentTypeIds(const std::string& module) const;

        // ---- hot-reload unload (E12) ---------------------------------------
        // Forget every script + noted component a module registered. Does NOT touch
        // entt storage or the Reflect registry entries — the caller strips scene
        // storage first (it owns the scene); Reflect descriptors are overwritten on
        // the next load.
        void UnregisterModule(const std::string& module);

    private:
        std::string m_CurrentModule;                            // active during Begin/EndModule
        std::unordered_map<std::string, ScriptDescriptor> m_Scripts;   // by class name
        std::unordered_map<std::string, SystemDescriptor> m_Systems;   // by class name (H9)

        struct ComponentNote { entt::id_type Id; std::string Name; std::string Module; };
        std::vector<ComponentNote> m_Components;
    };
}
