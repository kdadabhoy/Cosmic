// StarforgeApp.cpp — root editor layer + plugin exports. See StarforgeApp.h.

#include "StarforgeApp.h"

#include "commands/EditorCommands.h"
#include "Prefabs.h"
#ifndef COSMIC_2D_ONLY
#include "editors/AnimationEditor.h"   // M3 — the AssetEditorHost factory target
#endif
#include "editors/FlowEditor.h"        // Q1 — flow document factory target
#include "editors/StoryEditor.h"       // Q4 — story document factory target

#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#ifndef COSMIC_2D_ONLY
#include "scene/SceneNav.h"          // N3 — async navmesh bake orchestration
#include "nav/NavWorld.h"            // N3 — NavMeshComponent::Nav->IsBuilt()
#endif
#include "scene/ui/UiComponents.h"   // U1 — UI entity components
#include "scene/ui/UiSystem.h"       // U1 — canvas overlay render in the viewport
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#ifndef COSMIC_2D_ONLY
#include "voxel/VoxelVolume.h"       // Phase 18 — ForgeBlocks sample authoring
#include "voxel/BlockPalette.h"
#include "voxel/VoxelGenerator.h"
#include "voxel/VoxelRender.h"
#endif
#include "core/Version.h"
#include "utils/FileSystem.h"
#include "utils/Branding.h"          // K1 — drop-a-file branding resolution
#include "ui/IconsLucide.h"          // K2 — icon toolbar glyphs

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>

namespace fs = std::filesystem;

namespace
{
    std::string ReplaceAll(std::string s, const std::string& from, const std::string& to)
    {
        if (from.empty()) return s;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos)
        {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return s;
    }
}

namespace Starforge
{
    StarforgeApp::StarforgeApp() : Cosmic::Layer("Starforge") {}

    // =========================================================================
    void StarforgeApp::OnAttach()
    {
        CS_INFO("Starforge: attaching the Cosmic editor (Phase 13 Stage B).");

        Cosmic::FileSystem::SetActiveProject("Starforge");
        // Logs go to the WRITABLE user root, not project://logs (that lives in the
        // read-only content area, and a packaged app under Program Files can't write
        // there) — H7.
        {
            const std::string logDir = Cosmic::FileSystem::Resolve("user://logs");
            Cosmic::Log::SetLogDirectory(logDir);
            CS_INFO("Log files -> {}", logDir);
        }

        // Mirror the engine log into the Console panel (H7). The sink fires from any
        // thread → enqueue under a mutex; DrainLogQueue drains on the UI thread.
        m_LogSink = std::make_shared<Cosmic::CallbackSink>(
            [this](spdlog::level::level_enum lvl, const std::string& line)
            {
                LogSeverity sev = LogSeverity::Info;
                if (lvl == spdlog::level::warn)      sev = LogSeverity::Warn;
                else if (lvl >= spdlog::level::err)  sev = LogSeverity::Error;
                std::lock_guard<std::mutex> lk(m_LogQueueMutex);
                m_LogQueue.emplace_back(sev, line);
            });
        m_LogSink->set_pattern("[%n] %v");   // panel adds its own timestamp column (H10)
        Cosmic::Log::AddSink(m_LogSink);

        m_Rig.Orbit().SnapView(Cosmic::ViewPreset::Iso, /*animate=*/false);
        m_Viewport.Init();

        // Orbit-about-surface (H1): pivot on the point under the cursor via a one-off
        // depth probe. Invoked only when an orbit drag begins; misses fall back to the
        // controller's ray/target-plane pivot. The probe is a ScenePicker ID pass —
        // 3D only; the 2D build never orbits (it authors on the 2D rig), so the
        // controller keeps its ray/target-plane pivot unconditionally there.
#ifndef COSMIC_2D_ONLY
        m_Rig.Orbit().SetPivotProbe([this](const glm::vec2& screenMouse, glm::vec3& out) -> bool
        {
            return m_Viewport.ProbeWorldPoint(m_Ctx, m_Rig.Orbit().GetCamera(), screenMouse, out);
        });
#endif

        // Editor identity: apply the forge accent, remembering the previous theme
        // so OnDetach restores it (other apps in the same process stay untouched).
        m_PrevTheme = Cosmic::ThemeManager::CurrentName();
        ApplyEditorTheme();

        // Drop-a-file branding (K1): window/taskbar icon + top-bar logo from
        // branding/icon.png (user:// override wins next) — then hot-swapped
        // whenever the file changes on disk.
        ApplyBrand();

        m_Settings = Prefs::LoadSettings();
        m_Viewport.LoadSnapPrefs(m_Settings);   // K6 — per-op snap values persist
        m_Content.LoadPrefs(m_Settings);        // T4 — content-browser layout persists

        // First-run: offer the "Forge Playground" sample once (E21). Only when the
        // sample isn't already present and the user hasn't been asked before.
#ifndef COSMIC_2D_ONLY
        if (!m_Settings.PlaygroundOffered && !ForgePlaygroundExists())
            m_OpenFirstRun = true;
#else
        if (!m_Settings.PlaygroundOffered && !ForgePongExists())
            m_OpenFirstRun = true;   // W7 — the 2D build offers ForgePong instead
#endif

        // Route command-stack activity to the dirty flag (belt-and-suspenders —
        // commands also mark dirty directly).
        m_Ctx.Commands.SetDirtyCallback([this] { m_Ctx.MarkDirty(); });

        // Boot into the product homescreen (S2/S3): the project library, not a
        // sandbox. FileSystem stays pointed at the editor's own bundled assets so a
        // stray project:// read resolves there until a real project opens. The
        // scene Viewport panel is hidden until a project opens — the homescreen is
        // a full-window page, not viewport content (MountProject re-shows it).
        m_Ctx.ProjectOpen = false;
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
            ws->SetViewportVisible(false);

        m_Ctx.Log("[Starforge] Editor attached — open or create a project from the homescreen.");
        m_Ctx.Log("[Starforge] Viewport: MMB orbit | Ctrl+MMB pan | scroll zoom | LMB pick.");
        m_Ctx.Log("[Starforge] W/E/R gizmo | F frame | Ctrl+Z/Y undo | Ctrl+S save.");
    }

    // =========================================================================
    void StarforgeApp::OnDetach()
    {
        // Stop the engine log feeding the (about-to-be-destroyed) Console first (H7).
        if (m_LogSink)
        {
            Cosmic::Log::RemoveSink(m_LogSink);
            m_LogSink.reset();
        }

        StopScene();                 // tear down script instances before scene reset
        m_SrcWatcher.Stop();
        m_BrandWatcher.Stop();       // K1 — stop the branding hot-swap watcher
        m_BrandTex.reset();          // release the logo texture while GL is live
        m_Viewport.SaveSnapPrefs(m_Settings);   // K6 — persist per-op snap values
        m_Content.SavePrefs(m_Settings);        // T4 — persist content-browser layout
        Prefs::SaveSettings(m_Settings);
        m_SceneRenderer.Shutdown();  // free GPU subsystems while the GL context is live (H2)
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();
        m_Ctx.Scene.reset();         // drop the scene while the module is still loaded
        m_EditSceneBackup.reset();
        m_Module.Unload();           // then FreeLibrary

        // Restore the engine chrome menus + default viewport title for whatever app
        // (or the Launcher) runs next in this process (H5).
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
        {
            ws->SetChromeMenusVisible(true);
            ws->SetViewportTitle("Viewport");
            ws->SetEdgeMinPixels(0.0f, 0.0f, 0.0f, 0.0f);
            ws->SetBottomInsetPixels(0.0f);   // K5 — release the status-bar band
            ws->SetViewportVisible(true);   // homescreen may have hidden it
        }

        // Restore the theme we replaced so a sibling app (or the Launcher) shown
        // next in this process gets its own look, not the forge accent (E21).
        if (!m_PrevTheme.empty())
            Cosmic::ThemeManager::Apply(m_PrevTheme);

        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Starforge: detached.");
    }

    // =========================================================================
    // Project / scene lifecycle (E6 / S1 external folders)
    // =========================================================================
    void StarforgeApp::MountProject(const Prefs::ProjectEntry& e)
    {
        // NAME mode for legacy in-tree projects (assets/projects/<name>), PATH mode
        // for self-contained external folders (S1).
        if (e.Path.empty())
            Cosmic::FileSystem::SetActiveProject(e.Name);
        else
            Cosmic::FileSystem::SetActiveProjectPath(e.Path);
        m_Ctx.ProjectOpen  = true;
        m_Ctx.ProjectName  = e.Name;
        m_Ctx.ProjectTitle = e.Name;
        m_Ctx.ProjectPath  = e.Path;

        // U3 — pixel-art preset: point-filter textures loaded for this project
        // (same manifest key the PlayerLayer honors). Cleared for projects
        // without it so switching projects can't leak the override.
        const ProjectManifest pm = ProjectManifest::Load("project://project.cproj");
        if (pm.PixelArt)
            Cosmic::AssetLibrary::SetDefaultTextureSampling(Cosmic::TextureFilter::Nearest,
                                                            Cosmic::TextureWrap::ClampToEdge);
        else
            Cosmic::AssetLibrary::ClearDefaultTextureSampling();

        // U5/U8 — flow-driven Play offer: remember the manifest's startup flow.
        m_ManifestFlow = pm.StartupFlow;
        // The scene viewport is only meaningful with a project open; the homescreen
        // hides it (see OnAttach/CloseProject) so re-show it here.
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
            ws->SetViewportVisible(true);

        // K3 — restore this project's active workspace layout (Level when unset).
        const std::string preset = LayoutPresets::LoadActive(ProjectLayoutKey());
        ApplyLayoutPreset(preset.empty() ? "Level" : preset);
    }

    void StarforgeApp::OpenProject(const Prefs::ProjectEntry& e)
    {
        if (e.Name.empty()) return;
        if (IsPlaying()) StopScene();

        MountProject(e);
        m_Content.Reset();
        // A4 — thumbnail disk cache lives with the project's other editor state.
        m_Ctx.Preview.SetCacheDirectory(
            (fs::path(ProjectDir()) / ".starforge" / "thumbs").generic_string());

        const std::string main = "project://scenes/Main.cscene";
        if (fs::exists(Cosmic::FileSystem::Resolve(main)))
            OpenScene(main);
        else
            NewScene();

        // Game module (E12): a fresh open starts with no module loaded. Watch src/
        // for auto-build, and prompt to build if the project is scaffolded.
        m_Module.Unload();
        m_SrcWatcher.Stop();
        m_SrcWatchOn = false;
        if (ProjectIsScaffolded())
        {
            std::error_code ec;
            const fs::path src = fs::path(ProjectDir()) / "src";
            if (fs::exists(src, ec))
                m_SrcWatchOn = m_SrcWatcher.Watch(src.generic_string(), /*recursive=*/true);
            m_Ctx.Log("[Project] Scaffolded project — press Ctrl+B to build the game module.");
        }

        Prefs::TouchProject(e.Name, e.Path);
        m_Ctx.Log("[Project] Opened '" + e.Name + "'" +
                  (e.Path.empty() ? " (in-tree)." : (" @ " + e.Path)));
    }

    void StarforgeApp::OpenProject(const std::string& name)
    {
        Prefs::ProjectEntry e; e.Name = name; e.Path = "";   // legacy in-tree
        OpenProject(e);
    }

    bool StarforgeApp::OpenProjectPath(const std::string& absoluteRoot)
    {
        std::error_code ec;
        const fs::path root = fs::absolute(absoluteRoot, ec);
        if (!fs::exists(root / "project.cproj", ec))
        {
            m_Ctx.Log("[Project] '" + root.generic_string() + "' has no project.cproj.", LogSeverity::Error);
            return false;
        }
        // Read the manifest's declared name through the mount; fall back to the folder.
        Cosmic::FileSystem::SetActiveProjectPath(root.generic_string());
        const ProjectManifest man = ProjectManifest::Load("project://project.cproj");
        Prefs::ProjectEntry e;
        e.Name = man.Name.empty() ? root.filename().generic_string() : man.Name;
        e.Path = root.generic_string();
        OpenProject(e);
        return true;
    }

