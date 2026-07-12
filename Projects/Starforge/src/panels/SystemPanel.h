#pragma once

// SystemPanel.h — Jobs / Resources utility panel (T18 / gap §13.3).
//
// Two tabs behind one dockable window:
//   * Jobs      — live JobSystem counters (queued / active / completed, workers).
//   * Resources — the AssetLibrary::Enumerate (T2) table: path / type / refs /
//                 CPU+GPU bytes, with per-row Reload and a totals line that
//                 matches the K5 status-bar chip (same accounting source).

#include "EditorContext.h"

namespace Starforge
{
    class SystemPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);
    };
}
