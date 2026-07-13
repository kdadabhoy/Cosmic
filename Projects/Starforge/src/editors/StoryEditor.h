#pragma once

// editors/StoryEditor.h
//
// ============================================================================
// Starforge Story Graph editor — `.cstory` node-graph document (Phase 25 / Q4,
// gap §9.3; deps Q1 NodeCanvas, Q3 StoryGraph/StoryRunner, M1 host).
// ============================================================================
//
// A dialogue-tree document on the reusable NodeCanvas: nodes carry a speaker,
// text preview, and option pins (with condition [if] + [once] badges);
// transitions are links from an option pin to a target node's input pin (or the
// @end node). The right-side edit-node panel edits the selected node (name,
// speaker, asset slots, text, and its options — each with a Next target, a Once
// toggle, and a per-option guard via the shared guard editor). The left panel is
// the Q2 typed-variables blackboard. A toolbar **Play preview** runs the Q3
// StoryRunner in-panel with clickable option buttons.
//
// Undo: a document-local JSON snapshot stack (Undo/Redo buttons), matching the
// Flow editor — file-scoped, not the scene CommandStack.
// ============================================================================

#include "editors/IAssetEditor.h"
#include "widgets/NodeCanvas.h"

#include <Cosmic.h>
#include "scene/StoryGraph.h"

#include <string>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    class StoryEditor : public IAssetEditor
    {
    public:
        explicit StoryEditor(std::string vfsPath);

        const std::string& Path() const override  { return m_Path; }
        std::string        Title() const override;
        const char*        Icon()  const override { return ICON_LC_MESSAGES_SQUARE; }
        bool               Dirty() const override { return m_Dirty; }
        bool               Save(EditorContext& ctx) override { return SaveStory(ctx); }
        void               OnImGuiRender(EditorContext& ctx) override;

    private:
        void Revalidate();
        void Snapshot();
        void ApplySnapshot(const std::string& json);
        bool SaveStory(EditorContext& ctx);

        void DrawToolbar(EditorContext& ctx);
        void DrawCanvas(EditorContext& ctx);
        void DrawNodeInspector(EditorContext& ctx);
        void DrawVariablesPanel(EditorContext& ctx);
        void DrawPreview(EditorContext& ctx);

        // --- id mapping (opaque uintptr ids for NodeCanvas) -----------------
        static constexpr uintptr_t kEndNode  = 1000000;
        static constexpr uintptr_t kEndInPin = 2900000;
        static uintptr_t NodeId(int n)        { return (uintptr_t)(n + 1); }
        static uintptr_t InPin(int n)         { return 2000000 + (uintptr_t)n; }
        static uintptr_t AddPin(int n)        { return 3000000 + (uintptr_t)n; }
        static uintptr_t OptPin(int n, int o) { return 4000000 + (uintptr_t)n * 512 + (uintptr_t)o; }
        static uintptr_t LinkId(int n, int o) { return 5000000 + (uintptr_t)n * 512 + (uintptr_t)o; }
        static bool IsNodeNode(uintptr_t id)  { return id >= 1 && id < kEndNode; }
        static int  NodeOf(uintptr_t id)      { return (int)id - 1; }
        static bool IsOptPin(uintptr_t id)    { return id >= 4000000 && id < 5000000; }
        static bool IsAddPin(uintptr_t id)    { return id >= 3000000 && id < 4000000; }
        static bool IsInPin(uintptr_t id)     { return (id >= 2000000 && id < 3000000) || id == kEndInPin; }

        // --- state ----------------------------------------------------------
        NodeCanvas          m_Canvas;
        Cosmic::StoryGraph  m_Asset;
        std::string         m_Path;
        bool                m_Loaded = false;
        bool                m_Dirty  = false;
        bool                m_PlaceNodes = false;

        std::vector<std::string> m_Problems;
        int  m_SelNode  = -1;
        bool m_ShowVars = false;

        std::vector<std::string> m_UndoStack;
        std::vector<std::string> m_RedoStack;
        char m_NewNodeName[96] = "Node";

        // Play preview (Q4) — the Q3 runner over a throwaway scene (for signals).
        bool                        m_Preview = false;
        Cosmic::StoryRunner         m_Runner;
        Cosmic::Ref<Cosmic::Scene>  m_PreviewScene;
    };
}
