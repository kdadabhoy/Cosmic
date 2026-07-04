#pragma once

// panels/MaterialEditorPanel.h
//
// ============================================================================
// Starforge — Material Editor (E17).
// ============================================================================
//
// Author a PBR `.cmat` (a reflected MaterialAsset): albedo/metallic/roughness/AO/
// emissive + optional texture-map slots, New / Save, and Assign / Load to the
// selected entity's MeshRenderer. The reflection registry drives the UI, so the
// controls track the MaterialAsset fields automatically. The live viewport is the
// preview (a dedicated offscreen preview-sphere rig is a documented follow-up).
// ============================================================================

#include <Cosmic.h>

#include <string>

namespace Starforge
{
    struct EditorContext;

    class MaterialEditorPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx);

    private:
        Cosmic::MaterialAsset m_Asset;                 // the material being edited
        std::string           m_Path;                  // project:// .cmat ("" = unsaved)
        char                  m_SaveName[128] = "NewMaterial";
    };
}
