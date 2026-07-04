#pragma once
// scripting/ModuleMacros.h
//
// ============================================================================
// Cosmic scripting — the game-module registration DSL (Phase 13 / E11).
// ============================================================================
//
// A project's generated src/Module.cpp declares the module's scripts + custom
// components between CS_MODULE_BEGIN/END. The macros expand to the two exports the
// engine expects (§2.1):
//
//   CosmicModule_Register(ModuleRegistry&)  — scripts + components (editor calls
//                                             THIS on hot reload; no ImGui touched)
//   CreatePluginLayer()  -> Cosmic::PlayerLayer  — standalone player (Launcher /
//                                             `--project`), registers the module on
//                                             its own handle, then runs the scene
//   InitializePluginContexts(HostContext)   — the usual ImGui/ImPlot context share
//
// SYNTAX (a small, deliberate deviation from the plan's comma-separated sketch —
// a builder CHAIN cannot be comma-joined, and MSVC's default preprocessor warns
// C4003 on empty variadic macro args, so fields are chained and each block ends
// with CS_END):
//
//   #include <Cosmic.h>
//   #include "scripts/HoverController.h"
//
//   CS_MODULE_BEGIN(MyRover)
//       CS_SCRIPT(HoverController)
//           CS_FIELD(TargetAltitude).Range(0.f, 100.f)
//           CS_FIELD(Kp)
//           CS_FIELD(Kd)
//       CS_END;
//       CS_COMPONENT(ThrusterComponent)          // custom reflected entt component
//           CS_FIELD(MaxThrustN)
//       CS_END;
//   CS_MODULE_END()
//
// CS_SCRIPT / CS_COMPONENT / CS_FIELD / CS_END are ALSO usable standalone (no DLL)
// — the E11 unit test registers a script in-exe with them.
// ============================================================================

#include "scripting/ModuleRegistry.h"
#include "reflect/TypeRegistry.h"

#include <entt/entt.hpp>

// ---- field + block terminators (shared by CS_SCRIPT and CS_COMPONENT) --------
// CS_FIELD chains onto the builder opened by CS_SCRIPT/CS_COMPONENT; optional
// hint calls (.Range(...)/.Color()/.Tooltip(...)/...) follow it directly.
#define CS_FIELD(member) .Field(#member, &CS_ReflectedType::member)
#define CS_END ; }

// ---- script registration -----------------------------------------------------
// Register script class T (a ScriptableEntity subclass) into the process-wide
// ModuleRegistry, tagged with the current module. Chain CS_FIELD(...) then CS_END.
#define CS_SCRIPT(T)                                                            \
    {                                                                           \
        using CS_ReflectedType = T;                                            \
        ::Cosmic::ModuleRegistry::Get().AddScript<T>(#T)

// Register a plain custom reflected component T (needs CS_REGISTER_COMPONENT(T)
// in its own header). It becomes a first-class component: Inspector, serializer,
// and undo pick it up through the reflection registry. Chain CS_FIELD then CS_END.
#define CS_COMPONENT(T)                                                         \
    {                                                                           \
        using CS_ReflectedType = T;                                            \
        ::Cosmic::ModuleRegistry::Get().NoteComponent(                         \
            ::entt::type_hash<T>::value(), #T);                                \
        ::Cosmic::Reflect::ClassIn<T>(                                         \
            ::Cosmic::Reflect::GetRegistry(), #T, "Scripts")

// ---- module wrapper (the two exports) ----------------------------------------
#define CS_MODULE_BEGIN(ModuleName)                                             \
    static const char* CS_kModuleName = #ModuleName;                           \
    extern "C" __declspec(dllexport)                                            \
    void CosmicModule_Register(::Cosmic::ModuleRegistry& reg)                   \
    {                                                                           \
        reg.BeginModule(CS_kModuleName);

#define CS_MODULE_END()                                                        \
        reg.EndModule();                                                        \
    }                                                                           \
    extern "C" __declspec(dllexport)                                            \
    ::Cosmic::Layer* CreatePluginLayer()                                        \
    {                                                                           \
        CosmicModule_Register(::Cosmic::ModuleRegistry::Get());                 \
        return new ::Cosmic::PlayerLayer(CS_kModuleName);                       \
    }                                                                           \
    extern "C" __declspec(dllexport)                                            \
    void InitializePluginContexts(::Cosmic::HostContext context)               \
    {                                                                           \
        ImGui::SetCurrentContext(context.ImGuiCtx);                             \
        ImPlot::SetCurrentContext(context.ImPlotCtx);                           \
    }
