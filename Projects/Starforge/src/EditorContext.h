#pragma once

// EditorContext.h
//
// ============================================================================
// Starforge — shared editor state (docs/plans/11-phase13-starforge-plan.md).
// ============================================================================
//
// One instance owned by StarforgeApp, handed to every panel by reference each
// frame. Panels NEVER cache engine pointers across frames; they read this. It
// is the single hub the Stage-B work orders grew the skeleton into:
//   E6  — open-project + scene path + dirty tracking + console.
//   E7  — the Cosmic::CommandStack; ALL mutations route through Commands.
//   E8  — multi-selection (last selected == primary), mirrored to the shared
//         EntitySelection bus so engine panels stay in sync.
// ============================================================================

#include <Cosmic.h>
#include <entt/entt.hpp>

#include "TelemetryRecording.h"

#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace Starforge
{
    enum class LogSeverity { Info, Warn, Error };

    struct ConsoleLine
    {
        std::string Text;
        LogSeverity Severity = LogSeverity::Info;
        std::string Timestamp;   // "HH:MM:SS" wall clock at push (H10)
    };

    struct EditorContext
    {
        // --- Project (E6) --------------------------------------------------
        bool        ProjectOpen = false;
        std::string ProjectName;            // VFS active-project folder (FileSystem)
        std::string ProjectTitle;           // human-readable, for the window title

        // --- Scene (E2/E6) -------------------------------------------------
        Cosmic::Ref<Cosmic::Scene> Scene;
        std::string SceneName    = "Untitled";  // display name (file stem)
        std::string SceneVfsPath;               // "project://scenes/Foo.cscene" ("" = never saved)
        bool        Dirty = false;

        // --- Undo/redo (E7) ------------------------------------------------
        Cosmic::CommandStack Commands;

        // --- Selection (E8) — multi; Selection.back() is the primary ------
        std::vector<entt::entity> Selection;

        // --- Console (E6) --------------------------------------------------
        std::vector<ConsoleLine> ConsoleLines;

        // --- Telemetry (E20) — reflected fields marked "Recorded" ----------
        // Keyed by UUID so marks survive the Play snapshot rebuild. Owned here so
        // both the Inspector (marking) and the TelemetryPanel (capture) share one
        // list. Script-pushed channels are NOT here — they're discovered at Play.
        std::vector<Telemetry::RecordedChannel> Recorded;

        // --- Cross-panel requests (consumed by the shell each frame) -------
        std::string PendingOpenScene;         // "project://..." set by the Content Browser
        std::string PendingInstantiatePrefab; // "project://...cprefab" (E14) — Content Browser

        // ===================================================================
        // Dirty tracking
        // ===================================================================
        void MarkDirty()  { Dirty = true; }
        void ClearDirty() { Dirty = false; }

        bool HasScene() const { return static_cast<bool>(Scene); }

        // ===================================================================
        // Selection helpers
        // ===================================================================
        entt::entity Primary() const { return Selection.empty() ? entt::null : Selection.back(); }

        Cosmic::Entity PrimaryEntity()
        {
            const entt::entity e = Primary();
            if (Scene && e != entt::null && Scene->GetRegistry().valid(e))
                return Cosmic::Entity(e, Scene.get());
            return {};
        }

        bool HasSelection() const { return !Selection.empty(); }

        bool IsSelected(entt::entity e) const
        {
            return std::find(Selection.begin(), Selection.end(), e) != Selection.end();
        }

        // Drop any handles the registry no longer considers valid (post-undo /
        // post-delete). Fires a bus sync only when something actually changed.
        void ValidateSelection()
        {
            if (!Scene) { if (!Selection.empty()) { Selection.clear(); SyncBus(); } return; }
            auto& reg = Scene->GetRegistry();
            const size_t before = Selection.size();
            Selection.erase(std::remove_if(Selection.begin(), Selection.end(),
                            [&](entt::entity e) { return !reg.valid(e); }),
                            Selection.end());
            if (Selection.size() != before)
                SyncBus();
        }

        void ClearSelection() { if (!Selection.empty()) { Selection.clear(); SyncBus(); } }

        void SelectOnly(Cosmic::Entity e)
        {
            Selection.clear();
            if (e) Selection.push_back(static_cast<entt::entity>(e));
            SyncBus();
        }

        void AddSelect(Cosmic::Entity e)
        {
            if (!e) return;
            const entt::entity h = static_cast<entt::entity>(e);
            if (!IsSelected(h)) Selection.push_back(h);
            SyncBus();
        }

        void ToggleSelect(Cosmic::Entity e)
        {
            if (!e) return;
            const entt::entity h = static_cast<entt::entity>(e);
            auto it = std::find(Selection.begin(), Selection.end(), h);
            if (it != Selection.end()) Selection.erase(it);
            else                       Selection.push_back(h);
            SyncBus();
        }

        // ===================================================================
        // Console
        // ===================================================================
        void Log(const std::string& text, LogSeverity sev = LogSeverity::Info)
        {
            char ts[16] = { 0 };
            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);

            ConsoleLines.push_back({ text, sev, ts });
            if (ConsoleLines.size() > 2000)
                ConsoleLines.erase(ConsoleLines.begin(), ConsoleLines.begin() + 400);
        }

        // Mirror the primary selection onto the shared bus so engine-side panels
        // (telemetry, etc.) track the editor's focus. Multi-select is a Starforge
        // concept; the bus only carries the primary.
        void SyncBus()
        {
            Cosmic::Entity p = PrimaryEntity();
            if (p)
            {
                const std::string name = p.HasComponent<Cosmic::TagComponent>()
                    ? p.GetComponent<Cosmic::TagComponent>().Tag : std::string("Entity");
                Cosmic::EntitySelection::Set(p, name);
            }
            else
            {
                Cosmic::EntitySelection::Clear();
            }
        }
    };
}
