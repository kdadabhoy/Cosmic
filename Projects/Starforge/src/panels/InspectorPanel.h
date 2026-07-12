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
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        // Draws one component's fields; records undo on commit. `typeId` keys the
        // command; `mixedProbe` supplies per-field mixed-value detection. When a
        // property search (T9) is active, only matching fields are drawn and the
        // header is forced open.
        void DrawComponent(EditorContext& ctx, const Cosmic::Reflect::TypeDescriptor& desc);
        void DrawName(EditorContext& ctx);
        void DrawAddComponent(EditorContext& ctx);

        // T9 — is a field/component visible under the current property search?
        bool SearchActive() const { return m_Search[0] != 0; }
        bool NameMatches(const std::string& name) const;   // case-insensitive contains

        // NativeScript (E11) gets a bespoke section: a class picker (ModuleRegistry)
        // + the chosen script's reflected fields, edited on the component's override
        // map (there is no live instance in edit mode). Script-field edits mark the
        // scene dirty but are NOT on the undo stack in v1 (documented).
        void DrawScriptComponent(EditorContext& ctx, const Cosmic::Reflect::TypeDescriptor& desc);

        // Drag-start value of the item currently being edited (one active item at
        // a time), captured on IsItemActivated and consumed on commit.
        Cosmic::Reflect::FieldValue m_ActiveBefore;
        bool                        m_HasActive = false;

        // T9 — property search filter (empty = show everything).
        char m_Search[128] = { 0 };
    };
}
