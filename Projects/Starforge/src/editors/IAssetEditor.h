#pragma once

// editors/IAssetEditor.h
//
// ============================================================================
// Starforge — asset-editor document interface (Phase 24 / M1, gap §8.1).
// ============================================================================
//
// A heavyweight asset (a skeletal model, a story graph, a future sequencer)
// opens as a DOCUMENT in the AssetEditorHost's tab bar rather than crowding the
// shared Inspector. Every such editor implements this interface; the host owns
// the tab bar, the dirty dots, the close-with-save prompts, and the
// one-instance-per-path rule.
//
// The first consumer is M3's Starforge Animation Editor; Phase 25 (story graphs)
// and any later document editor ride the same host — build it once.
//
// An editor MAY own an interactive offscreen viewport: instantiate a
// PreviewRig, render into it in OnImGuiRender, and draw the returned texture as
// an ImGui::Image. The rig's doc-13 §0.5 state-restore contract keeps the scene
// viewport byte-identical no matter how many documents are open (M1 acceptance).
// ============================================================================

#include "ui/IconsLucide.h"

#include <string>

namespace Starforge
{
    struct EditorContext;

    class IAssetEditor
    {
    public:
        virtual ~IAssetEditor() = default;

        // The vfs path of the asset this document edits — the host's identity key
        // (one open editor per path; re-opening re-focuses). "" for an untitled /
        // context-only document.
        virtual const std::string& Path() const = 0;

        // Tab label WITHOUT the dirty dot (the host renders the unsaved marker).
        virtual std::string Title() const = 0;

        // A Lucide glyph shown ahead of the title on the tab.
        virtual const char* Icon() const { return ICON_LC_FILE; }

        // Unsaved changes → the host shows the dot and prompts before closing.
        virtual bool Dirty() const { return false; }

        // Persist to Path(); return success. Read-only / non-dirty editors just
        // return true (the host treats that as "nothing to save").
        virtual bool Save(EditorContext& /*ctx*/) { return true; }

        // Per-frame tick — called for EVERY open document (focused or not) so
        // background docs keep their playback / preview state coherent.
        virtual void OnUpdate(EditorContext& /*ctx*/, float /*ts*/) {}

        // Draw the document body. Invoked by the host INSIDE this document's tab
        // (the content region is already the tab's) — never call ImGui::Begin.
        virtual void OnImGuiRender(EditorContext& ctx) = 0;
    };
}
