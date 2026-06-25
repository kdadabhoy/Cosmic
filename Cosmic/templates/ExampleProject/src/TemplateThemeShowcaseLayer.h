#pragma once
#include <Cosmic.h>

// ============================================================================
// TemplateThemeShowcaseLayer — "Theme Studio"
//
// A live gallery + editor for the engine's theme system. It:
//   * lists every registered theme (ThemeManager::All) and applies on click,
//   * previews the common widgets so a theme switch visibly restyles everything,
//   * lets you tweak the live colours/style and save the result as a new theme
//     under project://themes (ThemeManager::CaptureCurrentStyle + SaveToFile).
//
// Demonstrates fonts + icons + widgets + the theme registry all at once, and
// doubles as the "create your own theme" workflow.
// ============================================================================

// The Theme Studio window title. Used both by ImGui::Begin() and by the
// WorkspaceLayer::DockWindow() binding, so they must be the exact same string.
#define THEME_STUDIO_WINDOW (ICON_LC_PALETTE "  Theme Studio")

namespace Workspace
{
	class TemplateThemeShowcaseLayer : public Cosmic::Layer
	{
	public:
		TemplateThemeShowcaseLayer();
		virtual ~TemplateThemeShowcaseLayer() override = default;

		virtual void OnAttach()      override;
		virtual void OnImGuiRender() override;

	private:
		void DrawThemePicker();
		void DrawEditor();
		void DrawPreviewGallery();

		void BeginEditFrom(const std::string& themeName);

	private:
		// Working copy edited live in the editor; saved as a named theme.
		Cosmic::Theme m_Edit;
		char          m_NewName[64] = "My Theme";
		bool          m_Editing     = false;
		std::string   m_LastSavedPath;

		// Preview state.
		bool  m_ToggleA = true;
		bool  m_ToggleB = false;
		bool  m_Check   = true;
		float m_Slider  = 0.42f;
		int   m_Combo   = 0;
	};
}
