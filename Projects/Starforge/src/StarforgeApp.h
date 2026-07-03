#pragma once

// StarforgeApp.h
//
// ============================================================================
// Starforge — the Cosmic editor (root layer).
// Plan: docs/plans/11-phase13-starforge-plan.md (work orders E1–E21).
// ============================================================================
//
// Assemble Cosmic scenes visually, attach C++ simulation logic (hot-reloaded
// project DLLs), press Play, record telemetry, package a standalone app.
//
// SKELETON STATUS (written with the plan, 2026-07-03): boots a hard-coded
// sandbox scene into the workspace viewport with CAD orbit navigation and four
// live-but-minimal panels (Hierarchy / Inspector / Content Browser / Console).
// Every wiring point for the work orders carries a TODO(E#) marker:
//   TODO(E2):  File▸Save/Open — SceneSerializer replaces the sandbox scene.
//   TODO(E5):  scene loads route through SceneManager (async + transitions).
//   TODO(E6):  homescreen (recent projects) + project open/create + menus +
//              autosave; FileSystem::SetActiveProject(<open project>).
//   TODO(E7):  CommandStack; Ctrl+Z/Y.
//   TODO(E9):  ScenePicker click-select, Gizmo::Manipulate in the viewport
//              overlay, DebugDraw grid/axes, view modes, camera bookmarks.
//   TODO(E13): Play/Pause/Step toolbar + runtime scene.
// ============================================================================

#include <Cosmic.h>

#include "EditorContext.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"

namespace Starforge
{
    class StarforgeApp : public Cosmic::Layer
    {
    public:
        StarforgeApp();
        virtual ~StarforgeApp() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        void BuildSandboxScene();   // TODO(E6): replaced by project open/create
        void ApplyDockLayout();

        EditorContext m_Ctx;

        // Editor camera — CAD navigation (S5: MMB orbit-about-cursor, scroll-to-
        // cursor, ViewCube-ready). TODO(E9): Fly toggle + bookmarks + speed UI.
        Cosmic::OrbitCameraController m_Camera{ 16.0f / 9.0f };

        // Panels (each draws one dock-port-bound window).
        HierarchyPanel      m_Hierarchy;
        InspectorPanel      m_Inspector;
        ContentBrowserPanel m_Content;
        ConsolePanel        m_Console;

        bool m_DockApplied = false;
    };

} // namespace Starforge
