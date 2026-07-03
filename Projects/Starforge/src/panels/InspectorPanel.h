#pragma once

// InspectorPanel.h — component editor for the selected entity.
//
// SKELETON: hand-coded UI for the built-in components (Tag, Transform,
// MeshRenderer, DirectionalLight, PointLight) writing directly to components.
//   TODO(E1+E8): REPLACED WHOLESALE by the reflection-driven inspector —
//                every registered component (engine, game module, scripts)
//                auto-generates its UI from TypeRegistry field descriptors.
//   TODO(E7):    edits become ReflectedFieldEdit commands (undo/redo, coalesced).
//   TODO(E8):    "Add Component" popup from the registry, remove-component,
//                multi-select fan-out.

#include "EditorContext.h"

namespace Starforge
{
    class InspectorPanel
    {
    public:
        // Draws the "Inspector" window. Dock binding happens in StarforgeApp.
        void OnImGuiRender(EditorContext& ctx);
    };
}
