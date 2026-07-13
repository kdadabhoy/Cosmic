#pragma once

// editors/FlowEditor.h
//
// ============================================================================
// Starforge Flow Editor — `.cflow` node-graph document (Phase 25 / Q1, gap §9.1).
// ============================================================================
//
// The Phase 17 / U6 Flow Graph panel, rehosted as an M1 asset-editor DOCUMENT so
// multiple flows open side by side without cross-talk (each owns its own asset +
// NodeCanvas context). Behaviour is otherwise U6's exactly: states are nodes
// (scene stem + start marker + red badges for missing scenes / unreachable
// states), transitions are links out of per-transition pins, a side inspector
// edits the selected state/transition (event picker fed by the scenes' UiButton
// signals, guard fields, @quit/@pop targets, onEnter actions), node positions
// persist into each state's EditorPos, and Save validates + lists problems.
//
// Q2 adds a typed-variables side panel + variable guards/actions on top of this.
//
// Undo (U6 v1, unchanged): a document-local JSON snapshot stack (Undo/Redo
// buttons) — flow edits are file-scoped, so they deliberately do NOT share the
// scene CommandStack.
// ============================================================================

#include "editors/IAssetEditor.h"
#include "widgets/NodeCanvas.h"

#include <Cosmic.h>
#include "scene/FlowMachine.h"   // FlowAsset (not aggregated by Cosmic.h)

#include <string>
#include <unordered_set>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    class FlowEditor : public IAssetEditor
    {
    public:
        // Loads `vfsPath` (a "project://flows/X.cflow") immediately; a parse
        // failure leaves the document in an error state its body reports.
        explicit FlowEditor(std::string vfsPath);

        const std::string& Path() const override  { return m_Path; }
        std::string        Title() const override;
        const char*        Icon()  const override { return ICON_LC_WORKFLOW; }
        bool               Dirty() const override { return m_Dirty; }
        bool               Save(EditorContext& ctx) override { return SaveFlow(ctx); }
        void               OnImGuiRender(EditorContext& ctx) override;

    private:
        void Revalidate();
        void ScanKnownSignals();
        void Snapshot();                    // push undo state (before an edit)
        void ApplySnapshot(const std::string& json);
        bool SaveFlow(EditorContext& ctx);

        void DrawToolbar(EditorContext& ctx);
        void DrawCanvas(EditorContext& ctx);
        void DrawInspector(EditorContext& ctx);
        void DrawStateInspector(EditorContext& ctx, int stateIdx);
        void DrawTransitionInspector(EditorContext& ctx, int stateIdx, int transIdx);
        void DrawVariablesPanel(EditorContext& ctx);   // Q2

        // --- id mapping (opaque uintptr ids for NodeCanvas) -----------------
        static constexpr uintptr_t kQuitNode   = 1000000;
        static constexpr uintptr_t kQuitInPin  = 2900000;
        static uintptr_t NodeId(int s)              { return (uintptr_t)(s + 1); }
        static uintptr_t InPin(int s)               { return 2000000 + (uintptr_t)s; }
        static uintptr_t AddPin(int s)              { return 3000000 + (uintptr_t)s; }
        static uintptr_t OutPin(int s, int t)       { return 4000000 + (uintptr_t)s * 512 + (uintptr_t)t; }
        static uintptr_t LinkId(int s, int t)       { return 5000000 + (uintptr_t)s * 512 + (uintptr_t)t; }
        static bool  IsStateNode(uintptr_t id)      { return id >= 1 && id < kQuitNode; }
        static int   StateOfNode(uintptr_t id)      { return (int)id - 1; }
        static bool  IsOutPin(uintptr_t id)         { return id >= 4000000 && id < 5000000; }
        static bool  IsAddPin(uintptr_t id)         { return id >= 3000000 && id < 4000000; }
        static bool  IsInPin(uintptr_t id)          { return (id >= 2000000 && id < 3000000) || id == kQuitInPin; }

        // --- state ----------------------------------------------------------
        NodeCanvas          m_Canvas;
        Cosmic::FlowAsset   m_Asset;
        std::string         m_Path;          // "project://flows/X.cflow"
        bool                m_Loaded = false;
        bool                m_Dirty  = false;
        bool                m_PlaceNodes = false;   // push EditorPos on next draw

        std::vector<std::string>  m_Problems;
        std::unordered_set<int>   m_MissingScene;
        std::unordered_set<int>   m_Unreachable;
        std::vector<std::string>  m_KnownSignals;

        int  m_SelState = -1;
        int  m_SelTrans = -1;
        bool m_ShowVars = false;   // Q2 — variables side panel toggle

        std::vector<std::string> m_UndoStack;
        std::vector<std::string> m_RedoStack;

        char m_NewStateName[96] = "State";
    };
}
