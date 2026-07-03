#pragma once

// HierarchyPanel.h — entity list + selection + create/delete.
//
// SKELETON: flat list of every entity with a TagComponent; click selects.
//   TODO(E3): parent/child tree (RelationshipComponent), drag-drop reparent.
//   TODO(E7): create/delete go through the CommandStack (undo).
//   TODO(E8): multi-select, rename-in-place (F2), duplicate, context create
//             menu (primitives/lights/camera), search filter.

#include "EditorContext.h"

namespace Starforge
{
    class HierarchyPanel
    {
    public:
        // Draws the "Hierarchy" window. Dock binding happens in StarforgeApp.
        void OnImGuiRender(EditorContext& ctx);
    };
}
