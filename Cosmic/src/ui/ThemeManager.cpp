// ThemeManager.cpp — see ThemeManager.h.

#include "ui/ThemeManager.h"
#include "ui/PlotStyle.h"
#include "layers/ImGuiThemes.h"   // built-in BuildXxx() definitions
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Cosmic
{
	namespace
	{
		// Single shared registry — lives in the engine DLL.
		std::vector<Theme> s_Themes;
		std::string        s_CurrentName;
		ImVec4             s_Accent = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
		bool               s_Initialized = false;

		// --- small text-format helpers -------------------------------------

		std::string Vec4ToStr(const ImVec4& v)
		{
			std::ostringstream os;
			os << v.x << ',' << v.y << ',' << v.z << ',' << v.w;
			return os.str();
		}

		bool StrToVec4(const std::string& s, ImVec4& out)
		{
			std::istringstream is(s);
			char comma;
			ImVec4 v;
			if (!(is >> v.x >> comma >> v.y >> comma >> v.z >> comma >> v.w))
				return false;
			out = v;
			return true;
		}

		// Apply a Theme to the live ImGui style. ImPlot styling is synced
		// separately (see PlotStyle, wired into Apply()).
		void ApplyToImGui(const Theme& t)
		{
			ImGuiStyle& style = ImGui::GetStyle();

			style.WindowRounding    = t.style.WindowRounding;
			style.ChildRounding     = t.style.ChildRounding;
			style.FrameRounding     = t.style.FrameRounding;
			style.PopupRounding     = t.style.PopupRounding;
			style.ScrollbarRounding = t.style.ScrollbarRounding;
			style.GrabRounding      = t.style.GrabRounding;
			style.TabRounding       = t.style.TabRounding;

			style.WindowBorderSize  = t.style.WindowBorderSize;
			style.FrameBorderSize   = t.style.FrameBorderSize;
			style.ChildBorderSize   = t.style.ChildBorderSize;

			style.ScrollbarSize     = t.style.ScrollbarSize;
			style.GrabMinSize       = t.style.GrabMinSize;

			style.WindowPadding     = t.style.WindowPadding;
			style.FramePadding      = t.style.FramePadding;
			style.ItemSpacing       = t.style.ItemSpacing;
			style.ItemInnerSpacing  = t.style.ItemInnerSpacing;

			for (int i = 0; i < ImGuiCol_COUNT; ++i)
				style.Colors[i] = t.colors[i];
		}
	}

	// =========================================================================
	// Lifecycle / registration
	// =========================================================================

	void ThemeManager::Init()
	{
		if (s_Initialized) return;
		s_Initialized = true;

		for (Theme& t : GetBuiltInThemes())
			Register(t);

		// User-authored themes (saved by the in-app editor) live alongside the
		// active project's assets. Best-effort; the folder may not exist yet.
		LoadFolder(FileSystem::Resolve("project://themes"));

		CS_CORE_INFO("ThemeManager: initialised ({0} theme(s))", s_Themes.size());
	}

	void ThemeManager::Register(const Theme& theme)
	{
		for (Theme& existing : s_Themes)
		{
			if (existing.name == theme.name)
			{
				existing = theme; // replace in place, keep ordering
				return;
			}
		}
		s_Themes.push_back(theme);
	}

	const std::vector<Theme>& ThemeManager::All() { return s_Themes; }

	const Theme* ThemeManager::Find(const std::string& name)
	{
		for (const Theme& t : s_Themes)
			if (t.name == name)
				return &t;
		return nullptr;
	}

	const std::string& ThemeManager::CurrentName() { return s_CurrentName; }
	const ImVec4&      ThemeManager::Accent()      { return s_Accent; }

	// =========================================================================
	// Apply
	// =========================================================================

	bool ThemeManager::Apply(const std::string& name)
	{
		const Theme* t = Find(name);
		if (!t)
		{
			CS_CORE_WARN("ThemeManager: theme '{0}' not found", name);
			return false;
		}
		ApplyTheme(*t);
		return true;
	}

	void ThemeManager::ApplyTheme(const Theme& t)
	{
		ApplyToImGui(t);
		s_CurrentName = t.name;
		s_Accent      = t.accent;

		// Keep charts in sync with the active theme (no-op if ImPlot isn't up yet).
		UI::ApplyPlotStyle(t);
	}

	// =========================================================================
	// Capture current live style -> Theme (for the editor's "save as")
	// =========================================================================

	Theme ThemeManager::CaptureCurrentStyle(const std::string& name)
	{
		Theme t;
		t.name    = name;
		t.builtIn = false;
		t.accent  = s_Accent;

		const ImGuiStyle& style = ImGui::GetStyle();
		t.style.WindowRounding    = style.WindowRounding;
		t.style.ChildRounding     = style.ChildRounding;
		t.style.FrameRounding     = style.FrameRounding;
		t.style.PopupRounding     = style.PopupRounding;
		t.style.ScrollbarRounding = style.ScrollbarRounding;
		t.style.GrabRounding      = style.GrabRounding;
		t.style.TabRounding       = style.TabRounding;
		t.style.WindowBorderSize  = style.WindowBorderSize;
		t.style.FrameBorderSize   = style.FrameBorderSize;
		t.style.ChildBorderSize   = style.ChildBorderSize;
		t.style.ScrollbarSize     = style.ScrollbarSize;
		t.style.GrabMinSize       = style.GrabMinSize;
		t.style.WindowPadding     = style.WindowPadding;
		t.style.FramePadding      = style.FramePadding;
		t.style.ItemSpacing       = style.ItemSpacing;
		t.style.ItemInnerSpacing  = style.ItemInnerSpacing;

		for (int i = 0; i < ImGuiCol_COUNT; ++i)
			t.colors[i] = style.Colors[i];

		return t;
	}

	// =========================================================================
	// Persistence (.ctheme — simple key=value text)
	// =========================================================================

	bool ThemeManager::SaveToFile(const Theme& theme, const std::string& resolvedPath)
	{
		std::error_code ec;
		std::filesystem::create_directories(
			std::filesystem::path(resolvedPath).parent_path(), ec);

		std::ofstream f(resolvedPath, std::ios::trunc);
		if (!f.is_open())
		{
			CS_CORE_WARN("ThemeManager: could not write '{0}'", resolvedPath);
			return false;
		}

		f << "# Cosmic theme\n";
		f << "name=" << theme.name << '\n';
		f << "accent=" << Vec4ToStr(theme.accent) << '\n';

		const ThemeStyle& s = theme.style;
		f << "style.WindowRounding="    << s.WindowRounding    << '\n';
		f << "style.ChildRounding="     << s.ChildRounding     << '\n';
		f << "style.FrameRounding="     << s.FrameRounding     << '\n';
		f << "style.PopupRounding="     << s.PopupRounding     << '\n';
		f << "style.ScrollbarRounding=" << s.ScrollbarRounding << '\n';
		f << "style.GrabRounding="      << s.GrabRounding      << '\n';
		f << "style.TabRounding="       << s.TabRounding       << '\n';
		f << "style.WindowBorderSize="  << s.WindowBorderSize  << '\n';
		f << "style.FrameBorderSize="   << s.FrameBorderSize   << '\n';
		f << "style.ChildBorderSize="   << s.ChildBorderSize   << '\n';
		f << "style.ScrollbarSize="     << s.ScrollbarSize     << '\n';
		f << "style.GrabMinSize="       << s.GrabMinSize       << '\n';
		f << "style.WindowPadding="     << s.WindowPadding.x   << ',' << s.WindowPadding.y    << '\n';
		f << "style.FramePadding="      << s.FramePadding.x    << ',' << s.FramePadding.y     << '\n';
		f << "style.ItemSpacing="       << s.ItemSpacing.x     << ',' << s.ItemSpacing.y      << '\n';
		f << "style.ItemInnerSpacing="  << s.ItemInnerSpacing.x<< ',' << s.ItemInnerSpacing.y << '\n';

		for (int i = 0; i < ImGuiCol_COUNT; ++i)
			f << "col." << ImGui::GetStyleColorName(i) << '=' << Vec4ToStr(theme.colors[i]) << '\n';

		return true;
	}

	bool ThemeManager::LoadFromFile(const std::string& resolvedPath, Theme& out)
	{
		std::ifstream f(resolvedPath);
		if (!f.is_open())
			return false;

		// Start from a complete dark table so any colour not present in the file
		// still has a sensible value.
		SeedDark(out);
		out.builtIn = false;

		std::string line;
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == '#') continue;
			const size_t eq = line.find('=');
			if (eq == std::string::npos) continue;

			const std::string key = line.substr(0, eq);
			const std::string val = line.substr(eq + 1);

			if (key == "name")        { out.name = val; continue; }
			if (key == "accent")      { StrToVec4(val, out.accent); continue; }

			if (key.rfind("style.", 0) == 0)
			{
				const std::string sk = key.substr(6);
				ThemeStyle& s = out.style;
				auto f2 = [&](float& dst) { dst = std::stof(val); };
				auto v2 = [&](ImVec2& dst) {
					std::istringstream is(val); char c; is >> dst.x >> c >> dst.y;
				};
				if      (sk == "WindowRounding")    f2(s.WindowRounding);
				else if (sk == "ChildRounding")     f2(s.ChildRounding);
				else if (sk == "FrameRounding")     f2(s.FrameRounding);
				else if (sk == "PopupRounding")     f2(s.PopupRounding);
				else if (sk == "ScrollbarRounding") f2(s.ScrollbarRounding);
				else if (sk == "GrabRounding")      f2(s.GrabRounding);
				else if (sk == "TabRounding")       f2(s.TabRounding);
				else if (sk == "WindowBorderSize")  f2(s.WindowBorderSize);
				else if (sk == "FrameBorderSize")   f2(s.FrameBorderSize);
				else if (sk == "ChildBorderSize")   f2(s.ChildBorderSize);
				else if (sk == "ScrollbarSize")     f2(s.ScrollbarSize);
				else if (sk == "GrabMinSize")       f2(s.GrabMinSize);
				else if (sk == "WindowPadding")     v2(s.WindowPadding);
				else if (sk == "FramePadding")      v2(s.FramePadding);
				else if (sk == "ItemSpacing")       v2(s.ItemSpacing);
				else if (sk == "ItemInnerSpacing")  v2(s.ItemInnerSpacing);
				continue;
			}

			if (key.rfind("col.", 0) == 0)
			{
				const std::string cname = key.substr(4);
				for (int i = 0; i < ImGuiCol_COUNT; ++i)
				{
					if (cname == ImGui::GetStyleColorName(i))
					{
						StrToVec4(val, out.colors[i]);
						break;
					}
				}
				continue;
			}
		}

		if (out.name.empty())
			out.name = std::filesystem::path(resolvedPath).stem().string();

		return true;
	}

	void ThemeManager::LoadFolder(const std::string& resolvedDir)
	{
		namespace fs = std::filesystem;
		std::error_code ec;
		if (!fs::exists(resolvedDir, ec) || !fs::is_directory(resolvedDir, ec))
			return;

		for (const auto& de : fs::directory_iterator(resolvedDir, ec))
		{
			if (ec || !de.is_regular_file()) continue;

			std::string ext = de.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(),
				[](unsigned char c) { return (char)std::tolower(c); });
			if (ext != ".ctheme") continue;

			Theme t;
			if (LoadFromFile(de.path().string(), t))
			{
				Register(t);
				CS_CORE_INFO("ThemeManager: loaded user theme '{0}'", t.name);
			}
		}
	}
}
