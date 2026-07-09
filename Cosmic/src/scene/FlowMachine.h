#pragma once
// scene/FlowMachine.h
//
// ============================================================================
// Cosmic screen-flow runtime — the `.cflow` state machine (Phase 17 / U5).
// ============================================================================
//
// A FlowAsset is a data description of an app's SCREENS and how they connect:
// named states (each pointing at a `.cscene`), transitions fired by signals
// (EventBus, U2), keys, or timers, optional guard conditions reading any
// reflected field (E1), and simple actions (emit / setField). A FlowMachine
// executes one over an injected scene-loader — so a whole menu→game→pause flow
// ships with ZERO C++.
//
// The machine is scene-agnostic and headless-testable: hand it a fake loader
// (the SceneManager SceneLoader shape, E5) and drive signals + time by hand.
// Signals arrive through FeedSignal (the host bridges the active scene's
// EventBus + key presses into it); transitions apply in OnUpdate so dispatch is
// deterministic and re-entrancy-safe.
//
// Overlay push/pop (v1 shipped): a machine-level STATE STACK. `push` adds a
// frame; `@pop` removes the top. A pushed state with a Scene loads its own top
// scene; a pushed state with an EMPTY Scene keeps the under-scene active (the
// "same-scene UI canvas toggle" overlay). The host renders the TOP active scene
// in v1 — additive rendering of the under-scene (so a pause menu shows the game
// behind it) is a documented follow-up.
//
// GL-free.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
    class Scene;

    /** @brief A typed literal for a guard comparison / setField value. */
    struct FlowValue
    {
        enum class Kind : int32_t { Bool, Number, String } ValueKind = Kind::Bool;
        bool        Bool   = false;
        double      Number = 0.0;
        std::string String;

        static FlowValue MakeBool(bool b)          { FlowValue v; v.ValueKind = Kind::Bool;   v.Bool = b;    return v; }
        static FlowValue MakeNumber(double n)      { FlowValue v; v.ValueKind = Kind::Number; v.Number = n;  return v; }
        static FlowValue MakeString(std::string s) { FlowValue v; v.ValueKind = Kind::String; v.String = std::move(s); return v; }
    };

    /** @brief A guard on a transition: find `Entity` (by Tag) in the ACTIVE scene,
     *  read `Component`.`Field` (E1 reflection), compare with `Value` under `Op`
     *  ("==","!=","<",">","<=",">="). A missing entity/field => the guard is false
     *  (one Console warning). */
    struct FlowGuard
    {
        std::string Entity;
        std::string Component;
        std::string Field;
        std::string Op = "==";
        FlowValue   Value;
    };

    /** @brief A state action (v1: emit a signal, or set a reflected field). */
    struct FlowAction
    {
        enum class Type : int32_t { Emit, SetField } ActionType = Type::Emit;
        std::string Signal;                       // Emit
        std::string Entity, Component, Field;     // SetField target (Entity by Tag)
        FlowValue   Value;                        // SetField value
    };

    /** @brief One transition out of a state. `On` is a signal name, "key:<Name>",
     *  or "timer:<seconds>". `To` is a target state name or "@quit" / "@pop". */
    struct FlowTransition
    {
        std::string On;
        std::string To;
        std::string Transition = "None";   // visual hint ("None"/"Fade") — host may honor
        bool        Push = false;          // overlay push
        bool        HasGuard = false;
        FlowGuard   Guard;
    };

    /** @brief One screen: a name + the scene it shows, its onEnter actions, and
     *  its outgoing transitions. Editor layout lives here but the runtime ignores it. */
    struct FlowState
    {
        std::string                 Name;
        std::string                 Scene;      // e.g. "project://scenes/MainMenu.cscene"
        bool                        Overlay = false;
        std::vector<FlowAction>     OnEnter;
        std::vector<FlowTransition> Transitions;
        glm::vec2                   EditorPos{ 0.0f };   // node-graph position (U6)
    };

    /** @brief A parsed `.cflow` document. */
    struct COSMIC_API FlowAsset
    {
        int                    Version = 1;
        std::string            Start;
        std::vector<FlowState> States;

        const FlowState* Find(const std::string& name) const;

        // (De)serialization — nlohmann JSON under the hood. LoadFromString fills
        // `error` (when non-null) with the first parse/schema problem.
        static bool LoadFromString(FlowAsset& out, const std::string& jsonText, std::string* error = nullptr);
        std::string SaveToString() const;

        // VFS file I/O (project:// resolved internally). Return false + log on failure.
        static bool Load(FlowAsset& out, const std::string& path, std::string* error = nullptr);
        bool        Save(const std::string& path) const;

        // Structural validation used by the editor (U6): missing start state,
        // duplicate names, transitions to unknown states. Scene-file existence is
        // checked by the editor (needs the VFS). Empty vector == valid.
        std::vector<std::string> Validate() const;
    };

    class COSMIC_API FlowMachine
    {
    public:
        // Build/replace the active scene for a state's Scene path. Return null to
        // keep the current active scene (and log). Same shape as SceneManager's
        // SceneLoader (E5) but keyed by the state's scene path.
        using SceneLoader = std::function<Ref<Scene>(const std::string& scenePath)>;

        FlowMachine() = default;
        ~FlowMachine();

        void SetSceneLoader(SceneLoader loader) { m_Loader = std::move(loader); }

        /** @brief Enter the asset's start state (loads its scene, runs onEnter,
         *  subscribes to the active scene bus). Replaces any running flow. */
        void Start(const FlowAsset& asset);

        /** @brief Advance timers + apply pending transitions deterministically. */
        void OnUpdate(float dt);

        /** @brief Stop the machine (clears the stack + bus subscription). */
        void Stop();

        /** @brief Feed a signal (host bridges the EventBus + key presses here).
         *  Queued; applied in the next OnUpdate. "key:<Name>" matches key transitions. */
        void FeedSignal(const std::string& signal);

        bool               IsRunning()     const { return m_Running; }
        bool               QuitRequested() const { return m_Quit; }
        const std::string& CurrentState()  const;              // top of stack ("" if none)
        Ref<Scene>         ActiveScene()   const;              // top scene (null if none)
        size_t             StackDepth()    const { return m_Stack.size(); }

    private:
        struct Frame { std::string StateName; Ref<Scene> ActiveScene; };

        void   Enter(const FlowState& state, bool push);
        void   RunActions(const std::vector<FlowAction>& actions);
        bool   TryFireSignal(const std::string& signal);   // one matching transition
        bool   TryFireTimer();                             // timer:N on the current state
        void   PerformTransition(const FlowTransition& t);
        void   SubscribeActiveBus();
        void   UnsubscribeActiveBus();
        const  FlowState* CurrentStateDef() const;
        bool   EvalGuard(const FlowGuard& guard) const;

        FlowAsset   m_Asset;
        SceneLoader m_Loader;
        std::vector<Frame> m_Stack;
        std::vector<std::string> m_Pending;   // queued signals
        float m_Elapsed = 0.0f;               // time in the current top state
        bool  m_Running = false;
        bool  m_Quit    = false;
        uint64_t m_BusHandle = 0;             // ConnectAny handle on the active scene bus
        Scene*   m_BusScene  = nullptr;       // scene the handle belongs to

        mutable std::unordered_set<std::string> m_GuardWarned;   // dedup guard warnings
    };
}
