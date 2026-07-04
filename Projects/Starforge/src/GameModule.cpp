// GameModule.cpp — project game-DLL lifecycle (E12). See GameModule.h.

#include "GameModule.h"

#include <Cosmic.h>   // ModuleRegistry, Log

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace Starforge
{
    using CosmicModuleRegisterFn = void(*)(Cosmic::ModuleRegistry&);

    GameModule::~GameModule()
    {
        Unload();
    }

    bool GameModule::Load(const std::string& moduleName, const std::string& dllStem)
    {
        Unload();

        const std::string dll = dllStem + ".dll";
        HMODULE h = ::LoadLibraryA(dll.c_str());
        if (!h)
        {
            CS_ERROR("GameModule: LoadLibrary('{0}') failed (err {1}).", dll, (unsigned)::GetLastError());
            return false;
        }

        auto reg = reinterpret_cast<CosmicModuleRegisterFn>(::GetProcAddress(h, "CosmicModule_Register"));
        if (!reg)
        {
            CS_ERROR("GameModule: '{0}' exports no CosmicModule_Register.", dll);
            ::FreeLibrary(h);
            return false;
        }

        reg(Cosmic::ModuleRegistry::Get());   // BeginModule/register/EndModule internally
        m_Handle     = h;
        m_ModuleName = moduleName;
        m_Stem       = dllStem;
        CS_INFO("GameModule: loaded '{0}'.", dll);
        return true;
    }

    void GameModule::Unload()
    {
        if (!m_Handle)
            return;
        Cosmic::ModuleRegistry::Get().UnregisterModule(m_ModuleName);
        ::FreeLibrary(reinterpret_cast<HMODULE>(m_Handle));
        m_Handle = nullptr;
        m_ModuleName.clear();
        m_Stem.clear();
    }
}
