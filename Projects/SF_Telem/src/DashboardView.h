#pragma once

// DashboardView.h
// ============================================================================
// Shared "live dashboard" rendering, used by BOTH the Main screen (live serial)
// and the Replay screen (player-driven values). All draw functions read through
// the TelemHub accessors (Rpm/Volt/Cur/Speed/Tip/...), so whatever is feeding
// the hub — live packets or a replayed frame — shows up identically here.
//
//   * DrawDashboard   — "Live Dashboard" window: weapon photo (top) + drivetrain
//                       photo (bottom), each with readout boxes overlaid at
//                       normalized positions.
//   * DrawWeaponReadouts / DrawDriveReadouts — the compact stat-box panels.
//   * DrawPlots       — "ESC Plots" tabbed per-ESC charts.
// ============================================================================

#include <Cosmic.h>

namespace Workspace
{
    class TelemHub;

    namespace DashboardView
    {
        // "Live Dashboard" window. If focusFrames is non-null and > 0 it forces the
        // window tab active for that many frames (used after a (re)dock), and is
        // decremented each call.
        void DrawDashboard(TelemHub* hub,
                           const Cosmic::Ref<Cosmic::Texture2D>& weaponTex,
                           const Cosmic::Ref<Cosmic::Texture2D>& drivetrainTex,
                           int* focusFrames = nullptr);

        void DrawWeaponReadouts(TelemHub* hub);                       // "Weapon" window
        void DrawDriveReadouts(TelemHub* hub, int id, const char* title); // Left/Right Drive windows
        void DrawPlots(TelemHub* hub);                               // "ESC Plots" window
    }

} // namespace Workspace