    bool StarforgeApp::ScaffoldProjectTo(const std::string& name, const std::string& destRoot)
    {
        // Copy the editor's templates/ into destRoot, replacing @PROJECT_NAME@ in
        // every (text) file. The templates ship with the Starforge DLL and sync to
        // assets/projects/Starforge/templates/.
        std::error_code ec;
        const fs::path templates = fs::path("assets") / "projects" / "Starforge" / "templates";
        if (!fs::exists(templates, ec))
            return false;

        const fs::path root = destRoot;
        for (auto it = fs::recursive_directory_iterator(templates, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            const fs::path rel = fs::relative(it->path(), templates, ec);
            const fs::path dst = root / rel;
            if (it->is_directory(ec)) { fs::create_directories(dst, ec); continue; }

            fs::create_directories(dst.parent_path(), ec);
            std::ifstream in(it->path(), std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            const std::string content = ReplaceAll(ss.str(), "@PROJECT_NAME@", name);
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            out << content;
        }
        return true;
    }

    bool StarforgeApp::ScaffoldProject(const std::string& name)
    {
        // Legacy in-tree scaffold (ForgePlayground) into assets/projects/<name>.
        const fs::path root = fs::path("assets") / "projects" / name;
        return ScaffoldProjectTo(name, root.generic_string());
    }

    bool StarforgeApp::NewProjectAt(const std::string& name, const std::string& location)
    {
        if (name.empty() || location.empty()) return false;
        if (IsPlaying()) StopScene();

        std::error_code ec;
        const fs::path root = fs::path(location) / name;
        if (fs::exists(root, ec))
        {
            m_Ctx.Log("[Project] A folder already exists at '" + root.generic_string() + "'.",
                      LogSeverity::Error);
            return false;
        }
        if (!ScaffoldProjectTo(name, root.generic_string()))
        {
            m_Ctx.Log("[Project] Could not scaffold '" + name + "' — templates unavailable.",
                      LogSeverity::Error);
            return false;
        }
        return OpenProjectPath(root.generic_string());
    }

    void StarforgeApp::CloseProject()
    {
        if (IsPlaying()) StopScene();
        m_Module.Unload();
        CleanStaleHotDlls("");
        m_SrcWatcher.Stop();
        m_SrcWatchOn = false;
        m_Ctx.ProjectOpen = false;
        m_Ctx.ProjectPath.clear();
        m_Ctx.Scene.reset();
        m_EditSceneBackup.reset();
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Content.Reset();
        m_Ctx.Preview.SetCacheDirectory("");   // A4 — thumbnails are per-project
#ifndef COSMIC_2D_ONLY
        m_Mode2D = false;   // view-only state; the next project starts in 3D
#endif   // W7: the 2D build has no 3D mode to fall back to — m_Mode2D stays on
        m_ManifestFlow.clear();   // U5/U8 — flow offer is per-project
        Cosmic::AssetLibrary::ClearDefaultTextureSampling();   // U3 — drop the pixel-art override
        // Back to the editor's own bundled assets for the homescreen; the scene
        // Viewport panel hides with the project (MountProject re-shows it).
        Cosmic::FileSystem::SetActiveProject("Starforge");
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
            ws->SetViewportVisible(false);
    }

    // =========================================================================
    // Game module build & hot reload (E12)
    // =========================================================================
    std::string StarforgeApp::ProjectDir() const
    {
        std::error_code ec;
        if (!m_Ctx.ProjectPath.empty())
            return fs::absolute(m_Ctx.ProjectPath, ec).generic_string();   // external root
        return fs::absolute(Cosmic::FileSystem::Resolve("project://"), ec).generic_string();
    }

    std::string StarforgeApp::ProjectContentDir() const
    {
        // Where the shipped content (scenes/models/…) lives on disk — the project
        // root in both modes (flat layout).
        return ProjectDir();
    }

    std::string StarforgeApp::ProjectBuildDir() const
    {
        // External projects build into their own tree; in-tree projects fall back to
        // the SDK runtime output ("" => the template default).
        if (m_Ctx.ProjectPath.empty())
            return "";
        return (fs::path(m_Ctx.ProjectPath) / "build").generic_string();
    }

    std::string StarforgeApp::ModuleSearchDir() const
    {
        const std::string bd = ProjectBuildDir();
        if (bd.empty())
            return "";   // legacy: DLL sits in the app dir
        return (fs::path(bd) / BuildRunner::kHotConfig).generic_string();
    }

    void StarforgeApp::CleanStaleHotDlls(const std::string& keepStem)
    {
        // Delete <project>_hotN.dll files in the module dir except keepStem — the
        // loaded one stays locked and its remove() silently fails (that's the point
        // of the suffix). Scans the external build dir (S1) or the app dir (legacy).
        std::error_code ec;
        const std::string dir = ModuleSearchDir().empty()
            ? fs::current_path(ec).generic_string() : ModuleSearchDir();
        if (m_Ctx.ProjectName.empty()) return;
        const std::string prefix = m_Ctx.ProjectName + "_hot";
        for (const auto& entry : fs::directory_iterator(dir, ec))
        {
            if (ec) break;
            if (entry.path().extension() != ".dll") continue;
            const std::string stem = entry.path().stem().string();
            if (stem.rfind(prefix, 0) != 0) continue;      // not a hot dll of this project
            if (!keepStem.empty() && stem == keepStem) continue;
            std::error_code rmec; fs::remove(entry.path(), rmec);   // locked => stays
        }
    }

    std::string StarforgeApp::SdkDir() const
    {
#pragma warning(push)
#pragma warning(disable: 4996)
        if (const char* env = std::getenv("COSMIC_SDK"))
            return env;
#pragma warning(pop)
        // Dev tree: the editor runs from build/Runtime/<cfg> — the SDK root is 3 up.
        std::error_code ec;
        return fs::absolute("../../..", ec).generic_string();
    }

    bool StarforgeApp::ProjectIsScaffolded() const
    {
        std::error_code ec;
        return m_Ctx.ProjectOpen &&
               fs::exists(fs::path(ProjectDir()) / "CMakeLists.txt", ec);
    }

    void StarforgeApp::BuildScripts()
    {
        if (!m_Ctx.ProjectOpen || m_Builder.IsBuilding())
            return;
        if (IsPlaying())
        {
            m_Ctx.Log("[Build] Stop Play before rebuilding scripts.", LogSeverity::Warn);
            return;
        }
        if (!ProjectIsScaffolded())
        {
            m_Ctx.Log("[Build] This project has no game module (no CMakeLists.txt). "
                      "Create a project from the homescreen to scaffold one.", LogSeverity::Warn);
            return;
        }
        ++m_HotCounter;
        const std::string suffix = "_hot" + std::to_string(m_HotCounter);
        m_LastBuiltStem = m_Ctx.ProjectName + suffix;
        m_BuildPurpose  = BuildPurpose::HotReload;
        m_Ctx.Log("[Build] Building '" + m_Ctx.ProjectName + "' -> " + m_LastBuiltStem + ".dll");
        // External projects emit the DLL into their own build tree (S1); in-tree
        // projects leave gameOutputDir empty and land in the SDK runtime dir.
        m_Builder.Start(ProjectDir(), SdkDir(), suffix, BuildRunner::kHotConfig, ProjectBuildDir());
    }

    void StarforgeApp::ReloadModule(const std::string& dllStem)
    {
        // Preserve edit-scene state across the module swap. Custom (module-owned)
        // components serialize while the OLD module is still loaded; NativeScript
        // data is engine-owned and survives regardless.
        std::string snapshot;
        if (m_Ctx.Scene)
            snapshot = Cosmic::SceneSerializer::SaveToString(*m_Ctx.Scene);

        if (IsPlaying()) StopScene();
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();
        // Drop the scene while the OLD module is still loaded so any module-typed
        // component destructors run against valid code, THEN unload the DLL.
        m_Ctx.Scene.reset();
        m_Module.Unload();

        if (!m_Module.Load(m_Ctx.ProjectName, dllStem, ModuleSearchDir()))
            m_Ctx.Log("[Module] Load failed — scripts unavailable this session.", LogSeverity::Error);
        CleanStaleHotDlls(dllStem);   // sweep older hot DLLs in the module dir (S1)

        // Rebuild the scene (custom components + script classes now resolve).
        Cosmic::Ref<Cosmic::Scene> fresh = Cosmic::Scene::Create();
        if (!snapshot.empty())
            Cosmic::SceneSerializer::LoadFromString(*fresh, snapshot);
        m_Ctx.Scene = fresh;
        m_Ctx.ClearDirty();
        CheckScriptsBuilt();   // H8 — classes now resolve; clears the Ctrl+B hint
        m_Ctx.Log("[Module] Reloaded '" + dllStem + "' (" +
                  std::to_string(Cosmic::ModuleRegistry::Get().ScriptNames(m_Ctx.ProjectName).size()) +
                  " script(s)).");
    }

    void StarforgeApp::NewScene()
    {
        if (IsPlaying()) StopScene();
        m_Ctx.Scene = Cosmic::Scene::Create();
        // A new 3D scene opens with a sun so meshes are lit on the first frame.
        // A new 2D scene has no sun to open with — sprites are unlit, and the
        // Environment's Ambient2D is what a Light2D multiplies against.
#ifndef COSMIC_2D_ONLY
        Cosmic::Entity sun = m_Ctx.Scene->CreateEntity("Sun");
        sun.AddComponent<Cosmic::DirectionalLightComponent>();
#endif

        m_Ctx.SceneName    = "Untitled";
        m_Ctx.SceneVfsPath = "";
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Ctx.Recorded.clear();   // telemetry marks are per-UUID (E20)
        m_Ctx.ClearDirty();
        m_Ctx.Log("[Scene] New scene.");
    }

    void StarforgeApp::OpenScene(const std::string& vfsPath)
    {
        if (IsPlaying()) StopScene();
        Cosmic::Ref<Cosmic::Scene> fresh = Cosmic::Scene::Create();
        if (!Cosmic::SceneSerializer::Load(*fresh, Cosmic::FileSystem::Resolve(vfsPath)))
        {
            m_Ctx.Log("[Scene] Failed to open '" + vfsPath + "'.", LogSeverity::Error);
            return;
        }
        m_Ctx.Scene        = fresh;
        m_Ctx.SceneVfsPath = vfsPath;
        m_Ctx.SceneName    = fs::path(vfsPath).stem().string();
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Ctx.Recorded.clear();   // telemetry marks are per-UUID (E20)
        m_Ctx.ClearDirty();
        m_Ctx.Log("[Scene] Opened '" + vfsPath + "'.");
        AdoptCameraForScene();    // H8 — first frame shows the authored shot, not a void
        CheckScriptsBuilt();      // H8 — nudge if the scene needs a script build
    }

    bool StarforgeApp::SaveScene()
    {
        if (!m_Ctx.Scene || IsPlaying()) return true;   // never persist the runtime scene
        if (m_Ctx.SceneVfsPath.empty())
            return false;   // needs a name — caller opens Save As
        SaveSceneToVfs(m_Ctx.SceneVfsPath);
        return true;
    }

    void StarforgeApp::SaveSceneToVfs(const std::string& vfsPath)
    {
        if (!m_Ctx.Scene || IsPlaying()) return;
        std::error_code ec;
        fs::create_directories(Cosmic::FileSystem::Resolve("project://scenes"), ec);
        const std::string disk = Cosmic::FileSystem::Resolve(vfsPath);
        if (Cosmic::SceneSerializer::Save(*m_Ctx.Scene, disk))
        {
            m_Ctx.SceneVfsPath = vfsPath;
            m_Ctx.SceneName    = fs::path(vfsPath).stem().string();
            m_Ctx.ClearDirty();
            m_ThumbRequested = true;   // S7 — refresh the library thumbnail on save
            m_Ctx.Log("[Scene] Saved '" + vfsPath + "'.");
        }
        else
        {
            m_Ctx.Log("[Scene] Save FAILED: '" + vfsPath + "'.", LogSeverity::Error);
        }
    }

    // =========================================================================
    // Play mode (E13)
    // =========================================================================
    void StarforgeApp::PlayScene()
    {
        if (!m_Ctx.Scene || IsPlaying())
            return;

        // E6 failsafe: autosave the edit scene before entering Play.
        {
            std::error_code ec;
            const std::string dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName);
            fs::create_directories(dir, ec);
            Cosmic::SceneSerializer::Save(*m_Ctx.Scene, dir + "/" + m_Ctx.SceneName + ".cscene");
        }

        // Snapshot the edit scene to JSON and build a fresh runtime scene from it
        // (this dogfoods the serializer every Play). The edit scene is kept intact.
        const std::string snapshot = Cosmic::SceneSerializer::SaveToString(*m_Ctx.Scene);
        Cosmic::Ref<Cosmic::Scene> runtime = Cosmic::Scene::Create();
        if (!Cosmic::SceneSerializer::LoadFromString(*runtime, snapshot))
        {
            m_Ctx.Log("[Play] Failed to build the runtime scene.", LogSeverity::Error);
            return;
        }

        // U5/U8 — flow-driven Play: when the manifest names a startup flow (and
        // the Flow toggle is on), Play boots the .cflow from its start state,
        // exactly like the shipped player. A state referencing the OPEN scene
        // loads the live snapshot, so unsaved edits play as seen.
        m_PlayFlowActive = false;
        m_PrevEscape     = false;
        if (m_PlayFlowUse && !m_ManifestFlow.empty())
        {
            Cosmic::FlowAsset asset;
            std::string err;
            if (Cosmic::FlowAsset::Load(asset, "project://" + m_ManifestFlow, &err))
            {
                const std::string openKey = m_Ctx.SceneVfsPath.empty()
                    ? std::string() : Cosmic::AssetLibrary::NormalizeKey(m_Ctx.SceneVfsPath);
                m_PlayFlow.SetSceneLoader(
                    [this, snapshot, openKey](const std::string& path) -> Cosmic::Ref<Cosmic::Scene>
                {
                    Cosmic::Ref<Cosmic::Scene> s = Cosmic::Scene::Create();
                    const bool isOpen = !openKey.empty() &&
                                        Cosmic::AssetLibrary::NormalizeKey(path) == openKey;
                    const bool ok = isOpen
                        ? Cosmic::SceneSerializer::LoadFromString(*s, snapshot)
                        : Cosmic::SceneSerializer::Load(*s, Cosmic::FileSystem::Resolve(path));
                    if (!ok)
                    {
                        m_Ctx.Log("[Play] Flow could not load scene '" + path + "'.",
                                  LogSeverity::Error);
                        return nullptr;
                    }
                    return s;
                });
                m_PlayFlow.Start(asset);
                if (Cosmic::Ref<Cosmic::Scene> fs = m_PlayFlow.ActiveScene())
                {
                    runtime = fs;
                    m_PlayFlowActive = true;
                }
                else
                {
                    m_PlayFlow.Stop();
                    m_Ctx.Log("[Play] Startup flow produced no scene — playing the open scene.",
                              LogSeverity::Warn);
                }
            }
            else
            {
                m_Ctx.Log("[Play] Startup flow failed to load (" + err +
                          ") — playing the open scene.", LogSeverity::Warn);
            }
        }

        m_EditSceneBackup = m_Ctx.Scene;
        m_Ctx.Scene       = runtime;
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();      // no undo across the play boundary (v1)
        m_FixedAccum   = 0.0f;
        m_StepRequested = false;

        // Route script telemetry pushes to the panel (must be set before Instantiate
        // so OnCreate/OnStart pushes have a sink), then arm the take (E20).
        m_Scripts.SetTelemetrySink(&m_Telemetry);
        m_Scripts.Instantiate(*runtime);

        // J4 — build physics bodies from the runtime scene's components. Build the
        // recipe-driven world systems first (terrain heightfield etc.) so a
        // TerrainCollider has its CPU heightfield ready before OnPhysicsStart.
        // W7: the world-system build and the nav bind are 3D; PHYSICS IS NOT —
        // Init/OnPhysicsStart run identically on both engines.
#ifndef COSMIC_2D_ONLY
        runtime->SyncWorldSystems();
#endif
        m_Physics.Init();
        runtime->OnPhysicsStart(m_Physics);
#ifndef COSMIC_2D_ONLY
        runtime->OnNavStart();   // N4 — bind the DetourCrowd to the baked navmesh
#endif

        m_Telemetry.OnPlayStart(m_Ctx, m_FixedDt);

        // U7 — game-view state per Play session: start in the game camera, read
        // the manifest window size for the "Project" aspect preset.
        m_Ejected = false;
        m_NoPrimaryCamWarned = false;
        {
            const ProjectManifest pm = ProjectManifest::Load("project://project.cproj");
            m_ProjectW = pm.WindowWidth;
            m_ProjectH = pm.WindowHeight;
        }

        m_Play = PlayMode::Playing;
        m_Ctx.Log("[Play] Started — " + std::to_string(m_Scripts.LiveCount()) + " script(s).");
    }

    void StarforgeApp::StopScene()
    {
        if (!IsPlaying())
            return;
        Cosmic::Application::Get().GetWindow().SetCursorCaptured(false);   // U7
        m_GameCamActive = false;
        if (m_PlayFlowActive)   // U5/U8 — unsubscribe from scene buses first
        {
            m_PlayFlow.Stop();
            m_PlayFlowActive = false;
        }
        m_Telemetry.OnPlayStop(m_Ctx);        // flush + keep the take (E20)
        m_Scripts.SetTelemetrySink(nullptr);
        if (m_Ctx.Scene)
        {
#ifndef COSMIC_2D_ONLY
            m_Ctx.Scene->OnNavStop();                // N4 — release the crowd before the runtime scene
#endif
            m_Ctx.Scene->OnPhysicsStop(m_Physics);   // J4 — destroy bodies before the runtime scene
        }
        m_Physics.Shutdown();
        m_Scripts.Destroy();
        m_Ctx.Scene = m_EditSceneBackup;   // untouched edit scene
        m_EditSceneBackup.reset();
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();
        m_Play = PlayMode::Edit;
        m_Ctx.Log("[Play] Stopped — edit scene restored.");
    }

    void StarforgeApp::TogglePausePlay()
    {
        if      (m_Play == PlayMode::Playing) m_Play = PlayMode::Paused;
        else if (m_Play == PlayMode::Paused)  m_Play = PlayMode::Playing;
    }

    void StarforgeApp::StepScene()
    {
        if (m_Play == PlayMode::Paused)
            m_StepRequested = true;
    }

    void StarforgeApp::TickPlay(float ts)
    {
        // U7 — cursor capture for mouse-look: only while actively PLAYING in
        // the game camera; Esc releases (and unchecks, so it stays released).
        {
            if (m_CaptureCursor && Cosmic::Input::IsKeyPressed(CS_KEY_ESCAPE))
                m_CaptureCursor = false;
            Cosmic::Application::Get().GetWindow().SetCursorCaptured(
                m_CaptureCursor && m_Play == PlayMode::Playing && !m_Ejected);
        }

        if (m_Play == PlayMode::Playing)
        {
            // U5/U8 — advance the screen flow first (drains queued button signals
            // into transitions, mirrors PlayerLayer). A scene swap rebinds
            // scripts + physics to the flow's new top scene.
            if (m_PlayFlowActive)
            {
                const bool esc = Cosmic::Input::IsKeyPressed(CS_KEY_ESCAPE);
                if (esc && !m_PrevEscape)
                    m_PlayFlow.FeedSignal("key:Escape");
                m_PrevEscape = esc;

                m_PlayFlow.OnUpdate(ts);
                if (m_PlayFlow.QuitRequested())
                {
                    m_Ctx.Log("[Play] Flow reached @quit — stopping.");
                    StopScene();
                    return;
                }
                if (Cosmic::Ref<Cosmic::Scene> fs = m_PlayFlow.ActiveScene();
                    fs && fs != m_Ctx.Scene)
                {
                    if (m_Ctx.Scene)
                    {
#ifndef COSMIC_2D_ONLY
                        m_Ctx.Scene->OnNavStop();
#endif
                        m_Ctx.Scene->OnPhysicsStop(m_Physics);
                    }
                    m_Scripts.Destroy();
                    m_Ctx.Scene = fs;
                    m_Ctx.ClearSelection();
                    m_Scripts.Instantiate(*fs);
#ifndef COSMIC_2D_ONLY
                    fs->SyncWorldSystems();
#endif
                    fs->OnPhysicsStart(m_Physics);
#ifndef COSMIC_2D_ONLY
                    fs->OnNavStart();
#endif
                }
            }

            m_Scripts.Tick(ts);
            if (m_Ctx.Scene)
            {
                m_Ctx.Scene->UpdateSpriteAnimations(ts);   // U4 — flipbooks advance in editor Play
#ifndef COSMIC_2D_ONLY
                m_Ctx.Scene->UpdateAnimators(ts);          // A2/M6 — skeletal animators + crossfades
                                                           // advance in Play (after scripts, so a
                                                           // CrossfadeTo lands the same frame;
                                                           // Paused ⇒ not called ⇒ pose frozen)
#endif
            }
            m_FixedAccum += ts;
            int guard = 0;
            while (m_FixedAccum >= m_FixedDt && guard++ < 8)   // clamp catch-up
            {
                // Tick order contract (J4 + N4): scripts OnFixedUpdate -> physics step
                // -> nav step -> collision-event dispatch -> telemetry sample.
                m_Scripts.FixedTick(m_FixedDt);
                if (m_Ctx.Scene)
                {
                    m_Ctx.Scene->OnPhysicsStep(m_FixedDt);
#ifndef COSMIC_2D_ONLY
                    m_Ctx.Scene->OnNavStep(m_FixedDt);
#endif
                    m_Ctx.Scene->DispatchPhysicsEvents(m_Scripts);
                }
                m_Telemetry.OnFixedStep(m_Ctx);   // one telemetry sample per fixed step (E20)
                m_FixedAccum -= m_FixedDt;
            }
        }
        else if (m_Play == PlayMode::Paused && m_StepRequested)
        {
            m_Scripts.FixedTick(m_FixedDt);   // one deterministic step
            if (m_Ctx.Scene)
            {
                m_Ctx.Scene->OnPhysicsStep(m_FixedDt);
#ifndef COSMIC_2D_ONLY
                m_Ctx.Scene->OnNavStep(m_FixedDt);
#endif
                m_Ctx.Scene->DispatchPhysicsEvents(m_Scripts);
            }
            m_Telemetry.OnFixedStep(m_Ctx);
            m_StepRequested = false;
        }
    }

#ifndef COSMIC_2D_ONLY
    // =========================================================================
    // Navmesh (N3) — async bake orchestration (edit mode). The WorldSystems
    // terrain-build pattern: an Inspector "Regenerate now" (PendingNavBake) or an
    // AutoGenerate signature drift starts a one-shot JobSystem bake; we poll it here
    // and install + persist the `.cnav` sidecar when it lands. No frame stall.
    // =========================================================================
    void StarforgeApp::TickNavMeshes(float ts)
    {
        if (!m_Ctx.Scene) return;
        Cosmic::Scene& scene = *m_Ctx.Scene;
        auto& reg = scene.GetRegistry();

        const bool editMode = !IsPlaying();

        // 1) Explicit "Regenerate now" request from the Inspector (by entity UUID).
        if (m_Ctx.PendingNavBake != 0)
        {
            const uint64_t id = m_Ctx.PendingNavBake;
            m_Ctx.PendingNavBake = 0;
            if (editMode)
            {
                Cosmic::Entity e = scene.FindByUUID(Cosmic::UUID(id));
                if (e && e.HasComponent<Cosmic::NavMeshComponent>() && !m_NavBakes.count(id))
                {
                    m_NavBakes[id] = Cosmic::SceneNav::BeginBake(scene, (entt::entity)e);
                    m_Ctx.Log("[Nav] Baking navmesh in the background...");
                }
            }
        }

        // 2) AutoGenerate: rebake when the recipe/geometry signature drifts. Throttled
        //    (the gather is O(scene) — no per-frame cost when nothing changed).
        m_NavAutoTimer -= ts;
        if (editMode && m_NavAutoTimer <= 0.0f)
        {
            m_NavAutoTimer = 0.5f;
            for (auto e : reg.view<Cosmic::NavMeshComponent>())
            {
                auto& nm = reg.get<Cosmic::NavMeshComponent>(e);
                if (!nm.AutoGenerate || nm.Baking) continue;
                const uint64_t id = reg.get<Cosmic::IDComponent>(e).ID.Value();
                if (m_NavBakes.count(id)) continue;
                std::vector<float> v; std::vector<int> t;
                Cosmic::SceneNav::GatherGeometry(scene, e, v, t);
                if (Cosmic::SceneNav::Signature(nm, v, t) != nm.BuiltSignature)
                    m_NavBakes[id] = Cosmic::SceneNav::BeginBake(scene, e);
            }
        }

        // 3) Poll in-flight bakes; install + save the sidecar when done.
        for (auto it = m_NavBakes.begin(); it != m_NavBakes.end(); )
        {
            const uint64_t id = it->first;
            Cosmic::Entity e = scene.FindByUUID(Cosmic::UUID(id));
            if (!e || !e.HasComponent<Cosmic::NavMeshComponent>())
            {
                it = m_NavBakes.erase(it);   // entity gone (undo/delete) — drop the job
                continue;
            }
            if (!it->second.IsDone()) { ++it; continue; }

            Cosmic::SceneNav::FinishBake(scene, (entt::entity)e, it->second);
            auto& nm = e.GetComponent<Cosmic::NavMeshComponent>();
            if (nm.Nav && nm.Nav->IsBuilt())
            {
                // Persist a `.cnav` sidecar beside the scene (once saved), and set the
                // SidecarPath so it serializes + reloads next session (SyncNavMeshes).
                if (nm.SidecarPath.empty() && !m_Ctx.SceneVfsPath.empty())
                    nm.SidecarPath = Cosmic::SceneNav::SidecarPathFor(nm, m_Ctx.SceneVfsPath);
                if (!nm.SidecarPath.empty())
                    Cosmic::SceneNav::SaveSidecar(nm, nm.SidecarPath);
                m_Ctx.MarkDirty();
                m_Ctx.Log("[Nav] Navmesh baked.");
            }
            else
            {
                m_Ctx.Log("[Nav] Bake produced no walkable surface (check colliders / recipe).",
                          LogSeverity::Warn);
            }
            it = m_NavBakes.erase(it);
        }
    }
#endif   // COSMIC_2D_ONLY — TickNavMeshes

    // =========================================================================
    // Frame
    // =========================================================================
    void StarforgeApp::DrainLogQueue()
    {
        std::vector<std::pair<LogSeverity, std::string>> pending;
        {
            std::lock_guard<std::mutex> lk(m_LogQueueMutex);
            if (m_LogQueue.empty())
                return;
            pending.swap(m_LogQueue);
        }
        for (auto& [sev, text] : pending)
        {
            // T16 — the sink formats "[<logger>] msg"; the core logger is COSMIC
            // (Engine), anything else is the client/app logger (Game — e.g. scripts
            // during Play).
            const LogSource src = (text.rfind("[COSMIC]", 0) == 0)
                ? LogSource::Engine : LogSource::Game;
            m_Ctx.Log(text, sev, src);
        }
    }

    void StarforgeApp::AdoptCameraForScene()
    {
        // H8 — kill the "editor camera spawns inside the terrain / void" class of bug:
        // on scene open, adopt a Primary CameraComponent's pose (so the first frame is
        // the composed shot the author intended); otherwise frame all entity positions
        // (mesh assets may not be built until the first render, so use transforms).
        if (!m_Ctx.Scene)
            return;
        auto& reg = m_Ctx.Scene->GetRegistry();

        if (m_Settings.AdoptSceneCamera)
        {
            for (auto e : reg.view<Cosmic::CameraComponent, Cosmic::TransformComponent>())
            {
                const auto& cam = reg.get<Cosmic::CameraComponent>(e);
                if (!cam.Primary)
                    continue;
                const glm::mat4 world = m_Ctx.Scene->GetWorldTransform(Cosmic::Entity(e, m_Ctx.Scene.get()));
                const glm::vec3 pos = glm::vec3(world[3]);
                glm::vec3 fwd = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
                if (glm::length(fwd) < 1e-4f) fwd = { 0.0f, 0.0f, -1.0f };
                fwd = glm::normalize(fwd);

                const float dist = 10.0f;
                const glm::vec3 target = pos + fwd * dist;
                const glm::vec3 off = pos - target;   // = -fwd * dist, length dist
                const float pitch = glm::degrees(std::asin(glm::clamp(off.y / dist, -1.0f, 1.0f)));
                const float yaw   = glm::degrees(std::atan2(off.x, off.z));
                m_Rig.SetMode(EditorCameraRig::Mode::Orbit);   // scene open = CAD default
                m_Rig.RecallPose(target, yaw, pitch, dist);
                return;
            }
        }

        bool any = false;
        glm::vec3 mn(0.0f), mx(0.0f);
        for (auto e : reg.view<Cosmic::TransformComponent>())
        {
            const glm::vec3 p = reg.get<Cosmic::TransformComponent>(e).Position;
            if (!any) { mn = mx = p; any = true; }
            else      { mn = glm::min(mn, p); mx = glm::max(mx, p); }
        }
        if (any)
        {
            const glm::vec3 pad(2.0f);
            m_Rig.SetMode(EditorCameraRig::Mode::Orbit);   // scene open = CAD default
            m_Rig.FrameBounds(mn - pad, mx + pad, /*animate=*/false);
        }
    }

    void StarforgeApp::CheckScriptsBuilt()
    {
        // H8 — one actionable summary when a scene references script/system classes the
        // loaded module doesn't provide (build hasn't run). Per-entity warnings still
        // come from the ScriptHost at Play; this is the single "press Ctrl+B" nudge.
        m_ScriptsNeedBuild = false;
        if (!m_Ctx.Scene)
            return;
        auto& reg = m_Ctx.Scene->GetRegistry();
        int unresolved = 0;
        for (auto e : reg.view<Cosmic::NativeScriptComponent>())
        {
            const auto& nsc = reg.get<Cosmic::NativeScriptComponent>(e);
            if (!nsc.ClassName.empty() && !Cosmic::ModuleRegistry::Get().FindScript(nsc.ClassName))
                ++unresolved;
        }
        for (auto e : reg.view<Cosmic::SystemScriptComponent>())
        {
            const auto& ssc = reg.get<Cosmic::SystemScriptComponent>(e);
            if (!ssc.ClassName.empty() && !Cosmic::ModuleRegistry::Get().FindSystem(ssc.ClassName))
                ++unresolved;
        }
        if (unresolved > 0)
        {
            m_ScriptsNeedBuild = true;
            m_Ctx.Log("[Scripts] " + std::to_string(unresolved) +
                      " script(s) not built — press Ctrl+B to compile the project module.",
                      LogSeverity::Warn);
        }
    }

    void StarforgeApp::OnUpdate(float ts)
    {
        DrainLogQueue();   // engine-log lines → Console (H7)

        // Content-browser scene-open request (ignored mid-Play — Stop first).
        if (!m_Ctx.PendingOpenScene.empty())
        {
            const std::string p = m_Ctx.PendingOpenScene;
            m_Ctx.PendingOpenScene.clear();
            if (!IsPlaying())
                OpenScene(p);
        }

        // Content-browser prefab instantiate request (E14).
        if (!m_Ctx.PendingInstantiatePrefab.empty())
        {
            const std::string p = m_Ctx.PendingInstantiatePrefab;
            m_Ctx.PendingInstantiatePrefab.clear();
            if (m_Ctx.Scene && !IsPlaying())
                Prefabs::Instantiate(m_Ctx, p);
        }

        // Content-browser "Open in Animation Editor" request (M1/M3): open (or
        // re-focus) a document for the rigged model in the AssetEditorHost.
        // W7: the 2D build has no AnimationEditor TU. AssetTypes never tags a
        // file with AssetOpen::AnimationEditor there, so this request is never
        // raised — the clear() below keeps a hand-set path from wedging.
#ifndef COSMIC_2D_ONLY
        if (!m_Ctx.PendingOpenAnimEditor.empty())
        {
            const std::string p = m_Ctx.PendingOpenAnimEditor;
            m_Ctx.PendingOpenAnimEditor.clear();
            m_Editors.Open(p, [p]() { return std::make_unique<AnimationEditor>(p); }, &m_ShowEditors);
        }
#else
        m_Ctx.PendingOpenAnimEditor.clear();
#endif

        // Content-browser graph-document request (Q1/Q4): open (or re-focus) a
        // .cflow / .cstory document in the AssetEditorHost, dispatched by extension.
        if (!m_Ctx.PendingOpenDocument.empty())
        {
            const std::string p = m_Ctx.PendingOpenDocument;
            m_Ctx.PendingOpenDocument.clear();
            const std::string ext = fs::path(p).extension().string();
            if (ext == ".cflow")
                m_Editors.Open(p, [p]() { return std::make_unique<FlowEditor>(p); }, &m_ShowEditors);
            else if (ext == ".cstory")
                m_Editors.Open(p, [p]() { return std::make_unique<StoryEditor>(p); }, &m_ShowEditors);
        }

        // Content-browser model import request (T8): seed + open the E16 modal so
        // the user can set the .cmeta scale/up-axis before importing. 3D-only —
        // MeshImport is out of the 2D build (§4), so the request is dropped.
        if (!m_Ctx.PendingImportModel.empty())
        {
#ifndef COSMIC_2D_ONLY
            std::snprintf(m_ImportPath, sizeof(m_ImportPath), "%s", m_Ctx.PendingImportModel.c_str());
            m_OpenImportModel = true;
#endif
            m_Ctx.PendingImportModel.clear();
        }

        // Build pump (E12/S5): stream cmake output; on completion either hot-reload
        // the module or run the packaging pipeline, per the build's purpose.
        m_Builder.Poll(m_Ctx, [this](bool ok)
        {
            if (m_BuildPurpose == BuildPurpose::Package)
            {
                OnPackageBuildDone(ok);
            }
            else if (ok)
            {
                ReloadModule(m_LastBuiltStem);
            }
            else
            {
                m_Ctx.Log("[Build] Failed — see the Console. Keeping the current module.",
                          LogSeverity::Error);
            }
        });
        if (m_SrcWatchOn)
        {
            const auto changes = m_SrcWatcher.Poll();   // always drained
            if (m_AutoBuild && !changes.empty() && !m_Builder.IsBuilding() && !IsPlaying())
                BuildScripts();
        }

#ifndef COSMIC_2D_ONLY
        m_WorldSystems.OnUpdate(m_Ctx);   // E18 — drain the async terrain build
#endif
        m_Editors.OnUpdate(m_Ctx, ts);    // M1 — advance open document playback (Animation Editor scrub/play)
#ifndef COSMIC_2D_ONLY
        TickNavMeshes(ts);                // N3 — drain async navmesh (re)bakes + AutoGenerate
#endif

        // K1 — branding hot-swap: a change to the resolved icon.png re-applies the
        // window/taskbar icon + top-bar logo, debounced past the file copy.
        if (m_BrandWatcher.IsWatching())
        {
            for (const auto& c : m_BrandWatcher.Poll())
            {
                if (c.Path.find("icon.png") != std::string::npos)
                {
                    m_BrandDebounce = 0.35f;
                    m_BrandRetried  = false;
                    break;
                }
            }
        }
        if (m_BrandDebounce >= 0.0f)
        {
            m_BrandDebounce -= ts;
            if (m_BrandDebounce < 0.0f)
            {
                m_BrandDebounce = -1.0f;
                ApplyBrand();
            }
        }

        TickPlay(ts);
        RenderViewport(ts);
        Autosave(ts);
        UpdateWindowTitle();
    }

    void StarforgeApp::RenderViewport(float ts)
    {
        auto& app = Cosmic::Application::Get();
        auto* ws  = app.GetWorkspaceLayer();

        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        const bool vpHovered = ws && ws->IsViewportHovered();
        if (m_Mode2D)
        {
            // U3 — the 2D rig drives the viewport (MMB pan / wheel zoom); the
            // orbit camera keeps its pose for when the user toggles back.
            m_Camera2D.SetViewportRect(vpPos, vpSize);
            m_Camera2D.SetControlEnabled((vpHovered || m_Camera2D.IsDragging()) && !m_Viewport.GizmoBusy());
            m_Camera2D.OnUpdate(ts);
        }
        else
        {
            // K7 — the camera rig drives the 3D viewport: Orbit by default,
            // RMB-hold = temporary Fly (WASD+QE, scroll = speed), Possess =
            // read-only render from a scene camera. Pose hand-offs are seamless.
            const bool controlOk = (vpHovered || m_Rig.Orbit().IsDragging() ||
                                    m_Rig.Fly().IsLooking()) && !m_Viewport.GizmoBusy();
            const bool allowTempFly = !IsPlaying() && vpHovered && !m_Viewport.GizmoBusy();
            m_Rig.OnUpdate(m_Ctx, ts, vpPos, vpSize, controlOk, allowTempFly);
        }
        auto vfb = app.GetFrameBuffer();
        if (!vfb)
            return;

        // K8 — the navigation cube's offscreen pass runs OUTSIDE the main scene
        // (its own FBO; skipped in Play/2D where the widget is hidden). The cube
        // draws with direct Renderer3D calls, so the 2D build has no widget.
#ifndef COSMIC_2D_ONLY
        if (m_Ctx.Scene && !m_GameCamActive)
            m_Viewport.PrerenderNavCube(m_Rig.ActiveCamera(), IsPlaying(), m_Mode2D);
#endif

        // H2 — SceneRenderer is THE editor render path: environment/sky/shadows/HDR
        // + post all live here (and byte-identically in the standalone PlayerLayer).
        const uint32_t vw = vfb->GetWidth(), vh = vfb->GetHeight();
        if (!m_SceneRenderer.IsInitialized())
            m_SceneRenderer.Init(vw, vh);
        m_SceneRenderer.SetViewportSize(vw, vh);

#ifdef COSMIC_2D_ONLY
        // W7 — the 2D stats chip + Profiler read Renderer2D's batch counters,
        // which are OPT-IN (Renderer2D::StatsEnabled defaults false) and are
        // never reset by the engine. Arm them once and zero them per frame, so
        // the chip shows this frame's batch cost instead of a flat zero.
        // (Renderer3D's counters are always-on but Starforge never resets them,
        // so the 3D chip shows lifetime totals — a separate, pre-existing quirk
        // left alone here to keep the 3D build behaviour-identical.)
        {
            static bool s_Stats2DArmed = false;
            if (!s_Stats2DArmed) { Cosmic::Renderer2D::SetStatsStatus(true); s_Stats2DArmed = true; }
            Cosmic::Renderer2D::ResetStats();
        }
#endif

        // ---- Game view (U7): primary-camera adoption + letterbox band ------
        // While PLAYING (not ejected) the viewport shows the primary
        // CameraComponent's view, exactly like the shipped player; the aspect
        // preset letterboxes the frame (band centered, bars drawn by the ImGui
        // overlay) and the projection is NDC-scaled into the band.
        m_GameCamActive = false;
        float bx = 0.0f, by = 0.0f, bw = (float)vw, bh = (float)vh;
        if (IsPlaying() && !m_Ejected && m_Ctx.Scene)
        {
            auto& reg = m_Ctx.Scene->GetRegistry();
            for (auto e : reg.view<Cosmic::CameraComponent, Cosmic::TransformComponent>())
            {
                const auto& cc = reg.get<Cosmic::CameraComponent>(e);
                if (!cc.Primary)
                    continue;

                // Band from the aspect preset (fit + center; the resolution
                // preset additionally caps at its exact pixel size).
                float ar = 0.0f;   // 0 => Free (fill the viewport)
                switch (m_Aspect)
                {
                    case GameAspect::W16H9:      ar = 16.0f / 9.0f; break;
                    case GameAspect::R1920x1080: ar = 16.0f / 9.0f; break;
                    case GameAspect::Project:
                        if (m_ProjectW > 0 && m_ProjectH > 0)
                            ar = (float)m_ProjectW / (float)m_ProjectH;
                        break;
                    default: break;
                }
                if (ar > 0.0f)
                {
                    if ((float)vw / (float)vh > ar) { bh = (float)vh; bw = bh * ar; }
                    else                            { bw = (float)vw; bh = bw / ar; }
                    if (m_Aspect == GameAspect::R1920x1080 && bw > 1920.0f)
                    {
                        bw = 1920.0f; bh = 1080.0f;
                    }
                    bx = ((float)vw - bw) * 0.5f;
                    by = ((float)vh - bh) * 0.5f;
                }

                const glm::mat4 world = m_Ctx.Scene->GetWorldTransform(
                    Cosmic::Entity(e, m_Ctx.Scene.get()));
                glm::mat4 proj = cc.GetProjection(bh > 0.0f ? bw / bh : 1.0f);
                // NDC-scale the band into place (identity when the band fills).
                proj = glm::scale(glm::mat4(1.0f),
                                  { bw / (float)vw, bh / (float)vh, 1.0f }) * proj;
                m_GameCamera.Set(glm::inverse(world), proj, glm::vec3(world[3]));
                m_GameCamActive = true;
                break;
            }
            if (!m_GameCamActive && !m_NoPrimaryCamWarned)
            {
                m_NoPrimaryCamWarned = true;
                m_Ctx.Log("[Play] No Primary CameraComponent — using the editor camera "
                          "(a shipped app would warn the same).", LogSeverity::Warn);
            }
        }
        if (!m_GameCamActive) { bx = 0.0f; by = 0.0f; bw = (float)vw; bh = (float)vh; }
        m_GameBandUv = { bx / (float)vw, by / (float)vh, bw / (float)vw, bh / (float)vh };

        const Cosmic::Camera& activeCam = m_GameCamActive
            ? static_cast<const Cosmic::Camera&>(m_GameCamera)
            : (m_Mode2D ? static_cast<const Cosmic::Camera&>(m_Camera2D.GetCamera())
                        : m_Rig.ActiveCamera());

        vfb->Bind();
        Cosmic::RenderCommand::SetViewport(0, 0, vw, vh);
        Cosmic::RenderCommand::SetClearColor({ 0.086f, 0.098f, 0.129f, 1.0f });
        Cosmic::RenderCommand::Clear();

        // R8 — Entity-ID debug view: flat hash-colored meshes instead of the
        // SceneRenderer frame (sprites/UI/water are not entity meshes and are
        // deliberately absent from it). Picking still works — the picker runs
        // its own ID pass.
        const ViewportController::ViewMode viewMode = m_Viewport.GetViewMode();
#ifndef COSMIC_2D_ONLY
        if (m_Ctx.Scene && viewMode == ViewportController::ViewMode::EntityID)
        {
            m_Viewport.SetOutlinePassActive(false);   // wire boxes carry selection here
            m_Viewport.DrawEntityIdView(m_Ctx, activeCam);
            if (m_ThumbRequested) { m_ThumbRequested = false; CaptureThumbnail(); }
            return;
        }
#endif

        if (m_Ctx.Scene)
        {
#ifndef COSMIC_2D_ONLY
            // A2 — play preview in edit mode: animators sample every frame here
            // (during Play, TickPlay advanced them this frame and this renders
            // the SAME scene object — so gate on the mode; Paused ⇒ frozen pose).
            if (!IsPlaying())
                m_Ctx.Scene->UpdateAnimators(ts);
#endif

            Cosmic::SceneRenderDesc desc;
            m_Ctx.Scene->BuildRenderDesc(activeCam, ts, desc);
            desc.Settings.ClearColor = { 0.086f, 0.098f, 0.129f, 1.0f };

            if (auto* env = m_Ctx.Scene->FindEnvironment(); env && !m_Mode2D)
            {
                m_SceneRenderer.ApplyEnvironment(*env, desc);
            }
            else
            {
                // No Environment entity → keep today's flat grey-blue viewport
                // (no sky/IBL/shadows) so a scene without one looks unchanged.
                // 2D mode forces the same: a skybox under an ortho projection is
                // degenerate, and the flat backdrop is the 2D authoring surface.
                desc.Settings.Skybox  = false;
                desc.Settings.IBL     = false;
                desc.Settings.Shadows = false;
            }

            // R8 — view-mode overrides, applied after the environment so they win.
            if (viewMode == ViewportController::ViewMode::Wireframe)
            {
                // Pure geometry read: lines over the clear color, no lighting fx.
                desc.Settings.Wireframe = true;
                desc.Settings.Shadows   = false;
                desc.Settings.SSAO      = false;
                desc.Settings.Bloom     = false;
                desc.Settings.GodRays   = false;
                desc.Settings.LensFlare = false;
            }
#ifndef COSMIC_2D_ONLY
            else if (viewMode == ViewportController::ViewMode::Unlit)
            {
                // Flat albedo: no sun/points/IBL/shadows; ambient floor at 1 lights
                // every face fully. The sky stays (it is emissive by definition).
                desc.Settings.IBL     = false;
                desc.Settings.Shadows = false;
                desc.Settings.SSAO    = false;
                desc.Lights.SunIntensity = 0.0f;
                desc.Lights.Points.clear();
                desc.Lights.Ambient      = 1.0f;
            }
#endif

            // K12 — selection outline: the post-composite silhouette ring replaces
            // the mesh wire boxes (which stay as the fallback for un-meshed
            // selections — lights, colliders — and for the bypass view modes).
            // The outline pass rides on ScenePicker, which is 3D-only; the 2D
            // build keeps the wire-rect selection the 2D overlay already draws.
#ifndef COSMIC_2D_ONLY
            desc.Settings.OutlineEnabled = true;
            desc.SelectedEntities        = &m_Ctx.Selection;
            m_Viewport.SetOutlinePassActive(true);
#else
            m_Viewport.SetOutlinePassActive(false);
#endif

            // Editor overlays — drawn in HDR with scene depth still bound so they
            // occlude correctly. 2D mode swaps the ground grid for the pixel grid;
            // world-space sprites (U3) draw in BOTH modes, exactly like the player.
            Cosmic::Scene* scenePtr = m_Ctx.Scene.get();
            desc.DrawTransparent = [this, scenePtr, vw, vh](const Cosmic::SceneDrawContext& c)
            {
#ifndef COSMIC_2D_ONLY
                if (m_Mode2D) m_Viewport.DrawOverlayContent2D(m_Ctx, m_Camera2D);
                else          m_Viewport.DrawOverlayContent(m_Ctx);
#else
                // 2D mode is the only mode here, and the overlay is the
                // Renderer2D twin (pixel grid + sprite selection + tile
                // visuals + 2D light glyphs). See ViewportController.cpp.
                m_Viewport.DrawOverlayContent2D(m_Ctx, m_Camera2D);
#endif
                scenePtr->OnRenderSprites(c.ViewProjection, vw, vh);
                // X5 — 2D lights multiply over the sprite output (no-op without lights).
                scenePtr->OnRender2DLights(c.ViewProjection, vw, vh);
#ifdef COSMIC_2D_ONLY
                // W7 — the collider overlay draws LAST, over the sprites: a
                // collider normally sits exactly on its sprite, so drawing it
                // with the rest of the overlay (which has to stay under the art)
                // buried it completely. Found in the on-GPU pass.
                m_Viewport.DrawColliderOverlay2D(m_Ctx, m_Camera2D);
#endif
            };

            // U1 — canvas UI composites after post (LDR bound). U7: the canvases
            // lay out in the letterbox band so authored anchors are truthful
            // (band == full viewport whenever the game camera is not active).
            const glm::vec4 bandUv = m_GameBandUv;
            const glm::mat4 camVP = desc.Projection * desc.View;   // X6 — world-anchor projector
            desc.DrawOverlay2D = [scenePtr, vw, vh, bandUv, camVP]()
            {
                const Cosmic::UiRect band{
                    { bandUv.x * (float)vw,                       bandUv.y * (float)vh },
                    { (bandUv.x + bandUv.z) * (float)vw,          (bandUv.y + bandUv.w) * (float)vh } };
                Cosmic::UiSystem::Render(*scenePtr, band, vw, vh, &camVP);
            };

            m_SceneRenderer.Render(desc);   // PRE/POST: vfb stays the bound target
        }

        // S7 — thumbnail capture happens here, while the viewport FBO is the bound,
        // just-composited target (requested by a scene Save).
        if (m_ThumbRequested && m_Ctx.Scene)
        {
            m_ThumbRequested = false;
            CaptureThumbnail();
        }

        // A4 — budgeted asset-thumbnail generation (Content Browser requests).
        // Runs with the frame composited; every rig pass restores the bound
        // FBO + render-state defaults (doc 13 §0.5), which the self-test below
        // exists to prove.
        m_Ctx.Preview.PumpThumbnails(2);

        // A4 acceptance — Help ▸ Preview State Self-Test. Three frames on a
        // static scene: capture A (control baseline), capture B (must equal A —
        // proves the scene itself is deterministic), then run every PreviewRig
        // path and capture C (must equal B — proves the preview passes leak no
        // GL state into the scene render).
        // W7: the 2D build has no preview pass to leak state, so there is
        // nothing for this to prove — the test and its Help item both fence.
#ifndef COSMIC_2D_ONLY
        if (m_PreviewSelfTest > 0 && m_Ctx.Scene)
        {
            std::vector<uint8_t> pix;
            uint32_t w = 0, h = 0;
            vfb->Bind();
            const bool read = vfb->ReadPixels(0, pix, w, h);
            if (!read)
            {
                m_PreviewSelfTest = 0;
                m_Ctx.Log("[Preview] Self-test aborted — viewport read-back failed.",
                          LogSeverity::Error);
            }
            else if (m_PreviewSelfTest == 1)         // frame A: baseline
            {
                m_SelfTestPixels = std::move(pix);
                m_SelfTestW = w; m_SelfTestH = h;
                m_PreviewSelfTest = 2;
            }
            else if (m_PreviewSelfTest == 2)         // frame B: control, then previews
            {
                if (w != m_SelfTestW || h != m_SelfTestH || pix != m_SelfTestPixels)
                {
                    m_PreviewSelfTest = 0;
                    m_SelfTestPixels.clear();
                    m_Ctx.Log("[Preview] Self-test INCONCLUSIVE — the scene is not static "
                              "(frames differ before any preview ran). Retry without moving "
                              "the camera, in a scene without animated water/particles/sky.",
                              LogSeverity::Warn);
                }
                else
                {
                    // Hammer every preview path between the two compared frames.
                    Cosmic::MaterialAsset probe;
                    probe.Albedo    = { 0.85f, 0.30f, 0.10f, 1.0f };
                    probe.Metallic  = 0.7f;
                    probe.Roughness = 0.25f;
                    m_Ctx.Preview.RenderMaterial(probe, 220, 160);
                    m_Ctx.Preview.Orbit(24.0f, -10.0f);
                    m_Ctx.Preview.RenderMaterial(probe, 128, 128);
                    m_Ctx.Preview.PumpThumbnails(8);
                    m_SelfTestPixels = std::move(pix);
                    m_PreviewSelfTest = 3;
                }
            }
            else                                      // frame C: the verdict
            {
                m_PreviewSelfTest = 0;
                if (w == m_SelfTestW && h == m_SelfTestH && pix == m_SelfTestPixels)
                    m_Ctx.Log("[Preview] State-restore self-test PASSED — scene render "
                              "byte-identical after interactive + thumbnail preview passes ("
                              + std::to_string(w) + "x" + std::to_string(h) + ").");
                else
                    m_Ctx.Log("[Preview] State-restore self-test FAILED — the preview pass "
                              "leaked GL state into the scene render.", LogSeverity::Error);
                m_SelfTestPixels.clear();
                m_SelfTestPixels.shrink_to_fit();
            }
        }
#endif   // COSMIC_2D_ONLY — the A4 preview state self-test
    }

    void StarforgeApp::Autosave(float ts)
    {
        if (IsPlaying())            // never autosave the throwaway runtime scene
            return;
        if (!m_Ctx.Dirty || !m_Ctx.Scene)
            return;
        m_AutosaveTimer += ts;
        const float interval = m_Settings.AutosaveMinutes * 60.0f;
        if (interval <= 0.0f || m_AutosaveTimer < interval)
            return;
        m_AutosaveTimer = 0.0f;

        std::error_code ec;
        const std::string dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName);
        fs::create_directories(dir, ec);
        const std::string path = dir + "/" + m_Ctx.SceneName + ".cscene";
        if (Cosmic::SceneSerializer::Save(*m_Ctx.Scene, path))
            m_Ctx.Log("[Autosave] " + path);
    }

