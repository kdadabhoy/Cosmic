// Fonts.cpp — see Fonts.h.

#include "ui/Fonts.h"
#include "ui/IconsLucide.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace Cosmic
{
	namespace UI
	{
		namespace
		{
			struct Entry
			{
				std::string name;   // file stem, e.g. "Roboto-Bold"
				ImFont*     font;   // handle into the shared ImGui atlas
			};

			std::vector<Entry> s_Fonts;
			ImFont*            s_Default     = nullptr;
			bool               s_Initialized = false;

			// ImGui 1.92 renders one ImFont at any size on demand (AddText(font, size, ...)),
			// so each face is baked just once. This is only the legacy/default size used
			// when a size isn't specified — a sensible body-text size.
			constexpr float k_BaseSize = 18.0f;

			// Icon font (Lucide) merged into every text face so icon glyphs render
			// inline with text under any pushed face. Discovered in Init().
			std::string s_IconPath;
			bool        s_HasIcons = false;

			bool IEquals(const std::string& a, const std::string& b)
			{
				if (a.size() != b.size()) return false;
				for (size_t i = 0; i < a.size(); ++i)
					if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
						return false;
				return true;
			}

			// Icon fonts are merged into the text faces, not registered as their
			// own selectable body face.
			bool IsIconFontStem(const std::string& stem)
			{
				return IEquals(stem, "lucide");
			}

			// Merge the icon glyphs into the most recently added font (ImGui's
			// merge mode attaches to the previous AddFont* call). Call right
			// after adding each text face.
			void MergeIconsInto(float sizePx)
			{
				if (!s_HasIcons) return;

				static const ImWchar range[] = { ICON_MIN_LC, ICON_MAX_LC, 0 };
				ImFontConfig cfg;
				cfg.MergeMode        = true;
				cfg.PixelSnapH       = true;
				cfg.GlyphMinAdvanceX = sizePx; // keep icons a consistent width

				ImGui::GetIO().Fonts->AddFontFromFileTTF(s_IconPath.c_str(), sizePx, &cfg, range);
			}
		}

		void Fonts::LoadFolder(const std::string& resolvedDir)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			if (!fs::exists(resolvedDir, ec) || !fs::is_directory(resolvedDir, ec))
				return;

			ImGuiIO& io = ImGui::GetIO();

			for (const auto& de : fs::directory_iterator(resolvedDir, ec))
			{
				if (ec || !de.is_regular_file()) continue;

				std::string ext = de.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return (char)std::tolower(c); });
				if (ext != ".ttf" && ext != ".otf") continue;

				const std::string path = de.path().string();
				const std::string name = de.path().stem().string();

				// The icon font is merged into each text face below, not added as
				// a standalone selectable body face.
				if (IsIconFontStem(name)) continue;

				// First registration of a stem wins (engine faces load before
				// project faces) — and the project-mount rescan stays idempotent.
				bool exists = false;
				for (const auto& e : s_Fonts)
					if (IEquals(e.name, name)) { exists = true; break; }
				if (exists) continue;

				ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), k_BaseSize);
				if (f)
				{
					s_Fonts.push_back({ name, f });
					MergeIconsInto(k_BaseSize); // icons available under this face
					CS_CORE_INFO("Fonts: registered '{0}'", name);
				}
				else
				{
					CS_CORE_WARN("Fonts: failed to load '{0}'", path);
				}
			}
		}

		void Fonts::Init()
		{
			if (s_Initialized) return;
			s_Initialized = true;

			ImGuiIO& io = ImGui::GetIO();

			// Discover the icon font up front so it can be merged into each text
			// face as the faces are loaded.
			s_IconPath = FileSystem::Resolve("engine://fonts/lucide.ttf");
			{
				std::error_code ec;
				s_HasIcons = std::filesystem::exists(s_IconPath, ec);
			}

			// Keep ImGui's built-in bitmap font as a last-resort fallback (Fonts[0]),
			// but we override io.FontDefault below so the UI renders in Roboto.
			io.Fonts->AddFontDefault();

			// Engine faces only: Init runs at ImGuiLayer attach, BEFORE any project
			// is mounted, so "project://" cannot resolve here. Project faces load
			// via LoadProjectFonts(), called from Application::LoadProjectDLL.
			LoadFolder(FileSystem::Resolve("engine://fonts"));

			// Pick the default UI face: prefer Roboto-Regular, else the first
			// custom face, else ImGui's built-in.
			s_Default = Get("Roboto-Regular", k_BaseSize);
			if (!s_Default && !s_Fonts.empty())
				s_Default = s_Fonts.front().font;
			if (!s_Default && !io.Fonts->Fonts.empty())
				s_Default = io.Fonts->Fonts[0];

			// Make the proportional face the GLOBAL default. Previously the default
			// was ImGui's chunky bitmap font (ProggyClean), which is the main reason
			// the UI looked dated — every panel that didn't explicitly push a face
			// rendered in it. Now everything defaults to Roboto (+ merged icons).
			if (s_Default)
				io.FontDefault = s_Default;

			CS_CORE_INFO("Fonts: initialised ({0} custom face(s), icons {1})",
				s_Fonts.size(), s_HasIcons ? "on" : "off");
		}

		void Fonts::LoadProjectFonts()
		{
			// Project-mount rescan (Application::LoadProjectDLL). Requires an
			// initialised registry — Init always precedes any project mount.
			// Adding faces mid-run is safe with ImGui 1.92's dynamic atlas
			// (RendererHasTextures): glyphs bake on demand, no atlas rebuild —
			// but only call this OUTSIDE a frame (the engine calls it from the
			// Safe Zone, between EndFrame and the next NewFrame).
			if (!s_Initialized)
				return;

			LoadFolder(FileSystem::Resolve("project://fonts"));
		}

		ImFont* Fonts::Get(const std::string& name, float /*sizePx*/)
		{
			// Size is applied at draw time (AddText/PushFont with an explicit size);
			// selection is purely by name. Returns the default font if name unknown.
			for (const auto& e : s_Fonts)
				if (IEquals(e.name, name))
					return e.font;

			return s_Default;
		}

		ImFont* Fonts::Default()
		{
			return s_Default ? s_Default : ImGui::GetIO().FontDefault;
		}

		bool Fonts::Available()
		{
			return !s_Fonts.empty();
		}

		bool Fonts::HasIcons()
		{
			return s_HasIcons;
		}

		void Fonts::Push(const std::string& name, float sizePx)
		{
			ImFont* f = Get(name, sizePx);
			ImGui::PushFont(f ? f : Default(), sizePx > 0.0f ? sizePx : 0.0f);
		}

		void Fonts::Pop()
		{
			ImGui::PopFont();
		}
	}
}
