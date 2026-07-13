#pragma once

// editors/PostChainEditor.h
//
// ============================================================================
// Starforge Post Chain view — a graph VIEW of the fixed post-FX pipeline
// (Phase 25 / Q6, gap §9.4b). NOT an arbitrary pass-graph executor (roadmap
// decision #13): the topology is READ-ONLY and always mirrors the real chain —
//   Scene → SSAO → Bloom → God Rays → Tonemap(fog / vignette / haze) → FXAA.
// ============================================================================
//
// Each node shows its enable checkbox + params bound to the SAME reflected
// `EnvironmentComponent` fields the Environment panel edits, through the SAME
// `Commands::CommitFieldEditFor(… "Env " + field …)` path — so an edit here
// produces the IDENTICAL undo entry an edit in the Environment panel would.
// Links are drawn but never editable (QueryEdits is ignored). God Rays / heat
// haze are engine passes not authored on `EnvironmentComponent` (app-driven), so
// their nodes are informational.
// ============================================================================

#include "widgets/NodeCanvas.h"

#include <Cosmic.h>

namespace Starforge
{
    struct EditorContext;

    class PostChainEditor
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        // Draw ONE reflected EnvironmentComponent field (label + widget) and route
        // its edit through the Environment panel's exact command path.
        void DrawEnvField(EditorContext& ctx, Cosmic::Entity env,
                          const Cosmic::Reflect::TypeDescriptor* desc, void* comp,
                          const char* field);

        NodeCanvas m_Canvas;
        bool       m_Placed = false;

        // The capture-on-activate / commit-on-deactivate idiom (mirrors the
        // Environment panel) so drags coalesce into one undo entry.
        Cosmic::Reflect::FieldValue m_ActiveBefore;
        bool                        m_HasActive = false;
    };
}