    void StarforgeApp::UpdateWindowTitle()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws) return;
        std::string title = "Starforge";
        if (m_Ctx.ProjectOpen)
            title += "  —  " + m_Ctx.ProjectTitle + " / " + m_Ctx.SceneName + (m_Ctx.Dirty ? " *" : "");
        ws->SetProjectName(title);
        // Mirror onto the real Win32 title: the drawn chrome is invisible to task
        // switchers, screen capture, and desktop-automation tools — they identify
        // the window by its OS title (same S5 verb packaged apps use; idempotent).
        Cosmic::Application::Get().GetWindow().SetTitle(title);

        // The central viewport tab shows the scene name + dirty star (H5). The
        // duplicate corner overlay text is dropped (see OnImGuiRender).
        if (m_Ctx.ProjectOpen)
            ws->SetViewportTitle(m_Ctx.SceneName + (m_Ctx.Dirty ? " *" : ""));
        else
            ws->SetViewportTitle("Viewport");
    }

    // =========================================================================
    // ImGui
    // =========================================================================
    void StarforgeApp::ApplyDockLayout()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws)
            return;

        // Top edge sized by a PIXEL minimum (H5) so the menu row + the Play/Build/
        // gizmo toolbar row are always fully visible; the engine's own File/View
        // chrome menus are hidden (Starforge has its own). The dock tree itself is
        // now a LAYOUT PRESET (K3) — the coded default is the "Level" built-in.
        ws->SetChromeMenusVisible(false);
        ApplyLayoutPreset(m_ActivePreset.empty() ? "Level" : m_ActivePreset);

        m_DockApplied = true;
    }

    // ---- Workspace layout presets (K3) --------------------------------------

    LayoutPanels StarforgeApp::PanelSet()
    {
        LayoutPanels p;
        p.Hierarchy    = &m_ShowHierarchy;
        p.Inspector    = &m_ShowInspector;
        p.Content      = &m_ShowContent;
        p.Console      = &m_ShowConsole;
        p.Environment  = &m_ShowEnvironment;
        p.Material     = &m_ShowMaterial;
        p.WorldSystems = &m_ShowWorldSystems;
        p.Voxel        = &m_ShowVoxel;
        p.TilePalette  = &m_ShowTilePalette;
        p.Telemetry    = &m_ShowTelemetry;
        p.Stats        = &m_ShowStats;
        p.Profiler     = &m_ShowProfiler;   // T17
        p.System       = &m_ShowSystem;     // T18
        p.Editors      = &m_ShowEditors;    // M1
        return p;
    }

    std::string StarforgeApp::ProjectLayoutKey() const
    {
        if (!m_Ctx.ProjectOpen) return {};
        return m_Ctx.ProjectPath.empty() ? m_Ctx.ProjectName : m_Ctx.ProjectPath;
    }

    void StarforgeApp::ApplyLayoutPreset(const std::string& name)
    {
        const LayoutPanels panels = PanelSet();
        if (LayoutPresets::IsBuiltIn(name) || name.empty())
        {
            LayoutPresets::ApplyBuiltIn(name.empty() ? "Level" : name, panels);
            m_ActivePreset = name.empty() ? "Level" : name;
        }
        else
        {
            // User snapshot: visibility applies now; the ini blob loads at the
            // TOP of the next ImGui frame (windows re-dock as they resubmit).
            std::string ini;
            if (!LayoutPresets::LoadUser(name, panels, ini))
            {
                m_Ctx.Log("[Layout] Preset '" + name + "' could not be loaded — using Level.",
                          LogSeverity::Warn);
                LayoutPresets::ApplyBuiltIn("Level", panels);
                m_ActivePreset = "Level";
            }
            else
            {
                m_PendingLayoutIni = std::move(ini);
                m_ActivePreset     = name;
            }
        }
        LayoutPresets::SaveActive(ProjectLayoutKey(), m_ActivePreset);
    }

    void StarforgeApp::DrawLayoutPresetPicker(float squareSize)
    {
        // A compact dropdown (the reference editors' layout tabs, folded into one
        // control for width budget): built-ins, user presets, save/delete.
        (void)squareSize;
        const std::string label = std::string(ICON_LC_LAYOUT) + " " + m_ActivePreset;
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(label.c_str()).x + 34.0f);
        if (ImGui::BeginCombo("##k3layout", label.c_str(), ImGuiComboFlags_HeightLargest))
        {
            for (const auto& n : LayoutPresets::BuiltIns())
                if (ImGui::Selectable(n.c_str(), n == m_ActivePreset))
                    ApplyLayoutPreset(n);

            const auto user = LayoutPresets::UserPresets();
            if (!user.empty())
            {
                ImGui::Separator();
                for (const auto& n : user)
                {
                    if (ImGui::Selectable((n + "##user").c_str(), n == m_ActivePreset))
                        ApplyLayoutPreset(n);
                    if (ImGui::BeginPopupContextItem((n + "##ctx").c_str()))
                    {
                        if (ImGui::MenuItem(("Delete '" + n + "'").c_str()))
                        {
                            LayoutPresets::DeleteUser(n);
                            if (m_ActivePreset == n)
                                ApplyLayoutPreset("Level");
                        }
                        ImGui::EndPopup();
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Selectable("Save layout as…"))
                m_OpenSaveLayout = true;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Workspace layout preset (right-click a custom preset to delete).\n"
                              "The active preset is remembered per project.");
    }

    void StarforgeApp::DrawSaveLayoutPopup()
    {
        if (m_OpenSaveLayout)
        {
            ImGui::OpenPopup("Save Layout As");
            m_OpenSaveLayout = false;
        }
        if (ImGui::BeginPopupModal("Save Layout As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save the current window arrangement as a preset:");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##k3name", m_LayoutNameBuf, sizeof(m_LayoutNameBuf));
            ImGui::TextDisabled("Letters, digits, space, - and _ (a filename).");

            const std::string name = m_LayoutNameBuf;
            const bool clash = LayoutPresets::IsBuiltIn(name);
            if (clash)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                   "That name is a built-in preset.");
            ImGui::BeginDisabled(name.empty() || clash);
            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                if (LayoutPresets::SaveUser(name, PanelSet()))
                {
                    m_ActivePreset = name;
                    LayoutPresets::SaveActive(ProjectLayoutKey(), m_ActivePreset);
                    m_Ctx.Log("[Layout] Saved preset '" + name + "'.");
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    m_Ctx.Log("[Layout] Could not save '" + name + "' (bad name or unwritable folder).",
                              LogSeverity::Error);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void StarforgeApp::OnImGuiRender()
    {
        // K3 — a user layout preset loads its ini snapshot at the top of the
        // frame; windows re-dock by name as they resubmit (ImGui ApplyAll).
        if (!m_PendingLayoutIni.empty())
        {
            ImGui::LoadIniSettingsFromMemory(m_PendingLayoutIni.c_str(), m_PendingLayoutIni.size());
            m_PendingLayoutIni.clear();
        }

        if (!m_DockApplied)
            ApplyDockLayout();

        m_Ctx.Playing = IsPlaying();   // T15 — mirror play state for the panels

        DrawTopBar();
        DrawStatusBar();   // K5 — bottom strip (reserves its band; hides on home)

        if (m_Ctx.ProjectOpen)
        {
            // Pass each panel its visibility bool as p_open (H5) so its ✕ close
            // button flips the same flag the View-menu checkmark reads — they stay
            // in sync, and Reset Layout (which never clears these) reopens them.
            if (m_ShowHierarchy)    m_Hierarchy.OnImGuiRender(m_Ctx, &m_ShowHierarchy);
            if (m_ShowInspector)    m_Inspector.OnImGuiRender(m_Ctx, &m_ShowInspector);
            if (m_ShowContent)      m_Content.OnImGuiRender(m_Ctx, &m_ShowContent);
            else                    m_Ctx.PendingDroppedFiles.clear();   // T8 — don't accrue drops while the browser is closed
            if (m_ShowConsole)      m_Console.OnImGuiRender(m_Ctx, &m_ShowConsole);
            if (m_ShowEnvironment)  m_Environment.OnImGuiRender(m_Ctx, &m_ShowEnvironment);
            if (m_ShowMaterial)     m_Material.OnImGuiRender(m_Ctx, &m_ShowMaterial);
#ifndef COSMIC_2D_ONLY
            if (m_ShowWorldSystems) m_WorldSystems.OnImGuiRender(m_Ctx, &m_ShowWorldSystems);
            if (m_ShowVoxel)        m_Voxel.OnImGuiRender(m_Ctx, &m_ShowVoxel);
#endif
            if (m_ShowTilePalette)  m_TilePalette.OnImGuiRender(m_Ctx, &m_ShowTilePalette);
            if (m_ShowTelemetry)    m_Telemetry.OnImGuiRender(m_Ctx, &m_ShowTelemetry);
            if (m_ShowProfiler)     m_Profiler.OnImGuiRender(m_Ctx, &m_ShowProfiler);
            if (m_ShowSystem)       m_System.OnImGuiRender(m_Ctx, &m_ShowSystem);
            if (m_ShowPostChain)    m_PostChain.OnImGuiRender(m_Ctx, &m_ShowPostChain);   // Q6
            // M1 — the asset-editor document host stays visible while any document
            // is open even if the panel bool was toggled off (closing docs is the
            // tab ✕, not the panel ✕); auto-shown when a document opens.
            if (m_ShowEditors || m_Editors.AnyOpen())
                m_Editors.OnImGuiRender(m_Ctx, &m_ShowEditors);
            if (m_ShowStats)        DrawStatsWindow();
        }
        else
        {
            DrawHomescreen();
        }

        // Viewport overlay: the K6 header strip + K8 nav cube + K9 stats chips,
        // then the transform gizmo (only with a project open; the homescreen
        // fills the viewport region otherwise).
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (m_Ctx.ProjectOpen && ws && ws->BeginViewportOverlay())
        {
            const glm::vec2 pos = Cosmic::Application::Get().GetViewportPos();

            // K6/K8/K9 — the viewport instrument (strip hides while playing).
            if (m_Ctx.Scene)
                m_Viewport.DrawViewportOverlays(m_Ctx, m_Rig, IsPlaying(), m_Mode2D);

            // K13 — Content-Browser drops onto the viewport (spawn at the hit
            // point / assign material / assign sprite image; single undo each).
            if (m_Ctx.Scene)
                m_Viewport.UpdateViewportDragDrop(
                    m_Ctx,
                    m_Mode2D ? static_cast<const Cosmic::Camera&>(m_Camera2D.GetCamera())
                             : m_Rig.ActiveCamera(),
                    m_Mode2D ? &m_Camera2D : nullptr,
                    IsPlaying());

            // H8 — actionable hint while the scene references unbuilt script classes.
            if (m_ScriptsNeedBuild && !IsPlaying())
            {
                ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 44.0f));
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f),
                                   "Scripts not built - press Ctrl+B");
            }
            // The gizmo is an edit tool — hidden while playing (runtime scene).
            // 2D mode manipulates through the ortho camera (ImGuizmo auto-detects);
            // 3D uses the rig's active camera so Fly/Possess manipulate correctly.
            if (m_Ctx.Scene && !IsPlaying())
                m_Viewport.DrawGizmo(m_Ctx, m_Mode2D
                    ? static_cast<const Cosmic::Camera&>(m_Camera2D.GetCamera())
                    : m_Rig.ActiveCamera());
        }
        if (m_Ctx.ProjectOpen && ws)
            ws->EndViewportOverlay();

        // Play-mode viewport border tint (the universal "you are live" cue).
        if (IsPlaying())
        {
            const glm::vec2 p = Cosmic::Application::Get().GetViewportPos();
            const glm::vec2 s = Cosmic::Application::Get().GetViewportSize();

            // U7 — letterbox bars over the regions outside the game band (the
            // scene bleeds past the NDC-scaled band; the bars mask it).
            if (m_GameCamActive &&
                (m_GameBandUv.x > 0.0001f || m_GameBandUv.y > 0.0001f ||
                 m_GameBandUv.z < 0.9999f || m_GameBandUv.w < 0.9999f))
            {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                const ImU32 bar = IM_COL32(8, 9, 12, 255);
                const float bx0 = p.x + m_GameBandUv.x * s.x;
                const float by0 = p.y + m_GameBandUv.y * s.y;
                const float bx1 = bx0 + m_GameBandUv.z * s.x;
                const float by1 = by0 + m_GameBandUv.w * s.y;
                if (by0 > p.y)        dl->AddRectFilled({ p.x, p.y },        { p.x + s.x, by0 },       bar);
                if (by1 < p.y + s.y)  dl->AddRectFilled({ p.x, by1 },        { p.x + s.x, p.y + s.y }, bar);
                if (bx0 > p.x)        dl->AddRectFilled({ p.x, by0 },        { bx0, by1 },             bar);
                if (bx1 < p.x + s.x)  dl->AddRectFilled({ bx1, by0 },        { p.x + s.x, by1 },       bar);
            }

            const ImU32 col = (m_Play == PlayMode::Paused)
                ? IM_COL32(255, 200, 50, 255) : IM_COL32(70, 220, 90, 255);
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y), col, 0.0f, 0, 3.0f);
        }

        // Input (ImGui frame is live + fresh here). Gizmo already drawn above, so
        // picking sees this frame's gizmo state. Picking/selection stay live in Play
        // so you can inspect runtime entities; only the gizmo is suppressed.
        // UI goes live only while actually PLAYING (paused = frozen, like the
        // PlayerLayer's pause gate; clicks then select-for-inspection instead).
        // In 2D mode the controller picks sprites by rect and frames in XY —
        // suspended while the GAME camera drives (U7): its screen mapping, not
        // the 2D rig's, owns the viewport then, so picking unprojects through
        // it and the UI pointer uses the letterbox band.
        if (m_Ctx.ProjectOpen && m_Ctx.Scene)
            m_Viewport.OnUpdate(m_Ctx, m_Rig, ImGui::GetIO().DeltaTime,
                                m_Play == PlayMode::Playing,
                                (m_Mode2D && !m_GameCamActive) ? &m_Camera2D : nullptr,
                                m_GameCamActive ? &m_GameCamera : nullptr,
                                m_GameBandUv);

        DrawSaveAsPopup();
