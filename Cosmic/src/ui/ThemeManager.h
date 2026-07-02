#pragma once

// ThemeManager.h
// ============================================================================
// Cosmic::ThemeManager — the engine's runtime theme registry.
// ============================================================================
//
// Owns the list of available themes and the currently applied one. Lives in the
// engine DLL (storage is in the .cpp) so there is exactly ONE registry shared by
// the engine and every client project across the DLL boundary — clients call the
// exported API to list, apply, register, and author themes.
//
// Typical use:
//   ThemeManager::Init();                 // once, registers built-ins + user themes
//   ThemeManager::Apply("Sleek Pro");     // switch theme (engine or client)
//   for (auto& t : ThemeManager::All())   // e.g. build a picker
//       ...
//
// Authoring a new theme at runtime (see the template's ThemeShowcase layer):
//   Theme t = ThemeManager::CaptureCurrentStyle("My Theme"); // snapshot live style
//   ThemeManager::Register(t);                                // appears in All()
//   ThemeManager::SaveToFile(t, FileSystem::Resolve("project://themes/My Theme.ctheme"));
// ============================================================================

#include "core/Core.h"
#include "ui/Theme.h"

#include <string>
#include <vector>

namespace Cosmic
{
	class COSMIC_API ThemeManager
	{
	public:
		// Register the built-in themes. Safe to call more than once (no-op after
		// the first). Project themes (project://themes/*.ctheme) are loaded by
		// the engine's project-mount rescan (Application::LoadProjectDLL), since
		// Init runs before any project is mounted.
		static void Init();

		// Add a theme, or replace an existing one with the same name. Names are
		// the stable identity used by Apply() and the pickers.
		static void Register(const Theme& theme);

		// Apply a theme by name: writes its full colour table + style into the
		// live ImGui style and syncs the ImPlot style. Returns false if unknown.
		static bool Apply(const std::string& name);

		// Apply a Theme object directly without registering it — used by the
		// in-app editor for live preview while colours are being tweaked.
		static void ApplyTheme(const Theme& theme);

		// The name of the currently applied theme (empty before the first Apply).
		static const std::string& CurrentName();

		// The accent colour of the currently applied theme (for widgets/plots).
		static const ImVec4& Accent();

		// All registered themes, in registration order (built-ins first).
		static const std::vector<Theme>& All();

		// Look up a theme by name (nullptr if not found).
		static const Theme* Find(const std::string& name);

		// Snapshot the live ImGui style into a new (non-built-in) Theme. Used by
		// the editor's "save as" so the current edited look becomes a named theme.
		static Theme CaptureCurrentStyle(const std::string& name);

		// Simple text (.ctheme) persistence. resolvedPath is a real disk path
		// (resolve "project://themes/..." with FileSystem::Resolve first).
		static bool SaveToFile(const Theme& theme, const std::string& resolvedPath);
		static bool LoadFromFile(const std::string& resolvedPath, Theme& out);

		// Load and Register every *.ctheme in a resolved directory. Safe if the
		// directory does not exist.
		static void LoadFolder(const std::string& resolvedDir);
	};
}
