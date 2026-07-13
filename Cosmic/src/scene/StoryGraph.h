#pragma once
// scene/StoryGraph.h
//
// ============================================================================
// Starforge Story Graph — the `.cstory` dialogue runtime (Phase 25 / Q3, gap
// §9.3). Starforge-named per the 2026-07-11 rule (never "StoryFlow").
// ============================================================================
//
// A data-driven BRANCHING DIALOGUE runner in the FlowMachine mould: nodes carry
// a speaker, rich text, portrait/background/audio asset paths, enter/exit signal
// emissions, and a list of options. Each option has its own text, an optional
// guard (a FlowGuard — reflected field OR a Q2 flow variable), a Once flag
// (hidden after being chosen, per run), and a Next node. Start at the graph's
// Start node; "@end" (or an empty Next) ends the story.
//
// The engine ships the RUNNER, not presentation. A stock binding script
// (StoryUiBinding template) maps the current node onto U1 UI entities so a
// zero-code dialogue works; any app can render its own from Current()/
// ValidOptions()/Choose(). Signals ride the U2 EventBus; variables ride Q2 (the
// runner uses its own blackboard, or a shared FlowMachine store when a story
// runs under a flow). GL-free + headless-testable (the FlowMachine pattern).
// ============================================================================

#include "core/Core.h"
#include "scene/FlowMachine.h"   // FlowValue / FlowGuard / FlowVariable / EvaluateFlowGuard

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
    class Scene;
    class FlowMachine;

    /** @brief One choice out of a dialogue node. `Next` is a target node name;
     *  "" or "@end" ends the story. `Once` hides the option after it is chosen
     *  (per run). `Guard` (when HasGuard) reuses FlowGuard — a reflected-field or
     *  a Q2 variable comparison. */
    struct StoryOption
    {
        std::string Text;
        std::string Next;
        bool        Once     = false;
        bool        HasGuard = false;
        FlowGuard   Guard;
    };

    /** @brief One dialogue node. `OnEnter`/`OnExit` emit signals onto the scene's
     *  EventBus (U2). `EditorPos` is Q4 node-graph layout the runtime ignores. */
    struct StoryNode
    {
        std::string Name;
        std::string Speaker;
        std::string Text;
        std::string PortraitPath;
        std::string BackgroundPath;
        std::string AudioPath;
        std::vector<std::string> OnEnter;
        std::vector<std::string> OnExit;
        std::vector<StoryOption> Options;
        glm::vec2   EditorPos{ 0.0f };
    };

    /** @brief A parsed `.cstory` document. */
    struct COSMIC_API StoryGraph
    {
        int                       Version = 1;
        std::string               Start;
        std::vector<StoryNode>    Nodes;
        std::vector<FlowVariable> Variables;   // own blackboard (shared under a flow)

        const StoryNode* Find(const std::string& name) const;

        static bool LoadFromString(StoryGraph& out, const std::string& jsonText, std::string* error = nullptr);
        std::string SaveToString() const;
        static bool Load(StoryGraph& out, const std::string& path, std::string* error = nullptr);
        bool        Save(const std::string& path) const;

        // Editor validation (Q4): missing start, duplicate names, options to
        // unknown nodes. Empty == valid.
        std::vector<std::string> Validate() const;
    };

    class COSMIC_API StoryRunner
    {
    public:
        // Start at the graph's Start node. `scene` (optional) receives OnEnter/
        // OnExit signal emissions + is the field-guard source. `sharedVars`
        // (optional): run the blackboard on a FlowMachine so a story UNDER A FLOW
        // shares variables; null ⇒ the runner owns a store seeded from the graph's
        // Variables.
        void Start(const StoryGraph& graph, Scene* scene = nullptr, FlowMachine* sharedVars = nullptr);
        void Stop();

        bool IsRunning() const { return m_Running; }
        bool IsEnded()   const { return m_Ended; }

        // The active node, or null when ended / not running.
        const StoryNode* Current() const;

        // Indices into Current()->Options whose guard passes AND (if Once) have not
        // been chosen yet — rebuilt on each node enter.
        const std::vector<int>& ValidOptions() const { return m_Valid; }

        // Choose one of the VALID options (index INTO ValidOptions()). Emits the
        // node's OnExit, marks a Once option consumed, advances to Next (or ends).
        void Choose(int validIndex);

        // Blackboard access (own store, or the shared flow store).
        FlowValue GetVar(const std::string& name) const;
        void      SetVar(const std::string& name, const FlowValue& value);

    private:
        void Enter(const std::string& nodeName);
        void RebuildValid();
        void Emit(const std::vector<std::string>& signals);
        std::string OnceKey(int optionIndex) const;

        StoryGraph   m_Graph;
        Scene*       m_Scene = nullptr;
        FlowMachine* m_SharedVars = nullptr;
        std::unordered_map<std::string, FlowValue> m_Vars;   // own store (when not shared)
        std::string  m_Current;
        std::vector<int> m_Valid;
        std::unordered_set<std::string> m_Chosen;   // "<node>#<opt>" once-flags (per run)
        bool m_Running = false;
        bool m_Ended   = false;
    };
}