#ifndef COSMIC_2D_ONLY
        DrawImportModelPopup();
#endif
        DrawPackagePopup();
        DrawProjectSettingsPopup();
        DrawAboutPopup();
        DrawHelpPopups();
        DrawFirstRunPopup();
        DrawSaveLayoutPopup();   // K3
        HandleShortcuts();
        m_Ctx.ValidateSelection();
    }

    namespace
    {
        // K2 — square icon button with a tooltip; `active` renders accent-toggled,
        // `tint` colors the glyph (play-state coloring). Returns clicked.
        bool IconButton(const char* icon, const char* id, const char* tip,
                        float size, bool enabled = true, bool active = false,
                        const ImVec4* tint = nullptr)
        {
            ImGui::PushID(id);
            if (active)
            {
                const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.30f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(acc.x, acc.y, acc.z, 0.42f));
            }
            if (tint) ImGui::PushStyleColor(ImGuiCol_Text, *tint);
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(icon, ImVec2(size, size));
            ImGui::EndDisabled();
            if (tint) ImGui::PopStyleColor();
            if (active) ImGui::PopStyleColor(2);
            if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", tip);
            ImGui::PopID();
            return clicked;
        }
    }

    void StarforgeApp::DrawTopBar()
    {
        ImGui::Begin("Starforge", nullptr, ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar())
        {
            // K1 — the brand logo leads the menu bar (same file as the window icon).
            if (m_BrandTex)
            {
                DrawBrandLogo(ImGui::GetFrameHeight() - 6.0f);
                ImGui::Spacing();
            }
            DrawMenus();
            ImGui::EndMenuBar();
        }
        if (m_Ctx.ProjectOpen && m_Ctx.Scene)
        {
            // K2 — product toolbar: three measured groups. Left = file/tool icons,
            // center = the transport (measured + centered), right = Run/Package
            // (+ the U7 game-view controls while playing, + K3's layout tabs).
            const ImGuiStyle& style = ImGui::GetStyle();
            const float sq = ImGui::GetFrameHeight() + 4.0f;   // square icon buttons
            const float sp = style.ItemSpacing.x;

            // ---- LEFT: file/tool icons -----------------------------------
            ImGui::BeginGroup();
            if (IconButton(ICON_LC_SAVE, "k2save", "Save scene (Ctrl+S)", sq))
                if (!SaveScene()) m_OpenSaveAs = true;
            ImGui::SameLine();
            DrawBuildControls();   // hammer + status dot + auto toggle
            if (!IsPlaying())
            {
                // 2D authoring mode toggle (U3): swaps the viewport to the ortho
                // XY rig with the pixel grid; entering frames the scene's sprites
                // and arms 1-unit snapping (the pixel-grid convention).
                ImGui::SameLine();
                // W7 — in the 2D configuration the chip is PINNED ON: it still
                // shows (so the viewport's mode is legible, and clicking it
                // re-frames the sprites) but it never toggles off, because
                // there is no 3D rig to fall back to.
#ifdef COSMIC_2D_ONLY
                const bool toggled2D = IconButton("2D", "k2mode2d",
                               "2D mode (the only mode in this build): ortho XY view,\n"
                               "MMB pan, wheel zoom, pixel grid. Click to re-frame.",
                               sq, true, /*active*/ true);
#else
                const bool toggled2D = IconButton("2D", "k2mode2d",
                               "2D mode: ortho XY view, MMB pan, wheel zoom, pixel grid.",
                               sq, true, m_Mode2D);
#endif
                if (toggled2D)
                {
#ifndef COSMIC_2D_ONLY
                    m_Mode2D = !m_Mode2D;
#endif
                    if (m_Mode2D)
                    {
                        glm::vec2 mn(-8.0f), mx(8.0f);
                        if (m_Ctx.Scene)
                        {
                            bool any = false;
                            auto view = m_Ctx.Scene->GetRegistry()
                                .view<Cosmic::TransformComponent, Cosmic::SpriteRendererComponent>();
                            for (auto e : view)
                            {
                                const auto& t = view.get<Cosmic::TransformComponent>(e);
                                const auto& s = view.get<Cosmic::SpriteRendererComponent>(e);
                                const glm::vec2 half = Cosmic::SpriteRendererComponent::WorldSize(
                                    s, { t.Scale.x, t.Scale.y },
                                    s.Resolved ? (int)s.Resolved->GetWidth() : 0,
                                    s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f;
                                const glm::vec2 p{ t.Position.x, t.Position.y };
                                if (!any) { mn = p - half; mx = p + half; any = true; }
                                else      { mn = glm::min(mn, p - half); mx = glm::max(mx, p + half); }
                            }
                        }
                        m_Camera2D.FrameBounds(mn, mx);
                        m_Viewport.ArmPixelSnap();
                    }
                }
                // (K6 relocated the gizmo/snap/grid strip onto the viewport
                // overlay — the transport is now exactly centered at any width.)
            }
            ImGui::EndGroup();
            const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

            // ---- CENTER: transport (measured, truly centered) -------------
            const bool showFlow = !m_ManifestFlow.empty();
            const float flowW      = showFlow
                ? (ImGui::GetFrameHeight() + style.ItemInnerSpacing.x + ImGui::CalcTextSize("Flow").x + sp)
                : 0.0f;
            const float undoW      = 2.0f * sq + 3.0f * sp + ImGui::CalcTextSize("|").x;   // K4 pair + divider
            const float transportW = undoW + flowW + 5.0f * sq + 4.0f * sp;   // Play Pause Step Stop Eject
            const float avail      = ImGui::GetWindowContentRegionMax().x;
            float cx = (avail - transportW) * 0.5f;
            cx = std::max(cx, leftEnd + 2.0f * sp);   // never overlap the left group
            ImGui::SameLine();
            ImGui::SetCursorPosX(cx);
            DrawPlayControls();

            // ---- RIGHT: layout preset + Run App / Package (+ play controls) ---
            const std::string presetLabel = std::string(ICON_LC_LAYOUT) + " " + m_ActivePreset;
            const float presetW = ImGui::CalcTextSize(presetLabel.c_str()).x + 34.0f;
            float rightW = presetW + sp + 2.0f * sq + sp;        // picker + rocket + package
            if (IsPlaying())
                rightW += 110.0f + sp + sq + sp;                 // aspect combo + capture
            ImGui::SameLine();
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), avail - rightW));
            DrawLayoutPresetPicker(sq);                          // K3
            ImGui::SameLine();
            if (IsPlaying())
            {
                int aspect = (int)m_Aspect;
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::Combo("##gvaspect", &aspect, "Free\0" "16:9\0" "1920x1080\0" "Project\0"))
                    m_Aspect = (GameAspect)aspect;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Game-view aspect: letterboxes the frame so authored\n"
                                      "UI anchors match the shipped app. Project = the\n"
                                      "manifest [window] size.");
                ImGui::SameLine();
                if (IconButton(ICON_LC_CROSSHAIR, "k2capture",
                               "Capture the cursor for mouse-look while playing.\nEsc releases.",
                               sq, true, m_CaptureCursor))
                    m_CaptureCursor = !m_CaptureCursor;
                ImGui::SameLine();
            }
            if (IconButton(ICON_LC_ROCKET, "k2run",
                           "Run Standalone: the packaged exe if fresh,\nelse the dev exe with this project.", sq))
                RunStandalone();
            ImGui::SameLine();
            if (IconButton(ICON_LC_PACKAGE, "k2package", "Package the project for shipping...", sq))
                m_OpenPackage = true;
        }
        else
        {
            ImGui::TextDisabled("No project open — use the homescreen.");
        }
        // Record the top bar's bottom edge so the homescreen can anchor beneath it
        // (independent of the collapsible Viewport dock node — see the homescreen fix
        // in docs/engineering-notes/starforge-homescreen-hidden.md).
        m_TopBarBottomY = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::End();
    }

    void StarforgeApp::DrawBuildControls()
    {
        // K2 — Build Scripts as a hammer icon; the status text is now a colored
        // dot on the button's corner + tooltip; "Auto" is a compact toggle.
        const bool  scaffolded = ProjectIsScaffolded();
        const float sq = ImGui::GetFrameHeight() + 4.0f;

        const char* txt; ImVec4 col;
        switch (m_Builder.GetStatus())
        {
            case BuildRunner::Status::Building: txt = "building…";    col = ImVec4(1.0f, 0.85f, 0.30f, 1.0f); break;
            case BuildRunner::Status::Success:  txt = "module ok";    col = ImVec4(0.40f, 1.0f, 0.50f, 1.0f); break;
            case BuildRunner::Status::Failed:   txt = "build failed"; col = ImVec4(1.0f, 0.42f, 0.42f, 1.0f); break;
            default:
                txt = !scaffolded ? "no module"
                                  : (m_Module.IsLoaded() ? "module loaded" : "not built");
                col = !scaffolded ? ImVec4(0.45f, 0.45f, 0.45f, 1.0f)
                                  : (m_Module.IsLoaded() ? ImVec4(0.40f, 1.0f, 0.50f, 1.0f)
                                                         : ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                break;
        }

        char tip[160];
        std::snprintf(tip, sizeof(tip), "Build Scripts (Ctrl+B) — %s", txt);
        if (IconButton(ICON_LC_HAMMER, "k2build", tip, sq,
                       scaffolded && !m_Builder.IsBuilding() && !IsPlaying()))
            BuildScripts();

        // Status dot on the hammer's top-right corner.
        {
            const ImVec2 mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(mx.x - 5.0f, ImGui::GetItemRectMin().y + 5.0f), 3.5f,
                ImGui::GetColorU32(col));
        }

        ImGui::SameLine();
        if (IconButton(ICON_LC_ZAP, "k2auto",
                       "Auto-build: rebuild the game module whenever src/ changes.",
                       sq, true, m_AutoBuild))
            m_AutoBuild = !m_AutoBuild;
    }

    void StarforgeApp::DrawPlayControls()
    {
        // K2 — the centered transport: [Flow] Play · Pause · Step · Stop · Eject
        // as fixed square icon slots (the bar never reflows on state changes).
        // The caller (DrawTopBar) has already positioned the cursor; the group
        // width is measured there. Play-state coloring: Play glows green while
        // playing, Pause amber while paused (plus the viewport border tint).
        const bool  playing = IsPlaying();
        const bool  paused  = (m_Play == PlayMode::Paused);
        const float sq = ImGui::GetFrameHeight() + 4.0f;

        // K4 — undo/redo with count badges + a click-to-undo-N history popup,
        // riding just left of the transport (the 2211 idiom). Left-click = one
        // step; hover lists the last ~10 command names; right-click opens the
        // history popup where clicking entry i jumps back/forward i+1 steps.
        auto historyButton = [&](bool isUndo)
        {
            Cosmic::CommandStack& cmds = m_Ctx.Commands;
            const size_t count   = isUndo ? cmds.UndoCount() : cmds.RedoCount();
            const bool   enabled = count > 0;
            const char*  icon    = isUndo ? ICON_LC_UNDO : ICON_LC_REDO;
            const char*  id      = isUndo ? "k4undo" : "k4redo";

            ImGui::PushID(id);
            ImGui::BeginDisabled(!enabled);
            if (ImGui::Button(icon, ImVec2(sq, sq)))
            {
                if (isUndo) cmds.Undo(); else cmds.Redo();
            }
            ImGui::EndDisabled();

            // Count badge on the button's top-right corner.
            if (count > 0)
            {
                char badge[16];
                std::snprintf(badge, sizeof(badge), "%zu", count > 99 ? (size_t)99 : count);
                const ImVec2 mx = ImGui::GetItemRectMax();
                const ImVec2 ts = ImGui::CalcTextSize(badge);
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(mx.x - ts.x * 0.72f - 2.0f, ImGui::GetItemRectMin().y - 1.0f),
                    ImGui::GetColorU32(ImGuiCol_CheckMark), badge);
            }

            // Hover: the last ~10 command names (0 = what this button applies).
            if (enabled && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(isUndo ? "Undo (Ctrl+Z) — right-click for history"
                                              : "Redo (Ctrl+Y) — right-click for history");
                ImGui::Separator();
                const size_t n = std::min<size_t>(count, 10);
                for (size_t i = 0; i < n; ++i)
                    ImGui::Text("%zu. %s", i + 1,
                                (isUndo ? cmds.UndoNameAt(i) : cmds.RedoNameAt(i)).c_str());
                if (count > n)
                    ImGui::TextDisabled("… %zu more", count - n);
                ImGui::EndTooltip();
            }

            // Right-click history popup: clicking entry i applies i+1 steps.
            if (ImGui::BeginPopupContextItem("k4history"))
            {
                const size_t n = std::min<size_t>(count, 10);
                for (size_t i = 0; i < n; ++i)
                {
                    const std::string name = isUndo ? cmds.UndoNameAt(i) : cmds.RedoNameAt(i);
                    char row[192];
                    std::snprintf(row, sizeof(row), "%s %zu step%s to \"%s\"",
                                  isUndo ? "Undo" : "Redo", i + 1, i ? "s" : "", name.c_str());
                    if (ImGui::Selectable(row))
                        for (size_t k = 0; k <= i; ++k)
                        {
                            if (isUndo) cmds.Undo(); else cmds.Redo();
                        }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };
        historyButton(true);
        ImGui::SameLine();
        historyButton(false);
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // U5/U8 — the manifest names a startup flow: Play can boot it (the
        // shipped player's path) or play just the open scene.
        if (!m_ManifestFlow.empty())
        {
            ImGui::BeginDisabled(playing);
            ImGui::Checkbox("Flow", &m_PlayFlowUse);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Play the project's startup flow (%s) from its start\n"
                                  "state, like the shipped app. Unchecked: play the open scene.",
                                  m_ManifestFlow.c_str());
            ImGui::SameLine();
        }

        const ImVec4 green(0.30f, 1.00f, 0.42f, 1.0f);
        const ImVec4 amber(1.00f, 0.80f, 0.20f, 1.0f);

        // Play / Resume.
        {
            const ImVec4* tint = playing ? (paused ? nullptr : &green) : nullptr;
            if (IconButton(ICON_LC_PLAY, "k2play",
                           playing ? (paused ? "Resume" : "Playing")
                                   : "Play the scene",
                           sq, !playing || paused, playing && !paused, tint))
            {
                if (!playing) PlayScene();
                else          TogglePausePlay();   // resume from pause
            }
        }
        ImGui::SameLine();
        if (IconButton(ICON_LC_PAUSE, "k2pause",
                       paused ? "Paused" : "Pause the simulation",
                       sq, playing && !paused, paused, paused ? &amber : nullptr))
            TogglePausePlay();
        ImGui::SameLine();
        if (IconButton(ICON_LC_STEP_FORWARD, "k2step",
                       "Step one fixed update (while paused)", sq, paused))
            StepScene();
        ImGui::SameLine();
        if (IconButton(ICON_LC_SQUARE, "k2stop", "Stop and restore the edit scene",
                       sq, playing))
            StopScene();
        ImGui::SameLine();
        // U7 eject — a live toggle while playing (the doc's reserved slot landed
        // with Phase 17 the same day this bar was rebuilt).
        if (IconButton(ICON_LC_ARROW_UP_FROM_LINE, "k2eject",
                       "Eject: fly the editor camera while the sim runs;\n"
                       "re-dock returns to the game camera.",
                       sq, playing, m_Ejected))
            m_Ejected = !m_Ejected;
    }

    void StarforgeApp::DrawMenus()
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene", "Ctrl+N", false, m_Ctx.ProjectOpen)) NewScene();
            if (ImGui::BeginMenu("Open Scene", m_Ctx.ProjectOpen))
            {
                std::error_code ec;
                const fs::path scenes = Cosmic::FileSystem::Resolve("project://scenes");
                bool any = false;
                if (fs::exists(scenes, ec))
                    for (const auto& e : fs::directory_iterator(scenes, ec))
                    {
                        if (e.path().extension() != ".cscene") continue;
                        any = true;
                        const std::string vfs = "project://scenes/" + e.path().filename().string();
                        if (ImGui::MenuItem(e.path().filename().string().c_str()))
                            OpenScene(vfs);
                    }
                if (!any) ImGui::TextDisabled("(no scenes)");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, m_Ctx.ProjectOpen))
                if (!SaveScene()) m_OpenSaveAs = true;
            if (ImGui::MenuItem("Save As...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenSaveAs = true;
            ImGui::Separator();
#ifndef COSMIC_2D_ONLY
            if (ImGui::MenuItem("Import Model...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenImportModel = true;
#endif
            if (ImGui::MenuItem("Project Settings...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenProjectSettings = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Package...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenPackage = true;
            if (ImGui::MenuItem("Run Standalone", nullptr, false, m_Ctx.ProjectOpen))
                RunStandalone();
            if (ImGui::MenuItem("Package Starforge (self-host)..."))
                PackageStarforge();
            ImGui::Separator();
            if (ImGui::BeginMenu("Recent Projects"))
            {
                const auto recents = Prefs::LoadProjects();
                if (recents.empty()) ImGui::TextDisabled("(none)");
                for (const auto& e : recents)
                    if (ImGui::MenuItem(e.Name.c_str()))
                        OpenProject(e);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Close Project (Home)")) CloseProject();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit to Launcher"))
                Cosmic::Application::Get().TransitionToLauncher();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem(("Undo " + m_Ctx.Commands.UndoName()).c_str(), "Ctrl+Z",
                                false, m_Ctx.Commands.CanUndo()))
                m_Ctx.Commands.Undo();
            if (ImGui::MenuItem(("Redo " + m_Ctx.Commands.RedoName()).c_str(), "Ctrl+Y",
                                false, m_Ctx.Commands.CanRedo()))
                m_Ctx.Commands.Redo();
            ImGui::Separator();
            const bool sel = m_Ctx.HasSelection();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, sel))
                if (Cosmic::Entity e = m_Ctx.PrimaryEntity()) Commands::Duplicate(m_Ctx, e);
            if (ImGui::MenuItem("Delete", "Del", false, sel))
            {
                std::vector<uint64_t> ids;
                for (entt::entity h : m_Ctx.Selection)
                    if (Cosmic::Entity e(h, m_Ctx.Scene.get()); e && e.HasComponent<Cosmic::IDComponent>())
                        ids.push_back((uint64_t)e.GetComponent<Cosmic::IDComponent>().ID);
                for (uint64_t id : ids)
                    if (Cosmic::Entity e = m_Ctx.Scene->FindByUUID(Cosmic::UUID(id)))
                        Commands::Destroy(m_Ctx, e);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Entity", m_Ctx.ProjectOpen))
        {
            DrawEntityMenu();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            // R8 — viewport view modes (also on the viewport header strip, K6).
            if (ImGui::BeginMenu("View Mode", m_Ctx.ProjectOpen))
            {
                using VM = ViewportController::ViewMode;
                const VM cur = m_Viewport.GetViewMode();
                // W7 — Unlit neutralizes 3D lights and Entity ID renders the
                // mesh ID pass; neither exists in the 2D build. This list must
                // match the viewport strip's dropdown (ViewportController.cpp).
                if (ImGui::MenuItem("Lit",       nullptr, cur == VM::Lit))       m_Viewport.SetViewMode(VM::Lit);
#ifndef COSMIC_2D_ONLY
                if (ImGui::MenuItem("Unlit",     nullptr, cur == VM::Unlit))     m_Viewport.SetViewMode(VM::Unlit);
#endif
                if (ImGui::MenuItem("Wireframe", nullptr, cur == VM::Wireframe)) m_Viewport.SetViewMode(VM::Wireframe);
#ifndef COSMIC_2D_ONLY
                if (ImGui::MenuItem("Entity ID", nullptr, cur == VM::EntityID))  m_Viewport.SetViewMode(VM::EntityID);
#endif
                ImGui::EndMenu();
            }
            ImGui::Separator();
            ImGui::MenuItem("Hierarchy",       nullptr, &m_ShowHierarchy);
            ImGui::MenuItem("Inspector",       nullptr, &m_ShowInspector);
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContent);
            ImGui::MenuItem("Console",         nullptr, &m_ShowConsole);
            ImGui::MenuItem("Environment",     nullptr, &m_ShowEnvironment);
            ImGui::MenuItem("Post Chain",      nullptr, &m_ShowPostChain);   // Q6
            ImGui::MenuItem("Material Editor", nullptr, &m_ShowMaterial);
#ifndef COSMIC_2D_ONLY
            ImGui::MenuItem("World Systems",   nullptr, &m_ShowWorldSystems);
            ImGui::MenuItem("Voxels",          nullptr, &m_ShowVoxel);
#endif
            ImGui::MenuItem("Tile Palette",    nullptr, &m_ShowTilePalette);
#ifndef COSMIC_2D_ONLY
            ImGui::MenuItem("Editors (Animation / Flow / Story)", nullptr, &m_ShowEditors);   // M1 host (Q1/Q4 docs)
#else
            ImGui::MenuItem("Editors (Flow / Story)", nullptr, &m_ShowEditors);   // M1 host (Q1/Q4 docs)
#endif
            ImGui::MenuItem("Telemetry",       nullptr, &m_ShowTelemetry);
            ImGui::MenuItem("Profiler",        nullptr, &m_ShowProfiler);   // T17
            ImGui::MenuItem("System (Jobs/Resources)", nullptr, &m_ShowSystem);   // T18
            ImGui::MenuItem("Statistics",      nullptr, &m_ShowStats);
            ImGui::MenuItem("Viewport Stats Chips", nullptr, &m_Viewport.ShowStatsChips());   // K9
            ImGui::Separator();
            // K3 — the layout presets, mirrored from the top-bar picker.
            if (ImGui::BeginMenu("Layout"))
            {
                for (const auto& n : LayoutPresets::BuiltIns())
                    if (ImGui::MenuItem(n.c_str(), nullptr, n == m_ActivePreset))
                        ApplyLayoutPreset(n);
                const auto user = LayoutPresets::UserPresets();
                if (!user.empty())
                {
                    ImGui::Separator();
                    for (const auto& n : user)
                        if (ImGui::MenuItem(n.c_str(), nullptr, n == m_ActivePreset))
                            ApplyLayoutPreset(n);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save layout as…"))
                    m_OpenSaveLayout = true;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset Layout"))
            {
                // Re-apply the ACTIVE preset from scratch (a ✕ may have closed core
                // panels or docks were dragged apart): built-ins rebuild their coded
                // dock tree, user presets reload their ini snapshot (K3; the H5
                // behavior — reopen + rebuild — is what the Level preset does).
                ApplyLayoutPreset(m_ActivePreset);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts")) m_OpenShortcuts = true;
            if (ImGui::MenuItem("About Starforge"))    m_OpenAbout = true;
#ifndef COSMIC_2D_ONLY
            ImGui::Separator();
            // A4 acceptance — proves the doc 13 §0.5 state-restore contract:
            // the viewport must render byte-identically after preview passes.
            if (ImGui::MenuItem("Preview State Self-Test", nullptr, false,
                                m_Ctx.ProjectOpen && m_PreviewSelfTest == 0))
            {
                m_PreviewSelfTest = 1;
                m_Ctx.Log("[Preview] Self-test armed — keep the camera still for 3 frames.");
            }
#endif
            ImGui::EndMenu();
        }
    }

    void StarforgeApp::DrawStatusBar()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws)
            return;

        // Homescreen: no strip, no reserved band.
        if (!m_Ctx.ProjectOpen)
        {
            ws->SetBottomInsetPixels(0.0f);
            return;
        }

        const float h = ImGui::GetFrameHeight() + 2.0f;   // font-derived => DPI-safe
        ws->SetBottomInsetPixels(h);

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
        ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 2.0f));
        ImGui::Begin("##StarforgeStatus", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(3);

        // Play state (colored) — the strip's anchor cue.
        const bool paused = (m_Play == PlayMode::Paused);
        if (IsPlaying())
            ImGui::TextColored(paused ? ImVec4(1.0f, 0.80f, 0.20f, 1.0f)
                                      : ImVec4(0.30f, 1.0f, 0.42f, 1.0f),
                               paused ? ICON_LC_PAUSE " PAUSED" : ICON_LC_PLAY " PLAYING");
        else
            ImGui::TextDisabled(ICON_LC_PENCIL_RULER " EDIT");

        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("%.0f FPS (%.2f ms)", ImGui::GetIO().Framerate,
                    1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));

        size_t entities = 0;
        if (m_Ctx.Scene)
            for (auto e : m_Ctx.Scene->View<Cosmic::IDComponent>()) { (void)e; ++entities; }
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("%zu entities", entities);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("%zu selected", m_Ctx.Selection.size());

        // Asset-memory chip (T2/T18 — same AssetLibrary::Enumerate source as the
        // System ▸ Resources panel, so the totals always match).
        {
            size_t count = 0; uint64_t cpu = 0, gpu = 0;
            Cosmic::AssetLibrary::Enumerate([&](const Cosmic::AssetEntry& e)
            { ++count; cpu += e.CpuBytes; gpu += e.GpuBytes; });
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            ImGui::Text("%zu assets (%.1f MiB CPU / %.1f MiB GPU)",
                        count, cpu / (1024.0 * 1024.0), gpu / (1024.0 * 1024.0));
        }

        // Build-module state (mirrors the K2 hammer dot).
        {
            const char* txt; ImVec4 col;
            switch (m_Builder.GetStatus())
            {
                case BuildRunner::Status::Building: txt = "building…";    col = ImVec4(1.0f, 0.85f, 0.30f, 1.0f); break;
                case BuildRunner::Status::Success:  txt = "module ok";    col = ImVec4(0.40f, 1.0f, 0.50f, 1.0f); break;
                case BuildRunner::Status::Failed:   txt = "build failed"; col = ImVec4(1.0f, 0.42f, 0.42f, 1.0f); break;
                default:
                    txt = !ProjectIsScaffolded() ? "no module"
                                                 : (m_Module.IsLoaded() ? "module loaded" : "not built");
                    col = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                    break;
            }
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            ImGui::TextColored(col, ICON_LC_HAMMER " %s", txt);
        }

        // Right side: scene identity now; Phase 23 T2's asset-memory chip takes
        // this slot ("assets: N (X MiB CPU / Y MiB GPU)") once accounting exists.
        {
            const std::string right = m_Ctx.ProjectTitle + " / " + m_Ctx.SceneName
                                    + (m_Ctx.Dirty ? " *" : "");
            const float w = ImGui::CalcTextSize(right.c_str()).x;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20.0f,
                                     ImGui::GetWindowContentRegionMax().x - w));
            ImGui::TextDisabled("%s", right.c_str());
        }

        ImGui::End();
    }

    void StarforgeApp::DrawStatsWindow()
    {
        if (ImGui::Begin("Statistics", &m_ShowStats))
        {
            size_t entities = 0;
            if (m_Ctx.Scene)
                for (auto e : m_Ctx.Scene->View<Cosmic::IDComponent>()) { (void)e; ++entities; }

            ImGui::Text("Entities:          %zu", entities);
            ImGui::Text("Selected:          %zu", m_Ctx.Selection.size());
            ImGui::Separator();
            // W7 — the Renderer3D queue telemetry has no 2D counterpart; the
            // Renderer2D batch stats are the 2D build's row set instead.
#ifndef COSMIC_2D_ONLY
            const Cosmic::Renderer3D::Statistics s = Cosmic::Renderer3D::GetStats();
            ImGui::TextDisabled("Renderer3D (last frame)");
            ImGui::Text("Draw calls:        %u", s.DrawCalls);
            ImGui::Text("Meshes submitted:  %u", s.MeshesSubmitted);
            ImGui::Text("Culled (frustum):  %u", s.MeshesCulled);
            ImGui::Text("Drawn:             %u", s.MeshesDrawn);
            ImGui::Text("Auto-inst batches: %u", s.AutoInstanceBatches);
#else
            const Cosmic::Renderer2D::Statistics s = Cosmic::Renderer2D::GetStats();
            ImGui::TextDisabled("Renderer2D (last frame)");
            ImGui::Text("Draw calls:        %u", s.DrawCalls);
            ImGui::Text("Quads:             %u", s.QuadCount);
            ImGui::Text("Circles:           %u", s.CircleCount);
            ImGui::Text("Lines:             %u", s.LineCount);
#endif
            ImGui::Separator();
            ImGui::Text("%.1f FPS  (%.2f ms)", ImGui::GetIO().Framerate,
                        1000.0f / ImGui::GetIO().Framerate);
        }
        ImGui::End();
    }

    void StarforgeApp::DrawHelpPopups()
    {
        if (m_OpenShortcuts)
        {
            ImGui::OpenPopup("Keyboard Shortcuts");
            m_OpenShortcuts = false;
        }
        if (ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            struct Row { const char* keys; const char* action; };
            static const Row rows[] = {
                { "Ctrl+N",        "New scene" },
                { "Ctrl+S",        "Save scene" },
                { "Ctrl+Z / Ctrl+Y", "Undo / Redo" },
                { "Ctrl+D",        "Duplicate selection" },
                { "Del",           "Delete selection" },
                { "Ctrl+B",        "Build scripts (hot reload)" },
                { "F",             "Frame selection" },
                { "W / E / R",     "Gizmo: translate / rotate / scale" },
                { "MMB drag",      "Orbit camera" },
                { "Ctrl+MMB / scroll", "Pan / zoom" },
                { "1-9 / Ctrl+1-9","Recall / save camera bookmark" },
            };
            if (ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_SizingStretchProp))
            {
                for (const Row& r : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.keys);
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", r.action);
                }
                ImGui::EndTable();
            }
            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---- Starforge accent theme (E21) -------------------------------------
    void StarforgeApp::ApplyEditorTheme()
    {
        // Forge accent: start from the engine's data-driven "Sleek Pro" pro-dark
        // base and repaint just the accent colours molten-orange, so the editor
        // reads as Starforge without authoring a whole colour table. Registered
        // (so a Theme Studio picker can see it) and applied; OnDetach restores the
        // previous theme so sibling apps in the same process stay pristine.
        Cosmic::Theme t;
        if (const Cosmic::Theme* base = Cosmic::ThemeManager::Find("Sleek Pro"))
            t = *base;
        else
            t = Cosmic::ThemeManager::CaptureCurrentStyle("Starforge");   // fallback
        t.name    = "Starforge";
        t.builtIn = false;

        const ImVec4 forge  = ImVec4(0.95f, 0.48f, 0.16f, 1.00f);   // molten orange
        const ImVec4 forgeD = ImVec4(0.72f, 0.34f, 0.10f, 1.00f);
        t.accent = forge;

        ImVec4* c = t.colors;
        c[ImGuiCol_CheckMark]          = forge;
        c[ImGuiCol_SliderGrab]         = forgeD;
        c[ImGuiCol_SliderGrabActive]   = forge;
        c[ImGuiCol_HeaderHovered]      = ImVec4(forge.x, forge.y, forge.z, 0.22f);
        c[ImGuiCol_HeaderActive]       = ImVec4(forge.x, forge.y, forge.z, 0.34f);
        c[ImGuiCol_SeparatorHovered]   = ImVec4(forge.x, forge.y, forge.z, 0.50f);
        c[ImGuiCol_SeparatorActive]    = forge;
        c[ImGuiCol_ResizeGripHovered]  = ImVec4(forge.x, forge.y, forge.z, 0.55f);
        c[ImGuiCol_ResizeGripActive]   = forge;
        c[ImGuiCol_TabHovered]         = ImVec4(forge.x, forge.y, forge.z, 0.40f);
        c[ImGuiCol_TabActive]          = ImVec4(0.22f, 0.15f, 0.09f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.12f, 0.09f, 1.00f);
        c[ImGuiCol_DockingPreview]     = ImVec4(forge.x, forge.y, forge.z, 0.30f);
        c[ImGuiCol_TextSelectedBg]     = ImVec4(forge.x, forge.y, forge.z, 0.28f);
        c[ImGuiCol_TitleBgActive]      = ImVec4(0.13f, 0.10f, 0.08f, 1.00f);
        c[ImGuiCol_PlotLines]          = forge;
        c[ImGuiCol_PlotHistogram]      = forge;

        Cosmic::ThemeManager::Register(t);
        Cosmic::ThemeManager::Apply("Starforge");
    }

    // ---- Drop-a-file branding (K1) -----------------------------------------
    void StarforgeApp::ApplyBrand()
    {
        auto& win = Cosmic::Application::Get().GetWindow();

        // The editor's own brand: <exe>/branding/icon.png -> user:// override.
        // (Project icons brand the PLAYER/packaged app, not the editor window.)
        const std::string icon = Cosmic::Branding::ResolveProcessIcon();

        if (!icon.empty())
        {
            if (win.SetIcon(icon))
            {
                m_BrandTex     = Cosmic::Texture2D::Create(icon);
                m_BrandPath    = icon;
                m_BrandRetried = false;
            }
            else if (!m_BrandRetried)
            {
                // Likely a half-written file mid-copy (hot-swap race): keep the
                // current brand and try exactly once more shortly.
                m_BrandRetried  = true;
                m_BrandDebounce = 0.5f;
            }
        }
        else
        {
            // No branding file anywhere -> the platform default, cleanly.
            win.ClearIcon();
            m_BrandTex.reset();
            m_BrandPath.clear();
            m_BrandRetried = false;
        }

        // Aim the hot-swap watcher at the resolved file's folder (or the exe-dir
        // convention folder while nothing resolves, so dropping a FIRST icon is
        // still caught). Re-armed only when the target changes.
        std::string dir = !m_BrandPath.empty()
            ? fs::path(m_BrandPath).parent_path().generic_string()
            : (fs::path(Cosmic::Branding::ExecutableDir()) / "branding").generic_string();
        if (dir != m_BrandWatchDir)
        {
            m_BrandWatchDir = dir;
            m_BrandWatcher.Stop();
            std::error_code ec;
            if (fs::exists(dir, ec))
                m_BrandWatcher.Watch(dir, /*recursive=*/false);
        }
    }

    void StarforgeApp::DrawBrandLogo(float height)
    {
        if (!m_BrandTex || height <= 0.0f)
            return;
        // Engine textures load V-flipped for GL UVs — draw with the standard
        // flipped UV pair (the Content Browser preview convention).
        ImGui::Image((ImTextureID)(intptr_t)m_BrandTex->GetRendererID(),
                     ImVec2(height, height), ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Starforge %s\nRe-brand by replacing %s (no restart needed).",
                              COSMIC_VERSION_STRING,
                              m_BrandPath.empty() ? "branding/icon.png" : m_BrandPath.c_str());
    }

    // ---- Forge Playground first-run sample (E21) --------------------------
