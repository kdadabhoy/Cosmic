#pragma once

// HierarchyPanel.h — entity tree + selection + create/reparent/delete (E3/E8).
//
// Upgraded from the skeleton flat list into an E3 parent/child tree: click /
// ctrl-click multi-select (via EditorContext, mirrored to the EntitySelection
// bus), drag-drop reparent, F2 / context-menu rename, a create menu (empties,
// lights, camera, primitive meshes), duplicate (Ctrl+D), delete, and a search
// filter. Every mutation routes through the CommandStack (E7).

#include "EditorContext.h"

#include <functional>
#include <string>
#include <vector>

namespace Starforge
{
    class HierarchyPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        void DrawNode(EditorContext& ctx, Cosmic::Entity e);
        void DrawCreateMenu(EditorContext& ctx, Cosmic::Entity parent);
        void DrawContextMenu(EditorContext& ctx, Cosmic::Entity e);

        char        m_Search[128] = { 0 };
        uint64_t    m_RenameTarget = 0;     // UUID being renamed inline (0 = none)
        char        m_RenameBuf[256] = { 0 };
        bool        m_RenameFocus = false;

        // Deferred structural mutations — collected during the draw and run after,
        // so the tree isn't mutated mid-iteration.
        std::vector<std::function<void()>> m_Deferred;
    };
}
