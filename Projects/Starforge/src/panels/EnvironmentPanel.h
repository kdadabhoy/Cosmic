#pragma once

// panels/EnvironmentPanel.h
//
// ============================================================================
// Starforge — Environment panel (E17).
// ============================================================================
//
// A curated, always-available editor for the scene's rendering environment: the
// single "Environment" entity's EnvironmentComponent (sun / sky / time-of-day /
// fog / IBL / post). Reflection-driven (so it tracks the component automatically)
// with per-edit undo through the CommandStack. Creates the Environment entity on
// first use if the scene has none.
// ============================================================================

#include <Cosmic.h>

namespace Starforge
{
    struct EditorContext;

    class EnvironmentPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        Cosmic::Reflect::FieldValue m_ActiveBefore;   // value at drag-start (undo)
        bool                        m_HasActive = false;
    };
}
