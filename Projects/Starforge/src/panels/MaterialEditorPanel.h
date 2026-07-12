#pragma once

// panels/MaterialEditorPanel.h
//
// ============================================================================
// Starforge — Material Editor (E17; preview + undo completed by Phase 20 A4).
// ============================================================================
//
// Author a PBR `.cmat` (a reflected MaterialAsset): albedo/metallic/roughness/AO/
// emissive + optional texture-map slots, New / Save, and Assign / Load to the
// selected entity's MeshRenderer. The reflection registry drives the UI, so the
// controls track the MaterialAsset fields automatically.
//
// A4: the panel owns a PreviewRig in INTERACTIVE mode — a live preview sphere
// (drag-orbit / wheel-zoom / double-click-reset) rendered offscreen with the
// state-restore contract — and every field edit lands on the undo stack (one
// command per completed drag, the Inspector idiom).
// ============================================================================

#include <Cosmic.h>

#include "PreviewRig.h"

#include <string>

namespace Starforge
{
    struct EditorContext;

    class MaterialEditorPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        Cosmic::MaterialAsset m_Asset;                 // the material being edited
        std::string           m_Path;                  // project:// .cmat ("" = unsaved)
        char                  m_SaveName[128] = "NewMaterial";

        PreviewRig            m_Rig;                   // A4 — interactive preview sphere

        // Undo bookkeeping: field value captured when a widget drag began.
        std::string                 m_EditField;
        Cosmic::Reflect::FieldValue m_EditBefore;
    };
}
