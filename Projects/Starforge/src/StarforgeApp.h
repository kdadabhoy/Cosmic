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
#include "scene/FlowMachine.h"   // U5/U8 — flow-driven editor Play

#include "EditorContext.h"
#include "EditorPrefs.h"
#include "EditorCameraRig.h"
#include "LayoutPresets.h"
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
#include "panels/VoxelPanel.h"
#include "panels/TilePalettePanel.h"
#include "panels/FlowGraphPanel.h"
#include "panels/TelemetryPanel.h"
#include "panels/ProfilerPanel.h"
#include "panels/SystemPanel.h"

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <utility>
#include <unordered_map>

namespace Starforge
{
    // Fixed-pose camera fed from the scene's primary CameraComponent during
    // Play (U7) — the editor twin of PlayerLayer's internal camera holder.
    class PoseCamera : public Cosmic::Camera
    {
    public:
        void Set(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& pos)
        {
            m_View = view; m_Proj = proj; m_ViewProj = proj * view; m_Pos = pos;
        }
        const glm::mat4& GetViewMatrix() const override           { return m_View; }
        const glm::mat4& GetProjectionMatrix() const override     { return m_Proj; }
        const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProj; }
        const glm::vec3& GetPosition() const override             { return m_Pos; }
    private:
        glm::mat4 m_View{ 1.0f }, m_Proj{ 1.0f }, m_ViewProj{ 1.0f };
        glm::vec3 m_Pos{ 0.0f };
    };

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

        // --- Workspace layout presets (K3) -----------------------------------
        LayoutPanels PanelSet();                            // the View-menu bools
        void ApplyLayoutPreset(const std::string& name);    // built-in or user
        void DrawLayoutPresetPicker(float squareSize);      // top-bar dropdown
        void DrawSaveLayoutPopup();
        std::string ProjectLayoutKey() const;               // path, or name (legacy)

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

        // --- Drop-a-file branding (K1) --------------------------------------
        // Resolve the branding/icon.png convention (utils/Branding), apply it to
        // the live window/taskbar icon, (re)load the top-bar logo texture, and
        // aim the hot-swap watcher at the resolved file's folder. Called on
        // attach and whenever the watcher reports the file changed — replacing
        // the PNG on disk re-brands the running editor, no restart.
        void ApplyBrand();
        void DrawBrandLogo(float height);   // logo image (top bar / homescreen / About)

        // --- Status bar (K5) -------------------------------------------------
        // A NoDecoration strip pinned in the band the workspace reserves under
        // the dockspace (WorkspaceLayer::SetBottomInsetPixels — panels never
        // underlap it). Hidden on the homescreen. FPS/ms, entity + selected
        // counts, build-module state, play state; Phase 23 T2's asset-memory
        // chip gets the reserved right-side slot.
        void DrawStatusBar();

        // --- Polish (E21) --------------------------------------------------
        void DrawStatsWindow();  // entity + Renderer3D draw statistics
        void DrawHelpPopups();   // keyboard-shortcut reference
        void ApplyEditorTheme(); // register + apply the "Starforge" forge accent
        void DrawFirstRunPopup();          // one-time offer of the Forge Playground sample
        bool BuildForgePlayground();       // scaffold + author the sample project
        void GenerateSampleTake();         // pre-baked telemetry take for the sample
        bool ForgePlaygroundExists() const;

        bool BuildForgeBlocks();           // Phase 18 — scaffold + author the voxel sample
        bool ForgeBlocksExists() const;

        // Phase 17 / U8 samples. FlowDemo = the ZERO-CODE two-screen app
        // (menu -> game -> pause overlay, all navigation from Main.cflow);
        // ForgePong = 2D sprites + UI + flow + tiny scripts, playable pong.
        bool BuildFlowDemo();
        bool FlowDemoExists() const;
        bool BuildForgePong();
        bool ForgePongExists() const;

        // --- Frame helpers -------------------------------------------------
        void HandleShortcuts();
        void Autosave(float ts);
        void UpdateWindowTitle();
        void RenderViewport(float ts);
        void DrainLogQueue();       // move engine-log lines onto the UI thread (H7)
        void AdoptCameraForScene(); // H8 — adopt a Primary camera's pose, else frame-all
        void CheckScriptsBuilt();   // H8 — warn + hint when a scene references unbuilt scripts

        EditorContext m_Ctx;

        // K7 — the editor camera rig: Orbit (CAD default) + Fly (RMB-hold /
        // explicit) + Possess (render a CameraComponent's pose read-only).
        EditorCameraRig m_Rig;

        // 2D authoring mode (U3): a Z-facing ortho rig (MMB pan / wheel zoom)
        // swapped in for the orbit camera by the toolbar "2D" toggle. View-only
        // state — never serialized; 3D scenes are untouched when it stays off.
        Cosmic::Camera2DController m_Camera2D{ 16.0f / 9.0f };
        bool m_Mode2D = false;

        // Game view (U7): during Play the viewport renders from the scene's
        // primary CameraComponent (like the shipped player); Eject flies the
        // editor camera while the sim runs; aspect presets letterbox the frame
        // so authored UI anchors are truthful; cursor capture = mouse-look.
        enum class GameAspect : int { Free = 0, W16H9, R1920x1080, Project };
        PoseCamera m_GameCamera;              // fed each frame while playing
        bool       m_GameCamActive = false;   // primary cam found + not ejected
        GameAspect m_Aspect  = GameAspect::Free;
        bool       m_Ejected = false;
        bool       m_CaptureCursor = false;   // Esc releases (unchecks)
        bool       m_NoPrimaryCamWarned = false;
        int        m_ProjectW = 0, m_ProjectH = 0;   // manifest [window] (Project preset)
        glm::vec4  m_GameBandUv{ 0.0f, 0.0f, 1.0f, 1.0f };   // letterbox band, viewport fractions

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

        // Flow-driven Play (U5/U8): when the manifest names a startup_flow and
        // the "Flow" toggle is on, Play runs the .cflow from its start state —
        // exactly like the shipped player — swapping scenes on transitions. The
        // open scene, if dirty and referenced by a state, plays from the live
        // snapshot so what-you-see-is-what-you-play.
        Cosmic::FlowMachine m_PlayFlow;
        bool        m_PlayFlowActive = false;   // this Play session runs the flow
        bool        m_PlayFlowUse    = true;    // toolbar toggle (shown when a flow exists)
        bool        m_PrevEscape     = false;   // key:Escape edge for the flow
        std::string m_ManifestFlow;             // manifest startup_flow ("" = none)

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
        VoxelPanel          m_Voxel;           // Phase 18
        TilePalettePanel    m_TilePalette;    // U4
        FlowGraphPanel      m_FlowGraph;      // U6
        TelemetryPanel      m_Telemetry;      // E20
        ProfilerPanel       m_Profiler;       // T17 — GPU/CPU profiler
        SystemPanel         m_System;         // T18 — jobs / resources

        Prefs::EditorSettings m_Settings;

        // Engine-log → Console sink (H7). The CallbackSink fires from ANY thread, so
        // it enqueues under a mutex; DrainLogQueue moves lines onto the UI thread.
        std::shared_ptr<Cosmic::CallbackSink>            m_LogSink;
        std::mutex                                       m_LogQueueMutex;
        std::vector<std::pair<LogSeverity, std::string>> m_LogQueue;

        // Workspace layout presets (K3).
        std::string m_ActivePreset = "Level";
        std::string m_PendingLayoutIni;      // ImGui ini blob to load next frame
        bool        m_OpenSaveLayout = false;
        char        m_LayoutNameBuf[64] = "My Layout";

        // Drop-a-file branding state (K1).
        Cosmic::Ref<Cosmic::Texture2D> m_BrandTex;      // top-bar logo (the resolved icon)
        Cosmic::FileWatcher m_BrandWatcher;             // hot-swap: resolved file's folder
        std::string m_BrandWatchDir;
        std::string m_BrandPath;                        // resolved icon ("" = engine default)
        float       m_BrandDebounce = -1.0f;            // >= 0: seconds until re-apply
        bool        m_BrandRetried  = false;            // one retry after a failed decode

        bool  m_ScriptsNeedBuild = false;   // H8 — scene references classes the module lacks
        bool  m_DockApplied     = false;
        bool  m_OpenSaveAs      = false;
        bool  m_OpenImportModel = false;
        bool  m_OpenPackage     = false;
        bool  m_OpenProjectSettings = false;   // S5 — icon + window identity editor
        bool  m_OpenAbout       = false;       // S7 — About dialog
        bool  m_ThumbRequested  = false;       // S7 — capture thumbnail on the next render
        std::string m_LastDistDir;             // last packaged output (E19)

        // A4 — Help ▸ Preview State Self-Test (0 = idle; 1/2/3 = the capture
        // frames: baseline A, control B + preview passes, verdict C).
        int                  m_PreviewSelfTest = 0;
        std::vector<uint8_t> m_SelfTestPixels;
        uint32_t             m_SelfTestW = 0, m_SelfTestH = 0;
        float m_AutosaveTimer   = 0.0f;
        float m_TopBarBottomY   = 0.0f;        // bottom edge of the top bar (homescreen anchor)

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
        bool m_ShowVoxel = false;         // Phase 18 (off by default)
        bool m_ShowTilePalette = false;   // U4 (off by default)
        bool m_ShowFlowGraph = false;     // U6 (off by default)
        bool m_ShowTelemetry = false;     // E20 (off by default)
        bool m_ShowStats = false;         // E21 statistics window
        bool m_ShowProfiler = false;      // T17 (off by default)
        bool m_ShowSystem = false;        // T18 (off by default)
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
