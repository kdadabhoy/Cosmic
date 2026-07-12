// LayoutPresets.cpp — workspace layout presets (K3). See header.

#include "LayoutPresets.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string LayoutDir()
        {
            return Cosmic::FileSystem::Resolve("user://starforge/layouts");
        }

        std::string UserPresetPath(const std::string& name)
        {
            return LayoutDir() + "/" + name + ".ini";
        }

        // A preset name doubles as a filename — keep it filesystem-safe.
        bool NameIsSafe(const std::string& name)
        {
            if (name.empty() || name.size() > 48) return false;
            for (char c : name)
                if (!(std::isalnum(static_cast<unsigned char>(c)) ||
                      c == ' ' || c == '-' || c == '_'))
                    return false;
            return true;
        }

        void SetAll(const LayoutPanels& p, bool hierarchy, bool inspector, bool content,
                    bool console, bool environment, bool material, bool worldSystems,
                    bool voxel, bool tilePalette, bool flowGraph, bool telemetry, bool stats)
        {
            auto set = [](bool* b, bool v) { if (b) *b = v; };
            set(p.Hierarchy, hierarchy);   set(p.Inspector, inspector);
            set(p.Content, content);       set(p.Console, console);
            set(p.Environment, environment); set(p.Material, material);
            set(p.WorldSystems, worldSystems); set(p.Voxel, voxel);
            set(p.TilePalette, tilePalette);   set(p.FlowGraph, flowGraph);
            set(p.Telemetry, telemetry);       set(p.Stats, stats);
        }

        // The shared shell scaffold every built-in keeps: the Starforge top bar
        // (menu + toolbar rows, tab-less) with its pixel-minimum edge.
        void ScaffoldTopBar(Cosmic::WorkspaceLayer* ws)
        {
            ws->SetEdgeMinPixels(/*top*/ 78.0f, /*bottom*/ 0.0f, /*left*/ 0.0f, /*right*/ 0.0f);
            ws->DockWindow("Starforge", Cosmic::DockPort::TopCenter, Cosmic::DockFlags::NoTabBar);
        }
    }

    const std::vector<std::string>& LayoutPresets::BuiltIns()
    {
        static const std::vector<std::string> names = { "Level", "Assets", "Telemetry" };
        return names;
    }

    bool LayoutPresets::IsBuiltIn(const std::string& name)
    {
        for (const auto& n : BuiltIns())
            if (n == name)
                return true;
        return false;
    }

    void LayoutPresets::ApplyBuiltIn(const std::string& name, const LayoutPanels& p)
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws)
            return;

        ws->ClearDockWindows();
        ScaffoldTopBar(ws);

        if (name == "Assets")
        {
            // Browser-forward: a tall bottom dock for the Content Browser, the
            // Material Editor tabbed with the Inspector for assignment work.
            SetAll(p, true, true, true, true, false, true, false, false, false, false, false, false);
            ws->SetEdgeRatios(0.17f, 0.24f, 0.08f, 0.42f);
            ws->DockWindow("Hierarchy",       Cosmic::DockPort::LeftTop);
            ws->DockWindow("Inspector",       Cosmic::DockPort::RightTop);
            ws->DockWindow("Material Editor", Cosmic::DockPort::RightTop);   // tab
            ws->DockWindow("Content Browser", Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Console",         Cosmic::DockPort::BottomRight);
        }
        else if (name == "Telemetry")
        {
            // Scope-forward: the Telemetry panel owns a tall bottom dock.
            SetAll(p, true, true, false, true, false, false, false, false, false, false, true, false);
            ws->SetEdgeRatios(0.17f, 0.22f, 0.08f, 0.40f);
            ws->DockWindow("Hierarchy", Cosmic::DockPort::LeftTop);
            ws->DockWindow("Inspector", Cosmic::DockPort::RightTop);
            ws->DockWindow("Telemetry", Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Console",   Cosmic::DockPort::BottomRight);
        }
        else   // "Level" — the classic editing layout (the coded default).
        {
            SetAll(p, true, true, true, true, false, false, false, false, false, false, false, false);
            ws->SetEdgeRatios(0.19f, 0.22f, 0.08f, 0.26f);
            ws->DockWindow("Hierarchy",       Cosmic::DockPort::LeftTop);
            ws->DockWindow("Inspector",       Cosmic::DockPort::RightTop);
            ws->DockWindow("Content Browser", Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Console",         Cosmic::DockPort::BottomRight);
        }

        ws->ResetLayout();
    }

    std::vector<std::string> LayoutPresets::UserPresets()
    {
        std::vector<std::string> out;
        std::error_code ec;
        const fs::path dir = LayoutDir();
        if (!fs::exists(dir, ec)) return out;
        for (const auto& e : fs::directory_iterator(dir, ec))
        {
            if (!e.is_regular_file(ec) || e.path().extension() != ".ini") continue;
            const std::string stem = e.path().stem().string();
            if (stem == "active") continue;   // the active-preset map, not a preset
            out.push_back(stem);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    bool LayoutPresets::SaveUser(const std::string& name, const LayoutPanels& p)
    {
        if (!NameIsSafe(name))
            return false;

        std::error_code ec;
        fs::create_directories(LayoutDir(), ec);
        std::ofstream f(UserPresetPath(name), std::ios::trunc);
        if (!f)
            return false;

        auto v = [](const bool* b) { return (b && *b) ? 1 : 0; };
        f << "# starforge-layout v1\n";
        f << "# panels:"
          << " hierarchy="    << v(p.Hierarchy)
          << " inspector="    << v(p.Inspector)
          << " content="      << v(p.Content)
          << " console="      << v(p.Console)
          << " environment="  << v(p.Environment)
          << " material="     << v(p.Material)
          << " worldsystems=" << v(p.WorldSystems)
          << " voxel="        << v(p.Voxel)
          << " tilepalette="  << v(p.TilePalette)
          << " flowgraph="    << v(p.FlowGraph)
          << " telemetry="    << v(p.Telemetry)
          << " stats="        << v(p.Stats) << "\n";

        size_t iniSize = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&iniSize);
        f.write(ini, static_cast<std::streamsize>(iniSize));
        return true;
    }

    bool LayoutPresets::LoadUser(const std::string& name, const LayoutPanels& p,
                                 std::string& iniOut)
    {
        std::ifstream f(UserPresetPath(name));
        if (!f)
            return false;

        std::stringstream ini;
        std::string line;
        bool inHeader = true;
        while (std::getline(f, line))
        {
            if (inHeader && !line.empty() && line[0] == '#')
            {
                const size_t at = line.find("panels:");
                if (at != std::string::npos)
                {
                    auto flag = [&](const char* key, bool* out)
                    {
                        if (!out) return;
                        const std::string k = std::string(key) + "=";
                        const size_t pos = line.find(k, at);
                        if (pos != std::string::npos && pos + k.size() < line.size())
                            *out = line[pos + k.size()] == '1';
                    };
                    flag("hierarchy",     p.Hierarchy);
                    flag("inspector",     p.Inspector);
                    flag("content",       p.Content);
                    flag("console",       p.Console);
                    flag("environment",   p.Environment);
                    flag("material",      p.Material);
                    flag("worldsystems",  p.WorldSystems);
                    flag("voxel",         p.Voxel);
                    flag("tilepalette",   p.TilePalette);
                    flag("flowgraph",     p.FlowGraph);
                    flag("telemetry",     p.Telemetry);
                    flag("stats",         p.Stats);
                }
                continue;
            }
            inHeader = false;
            ini << line << "\n";
        }

        iniOut = ini.str();
        return !iniOut.empty();
    }

    bool LayoutPresets::DeleteUser(const std::string& name)
    {
        std::error_code ec;
        return fs::remove(UserPresetPath(name), ec);
    }

    // ---- active-preset persistence (per project key) -----------------------

    std::string LayoutPresets::LoadActive(const std::string& projectKey)
    {
        if (projectKey.empty()) return {};
        if (auto cfg = Cosmic::Config::Load("user://starforge/layouts/active.toml"))
        {
            for (const auto& t : cfg->GetTable("active"))
                if (t->GetString("project", "") == projectKey)
                    return t->GetString("preset", "");
        }
        return {};
    }

    void LayoutPresets::SaveActive(const std::string& projectKey, const std::string& preset)
    {
        if (projectKey.empty()) return;

        // Read-modify-write the whole map (Config is read-only by design).
        std::vector<std::pair<std::string, std::string>> entries;
        if (auto cfg = Cosmic::Config::Load("user://starforge/layouts/active.toml"))
        {
            for (const auto& t : cfg->GetTable("active"))
            {
                const std::string k = t->GetString("project", "");
                if (!k.empty() && k != projectKey)
                    entries.emplace_back(k, t->GetString("preset", ""));
            }
        }
        entries.emplace_back(projectKey, preset);

        std::error_code ec;
        fs::create_directories(LayoutDir(), ec);
        std::ofstream f(LayoutDir() + "/active.toml", std::ios::trunc);
        if (!f) return;
        f << "# Active workspace layout per project (K3). Managed by the editor.\n\n";
        auto esc = [](const std::string& s)
        {
            std::string out;
            for (char c : s) { if (c == '\\' || c == '"') out += '\\'; out += c; }
            return out;
        };
        for (const auto& [k, v] : entries)
        {
            f << "[[active]]\n";
            f << "project = \"" << esc(k) << "\"\n";
            f << "preset  = \"" << esc(v) << "\"\n\n";
        }
    }
}
