#pragma once

// widgets/NodeCanvas.h — generic node/pin/link canvas host (Phase 17 / U6).
//
// ============================================================================
// A thin RAII wrapper over the vendored imgui-node-editor: owns one editor
// context (settings file disabled — the DOCUMENT owns layout persistence),
// brackets the per-frame Begin/End, and funnels the create/delete/selection
// interaction queries into plain id lists. Deliberately knows NOTHING about
// flows, stories, or any specific graph — node ids, pin ids and link ids are
// opaque uintptr values the owning panel allocates. Phase 25 (doc 24, Q1)
// extracts this as the reusable NodeCanvas behind the Starforge Story Graph
// and post-chain views; keep graph-specific content OUT of this widget.
// ============================================================================

#include <imgui.h>
#include <imgui_node_editor.h>

#include <cstdint>
#include <vector>

namespace Starforge
{
    namespace ed = ax::NodeEditor;

    class NodeCanvas
    {
    public:
        // One link-creation gesture accepted this frame (pin ids as drawn).
        struct NewLink { uintptr_t StartPin = 0; uintptr_t EndPin = 0; };

        // Everything the user did to the graph structure this frame.
        struct Edits
        {
            std::vector<NewLink>   Created;        // accepted pin->pin gestures
            std::vector<uintptr_t> DeletedLinks;   // link ids accepted for deletion
            std::vector<uintptr_t> DeletedNodes;   // node ids accepted for deletion
        };

        NodeCanvas() = default;
        ~NodeCanvas();
        NodeCanvas(const NodeCanvas&) = delete;
        NodeCanvas& operator=(const NodeCanvas&) = delete;

        // Bracket the canvas region (context created lazily on first Begin).
        // Draw nodes/pins/links between Begin and End with the ed:: API.
        void Begin(const char* id, const ImVec2& size = ImVec2(0.0f, 0.0f));
        void End();

        // Collect this frame's create/delete gestures. Call between Begin/End,
        // AFTER drawing nodes + links. Rejects nothing itself — the owner
        // decides what a gesture means (ignoring one simply leaves the data,
        // and therefore the drawn graph, unchanged).
        void QueryEdits(Edits& out);

        // Node placement (between Begin/End).
        void   SetNodePosition(uintptr_t nodeId, const ImVec2& pos);
        ImVec2 GetNodePosition(uintptr_t nodeId) const;
        void   CenterOnContent();

        // Current selection (0 = none). Single-selection reads.
        uintptr_t SelectedNode() const;
        uintptr_t SelectedLink() const;

        bool Ready() const { return m_Ctx != nullptr; }

    private:
        ed::EditorContext* m_Ctx = nullptr;
    };
}
