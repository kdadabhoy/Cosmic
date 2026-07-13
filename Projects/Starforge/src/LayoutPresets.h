#pragma once

// LayoutPresets.h — workspace layout presets (Phase 22 / K3, gap §1.2).
//
// Two kinds of preset behind one name list:
//
//   BUILT-INS (Level · Assets · Telemetry) are code-defined dock layouts:
//   panel-visibility set + engine dock-port bindings + edge ratios, then a
//   WorkspaceLayer::ResetLayout() re-runs the DockBuilder. Deterministic and
//   DPI-safe (no captured pixel sizes). Later phases add one per new document
//   editor (Animation, Graphs).
//
//   USER presets are runtime snapshots: ImGui::SaveIniSettingsToMemory() (the
//   full dock/window state) + the panel-visibility bools, stored as
//   user://starforge/layouts/<name>.ini with a small "# panels:" header line.
//   Restoring hands the ini blob back to ImGui::LoadIniSettingsFromMemory()
//   (vendored ImGui implements ApplyAllFn, so live re-dock works) — the shell
//   defers the load to the top of the next ImGui frame.
//
// Panels must keep stable window names — they are the ini keys (they do; the
// central viewport uses the "Title###Viewport" idiom, and ImGui stores its
// settings under the "###Viewport" suffix, so scene renames don't break it).
//
// The active preset persists PER PROJECT in user://starforge/layouts/active.toml.

#include <string>
#include <vector>

namespace Starforge
{
    // Pointers to the shell's View-menu visibility bools — a preset owns them.
    struct LayoutPanels
    {
        bool* Hierarchy    = nullptr;
        bool* Inspector    = nullptr;
        bool* Content      = nullptr;
        bool* Console      = nullptr;
        bool* Environment  = nullptr;
        bool* Material     = nullptr;
        bool* WorldSystems = nullptr;
        bool* Voxel        = nullptr;
        bool* TilePalette  = nullptr;
        bool* FlowGraph    = nullptr;
        bool* Telemetry    = nullptr;
        bool* Stats        = nullptr;
        bool* Profiler     = nullptr;   // T17
        bool* System       = nullptr;   // T18
        bool* Editors      = nullptr;   // M1 — asset-editor documents
    };

    class LayoutPresets
    {
    public:
        static const std::vector<std::string>& BuiltIns();   // { Level, Assets, Animation, Telemetry }
        static bool IsBuiltIn(const std::string& name);

        // Apply a built-in: writes the visibility set and re-binds the engine dock
        // ports (the shell's scaffold — top bar, chrome — is re-applied too), then
        // triggers the DockBuilder. Unknown names fall back to "Level".
        static void ApplyBuiltIn(const std::string& name, const LayoutPanels& panels);

        // User snapshots at user://starforge/layouts/<name>.ini.
        static std::vector<std::string> UserPresets();
        static bool SaveUser(const std::string& name, const LayoutPanels& panels);
        // On success fills `iniOut` with the ini blob for a DEFERRED
        // ImGui::LoadIniSettingsFromMemory (top of the next frame) and applies
        // the stored visibility set immediately.
        static bool LoadUser(const std::string& name, const LayoutPanels& panels,
                             std::string& iniOut);
        static bool DeleteUser(const std::string& name);

        // Active-preset persistence, keyed per project (absolute path, or the
        // name for legacy in-tree projects). "" => never saved (default Level).
        static std::string LoadActive(const std::string& projectKey);
        static void        SaveActive(const std::string& projectKey, const std::string& preset);
    };
}
