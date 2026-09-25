#pragma once

// EditorPrefs.h — editor preferences + project-library registry (E6 / S1).
//
// Small helpers over the writable user:// root. Reads use the engine TOML facade
// (Config); writes are hand-emitted TOML (Config is read-only by design).
//
// The project registry lives at user://starforge/projects.toml. Schema v2 (S1):
//   [[project]]
//   name        = "MyRover"
//   path        = "D:/Work/MyRover"   # absolute root; "" => legacy in-tree
//   last_opened = "2026-07-05"
//   pinned      = false
// Projects are now self-contained folders ANYWHERE on disk. Legacy in-tree
// projects (assets/projects/<name>) are kept as path="" entries so they resolve
// and build exactly as before (compat: ForgePlayground). A one-shot migration
// upgrades the old name-only [[recent]] schema (v1). Prefs live in editor.toml.

#include <Cosmic.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Starforge::Prefs
{
    namespace fs = std::filesystem;

    struct EditorSettings
    {
        // UX-02 (contract §2 "Preferences", Edit ▸ Preferences…). The key
        // `autosave_minutes` keeps its name; it is WHOLE minutes 1-60 now (a legacy
        // float is rounded and clamped; a legacy value <= 0 meant "off" and reads as
        // autosave_enabled = false when that key is absent).
        int   AutosaveMinutes = 5;
        bool  AutosaveEnabled = true;    // autosave_enabled — the timed copy (the pre-Play safety copy is always written)
        bool  PromptUnsaved   = true;    // prompt_unsaved — Save / Discard / Cancel before New/Open/Close/Exit
        float CameraSpeed     = 1.0f;
        bool  PlaygroundOffered = false;   // E21 — first-run sample already offered
        bool  AdoptSceneCamera  = true;    // H8 — on open, adopt a Primary camera's pose
        bool  AutoResumePlay    = true;    // AP-03 (§6) — resume Play after a live rebuild

        // K6 — per-operation snapping (the viewport strip chips).
        bool  SnapMoveOn   = false;
        bool  SnapRotateOn = false;
        bool  SnapScaleOn  = false;
        float SnapMove     = 0.25f;   // metres
        float SnapRotate   = 15.0f;   // degrees
        float SnapScale    = 0.1f;    // scale increment

        // T4/T7 — Content Browser layout.
        float CbTreeWidth  = 220.0f;  // left folder-tree pane width (px)
        float CbTileSize   = 84.0f;   // grid cell size (px)
        bool  CbShowPreview = true;   // T7 — bottom preview/metadata pane visible
    };

    // One project-library entry (S1).
    struct ProjectEntry
    {
        std::string Name;         // display name
        std::string Path;         // absolute root ("" => legacy in-tree assets/projects/<Name>)
        std::string LastOpened;   // "YYYY-MM-DD"
        bool        Pinned = false;

        bool IsLegacy() const { return Path.empty(); }
    };

    inline std::string RecentPath() { return Cosmic::FileSystem::Resolve("user://starforge/projects.toml"); }
    inline std::string PrefsPath()  { return Cosmic::FileSystem::Resolve("user://starforge/editor.toml"); }

    inline void EnsureDir()
    {
        std::error_code ec;
        fs::create_directories(Cosmic::FileSystem::Resolve("user://starforge"), ec);
    }

    inline std::string TodayString()
    {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char buf[16] = { 0 };
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return buf;
    }

    inline std::string TomlEscape(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '\\' || c == '"') out += '\\';
            out += c;
        }
        return out;
    }

    inline void SaveProjects(const std::vector<ProjectEntry>& list)
    {
        EnsureDir();
        std::ofstream f(RecentPath(), std::ios::trunc);
        if (!f) return;
        f << "# Starforge project library (most recent first). Managed by the editor.\n";
        f << "# Schema v2 (S1): [[project]] name/path/last_opened/pinned. path=\"\" => in-tree.\n\n";
        for (const auto& e : list)
        {
            f << "[[project]]\n";
            f << "name = \""        << TomlEscape(e.Name) << "\"\n";
            f << "path = \""        << TomlEscape(e.Path) << "\"\n";
            f << "last_opened = \"" << TomlEscape(e.LastOpened) << "\"\n";
            f << "pinned = "        << (e.Pinned ? "true" : "false") << "\n\n";
        }
    }

    // Load the registry, migrating the v1 name-only schema once. Legacy [[recent]]
    // names become path="" entries (they keep resolving to assets/projects/<name>,
    // so ForgePlayground stays unaffected); the editor's own "Starforge" pseudo-
    // project is dropped (it is the tool, not a user project).
    inline std::vector<ProjectEntry> LoadProjects()
    {
        std::vector<ProjectEntry> out;
        std::error_code ec;
        if (!fs::exists(RecentPath(), ec)) return out;

        auto cfg = Cosmic::Config::Load("user://starforge/projects.toml");
        if (!cfg) return out;

        for (const auto& t : cfg->GetTable("project"))
        {
            ProjectEntry e;
            e.Name       = t->GetString("name", "");
            e.Path       = t->GetString("path", "");
            e.LastOpened = t->GetString("last_opened", "");
            e.Pinned     = t->GetBool("pinned", false);
            if (!e.Name.empty()) out.push_back(std::move(e));
        }
        if (!out.empty()) return out;

        // v1 -> v2 migration.
        bool migrated = false;
        for (const auto& t : cfg->GetTable("recent"))
        {
            const std::string n = t->GetString("name", "");
            if (n.empty() || n == "Starforge") continue;
            ProjectEntry e; e.Name = n; e.Path = "";   // legacy in-tree
            out.push_back(std::move(e));
            migrated = true;
        }
        if (migrated)
        {
            SaveProjects(out);
            CS_INFO("Starforge: migrated {} recent project(s) to the v2 library schema.", out.size());
        }
        return out;
    }

    // Upsert a project to the front of the library with today's date, preserving its
    // pin. Identity: external projects match by absolute path; legacy projects by
    // name (with an empty path).
    inline void TouchProject(const std::string& name, const std::string& path)
    {
        if (name.empty()) return;
        auto list = LoadProjects();

        auto same = [&](const ProjectEntry& e)
        {
            if (!path.empty()) return e.Path == path;
            return e.Path.empty() && e.Name == name;
        };

        bool pinned = false;
        for (const auto& e : list) if (same(e)) { pinned = e.Pinned; break; }
        list.erase(std::remove_if(list.begin(), list.end(), same), list.end());

        ProjectEntry e; e.Name = name; e.Path = path; e.LastOpened = TodayString(); e.Pinned = pinned;
        list.insert(list.begin(), std::move(e));
        if (list.size() > 32) list.resize(32);
        SaveProjects(list);
    }

    inline void RemoveProject(const std::string& name, const std::string& path)
    {
        auto list = LoadProjects();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const ProjectEntry& e)
        {
            if (!path.empty()) return e.Path == path;
            return e.Path.empty() && e.Name == name;
        }), list.end());
        SaveProjects(list);
    }

    inline void SetProjectPinned(const std::string& name, const std::string& path, bool pinned)
    {
        auto list = LoadProjects();
        for (auto& e : list)
        {
            const bool match = !path.empty() ? (e.Path == path) : (e.Path.empty() && e.Name == name);
            if (match) { e.Pinned = pinned; break; }
        }
        SaveProjects(list);
    }

    // UX-02 — the autosave interval rule: whole minutes, 1-60 (default 5).
    inline int ClampAutosaveMinutes(double v)
    {
        if (!(v == v)) return 5;                                  // NaN
        const long long r = (long long)std::llround(std::clamp(v, -1.0e6, 1.0e6));
        return (int)std::clamp<long long>(r, 1, 60);
    }

    // UX-02 — the path-taking halves (a test never touches the real editor.toml).
    // Absent file => defaults. Reads the same keys the editor always wrote.
    inline EditorSettings LoadSettingsFrom(const std::string& diskPath)
    {
        EditorSettings s;
        std::error_code ec;
        if (diskPath.empty() || !fs::exists(diskPath, ec)) return s;
        std::string text;
        {
            std::ifstream in(diskPath, std::ios::binary);
            if (!in) return s;
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (auto cfg = Cosmic::Config::Parse(text, diskPath))
        {
            if (cfg->Has("autosave_minutes"))
            {
                const double legacy = cfg->GetDouble("autosave_minutes", 5.0);   // int or legacy float
                s.AutosaveMinutes = ClampAutosaveMinutes(legacy > 0.0 ? legacy : 5.0);
                if (legacy <= 0.0 && !cfg->Has("autosave_enabled"))
                    s.AutosaveEnabled = false;                    // the old "0 = off" meaning
            }
            s.AutosaveEnabled = cfg->GetBool("autosave_enabled", s.AutosaveEnabled);
            s.PromptUnsaved   = cfg->GetBool("prompt_unsaved", s.PromptUnsaved);
            s.CameraSpeed     = cfg->GetFloat("camera_speed", s.CameraSpeed);
            s.PlaygroundOffered = cfg->GetBool("playground_offered", s.PlaygroundOffered);
            s.AdoptSceneCamera  = cfg->GetBool("adopt_scene_camera", s.AdoptSceneCamera);
            s.AutoResumePlay    = cfg->GetBool("auto_resume_play", s.AutoResumePlay);
            s.SnapMoveOn   = cfg->GetBool("snap_move_on",   s.SnapMoveOn);
            s.SnapRotateOn = cfg->GetBool("snap_rotate_on", s.SnapRotateOn);
            s.SnapScaleOn  = cfg->GetBool("snap_scale_on",  s.SnapScaleOn);
            s.SnapMove     = cfg->GetFloat("snap_move",   s.SnapMove);
            s.SnapRotate   = cfg->GetFloat("snap_rotate", s.SnapRotate);
            s.SnapScale    = cfg->GetFloat("snap_scale",  s.SnapScale);
            s.CbTreeWidth  = cfg->GetFloat("cb_tree_width", s.CbTreeWidth);
            s.CbTileSize   = cfg->GetFloat("cb_tile_size",  s.CbTileSize);
            s.CbShowPreview = cfg->GetBool("cb_show_preview", s.CbShowPreview);
        }
        return s;
    }

    inline EditorSettings LoadSettings() { return LoadSettingsFrom(PrefsPath()); }

    inline bool SaveSettingsTo(const EditorSettings& s, const std::string& diskPath)
    {
        std::error_code ec;
        const fs::path p(diskPath);
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        std::ofstream f(diskPath, std::ios::trunc);
        if (!f) return false;
        f << "# Starforge editor preferences\n";
        f << "autosave_minutes = " << ClampAutosaveMinutes(s.AutosaveMinutes) << "\n";   // whole minutes, 1-60
        f << "autosave_enabled = " << (s.AutosaveEnabled ? "true" : "false") << "\n";
        f << "prompt_unsaved = "   << (s.PromptUnsaved   ? "true" : "false") << "\n";
        f << "camera_speed = " << s.CameraSpeed << "\n";
        f << "playground_offered = " << (s.PlaygroundOffered ? "true" : "false") << "\n";
        f << "adopt_scene_camera = " << (s.AdoptSceneCamera ? "true" : "false") << "\n";
        f << "auto_resume_play = " << (s.AutoResumePlay ? "true" : "false") << "\n";
        f << "snap_move_on = "   << (s.SnapMoveOn   ? "true" : "false") << "\n";
        f << "snap_rotate_on = " << (s.SnapRotateOn ? "true" : "false") << "\n";
        f << "snap_scale_on = "  << (s.SnapScaleOn  ? "true" : "false") << "\n";
        f << "snap_move = "   << s.SnapMove   << "\n";
        f << "snap_rotate = " << s.SnapRotate << "\n";
        f << "snap_scale = "  << s.SnapScale  << "\n";
        f << "cb_tree_width = "  << s.CbTreeWidth << "\n";
        f << "cb_tile_size = "   << s.CbTileSize  << "\n";
        f << "cb_show_preview = " << (s.CbShowPreview ? "true" : "false") << "\n";
        return (bool)f;
    }

    inline void SaveSettings(const EditorSettings& s)
    {
        EnsureDir();
        SaveSettingsTo(s, PrefsPath());
    }

    // Legacy in-tree discovery = subfolders of assets/projects/ that carry a
    // project.cproj. Kept for the homescreen's "found on disk but not in the
    // library yet" case.
    inline std::vector<std::string> DiscoverProjects()
    {
        std::vector<std::string> out;
        std::error_code ec;
        const fs::path root = fs::path("assets") / "projects";
        if (!fs::exists(root, ec)) return out;
        for (const auto& e : fs::directory_iterator(root, ec))
            if (e.is_directory(ec) && fs::exists(e.path() / "project.cproj", ec))
                out.push_back(e.path().filename().string());
        std::sort(out.begin(), out.end());
        return out;
    }
}
