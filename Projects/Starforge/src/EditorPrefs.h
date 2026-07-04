#pragma once

// EditorPrefs.h — editor preferences + recent-projects registry (E6).
//
// Small helpers over the writable user:// root. Reads use the engine TOML
// facade (Config); writes are hand-emitted TOML (Config is read-only by design).
// Recent projects live at user://starforge/projects.toml; prefs at
// user://starforge/editor.toml. Projects are folders under assets/projects/
// (the existing VFS model) — external-folder relocation is an E12/E19 concern.

#include <Cosmic.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Starforge::Prefs
{
    namespace fs = std::filesystem;

    struct EditorSettings
    {
        float AutosaveMinutes = 5.0f;
        float CameraSpeed     = 1.0f;
        bool  PlaygroundOffered = false;   // E21 — first-run sample already offered
    };

    inline std::string RecentPath() { return Cosmic::FileSystem::Resolve("user://starforge/projects.toml"); }
    inline std::string PrefsPath()  { return Cosmic::FileSystem::Resolve("user://starforge/editor.toml"); }

    inline void EnsureDir()
    {
        std::error_code ec;
        fs::create_directories(Cosmic::FileSystem::Resolve("user://starforge"), ec);
    }

    inline std::vector<std::string> LoadRecentProjects()
    {
        std::vector<std::string> out;
        std::error_code ec;
        if (!fs::exists(RecentPath(), ec)) return out;
        if (auto cfg = Cosmic::Config::Load("user://starforge/projects.toml"))
            for (const auto& t : cfg->GetTable("recent"))
            {
                std::string n = t->GetString("name", "");
                if (!n.empty()) out.push_back(n);
            }
        return out;
    }

    inline void SaveRecentProjects(const std::vector<std::string>& names)
    {
        EnsureDir();
        std::ofstream f(RecentPath(), std::ios::trunc);
        if (!f) return;
        f << "# Starforge recent projects (most recent first)\n";
        for (const auto& n : names)
            f << "[[recent]]\nname = \"" << n << "\"\n\n";
    }

    inline void AddRecentProject(const std::string& name)
    {
        if (name.empty()) return;
        auto list = LoadRecentProjects();
        list.erase(std::remove(list.begin(), list.end(), name), list.end());
        list.insert(list.begin(), name);
        if (list.size() > 12) list.resize(12);
        SaveRecentProjects(list);
    }

    inline EditorSettings LoadSettings()
    {
        EditorSettings s;
        std::error_code ec;
        if (!fs::exists(PrefsPath(), ec)) return s;
        if (auto cfg = Cosmic::Config::Load("user://starforge/editor.toml"))
        {
            s.AutosaveMinutes = cfg->GetFloat("autosave_minutes", s.AutosaveMinutes);
            s.CameraSpeed     = cfg->GetFloat("camera_speed", s.CameraSpeed);
            s.PlaygroundOffered = cfg->GetBool("playground_offered", s.PlaygroundOffered);
        }
        return s;
    }

    inline void SaveSettings(const EditorSettings& s)
    {
        EnsureDir();
        std::ofstream f(PrefsPath(), std::ios::trunc);
        if (!f) return;
        f << "# Starforge editor preferences\n";
        f << "autosave_minutes = " << s.AutosaveMinutes << "\n";
        f << "camera_speed = " << s.CameraSpeed << "\n";
        f << "playground_offered = " << (s.PlaygroundOffered ? "true" : "false") << "\n";
    }

    // Existing projects = subfolders of assets/projects/ that carry a project.cproj.
    inline std::vector<std::string> DiscoverProjects()
    {
        std::vector<std::string> out;
        std::error_code ec;
        const fs::path root = fs::path("assets") / "projects";
        if (!fs::exists(root, ec)) return out;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec))
                out.push_back(e.path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }
}
