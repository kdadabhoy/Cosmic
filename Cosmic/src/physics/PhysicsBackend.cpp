// physics/PhysicsBackend.cpp — the backend registry. See PhysicsBackend.h.
//
// Deliberately tiny and dependency-free: a map, a default name, and the explicit
// built-in registration hook. It links into the 2D and 3D engines alike and does
// not know that Jolt exists beyond one #ifdef.

#include "physics/PhysicsBackend.h"
#include "physics/backends/BuiltinBackends.h"
#include "core/Log.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Cosmic
{
    namespace
    {
        struct RegistryState
        {
            std::unordered_map<std::string, PhysicsBackendRegistry::Factory> Factories;

            // The initial default is a build-time fact, not something registration
            // mutates — so RegisterBuiltinPhysicsBackends can be called at any point
            // without clobbering an app's SetDefault.
            std::string DefaultName =
#ifdef COSMIC_WITH_JOLT
                "jolt";
#else
                "null";
#endif
        };

        // Function-local static (Meyers singleton): constructed on first use, so
        // there is no static-initialization-order question across the DLL boundary.
        RegistryState& State()
        {
            static RegistryState s;
            return s;
        }
    }

    void PhysicsBackendRegistry::Register(std::string name, Factory factory)
    {
        if (name.empty() || !factory)
        {
            CS_CORE_WARN("PhysicsBackendRegistry: ignoring a registration with an empty name or null factory.");
            return;
        }
        State().Factories[std::move(name)] = std::move(factory);
    }

    bool PhysicsBackendRegistry::Has(const std::string& name)
    {
        return State().Factories.find(name) != State().Factories.end();
    }

    std::vector<std::string> PhysicsBackendRegistry::Names()
    {
        std::vector<std::string> names;
        names.reserve(State().Factories.size());
        for (const auto& [name, factory] : State().Factories)
            names.push_back(name);
        std::sort(names.begin(), names.end());   // stable listing for UI / logs
        return names;
    }

    void PhysicsBackendRegistry::SetDefault(const std::string& name)
    {
        if (name.empty())
            return;
        // Stored either way: an app may legitimately set the default before the
        // factory it names has been registered.
        if (!Has(name))
            CS_CORE_WARN("PhysicsBackendRegistry: default set to \"{0}\", which is not registered (yet).", name);
        State().DefaultName = name;
    }

    const std::string& PhysicsBackendRegistry::Default()
    {
        return State().DefaultName;
    }

    std::unique_ptr<IPhysicsBackend> PhysicsBackendRegistry::Create(const std::string& name)
    {
        auto it = State().Factories.find(name);
        if (it == State().Factories.end())
            return nullptr;
        return it->second();
    }

    void RegisterBuiltinPhysicsBackends()
    {
        static const bool s_Once = []
        {
            RegisterNullPhysicsBackend();
#ifdef COSMIC_WITH_JOLT
            RegisterJoltPhysicsBackend();
#endif
            return true;
        }();
        (void)s_Once;
    }
}
