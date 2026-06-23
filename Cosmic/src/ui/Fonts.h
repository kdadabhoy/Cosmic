#pragma once

// Fonts.h
// ============================================================================
// Cosmic::UI::Fonts — the engine's ImGui font registry.
// ============================================================================
//
// Drop any .ttf / .otf into the engine fonts folder (Cosmic/assets/fonts, which
// is synced to the runtime and addressed as "engine://fonts") and it becomes
// available to every panel and overlay. At startup the engine bakes each font
// into ImGui's own glyph atlas at a handful of sizes; Get() then hands back the
// ImFont* nearest the size you ask for.
//
// This is the standard "sprite font" path for UI text — ImGui rasterises the
// TTF into an atlas texture internally, so no SDF is involved here (SDF is only
// used by the world-space Cosmic::Font / Renderer2D::DrawString path).
//
// Lives in the engine DLL on purpose: ImFont* and the atlas belong to the one
// shared ImGui context, so the registry must be a single instance. Projects
// call Get()/Push() across the DLL boundary and receive valid ImFont* handles.
// ============================================================================

#include "core/Core.h"
#include <string>

struct ImFont;

namespace Cosmic
{
	namespace UI
	{
		class COSMIC_API Fonts
		{
		public:
			// Scan the font folders and register every face at the standard sizes.
			// Call once from ImGuiLayer::OnAttach, before the first frame (the atlas
			// is baked lazily on the first NewFrame, so fonts must be added first).
			static void Init();

			// Register every .ttf/.otf in a resolved directory at the standard sizes.
			// Safe to call with a non-existent path (it simply does nothing).
			static void LoadFolder(const std::string& resolvedDir);

			// Nearest registered face for `name` (the file stem, e.g. "Roboto-Bold")
			// at the requested pixel size. Falls back to the default font when the
			// name is unknown. Never returns null once Init() has run.
			static ImFont* Get(const std::string& name, float sizePx);

			// The default UI font (a regular face if one was loaded, else ImGui's).
			static ImFont* Default();

			// True once at least one custom face has been registered.
			static bool Available();

			// Convenience wrappers around ImGui::PushFont / PopFont.
			static void Push(const std::string& name, float sizePx);
			static void Pop();
		};
	}
}
