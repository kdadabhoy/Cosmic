// core/CommandStack.cpp — see CommandStack.h.

#include "core/CommandStack.h"

namespace Cosmic
{
    void CommandStack::Execute(Scope<ICommand> cmd)
    {
        if (!cmd)
            return;
        cmd->Do();
        Record(std::move(cmd));
    }

    void CommandStack::Push(Scope<ICommand> cmd)
    {
        if (!cmd)
            return;
        Record(std::move(cmd));   // effect already applied live — do NOT Do()
    }

    void CommandStack::Record(Scope<ICommand> cmd)
    {
        // A fresh action invalidates the redo branch.
        m_Redo.clear();

        // Try to coalesce with the current top (unless a barrier was requested
        // or keys don't line up).
        const std::string key = cmd->MergeKey();
        if (!m_Barrier && !m_Undo.empty() && !key.empty() &&
            m_Undo.back()->MergeKey() == key && m_Undo.back()->TryMerge(*cmd))
        {
            m_Barrier = false;
            MarkDirty();
            return;   // absorbed — no new history entry
        }

        m_Undo.push_back(std::move(cmd));
        m_Barrier = false;
        TrimToDepth();
        MarkDirty();
    }

    bool CommandStack::Undo()
    {
        if (m_Undo.empty())
            return false;

        Scope<ICommand> cmd = std::move(m_Undo.back());
        m_Undo.pop_back();
        cmd->Undo();
        m_Redo.push_back(std::move(cmd));
        m_Barrier = true;   // a fresh edit after undo never merges into old history
        MarkDirty();
        return true;
    }

    bool CommandStack::Redo()
    {
        if (m_Redo.empty())
            return false;

        Scope<ICommand> cmd = std::move(m_Redo.back());
        m_Redo.pop_back();
        cmd->Do();
        m_Undo.push_back(std::move(cmd));
        m_Barrier = true;
        MarkDirty();
        return true;
    }

    void CommandStack::Clear()
    {
        m_Undo.clear();
        m_Redo.clear();
        m_Barrier = false;
    }

    void CommandStack::TrimToDepth()
    {
        // Drop oldest entries (front) once we exceed the cap.
        if (m_Undo.size() <= m_MaxDepth)
            return;
        const size_t excess = m_Undo.size() - m_MaxDepth;
        m_Undo.erase(m_Undo.begin(), m_Undo.begin() + static_cast<std::ptrdiff_t>(excess));
    }
}
