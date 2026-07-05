// StarforgeApp.cpp — root editor layer + plugin exports. See StarforgeApp.h.

#include "StarforgeApp.h"

#include "commands/EditorCommands.h"
#include "Prefabs.h"

#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#include "core/Version.h"
#include "utils/FileSystem.h"

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

        m_Camera.SetNavigationStyle(Cosmic::NavStyle::CAD);
        m_Camera.SnapView(Cosmic::ViewPreset::Iso, /*animate=*/false);
        m_Viewport.Init();

        // Orbit-about-surface (H1): pivot on the point under the cursor via a one-off
        // depth probe. Invoked only when an orbit drag begins; misses fall back to the
        // controller's ray/target-plane pivot.
        m_Camera.SetPivotProbe([this](const glm::vec2& screenMouse, glm::vec3& out) -> bool
        {
            return m_Viewport.ProbeWorldPoint(m_Ctx, m_Camera.GetCamera(), screenMouse, out);
        });

        // Editor identity: apply the forge accent, remembering the previous theme
        // so OnDetach restores it (other apps in the same process stay untouched).
        m_PrevTheme = Cosmic::ThemeManager::CurrentName();
        ApplyEditorTheme();

        m_Settings = Prefs::LoadSettings();

        // First-run: offer the "Forge Playground" sample once (E21). Only when the
        // sample isn't already present and the user hasn't been asked before.
        if (!m_Settings.PlaygroundOffered && !ForgePlaygroundExists())
            m_OpenFirstRun = true;

        // Route command-stack activity to the dirty flag (belt-and-suspenders —
        // commands also mark dirty directly).
        m_Ctx.Commands.SetDirtyCallback([this] { m_Ctx.MarkDirty(); });

        // Boot into the product homescreen (S2/S3): the project library, not a
        // sandbox. FileSystem stays pointed at the editor's own bundled assets so a
        // stray project:// read resolves there until a real project opens.
        m_Ctx.ProjectOpen = false;

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
    }

    void StarforgeApp::OpenProject(const Prefs::ProjectEntry& e)
    {
        if (e.Name.empty()) return;
        if (IsPlaying()) StopScene();

        MountProject(e);
        m_Content.Reset();

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
        // Back to the editor's own bundled assets for the homescreen.
        Cosmic::FileSystem::SetActiveProject("Starforge");
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
        Cosmic::Entity sun = m_Ctx.Scene->CreateEntity("Sun");
        sun.AddComponent<Cosmic::DirectionalLightComponent>();

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
        runtime->SyncWorldSystems();
        m_Physics.Init();
        runtime->OnPhysicsStart(m_Physics);

        m_Telemetry.OnPlayStart(m_Ctx, m_FixedDt);

        m_Play = PlayMode::Playing;
        m_Ctx.Log("[Play] Started — " + std::to_string(m_Scripts.LiveCount()) + " script(s).");
    }

    void StarforgeApp::StopScene()
    {
        if (!IsPlaying())
            return;
        m_Telemetry.OnPlayStop(m_Ctx);        // flush + keep the take (E20)
        m_Scripts.SetTelemetrySink(nullptr);
        if (m_Ctx.Scene)
            m_Ctx.Scene->OnPhysicsStop(m_Physics);   // J4 — destroy bodies before the runtime scene
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
        if (m_Play == PlayMode::Playing)
        {
            m_Scripts.Tick(ts);
            m_FixedAccum += ts;
            int guard = 0;
            while (m_FixedAccum >= m_FixedDt && guard++ < 8)   // clamp catch-up
            {
                // Tick order contract (J4): scripts OnFixedUpdate -> physics step ->
                // collision-event dispatch -> telemetry sample.
                m_Scripts.FixedTick(m_FixedDt);
                if (m_Ctx.Scene)
                {
                    m_Ctx.Scene->OnPhysicsStep(m_FixedDt);
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
                m_Ctx.Scene->DispatchPhysicsEvents(m_Scripts);
            }
            m_Telemetry.OnFixedStep(m_Ctx);
            m_StepRequested = false;
        }
    }

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
            m_Ctx.Log(text, sev);
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
                m_Camera.SetTarget(target);
                m_Camera.SetYawPitch(yaw, pitch);
                m_Camera.SetDistance(dist);
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
            m_Camera.FrameBounds(mn - pad, mx + pad, /*animate=*/false);
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

        m_WorldSystems.OnUpdate(m_Ctx);   // E18 — drain the async terrain build

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
        m_Camera.OnResize(vpSize.x, vpSize.y);
        m_Camera.SetViewportRect(vpPos, vpSize);
        m_Camera.SetControlEnabled((vpHovered || m_Camera.IsDragging()) && !m_Viewport.GizmoBusy());
        m_Camera.OnUpdate(ts);

        auto vfb = app.GetFrameBuffer();
        if (!vfb)
            return;

        // H2 — SceneRenderer is THE editor render path: environment/sky/shadows/HDR
        // + post all live here (and byte-identically in the standalone PlayerLayer).
        const uint32_t vw = vfb->GetWidth(), vh = vfb->GetHeight();
        if (!m_SceneRenderer.IsInitialized())
            m_SceneRenderer.Init(vw, vh);
        m_SceneRenderer.SetViewportSize(vw, vh);

        vfb->Bind();
        Cosmic::RenderCommand::SetViewport(0, 0, vw, vh);
        Cosmic::RenderCommand::SetClearColor({ 0.086f, 0.098f, 0.129f, 1.0f });
        Cosmic::RenderCommand::Clear();

        if (m_Ctx.Scene)
        {
            Cosmic::SceneRenderDesc desc;
            m_Ctx.Scene->BuildRenderDesc(m_Camera.GetCamera(), ts, desc);
            desc.Settings.ClearColor = { 0.086f, 0.098f, 0.129f, 1.0f };

            if (auto* env = m_Ctx.Scene->FindEnvironment())
            {
                m_SceneRenderer.ApplyEnvironment(*env, desc);
            }
            else
            {
                // No Environment entity → keep today's flat grey-blue viewport
                // (no sky/IBL/shadows) so a scene without one looks unchanged.
                desc.Settings.Skybox  = false;
                desc.Settings.IBL     = false;
                desc.Settings.Shadows = false;
            }

            // Editor overlays (grid/axes/selection) — drawn in HDR with scene depth
            // still bound so they occlude correctly against the composited world.
            desc.DrawTransparent = [this](const Cosmic::SceneDrawContext&)
            {
                m_Viewport.DrawOverlayContent(m_Ctx);
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
        // gizmo toolbar row are always fully visible — not clipped by a small ratio
        // on a big monitor (the old fixed 6% clipped the toolbar). The "Starforge"
        // top bar gets NoTabBar so there's no wasted "▼ Starforge" tab header, and
        // the engine's own File/View chrome menus are hidden (Starforge has its own).
        ws->SetEdgeRatios(0.19f, 0.22f, 0.08f, 0.26f);
        ws->SetEdgeMinPixels(/*top*/ 78.0f, /*bottom*/ 0.0f, /*left*/ 0.0f, /*right*/ 0.0f);
        ws->SetChromeMenusVisible(false);
        ws->ClearDockWindows();
        ws->DockWindow("Starforge",       Cosmic::DockPort::TopCenter, Cosmic::DockFlags::NoTabBar);
        ws->DockWindow("Hierarchy",       Cosmic::DockPort::LeftTop);
        ws->DockWindow("Inspector",       Cosmic::DockPort::RightTop);
        ws->DockWindow("Content Browser", Cosmic::DockPort::BottomCenter);
        ws->DockWindow("Console",         Cosmic::DockPort::BottomRight);

        m_DockApplied = true;
    }

    void StarforgeApp::OnImGuiRender()
    {
        if (!m_DockApplied)
            ApplyDockLayout();

        DrawTopBar();

        if (m_Ctx.ProjectOpen)
        {
            // Pass each panel its visibility bool as p_open (H5) so its ✕ close
            // button flips the same flag the View-menu checkmark reads — they stay
            // in sync, and Reset Layout (which never clears these) reopens them.
            if (m_ShowHierarchy)    m_Hierarchy.OnImGuiRender(m_Ctx, &m_ShowHierarchy);
            if (m_ShowInspector)    m_Inspector.OnImGuiRender(m_Ctx, &m_ShowInspector);
            if (m_ShowContent)      m_Content.OnImGuiRender(m_Ctx, &m_ShowContent);
            if (m_ShowConsole)      m_Console.OnImGuiRender(m_Ctx, &m_ShowConsole);
            if (m_ShowEnvironment)  m_Environment.OnImGuiRender(m_Ctx, &m_ShowEnvironment);
            if (m_ShowMaterial)     m_Material.OnImGuiRender(m_Ctx, &m_ShowMaterial);
            if (m_ShowWorldSystems) m_WorldSystems.OnImGuiRender(m_Ctx, &m_ShowWorldSystems);
            if (m_ShowTelemetry)    m_Telemetry.OnImGuiRender(m_Ctx, &m_ShowTelemetry);
            if (m_ShowStats)        DrawStatsWindow();
        }
        else
        {
            DrawHomescreen();
        }

        // Viewport overlay: status chip + transform gizmo (only with a project open;
        // the homescreen fills the viewport region otherwise).
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (m_Ctx.ProjectOpen && ws && ws->BeginViewportOverlay())
        {
            const glm::vec2 pos = Cosmic::Application::Get().GetViewportPos();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 8.0f));
            // Scene name + dirty star now live in the viewport TAB (H5); the corner
            // overlay keeps just the selection-count chip + play status.
            ImGui::TextDisabled("%d selected%s",
                                (int)m_Ctx.Selection.size(),
                                IsPlaying() ? (m_Play == PlayMode::Paused ? "  |  PAUSED" : "  |  PLAYING") : "");

            // H8 — actionable hint while the scene references unbuilt script classes.
            if (m_ScriptsNeedBuild && !IsPlaying())
            {
                ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 26.0f));
                ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.25f, 1.0f),
                                   "Scripts not built - press Ctrl+B");
            }
            // The gizmo is an edit tool — hidden while playing (runtime scene).
            if (m_Ctx.Scene && !IsPlaying())
                m_Viewport.DrawGizmo(m_Ctx, m_Camera);
        }
        if (m_Ctx.ProjectOpen && ws)
            ws->EndViewportOverlay();

        // Play-mode viewport border tint (the universal "you are live" cue).
        if (IsPlaying())
        {
            const glm::vec2 p = Cosmic::Application::Get().GetViewportPos();
            const glm::vec2 s = Cosmic::Application::Get().GetViewportSize();
            const ImU32 col = (m_Play == PlayMode::Paused)
                ? IM_COL32(255, 200, 50, 255) : IM_COL32(70, 220, 90, 255);
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(p.x, p.y), ImVec2(p.x + s.x, p.y + s.y), col, 0.0f, 0, 3.0f);
        }

        // Input (ImGui frame is live + fresh here). Gizmo already drawn above, so
        // picking sees this frame's gizmo state. Picking/selection stay live in Play
        // so you can inspect runtime entities; only the gizmo is suppressed.
        if (m_Ctx.ProjectOpen && m_Ctx.Scene)
            m_Viewport.OnUpdate(m_Ctx, m_Camera, ImGui::GetIO().DeltaTime);

        DrawSaveAsPopup();
        DrawImportModelPopup();
        DrawPackagePopup();
        DrawProjectSettingsPopup();
        DrawAboutPopup();
        DrawHelpPopups();
        DrawFirstRunPopup();
        HandleShortcuts();
        m_Ctx.ValidateSelection();
    }

    void StarforgeApp::DrawTopBar()
    {
        ImGui::Begin("Starforge", nullptr, ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar())
        {
            DrawMenus();
            ImGui::EndMenuBar();
        }
        if (m_Ctx.ProjectOpen && m_Ctx.Scene)
        {
            DrawPlayControls();
            ImGui::SameLine();
            DrawBuildControls();
            ImGui::SameLine();
            if (ImGui::Button("Run App"))   // S7 — launch as if double-clicked
                RunStandalone();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Run Standalone: the packaged exe if fresh, else the dev exe with this project.");
            // The edit toolbar (gizmo/grid) is hidden while playing to signal the
            // viewport is showing the live runtime scene, not the editable one.
            if (!IsPlaying())
            {
                ImGui::SameLine();
                m_Viewport.DrawToolbar(m_Ctx, m_Camera);
            }
        }
        else
        {
            ImGui::TextDisabled("No project open — use the homescreen.");
        }
        ImGui::End();
    }

    void StarforgeApp::DrawBuildControls()
    {
        const bool scaffolded = ProjectIsScaffolded();

        ImGui::BeginDisabled(!scaffolded || m_Builder.IsBuilding() || IsPlaying());
        if (ImGui::Button("Build Scripts"))
            BuildScripts();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::Checkbox("Auto", &m_AutoBuild);

        ImGui::SameLine();
        const char* txt; ImVec4 col;
        switch (m_Builder.GetStatus())
        {
            case BuildRunner::Status::Building: txt = "building…";  col = ImVec4(1.0f, 0.85f, 0.30f, 1.0f); break;
            case BuildRunner::Status::Success:  txt = "module ok";  col = ImVec4(0.40f, 1.0f, 0.50f, 1.0f); break;
            case BuildRunner::Status::Failed:   txt = "build failed"; col = ImVec4(1.0f, 0.42f, 0.42f, 1.0f); break;
            default:
                txt = !scaffolded ? "no module"
                                  : (m_Module.IsLoaded() ? "module loaded" : "not built");
                col = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
                break;
        }
        ImGui::TextColored(col, "%s", txt);
    }

    void StarforgeApp::DrawPlayControls()
    {
        const bool paused = (m_Play == PlayMode::Paused);
        if (!IsPlaying())
        {
            if (ImGui::Button("Play"))
                PlayScene();
        }
        else
        {
            if (ImGui::Button("Stop"))
                StopScene();
            ImGui::SameLine();
            if (ImGui::Button(paused ? "Resume" : "Pause"))
                TogglePausePlay();
            ImGui::SameLine();
            ImGui::BeginDisabled(!paused);
            if (ImGui::Button("Step"))
                StepScene();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(paused ? ImVec4(1.0f, 0.80f, 0.20f, 1.0f)
                                      : ImVec4(0.30f, 1.0f, 0.42f, 1.0f),
                               paused ? "PAUSED" : "PLAYING");
        }
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
            if (ImGui::MenuItem("Import Model...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenImportModel = true;
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
            ImGui::MenuItem("Hierarchy",       nullptr, &m_ShowHierarchy);
            ImGui::MenuItem("Inspector",       nullptr, &m_ShowInspector);
            ImGui::MenuItem("Content Browser", nullptr, &m_ShowContent);
            ImGui::MenuItem("Console",         nullptr, &m_ShowConsole);
            ImGui::MenuItem("Environment",     nullptr, &m_ShowEnvironment);
            ImGui::MenuItem("Material Editor", nullptr, &m_ShowMaterial);
            ImGui::MenuItem("World Systems",   nullptr, &m_ShowWorldSystems);
            ImGui::MenuItem("Telemetry",       nullptr, &m_ShowTelemetry);
            ImGui::MenuItem("Statistics",      nullptr, &m_ShowStats);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
            {
                // Reopen the core docked panels (a ✕ may have closed them) and rebuild
                // the dock layout so everything returns to its home port (H5).
                m_ShowHierarchy = m_ShowInspector = m_ShowContent = m_ShowConsole = true;
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->ResetLayout();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts")) m_OpenShortcuts = true;
            if (ImGui::MenuItem("About Starforge"))    m_OpenAbout = true;
            ImGui::EndMenu();
        }
    }

    void StarforgeApp::DrawStatsWindow()
    {
        if (ImGui::Begin("Statistics", &m_ShowStats))
        {
            size_t entities = 0;
            if (m_Ctx.Scene)
                for (auto e : m_Ctx.Scene->View<Cosmic::IDComponent>()) { (void)e; ++entities; }

            const Cosmic::Renderer3D::Statistics s = Cosmic::Renderer3D::GetStats();
            ImGui::Text("Entities:          %zu", entities);
            ImGui::Text("Selected:          %zu", m_Ctx.Selection.size());
            ImGui::Separator();
            ImGui::TextDisabled("Renderer3D (last frame)");
            ImGui::Text("Draw calls:        %u", s.DrawCalls);
            ImGui::Text("Meshes submitted:  %u", s.MeshesSubmitted);
            ImGui::Text("Culled (frustum):  %u", s.MeshesCulled);
            ImGui::Text("Drawn:             %u", s.MeshesDrawn);
            ImGui::Text("Auto-inst batches: %u", s.AutoInstanceBatches);
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

    // ---- Forge Playground first-run sample (E21) --------------------------
    bool StarforgeApp::ForgePlaygroundExists() const
    {
        std::error_code ec;
        return fs::exists(fs::path("assets") / "projects" / "ForgePlayground" / "project.cproj", ec);
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
        // Fill the central viewport region so the top menu bar + window chrome stay
        // usable (Exit to Launcher, etc.). Fall back to the main viewport work area
        // if the workspace hasn't reported a region yet.
        auto& app = Cosmic::Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        const ImGuiViewport* mv = ImGui::GetMainViewport();
        ImVec2 pos  = (vpSize.x > 10.0f && vpSize.y > 10.0f) ? ImVec2(vpPos.x, vpPos.y) : mv->WorkPos;
        ImVec2 size = (vpSize.x > 10.0f && vpSize.y > 10.0f) ? ImVec2(vpSize.x, vpSize.y) : mv->WorkSize;
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
        ImGui::Begin("##StarforgeHome", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);
        ImGui::PopStyleVar(2);   // rounding + padding captured at Begin

        // Header.
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
        ImGui::SameLine();
        if (ImGui::Button("Open Sample", ImVec2(140, 34)))
        {
            if (!ForgePlaygroundExists())
                BuildForgePlayground();
            OpenProject("ForgePlayground");
        }
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

            // Template picker seam (v1 has the one C++ scaffold template).
            ImGui::TextUnformatted("Template");
            static const char* kTemplates[] = { "C++ scaffold (scripts + player)" };
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
                    ImGui::CloseCurrentPopup();
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
            ImGui::TextDisabled("Copied into project://models/. OBJ imports now; FBX/STL/DAE/PLY need the assimp backend.");

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
                                       ".%s needs the assimp backend (this build imports OBJ).", ext.c_str());
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

    bool StarforgeApp::ImportModelFile(const std::string& srcPath)
    {
        std::error_code ec;
        const std::string filename  = fs::path(srcPath).filename().string();
        const std::string modelsDir = Cosmic::FileSystem::Resolve("project://models");
        fs::create_directories(modelsDir, ec);

        fs::copy_file(srcPath, fs::path(modelsDir) / filename,
                      fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            m_Ctx.Log("[Import] Failed to copy '" + srcPath + "': " + ec.message(), LogSeverity::Error);
            return false;
        }

        const std::string vfs = "project://models/" + filename;
        Cosmic::AssetLibrary::Reload(vfs);   // fresh import: writes the .cmeta preset + applies units
        Commands::Create(m_Ctx, "Imported " + fs::path(filename).stem().string(), Cosmic::Entity{},
            [vfs](Cosmic::Entity e) { e.AddComponent<Cosmic::MeshRendererComponent>().MeshPath = vfs; });
        m_Ctx.Log("[Import] " + vfs + " (scale from .cmeta).");
        return true;
    }

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

        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("Project Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Project: %s", m_Ctx.ProjectName.c_str());
            ImGui::Separator();

            ImGui::TextUnformatted("Window title");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##pstitle", title, sizeof(title));

            ImGui::TextUnformatted("Window size (0 = engine default)");
            ImGui::SetNextItemWidth(120.0f); ImGui::InputInt("w##psw", &winW); ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f); ImGui::InputInt("h##psh", &winH);

            ImGui::TextUnformatted("Startup scene");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##psscene", scene, sizeof(scene));

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Fixed Hz", &fixedHz);

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
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.18f, 1.0f), "Starforge");
            ImGui::TextDisabled("The Cosmic editor — where worlds are forged.");
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
        m_Camera.OnEvent(e);
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
