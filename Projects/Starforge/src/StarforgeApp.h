#pragma once

// StarforgeApp.h
//
// ============================================================================
// Starforge — the Cosmic editor (root layer). Stage B (E6–E10) build-out.
// Plan: docs/plans/11-phase13-starforge-plan.md.
// ============================================================================
//
// Assemble Cosmic scenes visually, with undo/redo, a reflection-driven
// inspector, a CAD viewport (pick + gizmo + grid), and an asset browser. This
// layer is the shell: it owns the EditorContext hub, the editor camera, the
// panels, the viewport tools, the menus, project/scene open-save, and autosave.
// (Scripting + Play + packaging arrive in Stages C/D — E11+.)
// ============================================================================

#include <Cosmic.h>

#include "EditorContext.h"
#include "EditorPrefs.h"
#include "ViewportController.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"

#include <string>

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
        // --- Project / scene lifecycle (E6) --------------------------------
        void OpenProject(const std::string& name);
        void NewProject(const std::string& name);
        void CloseProject();
        void NewScene();
        void OpenScene(const std::string& vfsPath);
        bool SaveScene();                 // false if it needs a name (opens Save As)
        void SaveSceneToVfs(const std::string& vfsPath);
        void BuildSandboxScene();

        // --- Shell rendering -----------------------------------------------
        void ApplyDockLayout();
        void DrawTopBar();                // menus + tool strip (docked window)
        void DrawMenus();
        void DrawEntityMenu();
        void DrawHomescreen();
        void DrawSaveAsPopup();

        // --- Frame helpers -------------------------------------------------
        void HandleShortcuts();
        void Autosave(float ts);
        void UpdateWindowTitle();
        void RenderViewport(float ts);

        EditorContext m_Ctx;
        Cosmic::OrbitCameraController m_Camera{ 16.0f / 9.0f };
        ViewportController m_Viewport;

        HierarchyPanel      m_Hierarchy;
        InspectorPanel      m_Inspector;
        ContentBrowserPanel m_Content;
        ConsolePanel        m_Console;

        Prefs::EditorSettings m_Settings;

        bool  m_DockApplied   = false;
        bool  m_OpenSaveAs    = false;
        float m_AutosaveTimer = 0.0f;

        // View-menu panel toggles.
        bool m_ShowHierarchy = true, m_ShowInspector = true,
             m_ShowContent   = true, m_ShowConsole   = true;

        // Homescreen / dialogs scratch.
        char m_NewProjectName[128] = "MyProject";
        char m_SaveAsName[128]     = "Main";
    };
}
