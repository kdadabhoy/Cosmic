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
#include "PreviewRig.h"

#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdio>

namespace Starforge
{
    enum class LogSeverity { Info, Warn, Error };

    // T16 — where a console line came from (source chips). Engine = the engine
    // core logger (COSMIC); Game = the client/app logger (scripts at Play);
    // Editor = the editor's own ctx.Log messages.
    enum class LogSource { Engine, Editor, Game };

    struct ConsoleLine
    {
        std::string Text;
        LogSeverity Severity = LogSeverity::Info;
        std::string Timestamp;   // "HH:MM:SS" wall clock at push (H10)
        LogSource   Source = LogSource::Editor;
    };

    struct EditorContext
    {
        // --- Project (E6 / S1) ---------------------------------------------
        bool        ProjectOpen = false;
        std::string ProjectName;            // VFS active-project folder (FileSystem)
        std::string ProjectTitle;           // human-readable, for the window title
        // Absolute root of a self-contained external project folder (S1). Empty for
        // a legacy in-tree project (assets/projects/<ProjectName>); when set, the
        // FileSystem is in project:// PATH mode and build/ + .starforge/ live here.
        std::string ProjectPath;

        // --- Scene (E2/E6) -------------------------------------------------
        Cosmic::Ref<Cosmic::Scene> Scene;
        std::string SceneName    = "Untitled";  // display name (file stem)
        std::string SceneVfsPath;               // "project://scenes/Foo.cscene" ("" = never saved)
        bool        Dirty = false;

        // --- Play state (T15) — the shell mirrors its PlayMode here each frame so
        //     panels can react (live-value tint) without reaching into StarforgeApp.
        //     While playing, field edits are transient (they die with the Stop
        //     snapshot-restore) so they apply live but push NO undo entries.
        bool        Playing = false;

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
        std::string PendingImportModel;       // (T8) a model DISK path → the shell opens the E16 import modal seeded with it
        std::vector<std::string> PendingDroppedFiles; // (T8) OS file-drop paths → the Content Browser imports them
        std::string PendingRevealAsset;       // (T11) a "project://..." asset the Content Browser should reveal + select

        // --- Shared preview service (Phase 20 / A4, gap analysis §14.3) ----
        // The batch-thumbnail rig: Content Browser tiles (and Phase 23 asset
        // slots) request thumbnails here; the shell pumps it once per frame.
        // Interactive per-document rigs are separate PreviewRig instances
        // (the Material Editor owns one).
        PreviewRig Preview;

        // --- Voxel brush (Phase 18 / V4) — shared by the Voxels panel + the
        //     viewport brush. When Editing is on and the primary selection is a
        //     VoxelVolume, LMB places ActiveBlock / RMB breaks (raycast the grid).
        struct VoxelBrushState
        {
            bool     Editing     = false;   // brush active (LMB/RMB edit instead of pick)
            uint16_t ActiveBlock = 1;       // block id painted by LMB
            int      Stroke      = 0;       // increments per mouse-down (undo coalesce key)
            float    Reach       = 64.0f;   // max edit ray distance (world metres)
        } VoxelBrush;

        // --- Tile brush (Phase 17 / U4) — shared by the Tile Palette panel +
        //     the 2D-mode viewport painter. When Editing is on, 2D mode is
        //     active, and the primary selection has a Tilemap: LMB applies the
        //     Tool with Tile / RMB erases. A drag coalesces into ONE undo step
        //     per stroke (mouse-down → up).
        struct TileBrushState
        {
            enum class ToolKind : int { Paint = 0, Flood = 1, Rect = 2 };

            bool     Editing = false;
            uint16_t Tile    = 1;           // 1-based atlas tile painted by LMB
            ToolKind Tool    = ToolKind::Paint;
            int      Stroke  = 0;           // increments per mouse-down (undo coalesce key)

            // Rect-tool drag state (viewport-owned; cell anchor of the press).
            bool       RectDragging = false;
            glm::ivec2 RectAnchor{ 0, 0 };
        } TileBrush;

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
        void Log(const std::string& text, LogSeverity sev = LogSeverity::Info,
                 LogSource src = LogSource::Editor)
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

            ConsoleLines.push_back({ text, sev, ts, src });
            // Ring-buffer cap (T16) — long sessions stay bounded/responsive.
            if (ConsoleLines.size() > 4000)
                ConsoleLines.erase(ConsoleLines.begin(), ConsoleLines.begin() + 800);
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