#ifndef COSMIC_2D_ONLY
    bool StarforgeApp::ForgePlaygroundExists() const
    {
        std::error_code ec;
        return fs::exists(fs::path("assets") / "projects" / "ForgePlayground" / "project.cproj", ec);
    }
#endif

#ifndef COSMIC_2D_ONLY
    bool StarforgeApp::ForgeBlocksExists() const
    {
        std::error_code ec;
        return fs::exists(fs::path("assets") / "projects" / "ForgeBlocks" / "project.cproj", ec);
    }

    bool StarforgeApp::BuildForgeBlocks()
    {
        using namespace Cosmic;
        const std::string proj = "ForgeBlocks";

        // Reuse the C++ scaffold (Module.cpp + scripts incl. WalkController +
        // VoxelDigger), then replace the template scene with the voxel sample.
        if (!ScaffoldProject(proj))
        {
            m_Ctx.Log("[ForgeBlocks] Could not scaffold — templates unavailable.", LogSeverity::Error);
            return false;
        }
        FileSystem::SetActiveProject(proj);

        // The generated island's recipe (bounded so the whole world is baked to a
        // .cvox up front → collision is ready the instant Play starts).
        VoxelGeneratorRecipe recipe;
        recipe.Seed = 20260708u;
        recipe.SurfaceLevel = 24.0f;
        recipe.Amplitude    = 9.0f;
        recipe.Frequency    = 0.02f;
        recipe.Octaves      = 5;
        recipe.CaveThreshold = 0.30f;
        recipe.CaveFrequency = 0.045f;
        recipe.DirtDepth    = 4;
        recipe.SandLevel    = 18.0f;

        // Bake an 8x2x8-chunk island (256 x 64 x 256 voxels) to project://voxels/world.cvox.
        auto vol = VoxelVolume::Create();
        vol->SetVoxelSize(1.0f);
        for (int cx = -4; cx < 4; ++cx)
            for (int cz = -4; cz < 4; ++cz)
                for (int cy = 0; cy < 2; ++cy)
                    VoxelGenerator::GenerateChunk(*vol, { cx, cy, cz }, recipe);
        std::vector<glm::ivec3> drained; vol->TakeDirtyChunks(drained);   // clear (we saved them)
        vol->Save("project://voxels/world.cvox");

        Ref<Scene> scene = Scene::Create();

        { Entity e = scene->CreateEntity("Sun");
          auto& l = e.AddComponent<DirectionalLightComponent>();
          l.Direction = { -0.4f, -0.82f, -0.45f }; l.Color = { 1.0f, 0.96f, 0.88f }; l.Intensity = 1.1f; }

        { Entity e = scene->CreateEntity("Environment");
          auto& env = e.AddComponent<EnvironmentComponent>();
          env.TimeOfDay = 11.0f; env.Fog = true; env.FogDensity = 0.005f; }

        // The voxel world — loads the pre-baked .cvox (GenEnabled off = the island is
        // the whole bounded world; flip it on in the Voxels panel to stream endlessly).
        { Entity e = scene->CreateEntity("Voxel World");
          auto& v = e.AddComponent<VoxelVolumeComponent>();
          v.VolumePath = "project://voxels/world.cvox";
          v.GenEnabled = false;
          // Mirror the bake recipe so a Regenerate in-editor reproduces this island.
          v.Seed = recipe.Seed; v.SurfaceLevel = recipe.SurfaceLevel; v.Amplitude = recipe.Amplitude;
          v.Frequency = recipe.Frequency; v.Octaves = recipe.Octaves;
          v.CaveThreshold = recipe.CaveThreshold; v.CaveFrequency = recipe.CaveFrequency;
          v.DirtDepth = recipe.DirtDepth; v.SandLevel = recipe.SandLevel; }

        // Player: a character capsule that drops onto the voxel surface + walks
        // (WalkController, J6). The camera is a child so it follows; the digger on
        // the camera breaks/places along the view (V4). ClassNames resolve after
        // Build Scripts (Ctrl+B).
        Entity player = scene->CreateEntity("Player");
        {
            player.GetComponent<TransformComponent>().Position = { 3.0f, 40.0f, 3.0f };
            auto& cc = player.AddComponent<CharacterControllerComponent>();
            cc.Height = 1.8f; cc.Radius = 0.35f; cc.StepHeight = 0.6f;   // step up single voxels
            player.AddComponent<NativeScriptComponent>("WalkController");
        }

        { Entity cam = scene->CreateEntity("Camera");
          cam.GetComponent<TransformComponent>().Position = { 0.0f, 1.6f, 0.0f };   // eye height, local
          cam.AddComponent<CameraComponent>();
          cam.AddComponent<NativeScriptComponent>("VoxelDigger");
          scene->SetParent(cam, player, /*keepWorldPose*/ false); }

        // A minimal HUD (U1) so the sample ships with UI too.
        { Entity canvas = scene->CreateEntity("HUD");
          canvas.AddComponent<CanvasComponent>();
          Entity label = scene->CreateEntity("Hint");
          auto& rt = label.AddComponent<RectTransformComponent>();
          rt.AnchorMin = { 0.0f, 1.0f }; rt.AnchorMax = { 0.0f, 1.0f };
          rt.OffsetMin = { 16.0f, -40.0f }; rt.OffsetMax = { 460.0f, -12.0f };
          auto& txt = label.AddComponent<UiTextComponent>();
          txt.Text = "ForgeBlocks  |  WASD move  Space jump  LMB dig  RMB place";
          txt.SizePx = 18.0f; txt.Color = { 0.95f, 0.97f, 1.0f, 1.0f };
          scene->SetParent(label, canvas, false); }

        SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Main.cscene"));

        m_Ctx.Log("[ForgeBlocks] Created the voxel sample project.");
        return true;
    }
#endif   // COSMIC_2D_ONLY — the ForgeBlocks voxel sample

    // ---- Phase 17 / U8 samples ---------------------------------------------
    namespace
    {
        // A titled UI button: image + button + label on ONE entity (UiSystem
        // draws the image, then the state tint, then the text).
        Cosmic::Entity MakeUiButton(Cosmic::Ref<Cosmic::Scene>& scene, Cosmic::Entity canvas,
                                    const char* name, const char* label, const char* signal,
                                    float centerYFrac, const glm::vec2& size)
        {
            using namespace Cosmic;
            Entity e = scene->CreateEntity(name);
            auto& rt = e.AddComponent<RectTransformComponent>();
            rt.AnchorMin = rt.AnchorMax = { 0.5f, centerYFrac };
            rt.OffsetMin = { -size.x * 0.5f, -size.y * 0.5f };
            rt.OffsetMax = {  size.x * 0.5f,  size.y * 0.5f };
            e.AddComponent<UiImageComponent>().Tint = { 0.16f, 0.19f, 0.25f, 0.92f };
            e.AddComponent<UiButtonComponent>().Signal = signal;
            auto& txt = e.AddComponent<UiTextComponent>();
            txt.Text = label;
            txt.SizePx = 30.0f;
            scene->SetParent(e, canvas, /*keepWorldPose=*/false);
            return e;
        }

        // A centered UI label.
        Cosmic::Entity MakeUiLabel(Cosmic::Ref<Cosmic::Scene>& scene, Cosmic::Entity canvas,
                                   const char* name, const char* text, float centerYFrac,
                                   float sizePx, const glm::vec4& color)
        {
            using namespace Cosmic;
            Entity e = scene->CreateEntity(name);
            auto& rt = e.AddComponent<RectTransformComponent>();
            rt.AnchorMin = rt.AnchorMax = { 0.5f, centerYFrac };
            rt.OffsetMin = { -420.0f, -50.0f };
            rt.OffsetMax = {  420.0f,  50.0f };
            auto& txt = e.AddComponent<UiTextComponent>();
            txt.Text = text;
            txt.SizePx = sizePx;
            txt.Color = color;
            scene->SetParent(e, canvas, /*keepWorldPose=*/false);
            return e;
        }

        // A flat-color sprite (U3 sizing: untextured => Transform.Scale is the size).
        Cosmic::Entity MakeSprite(Cosmic::Ref<Cosmic::Scene>& scene, const char* name,
                                  const glm::vec3& pos, const glm::vec2& size,
                                  const glm::vec4& color, int z = 0)
        {
            using namespace Cosmic;
            Entity e = scene->CreateEntity(name);
            auto& t = e.GetComponent<TransformComponent>();
            t.Position = pos;
            t.Scale = { size.x, size.y, 1.0f };
            auto& s = e.AddComponent<SpriteRendererComponent>();
            s.Color = color;
            s.ZOrder = z;
            return e;
        }
    }

    bool StarforgeApp::FlowDemoExists() const
    {
        std::error_code ec;
        return fs::exists(fs::path("assets") / "projects" / "FlowDemo" / "project.cproj", ec);
    }

    bool StarforgeApp::BuildFlowDemo()
    {
        using namespace Cosmic;
        const std::string proj = "FlowDemo";

        // The ZERO-CODE two-screen app (U8 acceptance #1): every screen and all
        // navigation is data — scenes + Main.cflow — no scene references any
        // script. The C++ scaffold only provides the standalone player boot.
        if (!ScaffoldProject(proj))
        {
            m_Ctx.Log("[FlowDemo] Could not scaffold — templates unavailable.", LogSeverity::Error);
            return false;
        }
        FileSystem::SetActiveProject(proj);

        // ---- MainMenu.cscene ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 2.0f, 10.0f };
              cam.AddComponent<CameraComponent>(); }
            { Entity e = scene->CreateEntity("Environment");
              auto& env = e.AddComponent<EnvironmentComponent>();
              env.TimeOfDay = 19.0f; }   // dusk backdrop behind the menu

            Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>();
            MakeUiLabel(scene, canvas, "Title", "FLOW DEMO", 0.28f, 72.0f,
                        { 1.0f, 0.86f, 0.45f, 1.0f });
            MakeUiLabel(scene, canvas, "Sub", "two screens, zero code", 0.38f, 22.0f,
                        { 0.75f, 0.78f, 0.85f, 1.0f });
            MakeUiButton(scene, canvas, "PlayButton", "Play",  "play_clicked", 0.55f, { 260.0f, 56.0f });
            MakeUiButton(scene, canvas, "QuitButton", "Quit",  "quit_clicked", 0.67f, { 260.0f, 56.0f });

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/MainMenu.cscene"));
        }

        // ---- Game.cscene ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
#ifndef COSMIC_2D_ONLY
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 3.5f, 12.0f };
              cam.AddComponent<CameraComponent>();
#else
              // 2D: an ortho rig on the sprite plane, the ForgePong convention.
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f };
              auto& cc = cam.AddComponent<CameraComponent>();
              cc.ProjectionType = CameraComponent::Projection::Orthographic;
              cc.OrthoSize = 5.0f;
#endif
            }
            // W7 — FlowDemo ships on BOTH engines (its subject is the flow, not
            // the dressing). The Game screen's scenery swaps: lit primitives on
            // 3D, untextured flat-colour sprites in the SAME palette on 2D.
#ifndef COSMIC_2D_ONLY
            { Entity e = scene->CreateEntity("Sun");
              auto& l = e.AddComponent<DirectionalLightComponent>();
              l.Direction = { -0.45f, -0.8f, -0.4f }; l.Intensity = 1.1f; }
#endif
            { Entity e = scene->CreateEntity("Environment");
              auto& env = e.AddComponent<EnvironmentComponent>();
              env.TimeOfDay = 11.0f; }
#ifndef COSMIC_2D_ONLY
            { Entity e = scene->CreateEntity("Ground");
              e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Plane);
              e.GetComponent<TransformComponent>().Scale = { 20.0f, 1.0f, 20.0f };
              e.AddComponent<MeshRendererComponent>().Color = { 0.32f, 0.42f, 0.34f, 1.0f }; }
            { Entity e = scene->CreateEntity("Monument");
              e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Torus);
              e.GetComponent<TransformComponent>().Position = { 0.0f, 1.6f, 0.0f };
              e.AddComponent<MeshRendererComponent>().Color = { 0.85f, 0.55f, 0.20f, 1.0f }; }
#else
            { Entity e = scene->CreateEntity("Ground");
              auto& t = e.GetComponent<TransformComponent>();
              t.Position = { 0.0f, -2.5f, 0.0f }; t.Scale = { 20.0f, 1.0f, 1.0f };
              auto& s = e.AddComponent<SpriteRendererComponent>();
              s.Color = { 0.32f, 0.42f, 0.34f, 1.0f }; s.ZOrder = -1; }
            { Entity e = scene->CreateEntity("Monument");
              auto& t = e.GetComponent<TransformComponent>();
              t.Position = { 0.0f, -0.4f, 0.0f }; t.Scale = { 2.4f, 2.4f, 1.0f };
              e.AddComponent<SpriteRendererComponent>().Color = { 0.85f, 0.55f, 0.20f, 1.0f }; }
