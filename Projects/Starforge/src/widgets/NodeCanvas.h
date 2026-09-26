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

struct ImRect;   // imgui_internal.h — RouteLink's node rects

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

        // UX-01 (contract §1) — link routing. Pure (ImVec2 / ImRect / float, no ImGui state;
        // defined in NodeCanvasRoute.cpp, compiled into CosmicTests for FE01): the bezier
        // control points of a link from an output pin at `start` (node rect `startNode`,
        // pins leave to the right) to an input pin at `end` (node rect `endNode`, pins enter
        // from the left), in canvas coordinates.
        //   * forward (end.x - start.x >= 0, two different nodes): exactly imgui-node-editor's
        //     own points (`strength` eased by distance along (1,0) / (-1,0));
        //   * backward (the end pin left of the start pin) and self-loops: one cubic below the
        //     union of both rects that never crosses the source rect outside its pin.
        // Begin() installs it as the vendored editor's link router (VENDOR-NOTES.md, local
        // patch 2), so drawing, hit-testing and link bounds all follow it — for every
        // NodeCanvas (Flow, Story, PostChain).
        static void RouteLink(ImVec2 start, ImVec2 end, const ImRect& startNode, const ImRect& endNode,
                              float strength, ImVec2& cp0, ImVec2& cp1);

    private:
        ed::EditorContext* m_Ctx = nullptr;
    };
}
