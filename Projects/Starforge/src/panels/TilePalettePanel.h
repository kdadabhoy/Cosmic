#pragma once

// panels/TilePalettePanel.h
//
// ============================================================================
// Starforge — Tile Palette panel (Phase 17 / U4): tileset picker + paint tools.
// ============================================================================
//
// Authors the selected entity's TilemapComponent. The reflected fields
// (TilesetPath, tile/grid sizes, ZOrder) are edited in the Inspector like any
// component; this panel adds the tileset GRID picker (choose the tile LMB
// paints), the tool selector (Paint / Flood / Rect — RMB always erases), and
// the viewport painting toggle (2D mode; see ViewportController). Cell edits
// are undoable (Commands::TileEdit / TileEditRun — a paint drag coalesces into
// one stroke, fills are one step).
// ============================================================================

#include <Cosmic.h>

namespace Starforge
{
    struct EditorContext;

    class TilePalettePanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);
    };
}
