#pragma once

// ProjectManifest.h — project.cproj load/save (Phase 16 / S5).
//
// project.cproj is the flat TOML manifest the standalone PlayerLayer (E13) and
// Starforge read. Config is read-only by design, so writes rewrite the whole file
// from the known key set (v1 has no user-authored comments/extra keys to lose).
// The [window] table (title/width/height) + the icon key are what packaging (S5)
// stamps onto a shipped app.

#include <Cosmic.h>

#include <fstream>
#include <string>

namespace Starforge
{
    struct ProjectManifest
    {
        std::string Name;
        std::string StartupScene = "scenes/Main.cscene";
        std::string StartupFlow;          // U5 — "" => single-scene boot; else e.g. "flows/Main.cflow"
        int         FixedHz      = 60;
        std::string Icon;                 // "" => none; relative to the project root
        std::string WindowTitle;          // "" => use Name
        int         WindowWidth  = 0;     // 0 => keep the engine default
        int         WindowHeight = 0;
        bool        PixelArt     = false; // U3 — point-filter all textures (crisp pixel art)

        // Load from a resolvable VFS/disk path (e.g. "project://project.cproj").
        static ProjectManifest Load(const std::string& vfsOrDiskPath)
        {
            ProjectManifest m;
            if (auto cfg = Cosmic::Config::Load(vfsOrDiskPath))
            {
                m.Name         = cfg->GetString("name", m.Name);
                m.StartupScene = cfg->GetString("startup_scene", m.StartupScene);
                m.StartupFlow  = cfg->GetString("startup_flow", m.StartupFlow);
                m.FixedHz      = static_cast<int>(cfg->GetInt("fixed_dt_hz", m.FixedHz));
                m.Icon         = cfg->GetString("icon", m.Icon);
                m.WindowTitle  = cfg->GetString("window.title", cfg->GetString("window_title", m.WindowTitle));
                m.WindowWidth  = static_cast<int>(cfg->GetInt("window.width",  m.WindowWidth));
                m.WindowHeight = static_cast<int>(cfg->GetInt("window.height", m.WindowHeight));
                m.PixelArt     = cfg->GetBool("pixel_art", m.PixelArt);
            }
            return m;
        }

        // Rewrite the manifest at an absolute disk path.
        bool Save(const std::string& diskPath) const
        {
            std::ofstream f(diskPath, std::ios::trunc);
            if (!f) return false;
            f << "# Cosmic project manifest — " << Name << "\n";
            f << "# Consumed by the standalone PlayerLayer (E13) and Starforge.\n";
            f << "name          = \"" << Name << "\"\n";
            f << "startup_scene = \"" << StartupScene << "\"\n";
            if (!StartupFlow.empty())
                f << "startup_flow  = \"" << StartupFlow << "\"\n";
            f << "fixed_dt_hz   = " << FixedHz << "\n";
            if (!Icon.empty())
                f << "icon          = \"" << Icon << "\"\n";
            if (PixelArt)
                f << "pixel_art     = true\n";
            f << "\n[window]\n";
            f << "title  = \"" << (WindowTitle.empty() ? Name : WindowTitle) << "\"\n";
            if (WindowWidth  > 0) f << "width  = " << WindowWidth  << "\n";
            if (WindowHeight > 0) f << "height = " << WindowHeight << "\n";
            return true;
        }
    };
}
