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
#include "utils/FileSystem.h"

#include <imgui.h>
#include <implot.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        m_Camera.SetNavigationStyle(Cosmic::NavStyle::CAD);
        m_Camera.SnapView(Cosmic::ViewPreset::Iso, /*animate=*/false);
        m_Viewport.Init();

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

        // Boot straight into the editor's built-in project + a sandbox scene so
        // the viewport has content. New/Open Project switch via the homescreen.
        m_Ctx.ProjectOpen  = true;
        m_Ctx.ProjectName  = "Starforge";
        m_Ctx.ProjectTitle = "Starforge";
        BuildSandboxScene();
        Prefs::AddRecentProject("Starforge");

        m_Ctx.Log("[Starforge] Editor attached — Stage B (E6-E10).");
        m_Ctx.Log("[Starforge] Viewport: MMB orbit | Ctrl+MMB pan | scroll zoom | LMB pick.");
        m_Ctx.Log("[Starforge] W/E/R gizmo | F frame | Ctrl+Z/Y undo | Ctrl+S save.");
    }

    // =========================================================================
    void StarforgeApp::OnDetach()
    {
        StopScene();                 // tear down script instances before scene reset
        m_SrcWatcher.Stop();
        Prefs::SaveSettings(m_Settings);
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();
        m_Ctx.Scene.reset();         // drop the scene while the module is still loaded
        m_EditSceneBackup.reset();
        m_Module.Unload();           // then FreeLibrary

        // Restore the theme we replaced so a sibling app (or the Launcher) shown
        // next in this process gets its own look, not the forge accent (E21).
        if (!m_PrevTheme.empty())
            Cosmic::ThemeManager::Apply(m_PrevTheme);

        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Starforge: detached.");
    }

    // =========================================================================
    void StarforgeApp::BuildSandboxScene()
    {
        m_Ctx.Scene = Cosmic::Scene::Create();

        {
            Cosmic::Entity sun = m_Ctx.Scene->CreateEntity("Sun");
            auto& light = sun.AddComponent<Cosmic::DirectionalLightComponent>();
            light.Direction = { -0.35f, -1.0f, -0.45f };
            light.Intensity = 1.1f;
        }
        {
            Cosmic::Entity ground = m_Ctx.Scene->CreateEntity("Ground");
            ground.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreatePlane(24.0f, 24.0f)).Color = { 0.32f, 0.34f, 0.38f, 1.0f };
        }
        {
            Cosmic::Entity cube = m_Ctx.Scene->CreateEntity("Forge Cube");
            cube.GetComponent<Cosmic::TransformComponent>().Position = { 0.0f, 0.75f, 0.0f };
            cube.GetComponent<Cosmic::TransformComponent>().Scale    = { 1.5f, 1.5f, 1.5f };
            cube.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreateBox({ 1.0f, 1.0f, 1.0f })).Color = { 0.92f, 0.45f, 0.14f, 1.0f };
        }
        {
            Cosmic::Entity orb = m_Ctx.Scene->CreateEntity("Anvil Orb");
            orb.GetComponent<Cosmic::TransformComponent>().Position = { 2.6f, 0.6f, -1.2f };
            orb.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreateUVSphere(0.6f, 24, 32)).Color = { 0.55f, 0.62f, 0.75f, 1.0f };
        }

        m_Ctx.SceneName    = "Sandbox";
        m_Ctx.SceneVfsPath = "";
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Ctx.ClearDirty();
    }

    // =========================================================================
    // Project / scene lifecycle (E6)
    // =========================================================================
    void StarforgeApp::OpenProject(const std::string& name)
    {
        if (name.empty()) return;
        if (IsPlaying()) StopScene();
        Cosmic::FileSystem::SetActiveProject(name);
        m_Ctx.ProjectOpen  = true;
        m_Ctx.ProjectName  = name;
        m_Ctx.ProjectTitle = name;
        m_Content.Reset();
        Prefs::AddRecentProject(name);

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

        m_Ctx.Log("[Project] Opened '" + name + "'.");
    }

    bool StarforgeApp::ScaffoldProject(const std::string& name)
    {
        // Copy the editor's templates/ into assets/projects/<name>/, replacing the
        // @PROJECT_NAME@ token in every (text) file. The templates ship with the
        // Starforge DLL and sync to assets/projects/Starforge/templates/.
        std::error_code ec;
        const fs::path templates = fs::path("assets") / "projects" / "Starforge" / "templates";
        if (!fs::exists(templates, ec))
            return false;

        const fs::path root = fs::path("assets") / "projects" / name;
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

    void StarforgeApp::NewProject(const std::string& name)
    {
        if (name.empty()) return;
        if (IsPlaying()) StopScene();

        if (!ScaffoldProject(name))
        {
            // Fallback (templates unavailable): a minimal project with no game module.
            std::error_code ec;
            const fs::path root = fs::path("assets") / "projects" / name;
            fs::create_directories(root / "scenes", ec);

            std::ofstream cproj((root / "project.cproj").string(), std::ios::trunc);
            if (cproj)
            {
                cproj << "# Cosmic project manifest (Starforge)\n";
                cproj << "name = \"" << name << "\"\n";
                cproj << "startup_scene = \"scenes/Main.cscene\"\n";
                cproj << "fixed_dt_hz = 60\n";
            }
            Cosmic::FileSystem::SetActiveProject(name);
            Cosmic::Ref<Cosmic::Scene> empty = Cosmic::Scene::Create();
            empty->CreateEntity("Sun").AddComponent<Cosmic::DirectionalLightComponent>();
            Cosmic::SceneSerializer::Save(*empty, Cosmic::FileSystem::Resolve("project://scenes/Main.cscene"));
        }

        OpenProject(name);
    }

    void StarforgeApp::CloseProject()
    {
        if (IsPlaying()) StopScene();
        m_Module.Unload();
        m_SrcWatcher.Stop();
        m_SrcWatchOn = false;
        m_Ctx.ProjectOpen = false;
        m_Ctx.Scene.reset();
        m_EditSceneBackup.reset();
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Content.Reset();
    }

    // =========================================================================
    // Game module build & hot reload (E12)
    // =========================================================================
    std::string StarforgeApp::ProjectDir() const
    {
        std::error_code ec;
        return fs::absolute(Cosmic::FileSystem::Resolve("project://"), ec).generic_string();
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
        m_Ctx.Log("[Build] Building '" + m_Ctx.ProjectName + "' -> " + m_LastBuiltStem + ".dll");
        m_Builder.Start(ProjectDir(), SdkDir(), suffix);
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

        if (!m_Module.Load(m_Ctx.ProjectName, dllStem))
            m_Ctx.Log("[Module] Load failed — scripts unavailable this session.", LogSeverity::Error);

        // Rebuild the scene (custom components + script classes now resolve).
        Cosmic::Ref<Cosmic::Scene> fresh = Cosmic::Scene::Create();
        if (!snapshot.empty())
            Cosmic::SceneSerializer::LoadFromString(*fresh, snapshot);
        m_Ctx.Scene = fresh;
        m_Ctx.ClearDirty();
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
                m_Scripts.FixedTick(m_FixedDt);
                m_Telemetry.OnFixedStep(m_Ctx);   // one telemetry sample per fixed step (E20)
                m_FixedAccum -= m_FixedDt;
            }
        }
        else if (m_Play == PlayMode::Paused && m_StepRequested)
        {
            m_Scripts.FixedTick(m_FixedDt);   // one deterministic step
            m_Telemetry.OnFixedStep(m_Ctx);
            m_StepRequested = false;
        }
    }

    // =========================================================================
    // Frame
    // =========================================================================
    void StarforgeApp::OnUpdate(float ts)
    {
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

        // Game-module build pump (E12): stream cmake output, reload on success.
        m_Builder.Poll(m_Ctx, [this](bool ok)
        {
            if (ok) ReloadModule(m_LastBuiltStem);
            else    m_Ctx.Log("[Build] Failed — see the Console. Keeping the current module.",
                              LogSeverity::Error);
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

        vfb->Bind();
        Cosmic::RenderCommand::SetViewport(0, 0, vfb->GetWidth(), vfb->GetHeight());
        Cosmic::RenderCommand::SetClearColor({ 0.086f, 0.098f, 0.129f, 1.0f });
        Cosmic::RenderCommand::Clear();

        if (m_Ctx.Scene)
        {
            m_Ctx.Scene->OnRender3D(m_Camera.GetCamera());
            // World-system FX (E18): water + particle live preview, using the
            // viewport FBO's color/depth for refraction/depth-fade + soft particles.
            m_Ctx.Scene->OnRenderWorldFX(m_Camera.GetCamera(),
                vfb->GetColorAttachmentRendererID(0), vfb->GetDepthAttachmentRendererID(),
                vfb->GetWidth(), vfb->GetHeight(), ts);
            m_Viewport.DrawSceneOverlay(m_Ctx, m_Camera.GetCamera());
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
    }

    // =========================================================================
    // ImGui
    // =========================================================================
    void StarforgeApp::ApplyDockLayout()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws)
            return;

        ws->SetEdgeRatios(0.19f, 0.22f, 0.06f, 0.26f);
        ws->ClearDockWindows();
        ws->DockWindow("Starforge",       Cosmic::DockPort::TopCenter);
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
            if (m_ShowHierarchy)   m_Hierarchy.OnImGuiRender(m_Ctx);
            if (m_ShowInspector)   m_Inspector.OnImGuiRender(m_Ctx);
            if (m_ShowContent)     m_Content.OnImGuiRender(m_Ctx);
            if (m_ShowConsole)     m_Console.OnImGuiRender(m_Ctx);
            if (m_ShowEnvironment) m_Environment.OnImGuiRender(m_Ctx);
            if (m_ShowMaterial)    m_Material.OnImGuiRender(m_Ctx);
            if (m_ShowWorldSystems) m_WorldSystems.OnImGuiRender(m_Ctx);
            if (m_ShowTelemetry)   m_Telemetry.OnImGuiRender(m_Ctx);
            if (m_ShowStats)       DrawStatsWindow();
        }
        else
        {
            DrawHomescreen();
        }

        // Viewport overlay: status chip + transform gizmo.
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (ws && ws->BeginViewportOverlay())
        {
            const glm::vec2 pos = Cosmic::Application::Get().GetViewportPos();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 8.0f));
            ImGui::TextDisabled("%s%s  |  %d selected%s",
                                m_Ctx.SceneName.c_str(), m_Ctx.Dirty ? " *" : "",
                                (int)m_Ctx.Selection.size(),
                                IsPlaying() ? (m_Play == PlayMode::Paused ? "  |  PAUSED" : "  |  PLAYING") : "");
            // The gizmo is an edit tool — hidden while playing (runtime scene).
            if (m_Ctx.ProjectOpen && m_Ctx.Scene && !IsPlaying())
                m_Viewport.DrawGizmo(m_Ctx, m_Camera);
        }
        if (ws)
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
            ImGui::TextDisabled("No project open — see the Home panel.");
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
            if (ImGui::MenuItem("Package...", nullptr, false, m_Ctx.ProjectOpen))
                m_OpenPackage = true;
            ImGui::Separator();
            if (ImGui::BeginMenu("Recent Projects"))
            {
                for (const auto& n : Prefs::LoadRecentProjects())
                    if (ImGui::MenuItem(n.c_str())) OpenProject(n);
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
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->ResetLayout();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard Shortcuts")) m_OpenShortcuts = true;
            ImGui::EndMenu();
        }
    }

    void StarforgeApp::DrawStatsWindow()
    {
        if (ImGui::Begin("Statistics"))
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
          env.Fog = true; env.FogDensity = 0.012f; }

        { Entity e = scene->CreateEntity("Terrain");
          auto& t = e.AddComponent<TerrainComponent>();
          t.UseRecipe = true; t.WorldSize = 256.0f; t.Resolution = 257;
          t.HeightScale = 26.0f; t.Frequency = 2.5f; t.EdgeFalloff = 0.65f; }

        { Entity e = scene->CreateEntity("Lake");
          auto& w = e.AddComponent<WaterComponent>();
          w.UseRecipe = true; w.Preset = WaterPreset::Lake;
          w.Extent = { 130.0f, 130.0f }; w.SurfaceHeight = 2.0f; }

        { Entity e = scene->CreateEntity("Campfire");
          e.GetComponent<TransformComponent>().Position = { 0.0f, 3.0f, 0.0f };
          e.AddComponent<ParticleEmitterComponent>().UseRecipe = true; }   // default = ember cone

        { Entity e = scene->CreateEntity("Anvil");
          e.GetComponent<TransformComponent>().Position = { 2.2f, 3.2f, 0.0f };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Box).Size = { 1.4f, 0.8f, 0.7f };
          e.AddComponent<MeshRendererComponent>().Color = { 0.24f, 0.25f, 0.28f, 1.0f }; }

        { Entity e = scene->CreateEntity("Ingot");
          e.GetComponent<TransformComponent>().Position = { -2.2f, 3.4f, 0.0f };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Sphere).Radius = 0.6f;
          e.AddComponent<MeshRendererComponent>().Color = { 0.95f, 0.52f, 0.16f, 1.0f }; }

        // Telemetry demo: the BouncingBall script (in the scaffolded module) pushes
        // height/velY channels. ClassName resolves after "Build Scripts" (Ctrl+B).
        { Entity e = scene->CreateEntity("Bouncing Ball");
          e.GetComponent<TransformComponent>().Position = { 0.0f, 8.0f, 5.0f };
          e.AddComponent<PrimitiveMeshComponent>(PrimitiveMeshComponent::Shape::Sphere).Radius = 0.4f;
          e.AddComponent<MeshRendererComponent>().Color = { 0.90f, 0.90f, 0.95f, 1.0f };
          e.AddComponent<NativeScriptComponent>("BouncingBall"); }

        { Entity e = scene->CreateEntity("Camera");
          auto& tr = e.GetComponent<TransformComponent>();
          tr.Position = { 0.0f, 7.0f, 20.0f }; tr.Rotation = { -12.0f, 0.0f, 0.0f };
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

    void StarforgeApp::DrawHomescreen()
    {
        ImGui::Begin("Home");
        ImGui::TextUnformatted("Starforge");
        ImGui::TextDisabled("Where worlds are forged.");
        ImGui::Separator();

        ImGui::TextUnformatted("New Project");
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputText("##newproj", m_NewProjectName, sizeof(m_NewProjectName));
        ImGui::SameLine();
        if (ImGui::Button("Create") && m_NewProjectName[0])
            NewProject(m_NewProjectName);

        // First-run sample (E21) — reusable entry point beyond the welcome popup.
        if (ImGui::Button("Open \"Forge Playground\" sample"))
        {
            if (!ForgePlaygroundExists())
                BuildForgePlayground();
            OpenProject("ForgePlayground");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("terrain + water + campfire + a C++ script + a telemetry take");

        ImGui::Separator();
        ImGui::TextUnformatted("Recent");
        for (const auto& n : Prefs::LoadRecentProjects())
            if (ImGui::Selectable(n.c_str()))
                OpenProject(n);

        ImGui::Separator();
        ImGui::TextUnformatted("All Projects");
        for (const auto& n : Prefs::DiscoverProjects())
            if (ImGui::Selectable((n + "##all").c_str()))
                OpenProject(n);

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
            ImGui::TextUnformatted("Source model file (absolute path):");
            ImGui::SetNextItemWidth(440.0f);
            ImGui::InputText("##importsrc", m_ImportPath, sizeof(m_ImportPath));
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

    // ---- Package & ship (E19) ---------------------------------------------

    void StarforgeApp::DrawPackagePopup()
    {
        if (m_OpenPackage)
        {
            ImGui::OpenPopup("Package Project");
            m_OpenPackage = false;
        }

        if (ImGui::BeginPopupModal("Package Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Stage a standalone build of '%s'.", m_Ctx.ProjectName.c_str());
            ImGui::TextDisabled("Copies CosmicApp.exe (-> %s.exe), Cosmic.dll, the project DLL,", m_Ctx.ProjectName.c_str());
            ImGui::TextDisabled("its assets, and a boot.cfg so it runs with no Launcher.");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
                               "Uses the CURRENT build config — build Release first for a shipping app.");
            ImGui::Text("Output: %s/dist/%s", SdkDir().c_str(), m_Ctx.ProjectName.c_str());
            ImGui::Separator();

            if (ImGui::Button("Package", ImVec2(120, 0)))
                PackageProject();

            if (!m_LastDistDir.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.42f, 1.0f), "Staged.");
                ImGui::TextDisabled("%s", m_LastDistDir.c_str());
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void StarforgeApp::PackageProject()
    {
        if (!m_Ctx.ProjectOpen)
            return;

        std::error_code ec;
        const std::string proj = m_Ctx.ProjectName;

        // The editor runs from build/Runtime/<cfg> (Main.cpp sets CWD to the exe
        // dir): CosmicApp.exe, Cosmic.dll, <proj>.dll and assets/ all live here.
        const fs::path runtime = fs::current_path(ec);
        const fs::path outRoot = fs::path(SdkDir()) / "dist" / proj;

        const fs::path exeSrc    = runtime / "CosmicApp.exe";
        const fs::path engineDll = runtime / "Cosmic.dll";
        const fs::path projDll   = runtime / (proj + ".dll");
        if (!fs::exists(exeSrc, ec) || !fs::exists(engineDll, ec))
        {
            m_Ctx.Log("[Package] CosmicApp.exe / Cosmic.dll not next to the editor — cannot package.",
                      LogSeverity::Error);
            return;
        }
        const bool hasProjectDll = fs::exists(projDll, ec);
        if (!hasProjectDll)
            m_Ctx.Log("[Package] No " + proj + ".dll found — build the project (Ctrl+B) for a runnable app.",
                      LogSeverity::Warn);

        // Fresh output dir.
        fs::remove_all(outRoot, ec);
        fs::create_directories(outRoot, ec);

        // Executable (renamed) + engine + project DLLs.
        fs::copy_file(exeSrc,    outRoot / (proj + ".exe"), fs::copy_options::overwrite_existing, ec);
        fs::copy_file(engineDll, outRoot / "Cosmic.dll",    fs::copy_options::overwrite_existing, ec);
        if (hasProjectDll)
            fs::copy_file(projDll, outRoot / (proj + ".dll"), fs::copy_options::overwrite_existing, ec);

        // Assets: engine assets + ONLY this project's folder (skip other projects).
        const fs::path assetsSrc = runtime / "assets";
        const fs::path assetsDst = outRoot / "assets";
        if (fs::exists(assetsSrc, ec))
        {
            fs::create_directories(assetsDst / "projects", ec);
            for (const auto& entry : fs::directory_iterator(assetsSrc, ec))
            {
                if (entry.path().filename() == "projects")
                    continue;   // handled selectively below
                fs::copy(entry.path(), assetsDst / entry.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            }
            const fs::path projAssets = assetsSrc / "projects" / proj;
            if (fs::exists(projAssets, ec))
                fs::copy(projAssets, assetsDst / "projects" / proj,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }

        // boot.cfg — Main.cpp launches this project when run with no --project.
        {
            std::ofstream boot(outRoot / "boot.cfg", std::ios::trunc);
            boot << "# Cosmic packaged app (E19) — the project launched with no --project flag.\n"
                 << proj << "\n";
        }

        m_LastDistDir = fs::absolute(outRoot, ec).generic_string();
        m_Ctx.Log("[Package] Staged '" + proj + "' -> " + m_LastDistDir);
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