#endif

            Entity canvas = scene->CreateEntity("HUD");
            canvas.AddComponent<CanvasComponent>();
            { Entity hint = scene->CreateEntity("Hint");
              auto& rt = hint.AddComponent<RectTransformComponent>();
              rt.AnchorMin = { 0.0f, 1.0f }; rt.AnchorMax = { 0.0f, 1.0f };
              rt.OffsetMin = { 16.0f, -44.0f }; rt.OffsetMax = { 420.0f, -12.0f };
              auto& txt = hint.AddComponent<UiTextComponent>();
              txt.Text = "GAME  |  Esc = pause";
              txt.SizePx = 20.0f;
              txt.HAlign = UiHAlign::Left;
              scene->SetParent(hint, canvas, false); }

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Game.cscene"));
        }

        // ---- Pause.cscene (the pushed overlay screen) ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
              cam.AddComponent<CameraComponent>(); }

            Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>();
            { Entity dim = scene->CreateEntity("Dim");   // full-screen scrim
              auto& rt = dim.AddComponent<RectTransformComponent>();
              rt.AnchorMin = { 0.0f, 0.0f }; rt.AnchorMax = { 1.0f, 1.0f };
              rt.OffsetMin = { 0.0f, 0.0f }; rt.OffsetMax = { 0.0f, 0.0f };
              rt.ZOrder = -10;
              dim.AddComponent<UiImageComponent>().Tint = { 0.02f, 0.03f, 0.05f, 0.85f };
              scene->SetParent(dim, canvas, false); }
            MakeUiLabel(scene, canvas, "Title", "PAUSED", 0.32f, 56.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
            MakeUiButton(scene, canvas, "ResumeButton", "Resume", "resume_clicked", 0.52f, { 260.0f, 56.0f });
            MakeUiButton(scene, canvas, "QuitButton",   "Quit",   "quit_clicked",   0.64f, { 260.0f, 56.0f });

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Pause.cscene"));
        }

        // ---- flows/Main.cflow ----
        {
            FlowAsset flow;
            flow.Start = "MainMenu";

            FlowState menu;
            menu.Name  = "MainMenu";
            menu.Scene = "project://scenes/MainMenu.cscene";
            menu.EditorPos = { 40.0f, 60.0f };
            menu.Transitions.push_back({ "play_clicked", "Game", "None", false, false, {} });
            menu.Transitions.push_back({ "quit_clicked", "@quit", "None", false, false, {} });
            flow.States.push_back(menu);

            FlowState game;
            game.Name  = "Game";
            game.Scene = "project://scenes/Game.cscene";
            game.EditorPos = { 380.0f, 60.0f };
            game.Transitions.push_back({ "key:Escape", "Pause", "None", /*push=*/true, false, {} });
            flow.States.push_back(game);

            FlowState pause;
            pause.Name    = "Pause";
            pause.Scene   = "project://scenes/Pause.cscene";
            pause.Overlay = true;
            pause.EditorPos = { 720.0f, 60.0f };
            pause.Transitions.push_back({ "resume_clicked", "@pop", "None", false, false, {} });
            pause.Transitions.push_back({ "quit_clicked",   "@quit", "None", false, false, {} });
            flow.States.push_back(pause);

            std::error_code ec;
            fs::create_directories(FileSystem::Resolve("project://flows"), ec);
            flow.Save("project://flows/Main.cflow");
        }

        // ---- manifest: boot the flow ----
        {
            ProjectManifest pm = ProjectManifest::Load("project://project.cproj");
            pm.Name         = proj;
            pm.StartupScene = "scenes/MainMenu.cscene";   // fallback if the flow is removed
            pm.StartupFlow  = "flows/Main.cflow";
            pm.WindowTitle  = "Flow Demo";
            pm.Save(FileSystem::Resolve("project://project.cproj"));
        }

        m_Ctx.Log("[FlowDemo] Created the zero-code two-screen sample.");
        return true;
    }

    bool StarforgeApp::ForgePongExists() const
    {
        std::error_code ec;
        return fs::exists(fs::path("assets") / "projects" / "ForgePong" / "project.cproj", ec);
    }

    bool StarforgeApp::BuildForgePong()
    {
        using namespace Cosmic;
        const std::string proj = "ForgePong";

        // 2D + UI + flow + scripts together (U8 acceptance #2): sprites and an
        // ortho camera (U3), a flipbook hit effect (U4), score UiTexts (U1),
        // menu -> game -> win flow (U5/U6), PaddleController/PongBall scripts.
        if (!ScaffoldProject(proj))
        {
            m_Ctx.Log("[ForgePong] Could not scaffold — templates unavailable.", LogSeverity::Error);
            return false;
        }
        FileSystem::SetActiveProject(proj);

        // ---- textures/hit.png — an 8-frame 16x16 expanding-ring burst sheet ----
        {
            const int fw = 16, fh = 16, frames = 8;
            std::vector<uint8_t> px((size_t)fw * frames * fh * 4, 0);
            for (int f = 0; f < frames; ++f)
            {
                const float radius = 2.0f + 5.5f * (float)f / (float)(frames - 1);
                const float fade   = 1.0f - (float)f / (float)frames;
                for (int y = 0; y < fh; ++y)
                    for (int x = 0; x < fw; ++x)
                    {
                        const float dx = (float)x - 7.5f, dy = (float)y - 7.5f;
                        const float d  = std::sqrt(dx * dx + dy * dy);
                        const float band = 1.4f - std::abs(d - radius);
                        if (band <= 0.0f) continue;
                        const float a = std::min(1.0f, band) * fade;
                        uint8_t* p = &px[(((size_t)y * fw * frames) + (size_t)(f * fw + x)) * 4];
                        p[0] = 255; p[1] = 244; p[2] = 200;
                        p[3] = (uint8_t)(a * 255.0f);
                    }
            }
            std::error_code ec;
            fs::create_directories(FileSystem::Resolve("project://textures"), ec);
            ImageIO::WritePNG(FileSystem::Resolve("project://textures/hit.png"),
                              fw * frames, fh, 4, px.data());
        }

        // ---- Menu.cscene ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
              auto& c = cam.AddComponent<CameraComponent>();
              c.ProjectionType = CameraComponent::Projection::Orthographic;
              c.OrthoSize = 5.0f;
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f }; }

            Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>();
            MakeUiLabel(scene, canvas, "Title", "FORGEPONG", 0.26f, 84.0f,
                        { 0.95f, 0.98f, 1.0f, 1.0f });
            MakeUiLabel(scene, canvas, "Sub", "W/S  vs  Up/Down  -  first to 5", 0.38f, 22.0f,
                        { 0.7f, 0.74f, 0.82f, 1.0f });
            MakeUiButton(scene, canvas, "PlayButton", "Play", "play_clicked", 0.56f, { 260.0f, 56.0f });
            MakeUiButton(scene, canvas, "QuitButton", "Quit", "quit_clicked", 0.68f, { 260.0f, 56.0f });

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Menu.cscene"));
        }

        // ---- Game.cscene ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
              auto& c = cam.AddComponent<CameraComponent>();
              c.ProjectionType = CameraComponent::Projection::Orthographic;
              c.OrthoSize = 5.0f;   // court: 16 x 9 world units at 16:9
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f }; }

            // Court dressing (flat-color sprites; U3 sizing = Transform.Scale).
            MakeSprite(scene, "WallTop",    { 0.0f,  4.5f, 0.0f }, { 16.4f, 0.25f }, { 0.85f, 0.88f, 0.95f, 1.0f });
            MakeSprite(scene, "WallBottom", { 0.0f, -4.5f, 0.0f }, { 16.4f, 0.25f }, { 0.85f, 0.88f, 0.95f, 1.0f });
            MakeSprite(scene, "CenterLine", { 0.0f,  0.0f, -0.1f }, { 0.08f, 8.8f }, { 0.35f, 0.38f, 0.46f, 0.6f }, -1);

            { Entity e = MakeSprite(scene, "PaddleL", { -7.4f, 0.0f, 0.0f }, { 0.3f, 1.6f },
                                    { 0.95f, 0.97f, 1.0f, 1.0f }, 1);
              e.AddComponent<NativeScriptComponent>("PaddleController"); }
            { Entity e = MakeSprite(scene, "PaddleR", {  7.4f, 0.0f, 0.0f }, { 0.3f, 1.6f },
                                    { 0.95f, 0.97f, 1.0f, 1.0f }, 1);
              auto& nsc = e.AddComponent<NativeScriptComponent>("PaddleController");
              nsc.Fields["UseArrows"] = Reflect::FieldValue{ true }; }
            { Entity e = MakeSprite(scene, "Ball", { 0.0f, 0.0f, 0.1f }, { 0.3f, 0.3f },
                                    { 1.0f, 0.9f, 0.5f, 1.0f }, 2);
              e.AddComponent<NativeScriptComponent>("PongBall"); }

            // The one-shot hit flipbook (U4): parked offscreen; PongBall places
            // and restarts it per impact. One sheet frame = 1.2 world units.
            { Entity e = scene->CreateEntity("HitFx");
              auto& t = e.GetComponent<TransformComponent>();
              t.Position = { 0.0f, 1000.0f, 0.2f };
              t.Scale    = { 1.0f, 1.0f, 1.0f };
              auto& s = e.AddComponent<SpriteRendererComponent>();
              s.TexturePath   = "project://textures/hit.png";
              s.PixelsPerUnit = 13.0f;   // 16 px frame ≈ 1.2 units
              s.ZOrder = 5;
              auto& a = e.AddComponent<SpriteAnimationComponent>();
              a.SheetPath = "project://textures/hit.png";
              a.FrameW = 16; a.FrameH = 16; a.Frames = 8; a.FPS = 24.0f;
              a.Loop = false; a.Playing = false; }

            // Score HUD (U1): the PongBall script writes these by Tag.
            Entity canvas = scene->CreateEntity("HUD");
            canvas.AddComponent<CanvasComponent>();
            auto score = [&](const char* tag, float xFrac)
            {
                Entity e = scene->CreateEntity(tag);
                auto& rt = e.AddComponent<RectTransformComponent>();
                rt.AnchorMin = rt.AnchorMax = { xFrac, 0.0f };
                rt.OffsetMin = { -60.0f, 18.0f };
                rt.OffsetMax = {  60.0f, 92.0f };
                auto& txt = e.AddComponent<UiTextComponent>();
                txt.Text = "0";
                txt.SizePx = 56.0f;
                txt.Color = { 0.9f, 0.93f, 1.0f, 0.9f };
                scene->SetParent(e, canvas, false);
            };
            score("ScoreL", 0.38f);
            score("ScoreR", 0.62f);

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Game.cscene"));
        }

        // ---- Win.cscene ----
        {
            Ref<Scene> scene = Scene::Create();
            { Entity cam = scene->CreateEntity("Camera");
              auto& c = cam.AddComponent<CameraComponent>();
              c.ProjectionType = CameraComponent::Projection::Orthographic;
              c.OrthoSize = 5.0f;
              cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f }; }

            Entity canvas = scene->CreateEntity("Canvas");
            canvas.AddComponent<CanvasComponent>();
            MakeUiLabel(scene, canvas, "Title", "MATCH POINT!", 0.30f, 64.0f,
                        { 1.0f, 0.85f, 0.4f, 1.0f });
            MakeUiButton(scene, canvas, "RematchButton", "Rematch", "rematch_clicked", 0.52f, { 280.0f, 56.0f });
            MakeUiButton(scene, canvas, "MenuButton",    "Menu",    "menu_clicked",    0.64f, { 280.0f, 56.0f });

            SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Win.cscene"));
        }

        // ---- flows/Main.cflow: menu -> game -> win ----
        {
            FlowAsset flow;
            flow.Start = "Menu";

            FlowState menu;
            menu.Name  = "Menu";
            menu.Scene = "project://scenes/Menu.cscene";
            menu.EditorPos = { 40.0f, 60.0f };
            menu.Transitions.push_back({ "play_clicked", "Game", "None", false, false, {} });
            menu.Transitions.push_back({ "quit_clicked", "@quit", "None", false, false, {} });
            flow.States.push_back(menu);

            FlowState game;
            game.Name  = "Game";
            game.Scene = "project://scenes/Game.cscene";
            game.EditorPos = { 380.0f, 60.0f };
            game.Transitions.push_back({ "left_wins",  "Win", "None", false, false, {} });
            game.Transitions.push_back({ "right_wins", "Win", "None", false, false, {} });
            game.Transitions.push_back({ "key:Escape", "Menu", "None", false, false, {} });
            flow.States.push_back(game);

            FlowState win;
            win.Name  = "Win";
            win.Scene = "project://scenes/Win.cscene";
            win.EditorPos = { 720.0f, 60.0f };
            win.Transitions.push_back({ "rematch_clicked", "Game", "None", false, false, {} });
            win.Transitions.push_back({ "menu_clicked",    "Menu", "None", false, false, {} });
            flow.States.push_back(win);

            std::error_code ec;
            fs::create_directories(FileSystem::Resolve("project://flows"), ec);
            flow.Save("project://flows/Main.cflow");
        }

        // ---- manifest ----
        {
            ProjectManifest pm = ProjectManifest::Load("project://project.cproj");
            pm.Name         = proj;
            pm.StartupScene = "scenes/Menu.cscene";
            pm.StartupFlow  = "flows/Main.cflow";
            pm.WindowTitle  = "ForgePong";
            pm.WindowWidth  = 1280;
            pm.WindowHeight = 720;
            pm.Save(FileSystem::Resolve("project://project.cproj"));
        }

        m_Ctx.Log("[ForgePong] Created the 2D pong sample (Build Scripts, then Play).");
        return true;
    }

    void StarforgeApp::DrawFirstRunPopup()
    {
        if (m_OpenFirstRun)
        {
            ImGui::OpenPopup("Welcome to Starforge");
            m_OpenFirstRun = false;
        }
        if (ImGui::BeginPopupModal("Welcome to Starforge", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Welcome to Starforge — where worlds are forged.");
            ImGui::Spacing();
            // W7 — Forge Playground is the 3D showcase (terrain/water/particles/
            // nav). The 2D engine offers ForgePong instead, which is its own
            // flagship sample and is entirely 2D.
#ifndef COSMIC_2D_ONLY
            ImGui::TextDisabled("\"Forge Playground\" is a ready-made project that shows the toolset:");
            ImGui::BulletText("terrain, a lake, and a campfire emitter");
            ImGui::BulletText("primitives + a bouncing-ball C++ script (Build Scripts to compile)");
            ImGui::BulletText("a saved telemetry take to load in the Telemetry panel");
            ImGui::Spacing();
            ImGui::TextDisabled("Create it now, or start from a blank project any time.");
            ImGui::Separator();

            if (ImGui::Button("Create Forge Playground", ImVec2(200, 0)))
            {
                m_Settings.PlaygroundOffered = true;
                Prefs::SaveSettings(m_Settings);
                if (BuildForgePlayground())
                    OpenProject("ForgePlayground");
                ImGui::CloseCurrentPopup();
            }
#else
            ImGui::TextDisabled("\"ForgePong\" is a ready-made project that shows the toolset:");
            ImGui::BulletText("sprites + an ortho 2D camera, and a flipbook hit effect");
            ImGui::BulletText("canvas UI score + a menu -> game -> win flow graph");
            ImGui::BulletText("paddle + ball C++ scripts (Build Scripts to compile)");
            ImGui::Spacing();
            ImGui::TextDisabled("Create it now, or start from a blank project any time.");
            ImGui::Separator();

            if (ImGui::Button("Create ForgePong", ImVec2(200, 0)))
            {
                m_Settings.PlaygroundOffered = true;
                Prefs::SaveSettings(m_Settings);
                if (BuildForgePong())
                    OpenProject("ForgePong");
                ImGui::CloseCurrentPopup();
            }
#endif
            ImGui::SameLine();
            if (ImGui::Button("Maybe later", ImVec2(120, 0)))
            {
                m_Settings.PlaygroundOffered = true;
                Prefs::SaveSettings(m_Settings);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

#ifndef COSMIC_2D_ONLY
    bool StarforgeApp::BuildForgePlayground()
    {
        using namespace Cosmic;
        const std::string proj = "ForgePlayground";

        // Reuse the E12 scaffold path (scripts + Module.cpp + CMakeLists +
        // project.cproj), then replace the template scene with a richer one.
        if (!ScaffoldProject(proj))
        {
            m_Ctx.Log("[Playground] Could not scaffold — templates unavailable.", LogSeverity::Error);
            return false;
        }
        FileSystem::SetActiveProject(proj);

        // Author the showcase scene purely in data (no GL needed here — the
        // primitive/terrain/water/emitter meshes are built later by the scene's
        // Sync* passes on first render).
        Ref<Scene> scene = Scene::Create();

        { Entity e = scene->CreateEntity("Sun");
          auto& l = e.AddComponent<DirectionalLightComponent>();
          l.Direction = { -0.32f, -0.85f, -0.45f }; l.Color = { 1.0f, 0.93f, 0.82f }; l.Intensity = 1.15f; }

        { Entity e = scene->CreateEntity("Environment");
          auto& env = e.AddComponent<EnvironmentComponent>();
          env.TimeOfDay = 17.5f;                 // warm low-sun look (tune in the Environment panel)
          env.Fog = true; env.FogDensity = 0.012f;
          env.Bloom = true; env.BloomIntensity = 0.5f; }   // values that visibly change when toggled (H8)

        // Build the terrain heightfield NOW (CPU-only, GL-free) so we can author every
        // object ON its surface via SampleHeight — the fix for the old "content buried
        // inside the 26 m island / camera underground" bug (H8). The saved scene stores
        // only the recipe; SyncWorldSystems rebuilds the terrain (with splat textures)
        // on first render, so this local build is placement math only.
        Ref<Terrain> terrain;
        { Entity e = scene->CreateEntity("Terrain");
          auto& t = e.AddComponent<TerrainComponent>();
          t.UseRecipe = true; t.WorldSize = 256.0f; t.Resolution = 257;
          t.HeightScale = 26.0f; t.Frequency = 2.5f; t.EdgeFalloff = 0.65f;
          terrain = Terrain::Create(BuildTerrainSpec(t));
          e.AddComponent<TerrainColliderComponent>(); }   // J9 — physics ground

        auto groundAt = [&](float x, float z) { return terrain ? terrain->SampleHeight(x, z) : 0.0f; };

        // The forge sits on a shoulder off the peak; everything is placed relative to it.
        const float fx = 34.0f, fz = 22.0f;
        const float fy = groundAt(fx, fz);

        // Lake in the low edge band (EdgeFalloff drops the terrain to ~0 at the rim),
        // so its surface reads as a real shoreline rather than a plane buried in the hill.
        { Entity e = scene->CreateEntity("Lake");
          auto& w = e.AddComponent<WaterComponent>();
          w.UseRecipe = true; w.Preset = WaterPreset::Lake;
          w.Center = { -72.0f, -72.0f }; w.Extent = { 96.0f, 96.0f }; w.SurfaceHeight = 3.0f; }

        { Entity e = scene->CreateEntity("Campfire");
          e.GetComponent<TransformComponent>().Position = { fx, fy, fz };
          e.AddComponent<ParticleEmitterComponent>().UseRecipe = true; }   // default = ember cone

        { Entity e = scene->CreateEntity("Anvil");
          e.GetComponent<TransformComponent>().Position = { fx + 2.2f, fy + 0.4f, fz };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Box).Size = { 1.4f, 0.8f, 0.7f };
          e.AddComponent<MeshRendererComponent>().Color = { 0.24f, 0.25f, 0.28f, 1.0f }; }

        { Entity e = scene->CreateEntity("Ingot");
          e.GetComponent<TransformComponent>().Position = { fx - 2.2f, fy + 0.6f, fz };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Sphere).Radius = 0.6f;
          e.AddComponent<MeshRendererComponent>().Color = { 0.95f, 0.52f, 0.16f, 1.0f }; }

        // Physics demo (Phase 15 / J9): a REAL dynamic sphere that falls under gravity
        // and rests/bounces on the terrain collider (TerrainCollider on "Terrain"). The
        // PhysicsBall script only reads the result and pushes height/velY telemetry, so
        // the Telemetry panel plots the physical bounce. ClassName resolves after Build
        // Scripts (Ctrl+B); until then the Ctrl+B hint shows.
        { Entity e = scene->CreateEntity("Physics Ball");
          const float bz = fz + 3.0f;
          const float by = groundAt(fx, bz);
          e.GetComponent<TransformComponent>().Position = { fx, by + 6.0f, bz };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Sphere).Radius = 0.4f;
          e.AddComponent<MeshRendererComponent>().Color = { 0.90f, 0.90f, 0.95f, 1.0f };
          e.AddComponent<RigidBodyComponent>(MotionType::Dynamic).Restitution = 0.55f;
          e.AddComponent<SphereColliderComponent>().Radius = 0.4f;
          e.AddComponent<NativeScriptComponent>("PhysicsBall"); }

        // A second live script: a HoverController-driven orb settling above the forge.
        { Entity e = scene->CreateEntity("Hover Orb");
          const float hy = fy + 4.0f;
          e.GetComponent<TransformComponent>().Position = { fx + 4.0f, hy, fz - 2.0f };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Sphere).Radius = 0.35f;
          e.AddComponent<MeshRendererComponent>().Color = { 0.45f, 0.70f, 0.95f, 1.0f };
          auto& ns = e.AddComponent<NativeScriptComponent>("HoverController");
          ns.Fields["TargetAltitude"] = Reflect::FieldValue{ hy }; }

        // --- Nav critters (Phase 26 / N5) -----------------------------------
        // A small walkable arena (parented UNDER the NavMesh, SourceMode = From
        // children so the 256 m terrain isn't rasterized) with a ramp up to a
        // platform. Three "Critter" NavAgents patrol it and avoid each other, and
        // chase the "Player" character when it comes near — driven by the NavCritter
        // SystemScript (H9). Baked here at author time into a `.cnav` sidecar, so the
        // sample plays + packages without a manual Regenerate (Ctrl+B builds the
        // scripts first, then Play).
        {
            const float cx = fx - 20.0f, cz = fz + 4.0f;
            const float cy = groundAt(cx, cz) + 0.4f;

            Entity nav = scene->CreateEntity("Nav Mesh");
            auto& nm = nav.AddComponent<NavMeshComponent>();
            nm.SourceMode = NavSourceMode::FromChildren;
            nm.AlwaysRenderHelper = true;
            nm.CellSize = 0.15f; nm.AgentRadius = 0.4f; nm.AgentHeight = 1.6f; nm.AgentMaxClimb = 0.5f;

            auto plate = [&](const char* name, glm::vec3 p, glm::vec3 he, glm::vec3 euler)
            {
                Entity e = scene->CreateEntity(name);
                auto& tr = e.GetComponent<TransformComponent>();
                tr.Position = p; tr.Rotation = euler;
                e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Box).Size = he * 2.0f;
                e.AddComponent<MeshRendererComponent>().Color = { 0.30f, 0.33f, 0.38f, 1.0f };
                e.AddComponent<BoxColliderComponent>().HalfExtents = he;   // static (no RigidBody) — nav + physics ground
                scene->SetParent(e, nav, false);
            };
            plate("Arena Floor",    { cx, cy, cz },                     { 9.0f, 0.25f, 7.0f }, {   0.0f, 0, 0 });
            plate("Arena Ramp",     { cx, cy + 0.75f, cz + 8.4f },      { 3.0f, 0.20f, 2.4f }, { -18.0f, 0, 0 });
            plate("Arena Platform", { cx, cy + 1.55f, cz + 11.6f },     { 3.0f, 0.25f, 2.6f }, {   0.0f, 0, 0 });

            auto critter = [&](glm::vec3 p, glm::vec4 color)
            {
                Entity e = scene->CreateEntity("Critter");   // name == Tag "Critter" (WithTag match)
                e.GetComponent<TransformComponent>().Position = p;
                e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Box).Size = { 0.6f, 0.9f, 0.6f };
                e.AddComponent<MeshRendererComponent>().Color = color;
                auto& ac = e.AddComponent<NavAgentComponent>();
                ac.Radius = 0.4f; ac.Height = 1.6f; ac.MaxSpeed = 3.0f; ac.StoppingDistance = 0.5f;
            };
            critter({ cx - 5.0f, cy + 0.5f, cz - 3.0f }, { 0.85f, 0.35f, 0.30f, 1.0f });
            critter({ cx + 0.0f, cy + 0.5f, cz + 2.0f }, { 0.35f, 0.75f, 0.45f, 1.0f });
            critter({ cx + 5.0f, cy + 0.5f, cz - 2.0f }, { 0.45f, 0.55f, 0.90f, 1.0f });

            // The player character the critters chase (WASD / left stick at Play).
            { Entity e = scene->CreateEntity("Player");
              e.GetComponent<TransformComponent>().Position = { cx, cy + 1.2f, cz - 6.0f };
              auto& pm = e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Cylinder);
              pm.Radius = 0.35f; pm.Height = 1.6f;
              e.AddComponent<MeshRendererComponent>().Color = { 0.95f, 0.85f, 0.35f, 1.0f };
              e.AddComponent<CharacterControllerComponent>();
              e.AddComponent<NativeScriptComponent>("WalkController"); }

            // The class-of-critters system script holder (H9).
            { Entity e = scene->CreateEntity("Critter AI");
              e.AddComponent<SystemScriptComponent>().ClassName = "NavCritter"; }

            // Bake the navmesh now (GL-free — box colliders) and persist the sidecar.
            if (SceneNav::BakeSync(*scene, (entt::entity)nav))
            {
                auto& baked = nav.GetComponent<NavMeshComponent>();
                baked.SidecarPath = "project://scenes/Main.cnav";
                SceneNav::SaveSidecar(baked, baked.SidecarPath);
                m_Ctx.Log("[Playground] Baked the nav-critter arena (3 agents).");
            }
        }

        // Primary camera framing the forge from over the shoulder — the editor adopts
        // this pose on open (H8), so the first frame is the composed shot.
        { Entity e = scene->CreateEntity("Camera");
          auto& tr = e.GetComponent<TransformComponent>();
          tr.Position = { fx, fy + 7.0f, fz + 22.0f }; tr.Rotation = { -12.0f, 0.0f, 0.0f };
          e.AddComponent<CameraComponent>(); }

        SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Main.cscene"));

        GenerateSampleTake();

        m_Ctx.Log("[Playground] Created the Forge Playground sample project.");
        return true;
    }
