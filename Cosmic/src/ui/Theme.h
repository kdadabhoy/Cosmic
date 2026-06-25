#pragma once

// Theme.h
// ============================================================================
// Cosmic::Theme — a complete, data-driven description of an ImGui look.
// ============================================================================
//
// A Theme is plain data: a name, an accent colour, a FULL ImGuiCol_ colour
// table, and the structural style knobs (rounding / padding / borders). Because
// every theme carries a complete colour array, applying one is deterministic —
// switching themes at runtime fully replaces the previous look (the old enum +
// "set a subset" approach left stale colours behind when switching).
//
// Themes are built as data in ImGuiThemes.h (BuildXxx() functions), registered
// with the engine's ThemeManager, and applied via ThemeManager::Apply(name).
// Clients and the in-app editor can construct their own Theme and Register() it.
// ============================================================================

#include <imgui.h>
#include <string>

namespace Cosmic
{
	// -------------------------------------------------------------------------
	// Structural style knobs. Defaults reproduce the engine's previous global
	// style exactly (the values ImGuiLayer::SetTheme used to hard-code, plus
	// ImGui's own defaults for everything it left untouched), so a theme that
	// doesn't customise these looks identical to before.
	// -------------------------------------------------------------------------
	struct ThemeStyle
	{
		float  WindowRounding    = 5.0f;
		float  ChildRounding     = 0.0f;
		float  FrameRounding     = 4.0f;
		float  PopupRounding     = 4.0f;
		float  ScrollbarRounding = 9.0f;
		float  GrabRounding      = 3.0f;
		float  TabRounding       = 4.0f;

		float  WindowBorderSize  = 1.0f;
		float  FrameBorderSize   = 0.0f;
		float  ChildBorderSize   = 1.0f;

		float  ScrollbarSize     = 14.0f;
		float  GrabMinSize       = 12.0f;

		ImVec2 WindowPadding     = ImVec2(8.0f, 8.0f);
		ImVec2 FramePadding      = ImVec2(4.0f, 3.0f);
		ImVec2 ItemSpacing       = ImVec2(6.0f, 4.0f);
		ImVec2 ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
	};

	// -------------------------------------------------------------------------
	// A complete theme. `colors` is the full ImGuiCol_ table; `accent` is a
	// semantic highlight colour reused by widgets (StatCard bars, toggles) and
	// the ImPlot sync so charts track the theme.
	// -------------------------------------------------------------------------
	struct Theme
	{
		std::string name;
		ImVec4      accent  = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		ImVec4      colors[ImGuiCol_COUNT] = {};
		ThemeStyle  style;
		bool        builtIn = false;
	};
}
