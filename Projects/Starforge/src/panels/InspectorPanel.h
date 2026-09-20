#pragma once

// InspectorPanel.h — reflection-driven component editor (E8).
//
// Rewritten on the E1 reflection registry: every registered component of the
// selection auto-generates its UI from field descriptors (PropertyRows). Edits
// route through the CommandStack (E7) with a single undo step per edit (capture
// on activate, commit on deactivate-after-edit), fanning out across a
// multi-selection. "Add/Remove Component" is driven by the registry.

#include "EditorContext.h"
#include "../SourceLocator.h"   // AP-03 — Open source / Reveal / Open producer / Find handlers

#include <Cosmic.h>

namespace Cosmic { class DataBus; class PanelRegistry; }

namespace Starforge
{
    class InspectorPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

        // AP-03 (§7) — what the source-link rows resolve against; the shell sets it
        // every frame (project root, the play bus, the play / last-seen panel registry).
        struct SourceLinks
        {
            std::string                 ProjectRoot;
            const Cosmic::DataBus*      Bus    = nullptr;
            const Cosmic::PanelRegistry* Panels = nullptr;
            bool                        Playing = false;
        };
        void SetSourceLinks(const SourceLinks& l) { m_Links = l; }

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

        // AP-03 — source-link rows (drawn next to Channel / Signal / PanelName fields
        // and under the NativeScript class picker).
        void DrawSourceLinkRow(const std::string& compName, const std::string& fieldName, void* comp,
                               const Cosmic::Reflect::FieldDescriptor& f);
        SourceLinks m_Links;
    };
}
