#pragma once

// GameModule.h
//
// ============================================================================
// Starforge — the loaded project game DLL (E12 / §3.3).
// ============================================================================
//
// Wraps LoadLibrary/FreeLibrary of a scaffolded project's module DLL and the
// call to its CosmicModule_Register export (scripts + custom components). The
// hot-reload orchestration (serialize scene -> drop -> Unload -> build -> Load ->
// restore) lives in StarforgeApp; this class owns only the DLL handle + the
// registry lifecycle. Unload UNregisters the module's types from the process-wide
// ModuleRegistry — the caller must have already cleared any module-owned entt
// storage from the scene (StarforgeApp drops the scene while this is still loaded,
// so component destructors run against valid module code before FreeLibrary).
// ============================================================================

#include <string>

namespace Starforge
{
    class GameModule
    {
    public:
        GameModule() = default;
        ~GameModule();

        GameModule(const GameModule&)            = delete;
        GameModule& operator=(const GameModule&) = delete;

        // Load "<dllStem>.dll" from the app directory and run CosmicModule_Register
        // (which BeginModule(moduleName)-tags its registrations). Returns false and
        // logs on any failure. Unloads a previous module first.
        bool Load(const std::string& moduleName, const std::string& dllStem);

        // UnregisterModule(moduleName) from the ModuleRegistry, then FreeLibrary.
        void Unload();

        bool IsLoaded() const { return m_Handle != nullptr; }
        const std::string& ModuleName() const { return m_ModuleName; }
        const std::string& DllStem()    const { return m_Stem; }

    private:
        void*       m_Handle = nullptr;   // HMODULE
        std::string m_ModuleName;
        std::string m_Stem;
    };
}