#endif   // COSMIC_2D_ONLY — the Forge Playground 3D showcase

    void StarforgeApp::GenerateSampleTake()
    {
        // Pre-bake a bouncing-ball telemetry take so the Telemetry panel's
        // "Saved takes" browser has something to Load out of the box. This dogfoods
        // the same DataRecorder->scene.bin/CSV path a live recording uses (E20), and
        // is fully headless (no GL / no Play loop).
        const std::string base = Cosmic::FileSystem::Resolve("user://starforge/takes");
        std::error_code ec; fs::create_directories(base, ec);
        const std::string name = "ForgePlayground_sample";

        Cosmic::DataRecorder rec;
        const uint32_t id = rec.Register("BouncingBall", "BouncingBall", { "height", "velY" });
        const float dt = 1.0f / 60.0f;
        rec.ReserveCapacity(static_cast<size_t>(6.0f / dt) + 4);

        float h = 8.0f, v = 0.0f;
        const float g = -9.81f, restitution = 0.72f;
        for (int i = 0; i < 360; ++i)   // 6 seconds
        {
            v += g * dt;
            h += v * dt;
            if (h < 0.4f) { h = 0.4f; v = -v * restitution; }   // floor bounce
            rec.Tick(dt);
            rec.Record(id, { h, v });
        }
        rec.Flush(base, name, 1.0f / dt);
        rec.WaitForFlush();
        m_Ctx.Log("[Playground] Sample telemetry take: user://starforge/takes/" + name);
    }

    void StarforgeApp::DrawEntityMenu()
    {
        auto make = [&](const char* label, std::function<void(Cosmic::Entity)> build)
        {
            if (ImGui::MenuItem(label))
                Commands::Create(m_Ctx, label, Cosmic::Entity{}, build);
        };

        make("Empty", nullptr);
        // W7 — the three 3D creation menus (Primitive / Light / World) build
        // MeshRenderer, DirectionalLight, Terrain, Water, ParticleEmitter,
        // VoxelVolume and NavMesh components, none of which the 2D build has.
        // The 2D authoring menus (Sprite, Tilemap, 2D Light, UI, Camera) below
        // are shared and untouched.
#ifndef COSMIC_2D_ONLY
        if (ImGui::BeginMenu("Primitive"))
        {
            // Parametric primitives (E15): store shape + params, let the scene
            // render path build the mesh. Live-editable in the Inspector w/ undo.
            using Shape = Cosmic::PrimitiveMeshComponent::Shape;
            auto prim = [&](const char* label, Shape shape)
            {
                make(label, [shape](Cosmic::Entity e)
                {
                    e.AddComponent<Cosmic::PrimitiveMeshComponent>(shape);
                    e.AddComponent<Cosmic::MeshRendererComponent>().Color = { 0.8f, 0.8f, 0.82f, 1.0f };
                });
            };
            prim("Cube",     Shape::Box);
            prim("Sphere",   Shape::Sphere);
            prim("Plane",    Shape::Plane);
            prim("Cylinder", Shape::Cylinder);
            prim("Cone",     Shape::Cone);
            prim("Torus",    Shape::Torus);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light"))
        {
            make("Directional Light", [](Cosmic::Entity e) { e.AddComponent<Cosmic::DirectionalLightComponent>(); });
            make("Point Light",       [](Cosmic::Entity e) { e.AddComponent<Cosmic::PointLightComponent>(); });
            ImGui::EndMenu();
        }
        // World systems (E18): create the entity with a recipe-authored component;
        // Scene::SyncWorldSystems builds the asset from the recipe. Opens the panel.
        if (ImGui::BeginMenu("World"))
        {
            auto world = [&](const char* label, std::function<void(Cosmic::Entity)> build)
            {
                if (ImGui::MenuItem(label))
                {
                    Commands::Create(m_Ctx, label, Cosmic::Entity{}, build);
                    m_ShowWorldSystems = true;
                }
            };
            world("Terrain",          [](Cosmic::Entity e) { e.AddComponent<Cosmic::TerrainComponent>().UseRecipe = true; });
            world("Water",            [](Cosmic::Entity e) { e.AddComponent<Cosmic::WaterComponent>().UseRecipe = true; });
            world("Particle Emitter", [](Cosmic::Entity e) { e.AddComponent<Cosmic::ParticleEmitterComponent>().UseRecipe = true; });
            if (ImGui::MenuItem("Voxel Volume"))
            {
                // A procedurally-generated voxel world (Phase 18). GenEnabled streams
                // hilly terrain around the camera; the Voxels panel edits it + picks
                // blocks. Static collision comes online in Play (per-chunk MeshShape).
                Commands::Create(m_Ctx, "Voxel Volume", Cosmic::Entity{}, [](Cosmic::Entity e)
                {
                    e.AddComponent<Cosmic::VoxelVolumeComponent>().GenEnabled = true;
                });
                m_ShowVoxel = true;
            }
            // Navmesh (N3): an empty marker carrying the bake recipe. Parent the level
            // geometry under it (SourceMode = From children), then hit "Regenerate now"
            // in the Inspector. The recipe + button author it (no dedicated panel).
            if (ImGui::MenuItem("Nav Mesh"))
            {
                Commands::Create(m_Ctx, "Nav Mesh", Cosmic::Entity{}, [](Cosmic::Entity e)
                {
                    e.AddComponent<Cosmic::NavMeshComponent>();
                });
            }
            ImGui::EndMenu();
        }
#endif   // COSMIC_2D_ONLY — Primitive / Light / World creation menus
        // In-game UI (U1). Canvas is a root; Image/Text/Button are parented to the
        // current selection (so they nest under a selected Canvas) and get a
        // RectTransform (authoritative under a canvas).
        if (ImGui::BeginMenu("UI"))
        {
            make("Canvas", [](Cosmic::Entity e) { e.AddComponent<Cosmic::CanvasComponent>(); });
            auto uiChild = [&](const char* label, std::function<void(Cosmic::Entity)> build)
            {
                if (ImGui::MenuItem(label))
                    Commands::Create(m_Ctx, label, m_Ctx.PrimaryEntity(), [build](Cosmic::Entity e)
                    {
                        e.AddComponent<Cosmic::RectTransformComponent>();
                        build(e);
                    });
            };
            uiChild("Image",  [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiImageComponent>(); });
            uiChild("Text",   [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiTextComponent>(); });
            uiChild("Button", [](Cosmic::Entity e)
            {
                e.AddComponent<Cosmic::UiImageComponent>().Tint = { 0.25f, 0.28f, 0.34f, 1.0f };
                e.AddComponent<Cosmic::UiButtonComponent>();
            });
            ImGui::EndMenu();
        }
        // 2D authoring (U3/U4): sprites, tilemaps, an orthographic camera.
        if (ImGui::BeginMenu("2D"))
        {
            make("Sprite", [](Cosmic::Entity e) { e.AddComponent<Cosmic::SpriteRendererComponent>(); });
            if (ImGui::MenuItem("Tilemap"))
            {
                Commands::Create(m_Ctx, "Tilemap", Cosmic::Entity{}, [](Cosmic::Entity e)
                {
                    e.AddComponent<Cosmic::TilemapComponent>().EnsureCells();
                });
                m_ShowTilePalette = true;   // paint tools live in the palette panel
            }
            make("Light", [](Cosmic::Entity e) { e.AddComponent<Cosmic::Light2DComponent>(); });   // X5
            make("Camera (Ortho)", [](Cosmic::Entity e)
            {
                auto& c = e.AddComponent<Cosmic::CameraComponent>();
                c.ProjectionType = Cosmic::CameraComponent::Projection::Orthographic;
            });
            ImGui::EndMenu();
        }
        make("Camera", [](Cosmic::Entity e) { e.AddComponent<Cosmic::CameraComponent>(); });
    }

    // ---- Product homescreen: the project library (S3) ---------------------
    namespace
    {
        // Middle-truncate a path so long absolute roots fit on a card.
        std::string MiddleTruncate(const std::string& s, size_t max = 46)
        {
            if (s.size() <= max) return s;
            const size_t head = max / 2 - 1, tail = max - head - 1;
            return s.substr(0, head) + "…" + s.substr(s.size() - tail);
        }

        std::string DefaultProjectsDir()
        {
        #pragma warning(push)
        #pragma warning(disable: 4996)
            const char* home = std::getenv("USERPROFILE");
        #pragma warning(pop)
            fs::path base = home ? fs::path(home) / "Documents" : fs::path(".");
            return (base / "Starforge Projects").generic_string();
        }
    }

    Cosmic::Ref<Cosmic::Texture2D> StarforgeApp::ThumbFor(const Prefs::ProjectEntry& e)
    {
        std::error_code ec;
        const fs::path root = e.Path.empty()
            ? (fs::path("assets") / "projects" / e.Name)
            : fs::path(e.Path);
        const std::string key = fs::absolute(root / ".starforge" / "thumb.png", ec).generic_string();

        auto it = m_ThumbCache.find(key);
        if (it != m_ThumbCache.end())
            return it->second;   // may be null ("checked, none")

        Cosmic::Ref<Cosmic::Texture2D> tex;
        if (fs::exists(key, ec))
            tex = Cosmic::Texture2D::Create(key);
        m_ThumbCache[key] = tex;
        return tex;
    }

    void StarforgeApp::DrawProjectCard(const Prefs::ProjectEntry& e, float cardW)
    {
        const std::string cardKey = e.Path.empty() ? ("name:" + e.Name) : e.Path;
        std::error_code ec;
        const bool missing = !e.Path.empty() && !fs::exists(fs::path(e.Path) / "project.cproj", ec);
        const bool selected = (m_HomeSelected == cardKey);

        ImGui::PushID(cardKey.c_str());
        if (selected)
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.22f, 0.13f, 0.06f, 1.0f));
        ImGui::BeginChild("card", ImVec2(cardW, 176.0f), true, ImGuiWindowFlags_NoScrollbar);

        // Thumbnail band (grey placeholder until a save writes one).
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        const float thumbH = 88.0f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(tp, ImVec2(tp.x + avail.x, tp.y + thumbH), IM_COL32(28, 30, 36, 255), 4.0f);
        if (Cosmic::Ref<Cosmic::Texture2D> thumb = ThumbFor(e))
            dl->AddImage((ImTextureID)(intptr_t)thumb->GetRendererID(),
                         tp, ImVec2(tp.x + avail.x, tp.y + thumbH), ImVec2(0, 0), ImVec2(1, 1));
        else
            dl->AddText(ImVec2(tp.x + avail.x * 0.5f - 24.0f, tp.y + thumbH * 0.5f - 7.0f),
                        IM_COL32(120, 120, 130, 255), "no preview");
        ImGui::Dummy(ImVec2(avail.x, thumbH));

        // Name + pin.
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.70f, 1.0f));
        ImGui::TextUnformatted(e.Name.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 8.0f);
        if (ImGui::SmallButton(e.Pinned ? "*" : "-"))
        {
            Prefs::SetProjectPinned(e.Name, e.Path, !e.Pinned);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(e.Pinned ? "Unpin" : "Pin");

        // Location + metadata.
        const std::string loc = e.Path.empty() ? ("assets/projects/" + e.Name) : e.Path;
        ImGui::TextDisabled("%s", MiddleTruncate(loc).c_str());
        if (missing)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.35f, 1.0f), "missing on disk");
        else if (!e.LastOpened.empty())
            ImGui::TextDisabled("opened %s", e.LastOpened.c_str());

        // Whole-card interaction: click selects, double-click opens.
        ImGui::EndChild();
        if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            m_HomeSelected = cardKey;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !missing)
            OpenProject(e);

        // Context menu.
        if (ImGui::BeginPopupContextItem("cardctx"))
        {
            if (!missing && ImGui::MenuItem("Open"))
                OpenProject(e);
            if (!e.Path.empty() && ImGui::MenuItem("Show in Explorer"))
                std::system(("explorer \"" + fs::path(e.Path).make_preferred().string() + "\"").c_str());
            if (missing && ImGui::MenuItem("Locate…"))
            {
                if (auto picked = Cosmic::FileDialog::PickFolder("Locate project folder"))
                {
                    Prefs::RemoveProject(e.Name, e.Path);
                    OpenProjectPath(*picked);
                }
            }
            if (ImGui::MenuItem(e.Pinned ? "Unpin" : "Pin"))
                Prefs::SetProjectPinned(e.Name, e.Path, !e.Pinned);
            ImGui::Separator();
            ImGui::TextDisabled("Remove never deletes files");
            if (ImGui::MenuItem("Remove from list"))
            {
                Prefs::RemoveProject(e.Name, e.Path);
                if (m_HomeSelected == cardKey) m_HomeSelected.clear();
            }
            ImGui::EndPopup();
        }

        if (selected)
            ImGui::PopStyleColor();
        ImGui::PopID();
    }

    void StarforgeApp::DrawHomescreen()
    {
        // Anchor the homescreen from just below the top bar (menu + "Exit to Launcher")
        // down to the bottom-right of the OS window work area. Deliberately NOT tied to
        // the editor Viewport dock node's rect: that node can collapse to a thin strip
        // in some saved layouts, which used to shrink the homescreen to an invisible
        // sliver (docs/engineering-notes/starforge-homescreen-hidden.md).
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        const float topY = (m_TopBarBottomY > mv->WorkPos.y + 1.0f &&
                            m_TopBarBottomY < mv->WorkPos.y + mv->WorkSize.y)
                         ? m_TopBarBottomY : mv->WorkPos.y;
        ImVec2 pos  = ImVec2(mv->WorkPos.x, topY);
        ImVec2 size = ImVec2(mv->WorkSize.x, mv->WorkPos.y + mv->WorkSize.y - topY);
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
        // No NoBringToFrontOnFocus here: ImGui inserts such windows at the BACK of
        // the z-stack (g.Windows.push_front) and FocusWindow never raises them, so
        // the homescreen rendered permanently behind the opaque ##CosmicWorkspace
        // dock host (created earlier by WorkspaceLayer) — invisible. Without the
        // flag the window is created/refocused above the dock tree; modals and
        // popups still stack above it. NoSavedSettings + NoDocking keep the
        // product screen fully code-positioned, immune to any stale imgui.ini.
        ImGui::Begin("##StarforgeHome", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);   // rounding + padding captured at Begin

        // Header (K1: the brand mark leads it — same file as the window icon).
        if (m_BrandTex)
        {
            DrawBrandLogo(40.0f);
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.18f, 1.0f));
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("STARFORGE");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("  v%s   —   where worlds are forged", COSMIC_VERSION_STRING);

        ImGui::Spacing();

        // Primary actions.
        if (ImGui::Button("New Project", ImVec2(150, 34)))
        {
            if (m_NewProjectLoc[0] == '\0')
                std::snprintf(m_NewProjectLoc, sizeof(m_NewProjectLoc), "%s", DefaultProjectsDir().c_str());
            ImGui::OpenPopup("New Project");
        }
        ImGui::SameLine();
        if (ImGui::Button("Open…", ImVec2(120, 34)))
        {
            if (auto picked = Cosmic::FileDialog::PickFolder("Open Project Folder"))
                OpenProjectPath(*picked);
        }
        // W7 — the two 3D showcases (Forge Playground, ForgeBlocks) have no 2D
        // build; Flow Sample and Pong Sample below ship on both engines.
#ifndef COSMIC_2D_ONLY
        ImGui::SameLine();
        if (ImGui::Button("Open Sample", ImVec2(140, 34)))
        {
            if (!ForgePlaygroundExists())
                BuildForgePlayground();
            OpenProject("ForgePlayground");
        }
        ImGui::SameLine();
        if (ImGui::Button("Voxel Sample", ImVec2(140, 34)))
        {
            if (!ForgeBlocksExists())
                BuildForgeBlocks();
            OpenProject("ForgeBlocks");
        }
