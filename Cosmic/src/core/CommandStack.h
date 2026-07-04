#pragma once
// core/CommandStack.h
//
// ============================================================================
// Cosmic undo/redo — a generic command stack (Phase 13 / E7).
// ============================================================================
//
// A tiny, engine-generic undo/redo service. It knows nothing about the editor,
// scenes, or ImGui — it stores a bounded history of ICommand objects and plays
// them backwards/forwards. Starforge's concrete commands (reflected-field edits,
// transform-gizmo edits, entity create/destroy, reparent, …) subclass ICommand
// and live in Projects/Starforge; the stack itself is reusable by any tool.
//
// TWO WAYS TO ADD A COMMAND
//   * Execute(cmd)  — the command has NOT run yet: the stack calls Do() now,
//                     then records it. Use for discrete actions (create/delete,
//                     menu ops).
//   * Push(cmd)     — the mutation ALREADY happened live (an ImGui drag mutated
//                     the component this frame): the stack records it WITHOUT
//                     calling Do(). Undo() reverts to the captured "before";
//                     Redo() re-applies via Do(). Use for widget/gizmo edits
//                     that the panel captures on activate and commits on
//                     deactivate.
//
// COALESCING
//   Consecutive commands that describe the same continuous edit can be merged
//   into one undo step. When a new command is added the stack offers it to the
//   top-of-history command via TryMerge(); if that returns true the newer
//   command is absorbed (its post-state wins) and no new history entry is made.
//   A panel that wants to *break* a merge run (e.g. the user released the mouse
//   and started a fresh drag) calls SetMergeBarrier() so the next command starts
//   a new entry even if its merge key matches.
//
// DIRTY TRACKING
//   Any successful Execute/Push/Undo/Redo fires the dirty callback (if set) so
//   the shell can flip the window-title "*". Clear() does not.
//
// Not thread-safe: drive it from the main (UI) thread only. GL-free + headless.
// ============================================================================

#include "core/Core.h"

#include <functional>
#include <string>
#include <vector>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // ICommand — one reversible mutation.
    // ------------------------------------------------------------------------
    class COSMIC_API ICommand
    {
    public:
        virtual ~ICommand() = default;

        // Apply the change (also used for Redo). For Push()'d commands the
        // change is already live, so Do() must be idempotent w.r.t. the current
        // state (re-applying the "after" value).
        virtual void Do() = 0;

        // Revert to the pre-command state.
        virtual void Undo() = 0;

        // Human-readable label for the Edit menu ("Undo Move", "Redo Delete").
        virtual std::string Name() const { return "Command"; }

        // Coalescing hook. `next` is a freshly-added command; if this command
        // can absorb it (same continuous edit), update this command's post-state
        // from `next` and return true — `next` is then discarded. Default: no
        // merge. Only offered when merge keys match and no barrier intervened.
        virtual bool TryMerge(const ICommand& next) { (void)next; return false; }

        // Non-empty coalescing key. Two commands only merge when their keys are
        // equal and non-empty (e.g. "xform:<uuid>"). Default: no coalescing.
        virtual std::string MergeKey() const { return {}; }
    };

    // ------------------------------------------------------------------------
    // CommandStack — bounded do/undo/redo history.
    // ------------------------------------------------------------------------
    class COSMIC_API CommandStack
    {
    public:
        explicit CommandStack(size_t maxDepth = 256) : m_MaxDepth(maxDepth ? maxDepth : 1) {}

        // Non-copyable (owns unique_ptr history), movable. Declared explicitly so
        // the dllexport'd class does not try to emit the implicit copy operations
        // (which cannot copy the Scope<ICommand> vectors).
        CommandStack(const CommandStack&)            = delete;
        CommandStack& operator=(const CommandStack&) = delete;
        CommandStack(CommandStack&&)                 = default;
        CommandStack& operator=(CommandStack&&)      = default;

        // Run the command now (calls Do()), then record it. Clears the redo
        // history. See header for Execute vs Push.
        void Execute(Scope<ICommand> cmd);

        // Record a command whose effect is already applied (does NOT call Do()).
        void Push(Scope<ICommand> cmd);

        // Reverse the most recent command. Returns false when nothing to undo.
        bool Undo();

        // Re-apply the most recently undone command. False when nothing to redo.
        bool Redo();

        bool CanUndo() const { return !m_Undo.empty(); }
        bool CanRedo() const { return !m_Redo.empty(); }

        // Labels for the next undo/redo (empty when unavailable).
        std::string UndoName() const { return m_Undo.empty() ? std::string{} : m_Undo.back()->Name(); }
        std::string RedoName() const { return m_Redo.empty() ? std::string{} : m_Redo.back()->Name(); }

        size_t UndoCount() const { return m_Undo.size(); }
        size_t RedoCount() const { return m_Redo.size(); }

        // Drop the whole history (project close / new scene). Does NOT fire the
        // dirty callback.
        void Clear();

        // Force the next added command to start a fresh history entry even if its
        // merge key matches the current top (ends a coalescing run).
        void SetMergeBarrier() { m_Barrier = true; }

        void   SetMaxDepth(size_t depth) { m_MaxDepth = depth ? depth : 1; TrimToDepth(); }
        size_t GetMaxDepth() const { return m_MaxDepth; }

        // Fired after any state-changing op (Execute/Push/Undo/Redo). The shell
        // uses it to mark the scene dirty.
        void SetDirtyCallback(std::function<void()> cb) { m_OnDirty = std::move(cb); }

    private:
        void Record(Scope<ICommand> cmd);   // shared tail of Execute/Push
        void TrimToDepth();
        void MarkDirty() { if (m_OnDirty) m_OnDirty(); }

        std::vector<Scope<ICommand>> m_Undo;   // back() == most recent
        std::vector<Scope<ICommand>> m_Redo;   // back() == next to redo
        size_t                       m_MaxDepth;
        bool                         m_Barrier = false;  // block next coalesce
        std::function<void()>        m_OnDirty;
    };
}
