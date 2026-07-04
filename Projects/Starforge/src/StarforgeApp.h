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
#include "GameModule.h"
#include "BuildRunner.h"
#include "panels/HierarchyPanel.h"
#include "panels/InspectorPanel.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/EnvironmentPanel.h"
#include "panels/MaterialEditorPanel.h"
#include "panels/WorldSystemsPanel.h"

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
        bool ScaffoldProject(const std::string& name);   // E12 — from templates/
        void CloseProject();

        // --- Game module build & hot reload (E12) --------------------------
        void        BuildScripts();                      // Ctrl+B — background cmake
        void        ReloadModule(const std::string& dllStem);
        void        DrawBuildControls();
        bool        ProjectIsScaffolded() const;         // has a src/ + CMakeLists.txt
        std::string ProjectDir() const;                  // absolute assets/projects/<name>
        std::string SdkDir() const;                      // COSMIC_SDK or dev-tree root
        void NewScene();
        void OpenScene(const std::string& vfsPath);
        bool SaveScene();                 // false if it needs a name (opens Save As)
        void SaveSceneToVfs(const std::string& vfsPath);
        void BuildSandboxScene();

        // --- Play mode (E13) -----------------------------------------------
        // Play snapshots the edit scene to JSON, builds a fresh runtime scene from
        // it, instantiates the ScriptHost, and renders/ticks THAT; Stop discards the
        // runtime scene and restores the untouched edit scene. Pause freezes script
        // ticking; Step advances exactly one fixed step. The editor viewport always
        // renders from the editor camera (the "always ejected" v1 default — the
        // Primary CameraComponent path is the standalone PlayerLayer's job).
        enum class PlayMode { Edit, Playing, Paused };
        void PlayScene();
        void StopScene();
        void TogglePausePlay();
        void StepScene();
        void TickPlay(float ts);
        void DrawPlayControls();
        bool IsPlaying() const { return m_Play != PlayMode::Edit; }

        // --- Shell rendering -----------------------------------------------
        void ApplyDockLayout();
        void DrawTopBar();                // menus + tool strip (docked window)
        void DrawMenus();
        void DrawEntityMenu();
        void DrawHomescreen();
        void DrawSaveAsPopup();

        // --- Model import (E16) --------------------------------------------
        void DrawImportModelPopup();
        bool ImportModelFile(const std::string& srcPath);   // copy into project://models/ + spawn

        // --- Package & ship (E19) ------------------------------------------
        void DrawPackagePopup();
        void PackageProject();   // stage a standalone dist/<Project>/ from the build outputs

        // --- Polish (E21) --------------------------------------------------
        void DrawStatsWindow();  // entity + Renderer3D draw statistics
        void DrawHelpPopups();   // keyboard-shortcut reference

        // --- Frame helpers -------------------------------------------------
        void HandleShortcuts();
        void Autosave(float ts);
        void UpdateWindowTitle();
        void RenderViewport(float ts);

        EditorContext m_Ctx;
        Cosmic::OrbitCameraController m_Camera{ 16.0f / 9.0f };
        ViewportController m_Viewport;

        // Play-mode state (E13). While playing, m_Ctx.Scene points at the runtime
        // scene and m_EditSceneBackup holds the untouched edit scene.
        PlayMode                   m_Play = PlayMode::Edit;
        Cosmic::Ref<Cosmic::Scene> m_EditSceneBackup;
        Cosmic::ScriptHost         m_Scripts;
        float                      m_FixedDt     = 1.0f / 60.0f;
        float                      m_FixedAccum  = 0.0f;
        bool                       m_StepRequested = false;

        // Game module / hot reload (E12).
        GameModule          m_Module;
        BuildRunner         m_Builder;
        int                 m_HotCounter = 0;         // -> <project>_hot<N>.dll
        std::string         m_LastBuiltStem;          // reload target on build success
        bool                m_AutoBuild = false;      // rebuild on src/ change
        Cosmic::FileWatcher m_SrcWatcher;
        bool                m_SrcWatchOn = false;

        HierarchyPanel      m_Hierarchy;
        InspectorPanel      m_Inspector;
        ContentBrowserPanel m_Content;
        ConsolePanel        m_Console;
        EnvironmentPanel    m_Environment;    // E17
        MaterialEditorPanel m_Material;        // E17
        WorldSystemsPanel   m_WorldSystems;   // E18

        Prefs::EditorSettings m_Settings;

        bool  m_DockApplied     = false;
        bool  m_OpenSaveAs      = false;
        bool  m_OpenImportModel = false;
        bool  m_OpenPackage     = false;
        std::string m_LastDistDir;             // last packaged output (E19)
        float m_AutosaveTimer   = 0.0f;

        // View-menu panel toggles.
        bool m_ShowHierarchy = true, m_ShowInspector = true,
             m_ShowContent   = true, m_ShowConsole   = true;
        bool m_ShowEnvironment = false, m_ShowMaterial = false;   // E17 (off by default)
        bool m_ShowWorldSystems = false;  // E18 (off by default)
        bool m_ShowStats = false;         // E21 statistics window
        bool m_OpenShortcuts = false;     // E21 shortcut reference modal

        // Homescreen / dialogs scratch.
        char m_NewProjectName[128] = "MyProject";
        char m_SaveAsName[128]     = "Main";
        char m_ImportPath[512]     = "";
    };
}
