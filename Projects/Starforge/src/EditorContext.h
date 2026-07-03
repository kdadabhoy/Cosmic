#pragma once

// EditorContext.h
//
// ============================================================================
// Starforge — shared editor state (docs/plans/11-phase13-starforge-plan.md).
// ============================================================================
//
// One instance owned by StarforgeApp, handed to every panel by pointer each
// frame. Panels NEVER cache engine pointers across frames; they read this.
//
// SKELETON STATUS: holds the in-memory sandbox scene + a single-entity
// selection + a console line buffer. The work orders grow it:
//   TODO(E6):  open-project state (paths, project.cproj values, dirty tracking,
//              recent-projects registry) replaces the hard-coded sandbox.
//   TODO(E7):  a Cosmic::CommandStack lives here; ALL mutations route through it.
//   TODO(E8):  multi-selection via the EntitySelection bus replaces `Selected`.
//   TODO(E13): play-state machine (Edit / Play / Paused) + runtime scene handle.
// ============================================================================

#include <Cosmic.h>
#include <entt/entt.hpp>

#include <string>
#include <vector>

namespace Starforge
{
    struct EditorContext
    {
        // --- Scene ---------------------------------------------------------
        Cosmic::Ref<Cosmic::Scene> Scene;          // the open (edit-mode) scene
        std::string SceneName = "Sandbox";         // TODO(E2): becomes the .cscene path

        // --- Selection (single-entity skeleton; TODO(E8): EntitySelection bus)
        entt::entity Selected = entt::null;

        bool HasSelection() const
        {
            return Scene && Selected != entt::null && Scene->GetRegistry().valid(Selected);
        }

        // --- Console (skeleton ring buffer; TODO(E6): engine log sink feeds this)
        std::vector<std::string> ConsoleLines;

        void Log(const std::string& line)
        {
            ConsoleLines.push_back(line);
            if (ConsoleLines.size() > 1000)
                ConsoleLines.erase(ConsoleLines.begin(), ConsoleLines.begin() + 200);
        }
    };
}
