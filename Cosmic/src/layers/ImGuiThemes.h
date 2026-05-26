#pragma once
#include <imgui.h>
#include <map>
#include <functional>

/**
 * @file ImGuiThemes.h
 * @brief Automated Visual Theme Registry Configuration Matrix
 * 
 * HOW TO ADD A NEW THEME IN 3 STEPS WITHOUT TOUCHING CORE ENGINE CODE:
 * =========================================================================
 * 1. Add your new unique identifier token to the `enum class ImGuiTheme` block.
 * 
 * 2. Write an `inline void ApplyYourThemeName(ImGuiStyle& style)` function
 * anywhere above the registry map to define your custom ImVec4 color arrays.
 * 
 * 3. Scroll down to `GetThemeRegistry()` and add a lookup entry inside the
 * initializer list tying your enum to your function:
 * `{ ImGuiTheme::YourThemeName, ApplyYourThemeName },`
 * 
 * Once added, the engine's automated lookup registry will instantly expose your
 * theme to runtime components, workspace wrappers, and client UI sliders automatically!
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
		SolarizedAsh      // Soft, low-fatigue slate-gray layout optimized for 12+ hour editing sessions
	};

	// =========================================================================
	// THEME PROFILES IMPLEMENTATION
	// =========================================================================

	inline void ApplyCosmicEmerald(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.13f, 0.14f, 0.95f);
		colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.21f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.15f, 0.16f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.26f, 0.26f, 0.28f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.30f, 0.32f, 1.00f);

		colors[ImGuiCol_Button] = ImVec4(0.10f, 0.48f, 0.24f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.14f, 0.60f, 0.32f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.08f, 0.40f, 0.20f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.18f, 0.80f, 0.42f, 1.00f);

		colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.60f, 0.32f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.14f, 0.60f, 0.32f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.12f, 0.13f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.14f, 0.60f, 0.32f, 0.35f);
	}

	inline void ApplyDeepEmbedded(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.09f, 0.12f, 0.95f);
		colors[ImGuiCol_Border] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.22f, 0.28f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.30f, 0.38f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);

		colors[ImGuiCol_Button] = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.14f, 0.28f, 0.48f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.46f, 0.76f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.58f, 0.92f, 1.00f);

		colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.18f, 0.35f, 0.58f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.35f, 0.58f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.09f, 0.11f, 0.14f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
	}

	inline void ApplyCyberpunkNeon(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 1.00f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.02f, 0.05f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.05f, 0.04f, 0.08f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.03f, 0.07f, 0.95f);
		colors[ImGuiCol_Border] = ImVec4(0.50f, 0.00f, 0.40f, 0.70f); // Neon Magenta Border
		colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.05f, 0.15f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.08f, 0.28f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.10f, 0.38f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.05f, 0.04f, 0.08f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.05f, 0.22f, 1.00f);

		// Hot Pink / Cyan contrasts
		colors[ImGuiCol_Button] = ImVec4(0.85f, 0.00f, 0.45f, 1.00f); // Hot Pink Buttons
		colors[ImGuiCol_ButtonHovered] = ImVec4(1.00f, 0.10f, 0.55f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.00f, 0.35f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.95f, 0.95f, 1.00f); // Cyan Checkmarks
		colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.95f, 0.95f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 1.00f, 1.00f, 1.00f);

		colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.04f, 0.12f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.85f, 0.00f, 0.45f, 0.75f);
		colors[ImGuiCol_TabActive] = ImVec4(0.85f, 0.00f, 0.45f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.00f, 0.95f, 0.95f, 0.30f);
	}

	inline void ApplyRetroTerminal(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		// Matrix / Fall-Out Phosphor Amber Aesthetic
		colors[ImGuiCol_Text] = ImVec4(1.00f, 0.65f, 0.00f, 1.00f); // Vintage Amber
		colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f); // Absolute Jet Black
		colors[ImGuiCol_ChildBg] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.98f);
		colors[ImGuiCol_Border] = ImVec4(0.80f, 0.50f, 0.00f, 0.50f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.04f, 0.03f, 0.00f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.25f, 0.16f, 0.00f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.06f, 0.00f, 1.00f);

		colors[ImGuiCol_Button] = ImVec4(0.15f, 0.10f, 0.00f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.30f, 0.00f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.70f, 0.10f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.20f, 0.00f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);

		colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.12f, 0.00f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.35f, 0.22f, 0.00f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(1.00f, 0.65f, 0.00f, 0.25f);
		style.WindowBorderSize = 1.0f;
	}

	inline void ApplyDraculaDark(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		// Dracula Official Theme Hex Mapping
		colors[ImGuiCol_Text] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);       // #f8f8f2
		colors[ImGuiCol_WindowBg] = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);   // #282a36
		colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);    // #1e1f29
		colors[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.19f, 0.95f);
		colors[ImGuiCol_Border] = ImVec4(0.27f, 0.29f, 0.38f, 1.00f);     // #44475a
		colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.29f, 0.38f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.38f, 0.41f, 0.54f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);

		colors[ImGuiCol_Button] = ImVec4(0.38f, 0.31f, 0.54f, 1.00f);    // #6272a4 (Comment/Purple)
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.74f, 0.58f, 0.98f, 1.00f); // #bd93f9 (Purple highlight)
		colors[ImGuiCol_ButtonActive] = ImVec4(0.53f, 0.41f, 0.71f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.31f, 0.98f, 0.48f, 1.00f);    // #50fa7b (Green)
		colors[ImGuiCol_SliderGrab] = ImVec4(0.74f, 0.58f, 0.98f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.49f, 0.75f, 1.00f); // #ff79c6 (Pink)

		colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.19f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.27f, 0.29f, 0.38f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.17f, 0.18f, 0.25f, 1.00f);
	}

	inline void ApplySolarizedAsh(ImGuiStyle& style)
	{
		auto& colors = style.Colors;
		// Low Contrast Matte Slate
		colors[ImGuiCol_Text] = ImVec4(0.80f, 0.83f, 0.85f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.14f, 0.15f, 0.17f, 0.96f);
		colors[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.27f, 1.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.27f, 0.31f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.31f, 0.35f, 1.00f);

		colors[ImGuiCol_TitleBg] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.20f, 0.23f, 1.00f);

		colors[ImGuiCol_Button] = ImVec4(0.28f, 0.40f, 0.45f, 1.00f); // Muted Teal-Gray
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.48f, 0.53f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.32f, 0.37f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.42f, 0.62f, 0.68f, 1.00f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.35f, 0.48f, 0.53f, 1.00f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.68f, 0.75f, 1.00f);

		colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
		colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.17f, 0.20f, 1.00f);
	}

	// =========================================================================
	// THE AUTOMATION REGISTRY MATRIX
	// =========================================================================
	using ThemeFunction = std::function<void(ImGuiStyle&)>;

	inline const std::map<ImGuiTheme, ThemeFunction>& GetThemeRegistry()
	{
		static const std::map<ImGuiTheme, ThemeFunction> registry = {
			{ ImGuiTheme::CosmicEmerald,  ApplyCosmicEmerald },
			{ ImGuiTheme::DeepEmbedded,   ApplyDeepEmbedded },
			{ ImGuiTheme::CyberpunkNeon,  ApplyCyberpunkNeon },
			{ ImGuiTheme::RetroTerminal,  ApplyRetroTerminal },
			{ ImGuiTheme::DraculaDark,    ApplyDraculaDark },
			{ ImGuiTheme::SolarizedAsh,   ApplySolarizedAsh },
			{ ImGuiTheme::CorporateLight, [](ImGuiStyle& s) { ImGui::StyleColorsLight(); s.WindowBorderSize = 1.0f; } },
			{ ImGuiTheme::DefaultDark,    [](ImGuiStyle& s) { ImGui::StyleColorsDark(); } }
		};
		return registry;
	}
}