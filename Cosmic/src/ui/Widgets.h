#pragma once

// Widgets.h
// ============================================================================
// Cosmic::UI — reusable, theme-aware ImGui widgets.
// ============================================================================
//
// A small kit of modern building blocks used across the engine and projects.
// They pull their highlight colour from the active theme (ThemeManager::Accent)
// and crisp text from the font registry, so they restyle automatically when the
// theme changes. Compiled into the engine DLL and exported, so client projects
// can call them across the DLL boundary.
//
//   UI::StatCard("rpm", ICON_LC_GAUGE, "Weapon RPM", "12,480", "avg 9,120", accent);
//   UI::ToggleSwitch("Live capture", &live);
//   if (UI::AccentButton(ICON_LC_PLUG "  Connect")) { ... }
// ============================================================================

#include "core/Core.h"
#include <imgui.h>

namespace Cosmic
{
	namespace UI
	{
		// A framed "indication" card: a left accent bar, an optional icon + small
		// label, a large crisp value, and a dimmed sub-line underneath. Pass icon
		// = nullptr to omit it. valueColor with w<=0 uses the theme's text colour.
		COSMIC_API void StatCard(const char* id, const char* icon, const char* label,
		                         const char* value, const char* sub, const ImVec4& accent,
		                         const ImVec2& size = ImVec2(0.0f, 0.0f),
		                         const ImVec4& valueColor = ImVec4(0, 0, 0, 0));

		// An animated on/off switch. Returns true on the frame it is toggled.
		// The visible label is the part of `label` before any "##" id suffix.
		COSMIC_API bool ToggleSwitch(const char* label, bool* v);

		// A bold section heading with an optional leading icon and an underline.
		COSMIC_API void SectionHeader(const char* icon, const char* text);

		// A compact square button showing just an icon, with an optional tooltip.
		// `str_id` keeps the ID unique even when several buttons share a glyph.
		COSMIC_API bool IconButton(const char* str_id, const char* icon,
		                           const char* tooltip = nullptr,
		                           const ImVec2& size = ImVec2(0.0f, 0.0f));

		// A filled "primary action" button tinted with the theme accent.
		COSMIC_API bool AccentButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));
	}
}
