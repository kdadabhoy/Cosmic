#pragma once

// panels/FlowGraphPanel.h
//
// ============================================================================
// Starforge — Flow Graph panel (Phase 17 / U6): node-graph `.cflow` authoring.
// ============================================================================
//
// Opens/creates `project://flows/*.cflow` and edits the FlowAsset visually:
// states are nodes (scene stem + start marker + red badges for missing scenes
// / unreachable states), transitions are links out of per-transition pin rows
// (drag a row's pin to retarget; drag the node's "+" pin to a target to add a
// transition; Del removes), and a side inspector edits the selected state /
// transition (event picker fed by the UiButton signals of the flow's scenes,
// guard fields, @quit/@pop targets, onEnter actions). Node positions persist
// into each state's EditorPos (the runtime ignores them). Save validates via
// FlowAsset::Validate + scene-file existence and lists problems in the panel.
//
// The generic node/pin/link plumbing lives in widgets/NodeCanvas (extracted by
// Phase 25 for the Story Graph); this panel owns only flow-specific content.
//
// Undo (v1, documented): panel-local snapshot stack (Undo/Redo buttons) —
// flow edits are file-scoped documents, so they deliberately do NOT share the
// scene CommandStack (Ctrl+Z over the viewport keeps meaning "undo the scene").
// ============================================================================

#include "widgets/NodeCanvas.h"

#include <Cosmic.h>
#include "scene/FlowMachine.h"   // FlowAsset (not aggregated by Cosmic.h)

#include <string>
#include <unordered_set>
#include <vector>

namespace Starforge
{
    struct EditorContext;

    class FlowGraphPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

        // Open a flow document by VFS path (also used by future Content Browser
        // double-click routing). Returns false + logs on parse failure.
        bool Open(EditorContext& ctx, const std::string& vfsPath);

    private:
        // --- document ------------------------------------------------------
        void NewFlow(EditorContext& ctx, const std::string& name);
        bool SaveFlow(EditorContext& ctx);
        void Revalidate();
        void ScanKnownSignals();
        void Snapshot();                    // push undo state (before an edit)
        void ApplySnapshot(const std::string& json);

        // --- drawing -------------------------------------------------------
        void DrawToolbar(EditorContext& ctx);
        void DrawCanvas(EditorContext& ctx);
        void DrawInspector(EditorContext& ctx);
        void DrawStateInspector(EditorContext& ctx, int stateIdx);
        void DrawTransitionInspector(EditorContext& ctx, int stateIdx, int transIdx);

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
        std::string         m_Path;          // "project://flows/X.cflow" ("" = none open)
        bool                m_Loaded = false;
        bool                m_Dirty  = false;
        bool                m_PlaceNodes = false;   // push EditorPos on next draw

        std::vector<std::string>  m_Problems;            // validation output
        std::unordered_set<int>   m_MissingScene;        // state indices
        std::unordered_set<int>   m_Unreachable;         // state indices
        std::vector<std::string>  m_KnownSignals;        // event-picker source

        int  m_SelState = -1;   // inspector focus (node click / row click)
        int  m_SelTrans = -1;

        std::vector<std::string> m_UndoStack;   // JSON snapshots (panel-local)
        std::vector<std::string> m_RedoStack;

        char m_NewFlowName[96]  = "Main";
        char m_NewStateName[96] = "State";
    };
}
