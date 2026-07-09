#pragma once

// panels/VoxelPanel.h
//
// ============================================================================
// Starforge — Voxels panel (Phase 18 / V4): block brush + palette + world tools.
// ============================================================================
//
// Authors the selected entity's VoxelVolumeComponent. The reflected recipe fields
// (palette/`.cvox` path, placement, generation) are edited in the Inspector like
// any component; this panel adds what a raw Inspector cannot: the block PALETTE
// picker (choose the block LMB paints), the viewport brush toggle (LMB place /
// RMB break — see ViewportController), a mesh-mode switch, resident-chunk stats,
// and Regenerate / Clear / Save-`.cvox` actions. Voxel edits are undoable
// (Commands::VoxelEdit, coalesced per brush stroke).
// ============================================================================

#include <Cosmic.h>

namespace Starforge
{
    struct EditorContext;

    class VoxelPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);
    };
}
