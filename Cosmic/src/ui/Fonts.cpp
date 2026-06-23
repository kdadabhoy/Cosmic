// Fonts.cpp — see Fonts.h.

#include "ui/Fonts.h"
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

			bool IEquals(const std::string& a, const std::string& b)
			{
				if (a.size() != b.size()) return false;
				for (size_t i = 0; i < a.size(); ++i)
					if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
						return false;
				return true;
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

				ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), k_BaseSize);
				if (f)
				{
					s_Fonts.push_back({ name, f });
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

			// Register ImGui's built-in font FIRST so it stays the global default —
			// the first font added is what ImGui uses by default, and we don't want
			// loading custom faces to silently restyle every existing panel/project.
			// Custom faces are strictly opt-in via Get()/Push().
			io.Fonts->AddFontDefault();

			LoadFolder(FileSystem::Resolve("engine://fonts"));
			LoadFolder(FileSystem::Resolve("project://fonts")); // best-effort; usually empty at startup

			// Fallback font for overlay helpers that don't name a face: prefer a
			// regular custom face (nicer than the bitmap font), else the built-in.
			s_Default = Get("Roboto-Regular", k_BaseSize);
			if (!s_Default && !s_Fonts.empty())
				s_Default = s_Fonts.front().font;
			if (!s_Default && !io.Fonts->Fonts.empty())
				s_Default = io.Fonts->Fonts[0];

			CS_CORE_INFO("Fonts: initialised ({0} custom face(s))", s_Fonts.size());
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
