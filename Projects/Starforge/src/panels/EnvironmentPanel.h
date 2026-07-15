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
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        // X2 — Elevation/Azimuth paired editor for the SunDirection field.
        void DrawSunAngleWidget(EditorContext& ctx, Cosmic::Entity env, void* comp,
                                const Cosmic::Reflect::TypeDescriptor& desc,
                                const Cosmic::Reflect::FieldDescriptor& field);

        Cosmic::Reflect::FieldValue m_ActiveBefore;   // value at drag-start (undo)
        bool                        m_HasActive = false;

        // X2 — Elevation/Azimuth paired widget for SunDirection. Captures the
        // vec3 at drag-start so the edit is one undoable step; the conversion is
        // an exact round-trip (see the .cpp), and the vector is written ONLY while
        // a slider is dragged, so merely displaying the widget never perturbs it.
        glm::vec3 m_SunAngleBefore{ 0.0f };
        bool      m_SunAngleActive = false;
    };
}
