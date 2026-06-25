#pragma once

// PlotStyle.h
// ============================================================================
// Cosmic::UI — sync the ImPlot style to a Cosmic Theme.
// ============================================================================
//
// Keeps charts visually consistent with the active ImGui theme: transparent plot
// frame/background (so plots blend into their panel), subtle theme-coloured grid,
// dimmed axis text, themed legend, and a clean line weight. Called automatically
// by ThemeManager::Apply, so charts restyle whenever the theme changes.
// ============================================================================

#include "core/Core.h"
#include "ui/Theme.h"

namespace Cosmic
{
	namespace UI
	{
		// Apply `theme` to the global ImPlot style. No-op if there is no current
		// ImPlot context.
		COSMIC_API void ApplyPlotStyle(const Theme& theme);
	}
}
