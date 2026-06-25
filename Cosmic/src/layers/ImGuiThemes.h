#pragma once
#include "ui/Theme.h"
#include <imgui.h>
#include <string>
#include <vector>

/**
 * @file ImGuiThemes.h
 * @brief Built-in theme definitions (data builders) for the Cosmic engine.
 *
 * Themes are plain data (see ui/Theme.h). Each BuildXxx() seeds a FULL ImGuiCol_
 * table from a dark/light base and then overrides the colours that give the theme
 * its identity, so applying any theme fully replaces the previous look.
 *
 * HOW TO ADD A NEW BUILT-IN THEME:
 * =========================================================================
 * 1. (Optional) Add an identifier to `enum class ImGuiTheme` if you want the
 *    legacy enum-based SetTheme() to reach it. New themes don't require this —
 *    name-based ThemeManager::Apply("My Theme") works without an enum entry.
 * 2. Write an `inline Theme BuildMyTheme()` below: call Seed*(t), set t.name,
 *    t.accent, override colours in t.colors[...], tweak t.style if desired.
 * 3. Add it to the list returned by GetBuiltInThemes().
 *
 * Clients and the in-app editor can also register themes at runtime without
 * touching this file — see ThemeManager::Register / CaptureCurrentStyle.
 */

namespace Cosmic
{
	enum class ImGuiTheme
	{
		DefaultDark = 0,
		CosmicEmerald,    // Industrial dark gray with vibrant emerald accents
		DeepEmbedded,     // Charcoal & slate aesthetic built for engineering panels
		CorporateLight,   // Clean, high-contrast crisp document reader theme
		CyberpunkNeon,    // Deep obsidian with hot pink, neon blue, and electric purple accents
		RetroTerminal,    // Pure monochrome black with glowing vintage amber/phosphor text
		DraculaDark,      // Refined, high-performance universal IDE dark mode palette
		SolarizedAsh,     // Soft, low-fatigue slate-gray layout optimized for long sessions
		SleekPro,         // Modern dark slate, subtle borders, emerald accent (default)
		NeonHUD,          // Near-black cockpit/telemetry readout with cyan + magenta
		CleanFlat         // Light, airy, rounded — modern flat app look
	};

	// =========================================================================
	// Base seeding helpers — fill the whole colour table before overrides.
	// =========================================================================

	inline void SeedDark(Theme& t)
	{
		ImGuiStyle s;
		ImGui::StyleColorsDark(&s);
		for (int i = 0; i < ImGuiCol_COUNT; ++i) t.colors[i] = s.Colors[i];
	}

	inline void SeedLight(Theme& t)
	{
		ImGuiStyle s;
		ImGui::StyleColorsLight(&s);
		for (int i = 0; i < ImGuiCol_COUNT; ++i) t.colors[i] = s.Colors[i];
	}

	// =========================================================================
	// THEME BUILDERS
	// =========================================================================