#endif
        ImGui::SameLine();
        if (ImGui::Button("Flow Sample", ImVec2(130, 34)))
        {
            if (!FlowDemoExists())
                BuildFlowDemo();
            OpenProject("FlowDemo");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The zero-code two-screen app: menu -> game -> pause,\n"
                              "all navigation authored as a .cflow (Phase 17 / U8).");
        ImGui::SameLine();
        if (ImGui::Button("Pong Sample", ImVec2(130, 34)))
        {
            if (!ForgePongExists())
                BuildForgePong();
            OpenProject("ForgePong");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ForgePong: 2D sprites + UI + flow + two tiny scripts\n"
                              "(Build Scripts, then Play — Phase 17 / U8).");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputTextWithHint("##search", "Search projects…", m_HomeSearch, sizeof(m_HomeSearch));

        ImGui::Separator();
        ImGui::Spacing();

        // Project grid (pinned first, then most-recent). Missing-on-disk still shows
        // so the user can Locate/Remove it.
        std::vector<Prefs::ProjectEntry> projects = Prefs::LoadProjects();
        std::stable_sort(projects.begin(), projects.end(),
            [](const Prefs::ProjectEntry& a, const Prefs::ProjectEntry& b) { return a.Pinned && !b.Pinned; });

        const std::string filter = m_HomeSearch;
        auto passes = [&](const Prefs::ProjectEntry& e)
        {
            if (filter.empty()) return true;
            std::string hay = e.Name + " " + e.Path;
            std::string needle = filter;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            return hay.find(needle) != std::string::npos;
        };

        ImGui::BeginChild("##grid");
        const float pad = 12.0f;
        const float cardW = 240.0f;
        const float regionW = ImGui::GetContentRegionAvail().x;
        int cols = (int)((regionW + pad) / (cardW + pad));
        if (cols < 1) cols = 1;

        bool any = false;
        int shown = 0;
        for (const auto& e : projects)
        {
            if (!passes(e)) continue;
            any = true;
            if (shown % cols != 0) ImGui::SameLine(0.0f, pad);
            DrawProjectCard(e, cardW);
            ++shown;
        }
        if (!any)
        {
            ImGui::Spacing();
            ImGui::TextDisabled(projects.empty()
                ? "No projects yet — click New Project, or Open Sample to explore the toolset."
                : "No projects match your search.");
        }
        ImGui::EndChild();

        // ---- New Project modal ----
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##npname", m_NewProjectName, sizeof(m_NewProjectName));

            ImGui::TextUnformatted("Location");
            ImGui::SetNextItemWidth(-90.0f);
            ImGui::InputText("##nploc", m_NewProjectLoc, sizeof(m_NewProjectLoc));
            ImGui::SameLine();
            if (ImGui::Button("Browse…", ImVec2(80, 0)))
                if (auto picked = Cosmic::FileDialog::PickFolder("Choose a location for the new project"))
                    std::snprintf(m_NewProjectLoc, sizeof(m_NewProjectLoc), "%s", picked->c_str());

            // Template picker seam. "Pixel Art" = the same scaffold with the
            // pixel_art manifest key preset (U3): every texture the project loads
            // is point-filtered so sprites stay crisp at integer zooms.
            ImGui::TextUnformatted("Template");
            static const char* kTemplates[] = { "C++ scaffold (scripts + player)",
                                                "Pixel Art 2D (point-filtered textures)" };
            static int templateIdx = 0;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::Combo("##nptpl", &templateIdx, kTemplates, IM_ARRAYSIZE(kTemplates));

            if (m_NewProjectName[0] && m_NewProjectLoc[0])
                ImGui::TextDisabled("Creates: %s/%s/", m_NewProjectLoc, m_NewProjectName);

            ImGui::Separator();
            ImGui::BeginDisabled(!(m_NewProjectName[0] && m_NewProjectLoc[0]));
            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                if (NewProjectAt(m_NewProjectName, m_NewProjectLoc))
                {
                    if (templateIdx == 1)   // Pixel Art preset
                    {
                        const std::string mpath = (fs::path(m_NewProjectLoc) / m_NewProjectName
                                                   / "project.cproj").generic_string();
                        ProjectManifest pm = ProjectManifest::Load(mpath);
                        pm.PixelArt = true;
                        pm.Save(mpath);
                        Cosmic::AssetLibrary::SetDefaultTextureSampling(
                            Cosmic::TextureFilter::Nearest, Cosmic::TextureWrap::ClampToEdge);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void StarforgeApp::DrawSaveAsPopup()
    {
        if (m_OpenSaveAs)
        {
            if (m_Ctx.SceneName != "Untitled" && m_Ctx.SceneName != "Sandbox")
                std::snprintf(m_SaveAsName, sizeof(m_SaveAsName), "%s", m_Ctx.SceneName.c_str());
            ImGui::OpenPopup("Save Scene As");
            m_OpenSaveAs = false;
        }

        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Save to project://scenes/");
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText(".cscene", m_SaveAsName, sizeof(m_SaveAsName));
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0)) && m_SaveAsName[0])
            {
                SaveSceneToVfs(std::string("project://scenes/") + m_SaveAsName + ".cscene");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // ---- Model import (E16) — 3D only -------------------------------------
    // MeshImport (and assimp behind it) is excluded from the 2D configuration,
    // and the spawn path authors MeshRenderer/Animator components that do not
    // exist there. The whole importer — modal, texture staging, spawn — fences.
#ifndef COSMIC_2D_ONLY
    void StarforgeApp::DrawImportModelPopup()
    {
        if (m_OpenImportModel)
        {
            ImGui::OpenPopup("Import Model");
            m_OpenImportModel = false;
        }

        if (ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Source model file:");
            ImGui::SetNextItemWidth(440.0f);
            ImGui::InputText("##importsrc", m_ImportPath, sizeof(m_ImportPath));
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))   // native file dialog (H6)
            {
                Cosmic::FileDialogDesc dlg;
                dlg.Title   = "Import Model";
                dlg.Filters = { { "3D models", "*.obj;*.fbx;*.stl;*.dae;*.ply;*.gltf;*.glb" },
                                { "All files", "*.*" } };
                if (auto picked = Cosmic::FileDialog::Open(dlg))
                    std::snprintf(m_ImportPath, sizeof(m_ImportPath), "%s", picked->c_str());
            }
            if (Cosmic::MeshImport::AssimpEnabled())
                ImGui::TextDisabled("Copied into project://models/. Multi-mesh sources spawn a parent with\n"
                                    "child meshes; source materials become .cmat files (textures copied alongside).");
            else
                ImGui::TextDisabled("Copied into project://models/. OBJ/glTF import in this build; FBX/STL/DAE/PLY need the assimp backend.");

            const std::string src = m_ImportPath;
            const std::string ext = Cosmic::MeshImport::Extension(src);
            const bool exists    = !src.empty() && fs::exists(fs::path(src));
            const bool supported = Cosmic::MeshImport::Supports(ext);
            const Cosmic::ImportSettings preset = Cosmic::ImportSettings::DefaultFor(ext);

            if (!src.empty())
            {
                if (!exists)
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "File not found.");
                else if (!supported)
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                                       ".%s needs the assimp backend (this build imports OBJ/glTF).", ext.c_str());
                else
                    ImGui::Text("Assumed unit scale: x%.4f  (edit the generated .cmeta to change).", preset.Scale);
            }

            ImGui::Separator();
            ImGui::BeginDisabled(!(exists && supported));
            if (ImGui::Button("Import", ImVec2(120, 0)))
                if (ImportModelFile(src))
                    ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    namespace
    {
        // File-name-safe slug for generated .cmat / texture names.
        std::string SanitizeAssetName(const std::string& in)
        {
            std::string out;
            out.reserve(in.size());
            for (char c : in)
                out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
            while (!out.empty() && out.back() == '_')
                out.pop_back();
            return out;
        }

        // Materialize one texture reference from an imported model into
        // project://models/: embedded blobs ("*<i>") are written out; file
        // references are copied from the ORIGINAL source directory (relative
        // refs — and, for the absolute-path mess FBX exporters leave behind,
        // a filename-only fallback). Returns the project:// path, "" if the
        // texture could not be found (warned).
        std::string StageImportedTexture(EditorContext& ctx, const Cosmic::ImportedModelDesc& desc,
                                         const std::string& ref, const fs::path& srcDir,
                                         const fs::path& modelsDir, const std::string& modelStem)
        {
            if (ref.empty())
                return {};

            std::error_code ec;
            if (ref[0] == '*')   // embedded texture
            {
                const int idx = std::atoi(ref.c_str() + 1);
                if (idx < 0 || idx >= (int)desc.EmbeddedTextures.size())
                    return {};
                const Cosmic::ImportedTextureDesc& t = desc.EmbeddedTextures[(size_t)idx];
                std::string name = SanitizeAssetName(modelStem + "_" + t.Name);
                if (t.Height == 0)
                {
                    name += "." + (t.FormatHint.empty() ? std::string("png") : t.FormatHint);
                    std::ofstream out(modelsDir / name, std::ios::binary | std::ios::trunc);
                    if (!out.good())
                        return {};
                    out.write(reinterpret_cast<const char*>(t.Bytes.data()), (std::streamsize)t.Bytes.size());
                }
                else
                {
                    name += ".png";
                    if (!Cosmic::ImageIO::WritePNG((modelsDir / name).string(),
                                                   (int)t.Width, (int)t.Height, 4, t.Bytes.data()))
                        return {};
                }
                return "project://models/" + name;
            }

            // File reference: absolute, source-relative, or filename-in-source-dir.
            fs::path refPath = fs::path(ref).make_preferred();
            fs::path found;
            if (refPath.is_absolute() && fs::exists(refPath, ec))
                found = refPath;
            else if (fs::exists(srcDir / refPath, ec))
                found = srcDir / refPath;
            else if (fs::exists(srcDir / refPath.filename(), ec))
                found = srcDir / refPath.filename();
            if (found.empty())
            {
                ctx.Log("[Import] Texture '" + ref + "' not found next to the source — skipped.",
                        LogSeverity::Warn);
                return {};
            }

            const std::string name = found.filename().string();
            fs::copy_file(found, modelsDir / name, fs::copy_options::overwrite_existing, ec);
            if (ec)
            {
                ctx.Log("[Import] Failed to copy texture '" + found.string() + "': " + ec.message(),
                        LogSeverity::Warn);
                return {};
            }
            return "project://models/" + name;
        }

        // Copy the sidecar files a copied source still needs to parse or shade:
        // OBJ "mtllib" material libraries and glTF external ".bin" buffers.
        void StageSourceSidecars(const fs::path& src, const fs::path& modelsDir, const std::string& extLower)
        {
            std::error_code ec;
            if (extLower == "obj")
            {
                std::ifstream in(src);
                std::string   line;
                while (std::getline(in, line))
                {
                    if (line.rfind("mtllib", 0) != 0)
                        continue;
                    std::string name = line.substr(6);
                    name.erase(0, name.find_first_not_of(" \t"));
                    while (!name.empty() && (name.back() == '\r' || name.back() == ' ' || name.back() == '\t'))
                        name.pop_back();
                    if (name.empty())
                        continue;
                    const fs::path mtl = src.parent_path() / name;
                    if (fs::exists(mtl, ec))
                        fs::copy_file(mtl, modelsDir / mtl.filename(), fs::copy_options::overwrite_existing, ec);
                }
            }
            else if (extLower == "gltf")
            {
                // .gltf buffers live in sibling .bin files; copy them so the
                // copied .gltf stays parseable. (.glb is self-contained.)
                for (const auto& entry : fs::directory_iterator(src.parent_path(), ec))
                    if (entry.is_regular_file(ec) && entry.path().extension() == ".bin")
                        fs::copy_file(entry.path(), modelsDir / entry.path().filename(),
                                      fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    bool StarforgeApp::ImportModelFile(const std::string& srcPath)
    {
        std::error_code ec;
        const fs::path    src       = fs::path(srcPath);
        const std::string filename  = src.filename().string();
        const std::string stem      = src.stem().string();
        const std::string ext       = Cosmic::MeshImport::Extension(filename);
        const fs::path    modelsDir = fs::path(Cosmic::FileSystem::Resolve("project://models"));
        fs::create_directories(modelsDir, ec);

        fs::copy_file(src, modelsDir / filename, fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            m_Ctx.Log("[Import] Failed to copy '" + srcPath + "': " + ec.message(), LogSeverity::Error);
            return false;
        }
        StageSourceSidecars(src, modelsDir, ext);

        const std::string vfs      = "project://models/" + filename;
        const std::string resolved = Cosmic::FileSystem::Resolve(vfs);

        // Settings truth: the .cmeta next to the COPY (seeded from the extension
        // preset on first import, kept across re-imports so scale edits stick).
        const Cosmic::ImportSettings settings = Cosmic::MeshImport::LoadOrInitMeta(resolved);

        // Describe the source: sub-meshes + materials + embedded textures (A1).
        Cosmic::ImportedModelDesc desc;
        if (!Cosmic::MeshImport::ImportModelData(desc, resolved, settings) || desc.Meshes.empty())
        {
            m_Ctx.Log("[Import] '" + vfs + "' produced no importable meshes (see log).",
                      LogSeverity::Error);
            return false;
        }

        // Source materials -> generated .cmat files, textures staged alongside.
        // assimp's synthetic "DefaultMaterial" (STL and friends) is skipped so
        // plain CAD parts keep the engine's default Lambert look (E16 spec).
        std::vector<std::string> matVfs(desc.Materials.size());
        for (size_t i = 0; i < desc.Materials.size(); ++i)
        {
            const Cosmic::ImportedMaterialDesc& m = desc.Materials[i];
            const bool referenced = std::any_of(desc.Meshes.begin(), desc.Meshes.end(),
                [&](const Cosmic::ImportedMeshDesc& sm) { return sm.MaterialIndex == (int)i; });
            if (!referenced || m.Name == "DefaultMaterial")
                continue;

            Cosmic::MaterialAsset a;
            a.Albedo      = m.Albedo;
            a.Metallic    = m.Metallic;
            a.Roughness   = m.Roughness;
            a.Emissive    = m.Emissive;
            a.Transparent = m.Opacity < 0.999f;
            a.AlbedoMap     = StageImportedTexture(m_Ctx, desc, m.AlbedoMap,     src.parent_path(), modelsDir, stem);
            a.NormalMap     = StageImportedTexture(m_Ctx, desc, m.NormalMap,     src.parent_path(), modelsDir, stem);
            a.MetalRoughMap = StageImportedTexture(m_Ctx, desc, m.MetalRoughMap, src.parent_path(), modelsDir, stem);
            a.AOMap         = StageImportedTexture(m_Ctx, desc, m.AOMap,         src.parent_path(), modelsDir, stem);
            a.EmissiveMap   = StageImportedTexture(m_Ctx, desc, m.EmissiveMap,   src.parent_path(), modelsDir, stem);

            const std::string matName = SanitizeAssetName(
                stem + "_" + (m.Name.empty() ? "mat" + std::to_string(i) : m.Name));
            const std::string cmat = "project://models/" + matName + ".cmat";
            if (Cosmic::AssetLibrary::SaveMaterialAsset(a, cmat))
            {
                Cosmic::AssetLibrary::Reload(cmat);   // refresh if a re-import overwrote it
                matVfs[i] = cmat;
            }
        }

        // Evict the model (and every "#N" sub-mesh) so a RE-import reloads with
        // fresh geometry/.cmeta units, then let already-placed entities pick the
        // change up: clear their resolved asset so the scene sync re-resolves.
        Cosmic::AssetLibrary::Reload(vfs);
        {
            auto view = m_Ctx.Scene->GetRegistry().view<Cosmic::MeshRendererComponent>();
            for (auto e : view)
            {
                auto& mr = view.get<Cosmic::MeshRendererComponent>(e);
                const bool sameModel = mr.MeshPath == vfs ||
                    (mr.MeshPath.rfind(vfs + "#", 0) == 0);
                if (sameModel)
                {
                    mr.MeshAsset        = nullptr;
                    mr.MeshPathResolved = false;
                }
                if (!mr.MaterialPath.empty() &&
                    std::find(matVfs.begin(), matVfs.end(), mr.MaterialPath) != matVfs.end())
                {
                    mr.MaterialAsset        = nullptr;
                    mr.MaterialPathResolved = false;
                }
            }
        }

        // A4 — regenerate browser thumbnails for everything this import touched.
        m_Ctx.Preview.Invalidate(vfs);
        for (const std::string& m : matVfs)
            if (!m.empty())
                m_Ctx.Preview.Invalidate(m);

        const auto materialFor = [&](const Cosmic::ImportedMeshDesc& sm) -> std::string
        {
            return (sm.MaterialIndex >= 0 && sm.MaterialIndex < (int)matVfs.size())
                       ? matVfs[(size_t)sm.MaterialIndex] : std::string();
        };

        // A2 — a rigged source spawns ready to play: an Animator pointed at the
        // file's first clip (the Inspector's clip picker switches it).
        const std::string firstClip = desc.Clips.empty()
            ? std::string() : vfs + "#" + desc.Clips[0].Name;

        if (desc.Meshes.size() == 1)
        {
            // Single mesh: one entity on the plain path (for OBJ that is the
            // engine's own parser — the pre-A1 byte-identical route).
            const std::string mat = materialFor(desc.Meshes[0]);
            Commands::Create(m_Ctx, "Imported " + stem, Cosmic::Entity{},
                [vfs, mat, firstClip](Cosmic::Entity e)
                {
                    auto& mr = e.AddComponent<Cosmic::MeshRendererComponent>();
                    mr.MeshPath     = vfs;
                    mr.MaterialPath = mat;
                    if (!firstClip.empty())
                        e.AddComponent<Cosmic::AnimatorComponent>().ClipPath = firstClip;
                });
        }
        else
        {
            // Multi-mesh: parent + one child per sub-mesh (E16 hierarchy),
            // recorded as ONE undo step (the K13 RecordSpawn pattern).
            Cosmic::Entity root = m_Ctx.Scene->CreateEntity(stem);
            if (!firstClip.empty())
                root.AddComponent<Cosmic::AnimatorComponent>().ClipPath = firstClip;
            for (size_t i = 0; i < desc.Meshes.size(); ++i)
            {
                const Cosmic::ImportedMeshDesc& sm = desc.Meshes[i];
                Cosmic::Entity child = m_Ctx.Scene->CreateEntity(
                    sm.Name.empty() ? "Mesh_" + std::to_string(i) : sm.Name);
                auto& mr = child.AddComponent<Cosmic::MeshRendererComponent>();
                mr.MeshPath     = Cosmic::MeshImport::SubmeshPath(vfs, (int)i);
                mr.MaterialPath = materialFor(sm);
                m_Ctx.Scene->SetParent(child, root);
            }
            Commands::RecordSpawn(m_Ctx, root, "Imported " + stem);
        }

        const size_t cmatCount = (size_t)std::count_if(matVfs.begin(), matVfs.end(),
                                                       [](const std::string& s) { return !s.empty(); });
        m_Ctx.Log("[Import] " + vfs + " — " + std::to_string(desc.Meshes.size()) + " mesh(es), " +
                  std::to_string(cmatCount) + " material(s), scale x" +
                  std::to_string(settings.Scale) + " (edit the .cmeta + re-import to change).");
        return true;
    }
#endif   // COSMIC_2D_ONLY — the E16 model importer

    // ---- Package & ship (E19 / S5, S2) ------------------------------------

    void StarforgeApp::DrawPackagePopup()
    {
        if (m_OpenPackage)
        {
            ImGui::OpenPopup("Package Project");
            m_OpenPackage = false;
        }

        if (ImGui::BeginPopupModal("Package Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Ship a standalone build of '%s'.", m_Ctx.ProjectName.c_str());
            ImGui::TextDisabled("Stages %s.exe + Cosmic.dll + the project DLL + assets + boot.cfg,", m_Ctx.ProjectName.c_str());
            ImGui::TextDisabled("then embeds the icon and (optionally) zips / builds an installer.");
            ImGui::Text("Output: %s/dist/%s", SdkDir().c_str(), m_Ctx.ProjectName.c_str());
            ImGui::Separator();

            ImGui::Checkbox("Build Release first (recommended for shipping)", &m_PkgOpt.ReleaseBuild);
            if (!m_PkgOpt.ReleaseBuild)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Packaging the CURRENT (Debug) build.");
            ImGui::Checkbox("Zip the output", &m_PkgOpt.MakeZip);
            ImGui::Checkbox("Generate installer script (+ build if Inno on PATH)", &m_PkgOpt.MakeInstaller);

            {
                const ProjectManifest man = ProjectManifest::Load("project://project.cproj");
                if (man.Icon.empty())
                    ImGui::TextDisabled("Icon: none set — see File ▸ Project Settings to add icon.png.");
                else
                    ImGui::TextDisabled("Icon: %s", man.Icon.c_str());
            }

            ImGui::Separator();
            const bool busy = m_Builder.IsBuilding();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Package", ImVec2(120, 0)))
                PackageProject();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (busy && m_PkgAwaitingBuild)
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "building… (see Console)");
            else if (!m_LastDistDir.empty())
            {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.42f, 1.0f), "Done:");
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_LastDistDir.c_str());
                if (ImGui::Button("Show in Explorer", ImVec2(150, 0)))
                    std::system(("explorer \"" + fs::path(m_LastDistDir).make_preferred().string() + "\"").c_str());
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // Assemble a PackageInputs for `target`, then either kick the async Release
    // build (staging on success) or stage the current config immediately.
    bool StarforgeApp::BeginPackage(const Prefs::ProjectEntry& target)
    {
        if (m_Builder.IsBuilding())
        {
            m_Ctx.Log("[Package] A build is already running — wait for it to finish.", LogSeverity::Warn);
            return false;
        }

        std::error_code ec;
        const std::string proj = target.Name;
        const std::string sdk  = SdkDir();
        const bool external    = !target.Path.empty();
        const bool isStarforge = (proj == "Starforge");

        PackageInputs in;
        in.ProjectName = proj;
        in.SdkDir      = sdk;
        in.OutDistDir  = (fs::path(sdk) / "dist" / proj).generic_string();
        in.ExeName     = proj + ".exe";
        in.Version     = COSMIC_VERSION_STRING;

        if (m_PkgOpt.ReleaseBuild)
            in.RuntimeSourceDir = (fs::path(sdk) / "build" / "Runtime" / "Release").generic_string();
        else
            in.RuntimeSourceDir = fs::current_path(ec).generic_string();   // editor's own dir (current config)

        const std::string cfg = m_PkgOpt.ReleaseBuild ? "Release" : BuildRunner::kHotConfig;
        if (external)
            in.ProjectDllPath = (fs::path(target.Path) / "build" / cfg / (proj + ".dll")).generic_string();
        else
            in.ProjectDllPath = (fs::path(in.RuntimeSourceDir) / (proj + ".dll")).generic_string();

        in.ProjectContentDir = external
            ? target.Path
            : (fs::path(in.RuntimeSourceDir) / "assets" / "projects" / proj).generic_string();

        // Icon from the manifest (relative to the project root) or a plain icon.png.
        {
            const std::string cprojDisk = (fs::path(in.ProjectContentDir) / "project.cproj").generic_string();
            const ProjectManifest man = ProjectManifest::Load(cprojDisk);
            std::string icon = man.Icon.empty() ? "" : (fs::path(in.ProjectContentDir) / man.Icon).generic_string();
            if (icon.empty() || !fs::exists(icon, ec))
            {
                const std::string fallback = (fs::path(in.ProjectContentDir) / "icon.png").generic_string();
                if (fs::exists(fallback, ec)) icon = fallback;
            }
            in.IconPng = (icon.empty() || !fs::exists(icon, ec)) ? "" : icon;
        }

        m_PkgPending = in;

        if (!m_PkgOpt.ReleaseBuild)
        {
            // Fast path: package the current build outputs immediately.
            if (Packager::Stage(m_Ctx, in))
            {
                Packager::Finalize(m_Ctx, in, m_PkgOpt);
                m_LastDistDir = in.OutDistDir;
                m_Ctx.Log("[Package] Done -> " + m_LastDistDir);
            }
            return true;
        }

        // Release path: build engine+runtime (if stale) + the project DLL, then stage.
        const std::string cmake   = BuildRunner::FindCMake();
        const std::string sdkBuild = (fs::path(sdk) / "build").generic_string();
        std::vector<BuildStep> steps;

        const fs::path relDir = fs::path(sdk) / "build" / "Runtime" / "Release";
        const bool sdkReleaseReady = fs::exists(relDir / "Cosmic.dll", ec) &&
                                     fs::exists(relDir / "CosmicApp.exe", ec) &&
                                     (!isStarforge || fs::exists(relDir / "Starforge.dll", ec));
        if (!sdkReleaseReady)
        {
            steps.push_back({ "[package] configuring SDK",
                "\"" + cmake + "\" -S \"" + sdk + "\" -B \"" + sdkBuild + "\" -A x64" });
            const std::string sdkTargets = isStarforge ? "Cosmic CosmicApp Starforge" : "Cosmic CosmicApp";
            steps.push_back({ "[package] building engine + runtime (Release)",
                "\"" + cmake + "\" --build \"" + sdkBuild + "\" --config Release --parallel --target " + sdkTargets });
        }

        if (external)
        {
            const std::string projBuild = (fs::path(target.Path) / "build").generic_string();
            steps.push_back({ "[package] configuring project",
                "\"" + cmake + "\" -S \"" + target.Path + "\" -B \"" + projBuild +
                "\" -A x64 -DCOSMIC_SDK_DIR=\"" + sdk + "\" -DGAME_OUTPUT_DIR=\"" + projBuild + "\"" });
            steps.push_back({ "[package] building project (Release)",
                "\"" + cmake + "\" --build \"" + projBuild + "\" --config Release --parallel" });
        }
        else if (!isStarforge)
        {
            const fs::path inTreeSrc = fs::current_path(ec) / "assets" / "projects" / proj;
            const std::string projBuild = (inTreeSrc / "build").generic_string();
            steps.push_back({ "[package] configuring project",
                "\"" + cmake + "\" -S \"" + inTreeSrc.generic_string() + "\" -B \"" + projBuild +
                "\" -A x64 -DCOSMIC_SDK_DIR=\"" + sdk + "\"" });
            steps.push_back({ "[package] building project (Release)",
                "\"" + cmake + "\" --build \"" + projBuild + "\" --config Release --parallel" });
        }
        // Starforge's own DLL rides the SDK build above (via the Starforge target).

        m_BuildPurpose     = BuildPurpose::Package;
        m_PkgAwaitingBuild = true;
        m_Builder.StartSteps(std::move(steps));
        m_Ctx.Log("[Package] Building Release for '" + proj + "'… (see Console)");
        return true;
    }

    void StarforgeApp::PackageProject()
    {
        if (!m_Ctx.ProjectOpen)
            return;
        Prefs::ProjectEntry e;
        e.Name = m_Ctx.ProjectName;
        e.Path = m_Ctx.ProjectPath;
        BeginPackage(e);
    }

    void StarforgeApp::PackageStarforge()
    {
        // S2 — self-package the editor as a product. Starforge is just an in-tree
        // project named "Starforge" whose DLL + content already live in the tree.
        Prefs::ProjectEntry e;
        e.Name = "Starforge";
        e.Path = "";
        BeginPackage(e);
    }

    void StarforgeApp::OnPackageBuildDone(bool ok)
    {
        m_BuildPurpose = BuildPurpose::HotReload;   // reset the shared runner's mode
        if (!m_PkgAwaitingBuild)
            return;
        m_PkgAwaitingBuild = false;
        if (!ok)
        {
            m_Ctx.Log("[Package] Release build failed — see the Console. Nothing was staged.",
                      LogSeverity::Error);
            return;
        }
        if (Packager::Stage(m_Ctx, m_PkgPending))
        {
            Packager::Finalize(m_Ctx, m_PkgPending, m_PkgOpt);
            m_LastDistDir = m_PkgPending.OutDistDir;
            m_Ctx.Log("[Package] Done -> " + m_LastDistDir);
        }
    }

    // ---- Editor conveniences (S7) -----------------------------------------

    void StarforgeApp::RunStandalone()
    {
        if (!m_Ctx.ProjectOpen)
            return;
        std::error_code ec;
        fs::path distExe = fs::path(SdkDir()) / "dist" / m_Ctx.ProjectName / (m_Ctx.ProjectName + ".exe");
        if (fs::exists(distExe, ec))
        {
            m_Ctx.Log("[Run] Launching packaged " + m_Ctx.ProjectName + ".exe");
            std::system(("start \"\" \"" + distExe.make_preferred().string() + "\"").c_str());
            return;
        }
        if (m_Ctx.ProjectPath.empty())
        {
            // In-tree project: the dev exe can boot it directly with --project.
            fs::path devExe = fs::current_path(ec) / "CosmicApp.exe";
            m_Ctx.Log("[Run] Launching CosmicApp --project " + m_Ctx.ProjectName);
            std::system(("start \"\" \"" + devExe.make_preferred().string() + "\" --project " + m_Ctx.ProjectName).c_str());
            return;
        }
        // External project without a package: --project can't mount an external
        // folder yet, so packaging is the path to a runnable standalone.
        m_Ctx.Log("[Run] Package this external project first (File ▸ Package) to run it standalone.",
                  LogSeverity::Warn);
    }

    void StarforgeApp::CaptureThumbnail()
    {
        if (!m_Ctx.ProjectOpen)
            return;
        auto vfb = Cosmic::Application::Get().GetFrameBuffer();
        if (!vfb)
            return;
        vfb->Bind();
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        if (!vfb->ReadPixels(0, rgba, w, h) || w == 0 || h == 0)
            return;

        // Downscale (nearest) to a compact, opaque thumbnail.
        const uint32_t maxW = 480;
        const uint32_t tw = std::min(w, maxW);
        const uint32_t th = std::max<uint32_t>(1, (uint32_t)((uint64_t)h * tw / w));
        std::vector<uint8_t> out((size_t)tw * th * 4);
        for (uint32_t y = 0; y < th; ++y)
            for (uint32_t x = 0; x < tw; ++x)
            {
                const uint32_t sx = x * w / tw, sy = y * h / th;
                const uint8_t* s = rgba.data() + ((size_t)sy * w + sx) * 4;
                uint8_t* d = out.data() + ((size_t)y * tw + x) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255;
            }

        std::error_code ec;
        const fs::path dir = fs::path(ProjectDir()) / ".starforge";
        fs::create_directories(dir, ec);
        const fs::path outPath = dir / "thumb.png";
        Cosmic::ImageIO::WritePNG(outPath.generic_string(), (int)tw, (int)th, 4, out.data());
        m_ThumbCache.erase(fs::absolute(outPath, ec).generic_string());   // force reload in the library
    }

    void StarforgeApp::DrawProjectSettingsPopup()
    {
        static char title[128] = "";
        static char scene[128] = "";
        static int  fixedHz = 60, winW = 0, winH = 0;

        if (m_OpenProjectSettings)
        {
            const ProjectManifest man = ProjectManifest::Load("project://project.cproj");
            std::snprintf(m_IconPathBuf, sizeof(m_IconPathBuf), "%s", man.Icon.c_str());
            std::snprintf(title, sizeof(title), "%s", man.WindowTitle.empty() ? man.Name.c_str() : man.WindowTitle.c_str());
            std::snprintf(scene, sizeof(scene), "%s", man.StartupScene.c_str());
            fixedHz = man.FixedHz; winW = man.WindowWidth; winH = man.WindowHeight;
            ImGui::OpenPopup("Project Settings");
            m_OpenProjectSettings = false;
        }

        // X2 — the popup is organized into a left-nav (General · Window · Packaging ·
        // Physics defaults). Consolidation only: every control below already existed;
        // the Packaging/Physics pages surface where those settings live (no new state).
        static int section = 0;
        ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_NoResize))
        {
            ImGui::Text("Project: %s", m_Ctx.ProjectName.c_str());
            ImGui::Separator();

            const char* sections[] = { "General", "Window", "Packaging", "Physics defaults" };
            ImGui::BeginChild("##ps_nav", ImVec2(150, -ImGui::GetFrameHeightWithSpacing()), true);
            for (int i = 0; i < IM_ARRAYSIZE(sections); ++i)
                if (ImGui::Selectable(sections[i], section == i))
                    section = i;
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild("##ps_body", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

            if (section == 0)   // General
            {
                ImGui::SeparatorText("General");
                ImGui::TextUnformatted("Startup scene");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##psscene", scene, sizeof(scene));

                ImGui::Spacing();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputInt("Fixed Hz", &fixedHz);
                ImGui::SameLine(); ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fixed-timestep rate for the play/game loop.");
            }
            else if (section == 1)   // Window
            {
                ImGui::SeparatorText("Window");
                ImGui::TextUnformatted("Window title");
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputText("##pstitle", title, sizeof(title));

                ImGui::TextUnformatted("Window size (0 = engine default)");
                ImGui::SetNextItemWidth(120.0f); ImGui::InputInt("w##psw", &winW); ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f); ImGui::InputInt("h##psh", &winH);

                ImGui::Spacing();
                ImGui::TextUnformatted("App icon (PNG, relative to the project root)");
                ImGui::SetNextItemWidth(-90.0f);
                ImGui::InputText("##psicon", m_IconPathBuf, sizeof(m_IconPathBuf));
                ImGui::SameLine();
                if (ImGui::Button("Browse…", ImVec2(80, 0)))
                {
                    Cosmic::FileDialogDesc dlg;
                    dlg.Title   = "Choose an icon";
                    dlg.Filters = { { "PNG images", "*.png" } };
                    if (auto picked = Cosmic::FileDialog::Open(dlg))
                    {
                        // Copy into the project root as icon.png so the manifest key stays relative.
                        std::error_code ec;
                        const fs::path dst = fs::path(ProjectContentDir()) / "icon.png";
                        fs::copy_file(*picked, dst, fs::copy_options::overwrite_existing, ec);
                        std::snprintf(m_IconPathBuf, sizeof(m_IconPathBuf), "icon.png");
                    }
                }
            }
            else if (section == 2)   // Packaging
            {
                ImGui::SeparatorText("Packaging");
                ImGui::TextWrapped("Build a shippable app (Release exe, embedded icon, zip/installer) "
                                   "from File \xE2\x96\xB8 Package\xE2\x80\xA6. The exe icon is embedded "
                                   "from the same App icon set under Window.");
            }
            else                     // Physics defaults
            {
                ImGui::SeparatorText("Physics defaults");
                ImGui::TextWrapped("Physics gravity and solver settings are authored per scene on the "
                                   "PhysicsWorld (Play session). There are no project-wide physics "
                                   "defaults to set here yet.");
            }

            ImGui::EndChild();
            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                ProjectManifest man;
                man.Name         = m_Ctx.ProjectName;
                man.WindowTitle  = title;
                man.WindowWidth  = winW; man.WindowHeight = winH;
                man.StartupScene = scene;
                man.FixedHz      = fixedHz;
                man.Icon         = m_IconPathBuf;
                man.Save(Cosmic::FileSystem::Resolve("project://project.cproj"));
                m_Ctx.Log("[Project] Settings saved to project.cproj.");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void StarforgeApp::DrawAboutPopup()
    {
        if (m_OpenAbout)
        {
            ImGui::OpenPopup("About Starforge");
            m_OpenAbout = false;
        }
        if (ImGui::BeginPopupModal("About Starforge", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (m_BrandTex)   // K1 — the same brand mark as the window icon
            {
                DrawBrandLogo(48.0f);
                ImGui::SameLine();
            }
            ImGui::BeginGroup();
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.18f, 1.0f), "Starforge");
            ImGui::TextDisabled("The Cosmic editor — where worlds are forged.");
            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::Text("Engine version: %s", COSMIC_VERSION_STRING);
            if (m_Ctx.ProjectOpen)
            {
                ImGui::Separator();
                ImGui::Text("Project: %s", m_Ctx.ProjectName.c_str());
                if (!m_Ctx.ProjectPath.empty())
                    ImGui::TextDisabled("%s", m_Ctx.ProjectPath.c_str());
                ImGui::Text("Scene: %s", m_Ctx.SceneName.c_str());
            }
            ImGui::Separator();
            if (ImGui::Button("Open Logs Folder", ImVec2(160, 0)))
            {
                std::error_code ec;
                const std::string logs = Cosmic::FileSystem::Resolve("user://logs");
                fs::create_directories(logs, ec);
                std::system(("explorer \"" + fs::path(logs).make_preferred().string() + "\"").c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void StarforgeApp::HandleShortcuts()
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
            return;

        const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) m_Ctx.Commands.Undo();
        if ((ctrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) ||
            (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
            m_Ctx.Commands.Redo();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            if (!SaveScene()) m_OpenSaveAs = true;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
            NewScene();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false))
            BuildScripts();
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            if (Cosmic::Entity e = m_Ctx.PrimaryEntity()) Commands::Duplicate(m_Ctx, e);
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && m_Ctx.HasSelection() && m_Ctx.Scene)
        {
            std::vector<uint64_t> ids;
            for (entt::entity h : m_Ctx.Selection)
                if (Cosmic::Entity e(h, m_Ctx.Scene.get()); e && e.HasComponent<Cosmic::IDComponent>())
                    ids.push_back((uint64_t)e.GetComponent<Cosmic::IDComponent>().ID);
            for (uint64_t id : ids)
                if (Cosmic::Entity e = m_Ctx.Scene->FindByUUID(Cosmic::UUID(id)))
                    Commands::Destroy(m_Ctx, e);
        }
    }

    // =========================================================================
    void StarforgeApp::OnEvent(Cosmic::Event& e)
    {
        if (m_Mode2D) m_Camera2D.OnEvent(e);   // U3 — wheel zoom in 2D mode
        else          m_Rig.OnEvent(e);        // K7 — scroll: orbit zoom / fly speed

        // OS file drop (T3/T8): queue the paths for the Content Browser to import
        // into its current folder next frame. Viewport-spawn-on-OS-drop defers to
        // K13's in-editor drag rules (documented follow-up).
        Cosmic::EventDispatcher dispatch(e);
        dispatch.Dispatch<Cosmic::WindowFileDropEvent>([this](Cosmic::WindowFileDropEvent& drop)
        {
            if (!m_Ctx.ProjectOpen) return false;
            for (const std::string& p : drop.GetPaths())
            {
                m_Ctx.PendingDroppedFiles.push_back(p);
                m_Ctx.Log("[Assets] Dropped: " + p);
            }
            return true;
        });

        if (m_Play == PlayMode::Playing)   // forward input to live scripts
            m_Scripts.DispatchEvent(e);
    }

} // namespace Starforge

// =============================================================================
// Plugin exports — the existing CosmicApp contract (plan §0.6).
// =============================================================================
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Starforge::StarforgeApp();
    }
}
