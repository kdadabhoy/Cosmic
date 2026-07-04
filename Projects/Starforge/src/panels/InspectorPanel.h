#pragma once

// InspectorPanel.h — reflection-driven component editor (E8).
//
// Rewritten on the E1 reflection registry: every registered component of the
// selection auto-generates its UI from field descriptors (PropertyRows). Edits
// route through the CommandStack (E7) with a single undo step per edit (capture
// on activate, commit on deactivate-after-edit), fanning out across a
// multi-selection. "Add/Remove Component" is driven by the registry.

#include "EditorContext.h"

#include <Cosmic.h>

namespace Starforge
{
    class InspectorPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        // Draws one component's fields; records undo on commit. `typeId` keys the
        // command; `mixedProbe` supplies per-field mixed-value detection.
        void DrawComponent(EditorContext& ctx, const Cosmic::Reflect::TypeDescriptor& desc);
        void DrawName(EditorContext& ctx);
        void DrawAddComponent(EditorContext& ctx);

        // Drag-start value of the item currently being edited (one active item at
        // a time), captured on IsItemActivated and consumed on commit.
        Cosmic::Reflect::FieldValue m_ActiveBefore;
        bool                        m_HasActive = false;
    };
}