	inline Theme BuildCosmicEmerald()
	{
		Theme t; t.name = "Cosmic Emerald"; t.builtIn = true;
		t.accent = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_WindowBg]        = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.13f, 0.13f, 0.14f, 0.95f);
		c[ImGuiCol_Border]          = ImVec4(0.19f, 0.19f, 0.21f, 1.00f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
		c[ImGuiCol_Header]          = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
		c[ImGuiCol_HeaderHovered]   = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
		c[ImGuiCol_HeaderActive]    = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.10f, 0.48f, 0.24f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.14f, 0.60f, 0.32f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.08f, 0.40f, 0.20f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(0.18f, 0.80f, 0.42f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.14f, 0.60f, 0.32f, 0.80f);
		c[ImGuiCol_TabActive]       = ImVec4(0.14f, 0.60f, 0.32f, 1.00f);
		c[ImGuiCol_TabUnfocused]    = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		c[ImGuiCol_DockingPreview]  = ImVec4(0.14f, 0.60f, 0.32f, 0.35f);
		return t;
	}

	inline Theme BuildDeepEmbedded()
	{
		Theme t; t.name = "Deep Embedded"; t.builtIn = true;
		t.accent = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_WindowBg]        = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.08f, 0.09f, 0.12f, 0.95f);
		c[ImGuiCol_Border]          = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.24f, 0.30f, 0.38f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.14f, 0.28f, 0.48f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(0.35f, 0.58f, 0.92f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.18f, 0.35f, 0.58f, 0.80f);
		c[ImGuiCol_TabActive]       = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
		c[ImGuiCol_TabUnfocused]    = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
		return t;
	}

	inline Theme BuildCyberpunkNeon()
	{
		Theme t; t.name = "Cyberpunk Neon"; t.builtIn = true;
		t.accent = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]            = ImVec4(0.95f, 0.95f, 1.00f, 1.00f);
		c[ImGuiCol_WindowBg]        = ImVec4(0.03f, 0.02f, 0.05f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.05f, 0.04f, 0.08f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.04f, 0.03f, 0.07f, 0.95f);
		c[ImGuiCol_Border]          = ImVec4(0.50f, 0.00f, 0.40f, 0.70f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.20f, 0.08f, 0.28f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.28f, 0.10f, 0.38f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.05f, 0.04f, 0.08f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.15f, 0.05f, 0.22f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.85f, 0.00f, 0.45f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(1.00f, 0.10f, 0.55f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.65f, 0.00f, 0.35f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(0.30f, 1.00f, 1.00f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.85f, 0.00f, 0.45f, 0.75f);
		c[ImGuiCol_TabActive]       = ImVec4(0.85f, 0.00f, 0.45f, 1.00f);
		c[ImGuiCol_DockingPreview]  = ImVec4(0.00f, 0.95f, 0.95f, 0.30f);
		return t;
	}

	inline Theme BuildRetroTerminal()
	{
		Theme t; t.name = "Retro Terminal"; t.builtIn = true;
		t.accent = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]            = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
		c[ImGuiCol_WindowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.98f);
		c[ImGuiCol_Border]          = ImVec4(0.80f, 0.50f, 0.00f, 0.50f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.04f, 0.03f, 0.00f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.25f, 0.16f, 0.00f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.10f, 0.06f, 0.00f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.45f, 0.30f, 0.00f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(1.00f, 0.70f, 0.10f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.20f, 0.12f, 0.00f, 0.80f);
		c[ImGuiCol_TabActive]       = ImVec4(0.35f, 0.22f, 0.00f, 1.00f);
		c[ImGuiCol_DockingPreview]  = ImVec4(1.00f, 0.65f, 0.00f, 0.25f);
		t.style.WindowBorderSize = 1.0f;
		return t;
	}

	inline Theme BuildDraculaDark()
	{
		Theme t; t.name = "Dracula Dark"; t.builtIn = true;
		t.accent = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]            = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
		c[ImGuiCol_WindowBg]        = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.13f, 0.14f, 0.19f, 0.95f);
		c[ImGuiCol_Border]          = ImVec4(0.27f, 0.29f, 0.38f, 1.00f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.27f, 0.29f, 0.38f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.38f, 0.41f, 0.54f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.38f, 0.31f, 0.54f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.53f, 0.41f, 0.71f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(0.31f, 0.98f, 0.48f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(1.00f, 0.49f, 0.75f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.27f, 0.29f, 0.38f, 0.80f);
		c[ImGuiCol_TabActive]       = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);
		return t;
	}

	inline Theme BuildSolarizedAsh()
	{
		Theme t; t.name = "Solarized Ash"; t.builtIn = true;
		t.accent = ImVec4(0.42f, 0.62f, 0.68f, 1.00f);
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]            = ImVec4(0.80f, 0.83f, 0.85f, 1.00f);
		c[ImGuiCol_WindowBg]        = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
		c[ImGuiCol_ChildBg]         = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		c[ImGuiCol_PopupBg]         = ImVec4(0.14f, 0.15f, 0.17f, 0.96f);
		c[ImGuiCol_Border]          = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
		c[ImGuiCol_FrameBg]         = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
		c[ImGuiCol_FrameBgHovered]  = ImVec4(0.25f, 0.27f, 0.31f, 1.00f);
		c[ImGuiCol_FrameBgActive]   = ImVec4(0.28f, 0.31f, 0.35f, 1.00f);
		c[ImGuiCol_TitleBg]         = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		c[ImGuiCol_TitleBgActive]   = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
		c[ImGuiCol_Button]          = ImVec4(0.28f, 0.40f, 0.45f, 1.00f);
		c[ImGuiCol_ButtonHovered]   = ImVec4(0.35f, 0.48f, 0.53f, 1.00f);
		c[ImGuiCol_ButtonActive]    = ImVec4(0.22f, 0.32f, 0.37f, 1.00f);
		c[ImGuiCol_CheckMark]       = ImVec4(0.42f, 0.62f, 0.68f, 1.00f);
		c[ImGuiCol_SliderGrab]      = ImVec4(0.35f, 0.48f, 0.53f, 1.00f);
		c[ImGuiCol_SliderGrabActive]= ImVec4(0.50f, 0.68f, 0.75f, 1.00f);
		c[ImGuiCol_Tab]             = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		c[ImGuiCol_TabHovered]      = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
		c[ImGuiCol_TabActive]       = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
		return t;
	}

	inline Theme BuildCorporateLight()
	{
		Theme t; t.name = "Corporate Light"; t.builtIn = true;
		t.accent = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		SeedLight(t);
		t.style.WindowBorderSize = 1.0f;
		return t;
	}

	inline Theme BuildDefaultDark()
	{
		Theme t; t.name = "Default Dark"; t.builtIn = true;
		t.accent = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		SeedDark(t);
		return t;
	}

	// ---- Modern flagship themes ---------------------------------------------

	// Sleek Pro — the default. Dark slate, neutral controls with emerald accents,
	// subtle borders, medium rounding. VS Code / Blender "pro tool" feel.
	inline Theme BuildSleekPro()
	{
		Theme t; t.name = "Sleek Pro"; t.builtIn = true;
		const ImVec4 acc  = ImVec4(0.18f, 0.80f, 0.45f, 1.00f); // emerald
		const ImVec4 accD = ImVec4(0.14f, 0.62f, 0.35f, 1.00f);
		t.accent = acc;
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]               = ImVec4(0.89f, 0.91f, 0.94f, 1.00f);
		c[ImGuiCol_TextDisabled]       = ImVec4(0.45f, 0.48f, 0.52f, 1.00f);
		c[ImGuiCol_WindowBg]           = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
		c[ImGuiCol_ChildBg]            = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
		c[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.11f, 0.13f, 0.98f);
		c[ImGuiCol_Border]             = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
		c[ImGuiCol_FrameBg]            = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		c[ImGuiCol_FrameBgHovered]     = ImVec4(0.17f, 0.19f, 0.22f, 1.00f);
		c[ImGuiCol_FrameBgActive]      = ImVec4(0.21f, 0.23f, 0.27f, 1.00f);
		c[ImGuiCol_TitleBg]            = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
		c[ImGuiCol_TitleBgActive]      = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
		c[ImGuiCol_MenuBarBg]          = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
		c[ImGuiCol_ScrollbarBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.31f, 0.36f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.33f, 0.37f, 0.42f, 1.00f);
		c[ImGuiCol_CheckMark]          = acc;
		c[ImGuiCol_SliderGrab]         = accD;
		c[ImGuiCol_SliderGrabActive]   = acc;
		c[ImGuiCol_Button]             = ImVec4(0.17f, 0.19f, 0.22f, 1.00f);
		c[ImGuiCol_ButtonHovered]      = ImVec4(0.22f, 0.25f, 0.29f, 1.00f);
		c[ImGuiCol_ButtonActive]      = ImVec4(0.26f, 0.30f, 0.35f, 1.00f);
		c[ImGuiCol_Header]             = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
		c[ImGuiCol_HeaderHovered]      = ImVec4(0.18f, 0.80f, 0.45f, 0.20f);
		c[ImGuiCol_HeaderActive]       = ImVec4(0.18f, 0.80f, 0.45f, 0.32f);
		c[ImGuiCol_Separator]          = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);
		c[ImGuiCol_SeparatorHovered]   = ImVec4(0.18f, 0.80f, 0.45f, 0.50f);
		c[ImGuiCol_SeparatorActive]    = acc;
		c[ImGuiCol_ResizeGrip]         = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);
		c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.18f, 0.80f, 0.45f, 0.55f);
		c[ImGuiCol_ResizeGripActive]   = acc;
		c[ImGuiCol_Tab]                = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
		c[ImGuiCol_TabHovered]         = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
		c[ImGuiCol_TabActive]          = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);
		c[ImGuiCol_TabUnfocused]       = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.13f, 0.16f, 1.00f);
		c[ImGuiCol_DockingPreview]     = ImVec4(0.18f, 0.80f, 0.45f, 0.30f);
		c[ImGuiCol_TextSelectedBg]     = ImVec4(0.18f, 0.80f, 0.45f, 0.28f);
		c[ImGuiCol_PlotLines]          = acc;
		c[ImGuiCol_PlotHistogram]      = acc;
		t.style.WindowRounding = 6.0f; t.style.ChildRounding = 6.0f;
		t.style.FrameRounding  = 5.0f; t.style.PopupRounding = 6.0f;
		t.style.GrabRounding   = 5.0f; t.style.TabRounding   = 5.0f;
		t.style.WindowPadding  = ImVec2(10.0f, 10.0f);
		t.style.FramePadding   = ImVec2(8.0f, 5.0f);
		t.style.ItemSpacing    = ImVec2(8.0f, 6.0f);
		t.style.ScrollbarSize  = 12.0f; t.style.GrabMinSize = 10.0f;
		return t;
	}

	// Neon HUD — near-black cockpit/telemetry look with cyan controls, magenta
	// docking highlight, thin framed outlines, tight spacing.
	inline Theme BuildNeonHUD()
	{
		Theme t; t.name = "Neon HUD"; t.builtIn = true;
		const ImVec4 cyan  = ImVec4(0.10f, 0.90f, 0.95f, 1.00f);
		const ImVec4 cyanD = ImVec4(0.08f, 0.62f, 0.70f, 1.00f);
		t.accent = cyan;
		SeedDark(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]               = ImVec4(0.82f, 0.92f, 0.95f, 1.00f);
		c[ImGuiCol_TextDisabled]       = ImVec4(0.38f, 0.50f, 0.55f, 1.00f);
		c[ImGuiCol_WindowBg]           = ImVec4(0.03f, 0.05f, 0.07f, 1.00f);
		c[ImGuiCol_ChildBg]            = ImVec4(0.05f, 0.07f, 0.10f, 1.00f);
		c[ImGuiCol_PopupBg]            = ImVec4(0.04f, 0.06f, 0.09f, 0.98f);
		c[ImGuiCol_Border]             = ImVec4(0.10f, 0.42f, 0.50f, 0.65f);
		c[ImGuiCol_FrameBg]            = ImVec4(0.06f, 0.10f, 0.13f, 1.00f);
		c[ImGuiCol_FrameBgHovered]     = ImVec4(0.09f, 0.16f, 0.20f, 1.00f);
		c[ImGuiCol_FrameBgActive]      = ImVec4(0.12f, 0.22f, 0.27f, 1.00f);
		c[ImGuiCol_TitleBg]            = ImVec4(0.03f, 0.05f, 0.07f, 1.00f);
		c[ImGuiCol_TitleBgActive]      = ImVec4(0.05f, 0.11f, 0.14f, 1.00f);
		c[ImGuiCol_MenuBarBg]          = ImVec4(0.04f, 0.07f, 0.10f, 1.00f);
		c[ImGuiCol_ScrollbarBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.10f, 0.28f, 0.33f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.12f, 0.40f, 0.46f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]  = cyanD;
		c[ImGuiCol_CheckMark]          = cyan;
		c[ImGuiCol_SliderGrab]         = cyanD;
		c[ImGuiCol_SliderGrabActive]   = cyan;
		c[ImGuiCol_Button]             = ImVec4(0.08f, 0.18f, 0.22f, 1.00f);
		c[ImGuiCol_ButtonHovered]      = ImVec4(0.10f, 0.30f, 0.36f, 1.00f);
		c[ImGuiCol_ButtonActive]       = ImVec4(0.10f, 0.45f, 0.52f, 1.00f);
		c[ImGuiCol_Header]             = ImVec4(0.07f, 0.14f, 0.18f, 1.00f);
		c[ImGuiCol_HeaderHovered]      = ImVec4(0.10f, 0.90f, 0.95f, 0.18f);
		c[ImGuiCol_HeaderActive]       = ImVec4(0.10f, 0.90f, 0.95f, 0.30f);
		c[ImGuiCol_Separator]          = ImVec4(0.10f, 0.42f, 0.50f, 0.50f);
		c[ImGuiCol_SeparatorHovered]   = ImVec4(0.10f, 0.90f, 0.95f, 0.50f);
		c[ImGuiCol_SeparatorActive]    = cyan;
		c[ImGuiCol_ResizeGrip]         = ImVec4(0.10f, 0.42f, 0.50f, 0.50f);
		c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.10f, 0.90f, 0.95f, 0.55f);
		c[ImGuiCol_ResizeGripActive]   = cyan;
		c[ImGuiCol_Tab]                = ImVec4(0.04f, 0.08f, 0.11f, 1.00f);
		c[ImGuiCol_TabHovered]         = ImVec4(0.10f, 0.90f, 0.95f, 0.40f);
		c[ImGuiCol_TabActive]          = ImVec4(0.08f, 0.22f, 0.27f, 1.00f);
		c[ImGuiCol_TabUnfocused]       = ImVec4(0.04f, 0.07f, 0.10f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.06f, 0.13f, 0.16f, 1.00f);
		c[ImGuiCol_DockingPreview]     = ImVec4(1.00f, 0.20f, 0.60f, 0.30f); // magenta
		c[ImGuiCol_TextSelectedBg]     = ImVec4(0.10f, 0.90f, 0.95f, 0.28f);
		c[ImGuiCol_PlotLines]          = cyan;
		c[ImGuiCol_PlotHistogram]      = ImVec4(1.00f, 0.20f, 0.60f, 1.00f);
		t.style.WindowRounding = 3.0f; t.style.ChildRounding = 3.0f;
		t.style.FrameRounding  = 3.0f; t.style.PopupRounding = 3.0f;
		t.style.GrabRounding   = 2.0f; t.style.TabRounding   = 3.0f;
		t.style.ScrollbarRounding = 3.0f;
		t.style.WindowBorderSize = 1.0f; t.style.FrameBorderSize = 1.0f;
		t.style.WindowPadding  = ImVec2(8.0f, 8.0f);
		t.style.FramePadding   = ImVec2(7.0f, 4.0f);
		t.style.ItemSpacing    = ImVec2(7.0f, 5.0f);
		return t;
	}

	// Clean Flat — light, airy, generous rounding, blue accent. Figma / Linear feel.
	inline Theme BuildCleanFlat()
	{
		Theme t; t.name = "Clean Flat"; t.builtIn = true;
		const ImVec4 blue  = ImVec4(0.20f, 0.55f, 0.95f, 1.00f);
		const ImVec4 blueD = ImVec4(0.16f, 0.45f, 0.82f, 1.00f);
		t.accent = blue;
		SeedLight(t);
		ImVec4* c = t.colors;
		c[ImGuiCol_Text]               = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
		c[ImGuiCol_TextDisabled]       = ImVec4(0.55f, 0.58f, 0.62f, 1.00f);
		c[ImGuiCol_WindowBg]           = ImVec4(0.96f, 0.97f, 0.98f, 1.00f);
		c[ImGuiCol_ChildBg]            = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		c[ImGuiCol_PopupBg]            = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
		c[ImGuiCol_Border]             = ImVec4(0.86f, 0.88f, 0.91f, 1.00f);
		c[ImGuiCol_FrameBg]            = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
		c[ImGuiCol_FrameBgHovered]     = ImVec4(0.89f, 0.91f, 0.94f, 1.00f);
		c[ImGuiCol_FrameBgActive]      = ImVec4(0.85f, 0.88f, 0.92f, 1.00f);
		c[ImGuiCol_TitleBg]            = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
		c[ImGuiCol_TitleBgActive]      = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
		c[ImGuiCol_MenuBarBg]          = ImVec4(0.94f, 0.95f, 0.97f, 1.00f);
		c[ImGuiCol_ScrollbarBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.80f, 0.83f, 0.87f, 1.00f);
		c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.72f, 0.76f, 0.81f, 1.00f);
		c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.64f, 0.69f, 0.75f, 1.00f);
		c[ImGuiCol_CheckMark]          = blue;
		c[ImGuiCol_SliderGrab]         = blueD;
		c[ImGuiCol_SliderGrabActive]   = blue;
		c[ImGuiCol_Button]             = ImVec4(0.92f, 0.94f, 0.96f, 1.00f);
		c[ImGuiCol_ButtonHovered]      = ImVec4(0.87f, 0.90f, 0.94f, 1.00f);
		c[ImGuiCol_ButtonActive]       = ImVec4(0.80f, 0.87f, 0.97f, 1.00f);
		c[ImGuiCol_Header]             = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
		c[ImGuiCol_HeaderHovered]      = ImVec4(0.20f, 0.55f, 0.95f, 0.15f);
		c[ImGuiCol_HeaderActive]       = ImVec4(0.20f, 0.55f, 0.95f, 0.25f);
		c[ImGuiCol_Separator]          = ImVec4(0.85f, 0.87f, 0.90f, 1.00f);
		c[ImGuiCol_SeparatorHovered]   = ImVec4(0.20f, 0.55f, 0.95f, 0.50f);
		c[ImGuiCol_SeparatorActive]    = blue;
		c[ImGuiCol_ResizeGrip]         = ImVec4(0.80f, 0.83f, 0.87f, 0.80f);
		c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.20f, 0.55f, 0.95f, 0.55f);
		c[ImGuiCol_ResizeGripActive]   = blue;
		c[ImGuiCol_Tab]                = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
		c[ImGuiCol_TabHovered]         = ImVec4(0.20f, 0.55f, 0.95f, 0.20f);
		c[ImGuiCol_TabActive]          = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		c[ImGuiCol_TabUnfocused]       = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
		c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.97f, 0.98f, 0.99f, 1.00f);
		c[ImGuiCol_DockingPreview]     = ImVec4(0.20f, 0.55f, 0.95f, 0.25f);
		c[ImGuiCol_TextSelectedBg]     = ImVec4(0.20f, 0.55f, 0.95f, 0.25f);
		c[ImGuiCol_PlotLines]          = blue;
		c[ImGuiCol_PlotHistogram]      = blue;
		t.style.WindowRounding = 8.0f; t.style.ChildRounding = 8.0f;
		t.style.FrameRounding  = 6.0f; t.style.PopupRounding = 8.0f;
		t.style.GrabRounding   = 6.0f; t.style.TabRounding   = 6.0f;
		t.style.WindowBorderSize = 1.0f; t.style.FrameBorderSize = 0.0f;
		t.style.WindowPadding  = ImVec2(12.0f, 12.0f);
		t.style.FramePadding   = ImVec2(9.0f, 6.0f);
		t.style.ItemSpacing    = ImVec2(9.0f, 7.0f);
		t.style.ScrollbarSize  = 13.0f;
		return t;
	}

	// =========================================================================
	// Registration list + legacy enum name mapping
	// =========================================================================

	// The order here is the order themes appear in pickers. The modern flagship
	// themes lead, with Sleek Pro first (the engine default).
	inline std::vector<Theme> GetBuiltInThemes()
	{
		return {
			BuildSleekPro(),
			BuildNeonHUD(),
			BuildCleanFlat(),
			BuildCosmicEmerald(),
			BuildDeepEmbedded(),
			BuildDraculaDark(),
			BuildSolarizedAsh(),
			BuildCyberpunkNeon(),
			BuildRetroTerminal(),
			BuildCorporateLight(),
			BuildDefaultDark()
		};
	}

	// Maps the legacy ImGuiTheme enum to a theme name so the old
	// SetTheme(enum) API keeps working on top of the name-based registry.
	inline const char* NameForTheme(ImGuiTheme theme)
	{
		switch (theme)
		{
		case ImGuiTheme::CosmicEmerald:  return "Cosmic Emerald";
		case ImGuiTheme::DeepEmbedded:   return "Deep Embedded";
		case ImGuiTheme::CyberpunkNeon:  return "Cyberpunk Neon";
		case ImGuiTheme::RetroTerminal:  return "Retro Terminal";
		case ImGuiTheme::DraculaDark:    return "Dracula Dark";
		case ImGuiTheme::SolarizedAsh:   return "Solarized Ash";
		case ImGuiTheme::CorporateLight: return "Corporate Light";
		case ImGuiTheme::SleekPro:       return "Sleek Pro";
		case ImGuiTheme::NeonHUD:        return "Neon HUD";
		case ImGuiTheme::CleanFlat:      return "Clean Flat";
		case ImGuiTheme::DefaultDark:
		default:                         return "Default Dark";
		}
	}
}
