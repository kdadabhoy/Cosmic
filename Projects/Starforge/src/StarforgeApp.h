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
#include "ProjectManifest.h"
#include "Packager.h"
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
#include "panels/TelemetryPanel.h"

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <utility>
#include <unordered_map>

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
        // --- Project / scene lifecycle (E6 / S1 external folders) ----------
        void OpenProject(const Prefs::ProjectEntry& entry);   // mount + load + register
        void OpenProject(const std::string& name);            // legacy in-tree convenience
        bool OpenProjectPath(const std::string& absoluteRoot);// validate + open an external folder
        bool NewProjectAt(const std::string& name, const std::string& location);
        bool ScaffoldProjectTo(const std::string& name, const std::string& destRoot);
        bool ScaffoldProject(const std::string& name);        // legacy: assets/projects/<name>
        void MountProject(const Prefs::ProjectEntry& entry);   // FileSystem mount + m_Ctx fields
        void CloseProject();

        // --- Game module build & hot reload (E12 / S1) ---------------------
        void        BuildScripts();                      // Ctrl+B — background cmake
        void        ReloadModule(const std::string& dllStem);
        void        DrawBuildControls();
        bool        ProjectIsScaffolded() const;         // has a src/ + CMakeLists.txt
        std::string ProjectDir() const;                  // absolute project root (external or in-tree)
        std::string ProjectContentDir() const;           // where scenes/models/… live on disk
        std::string ProjectBuildDir() const;             // external "<root>/build" ("" => SDK output)
        std::string ModuleSearchDir() const;             // external "<root>/build/<cfg>" ("" => app dir)
        void        CleanStaleHotDlls(const std::string& keepStem);
        std::string SdkDir() const;                      // COSMIC_SDK or dev-tree root
        void NewScene();
        void OpenScene(const std::string& vfsPath);
        bool SaveScene();                 // false if it needs a name (opens Save As)
        void SaveSceneToVfs(const std::string& vfsPath);

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
        void DrawSaveAsPopup();

        // --- Product homescreen / project library (S3) ---------------------
        void DrawHomescreen();
        void DrawProjectCard(const Prefs::ProjectEntry& e, float cardW);
        Cosmic::Ref<Cosmic::Texture2D> ThumbFor(const Prefs::ProjectEntry& e);

        // --- Model import (E16) --------------------------------------------
        void DrawImportModelPopup();
        bool ImportModelFile(const std::string& srcPath);   // copy into project://models/ + spawn

        // --- Package & ship (E19 / S5, S2) ---------------------------------
        void DrawPackagePopup();
        void DrawProjectSettingsPopup();   // icon + window title/size -> project.cproj
        void PackageProject();             // orchestrate (Release build ->) stage -> finalize
        void PackageStarforge();           // S2 — self-package the editor as a product
        void OnPackageBuildDone(bool ok);  // package-build completion (from BuildRunner Poll)
        bool BeginPackage(const Prefs::ProjectEntry& target);   // shared by project + Starforge

        // --- Editor conveniences (S7) --------------------------------------
        void RunStandalone();     // launch the app as if double-clicked
        void CaptureThumbnail();  // blit the viewport into <root>/.starforge/thumb.png
        void DrawAboutPopup();

        // --- Polish (E21) --------------------------------------------------
        void DrawStatsWindow();  // entity + Renderer3D draw statistics
        void DrawHelpPopups();   // keyboard-shortcut reference
        void ApplyEditorTheme(); // register + apply the "Starforge" forge accent
        void DrawFirstRunPopup();          // one-time offer of the Forge Playground sample
        bool BuildForgePlayground();       // scaffold + author the sample project
        void GenerateSampleTake();         // pre-baked telemetry take for the sample
        bool ForgePlaygroundExists() const;

        // --- Frame helpers -------------------------------------------------
        void HandleShortcuts();
        void Autosave(float ts);
        void UpdateWindowTitle();
        void RenderViewport(float ts);
        void DrainLogQueue();       // move engine-log lines onto the UI thread (H7)
        void AdoptCameraForScene(); // H8 — adopt a Primary camera's pose, else frame-all
        void CheckScriptsBuilt();   // H8 — warn + hint when a scene references unbuilt scripts

        EditorContext m_Ctx;
        Cosmic::OrbitCameraController m_Camera{ 16.0f / 9.0f };
        ViewportController m_Viewport;

        // The engine frame orchestrator (H2): environment/sky/shadows/HDR/post live
        // in the editor viewport (and, identically, the standalone PlayerLayer).
        Cosmic::SceneRenderer m_SceneRenderer;

        // Play-mode state (E13). While playing, m_Ctx.Scene points at the runtime
        // scene and m_EditSceneBackup holds the untouched edit scene.
        PlayMode                   m_Play = PlayMode::Edit;
        Cosmic::Ref<Cosmic::Scene> m_EditSceneBackup;
        Cosmic::ScriptHost         m_Scripts;
        Cosmic::PhysicsWorld       m_Physics;     // J4 — play-session physics (built on Play)
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

        // Whether the active build feeds hot reload or the packaging pipeline (S5),
        // so the shared BuildRunner Poll dispatches correctly.
        enum class BuildPurpose { HotReload, Package };
        BuildPurpose        m_BuildPurpose = BuildPurpose::HotReload;

        HierarchyPanel      m_Hierarchy;
        InspectorPanel      m_Inspector;
        ContentBrowserPanel m_Content;
        ConsolePanel        m_Console;
        EnvironmentPanel    m_Environment;    // E17
        MaterialEditorPanel m_Material;        // E17
        WorldSystemsPanel   m_WorldSystems;   // E18
        TelemetryPanel      m_Telemetry;      // E20

        Prefs::EditorSettings m_Settings;

        // Engine-log → Console sink (H7). The CallbackSink fires from ANY thread, so
        // it enqueues under a mutex; DrainLogQueue moves lines onto the UI thread.
        std::shared_ptr<Cosmic::CallbackSink>            m_LogSink;
        std::mutex                                       m_LogQueueMutex;
        std::vector<std::pair<LogSeverity, std::string>> m_LogQueue;

        bool  m_ScriptsNeedBuild = false;   // H8 — scene references classes the module lacks
        bool  m_DockApplied     = false;
        bool  m_OpenSaveAs      = false;
        bool  m_OpenImportModel = false;
        bool  m_OpenPackage     = false;
        bool  m_OpenProjectSettings = false;   // S5 — icon + window identity editor
        bool  m_OpenAbout       = false;       // S7 — About dialog
        bool  m_ThumbRequested  = false;       // S7 — capture thumbnail on the next render
        std::string m_LastDistDir;             // last packaged output (E19)
        float m_AutosaveTimer   = 0.0f;

        // Packaging pipeline state (S5). m_PkgPending is filled before an async
        // Release build and consumed when it finishes.
        PackageOptions m_PkgOpt;
        PackageInputs  m_PkgPending;
        bool           m_PkgAwaitingBuild = false;
        char           m_IconPathBuf[512] = "";

        // View-menu panel toggles.
        bool m_ShowHierarchy = true, m_ShowInspector = true,
             m_ShowContent   = true, m_ShowConsole   = true;
        bool m_ShowEnvironment = false, m_ShowMaterial = false;   // E17 (off by default)
        bool m_ShowWorldSystems = false;  // E18 (off by default)
        bool m_ShowTelemetry = false;     // E20 (off by default)
        bool m_ShowStats = false;         // E21 statistics window
        bool m_OpenShortcuts = false;     // E21 shortcut reference modal
        bool m_OpenFirstRun  = false;     // E21 first-run Forge Playground offer
        std::string m_PrevTheme;          // theme to restore on detach (E21)

        // Homescreen / dialogs scratch.
        char m_NewProjectName[128] = "MyProject";
        char m_NewProjectLoc[512]  = "";      // S3 — New Project location (folder)
        char m_SaveAsName[128]     = "Main";
        char m_ImportPath[512]     = "";
        char m_HomeSearch[128]     = "";      // S3 — library filter
        std::string m_HomeSelected;           // S3 — selected card key (path or name)

        // Thumbnail texture cache (S3): key = absolute thumb path; a null Ref means
        // "checked, none on disk" so we never re-hit the filesystem per frame.
        std::unordered_map<std::string, Cosmic::Ref<Cosmic::Texture2D>> m_ThumbCache;
    };
}
