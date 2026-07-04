// StarforgeApp.cpp — root editor layer + plugin exports. See StarforgeApp.h.

#include "StarforgeApp.h"

#include "commands/EditorCommands.h"

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
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

        m_Settings = Prefs::LoadSettings();

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
        Prefs::SaveSettings(m_Settings);
        m_Ctx.ClearSelection();
        m_Ctx.Commands.Clear();
        m_Ctx.Scene.reset();

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

        m_Ctx.Log("[Project] Opened '" + name + "'.");
    }

    void StarforgeApp::NewProject(const std::string& name)
    {
        if (name.empty()) return;
        std::error_code ec;
        const fs::path root = fs::path("assets") / "projects" / name;
        fs::create_directories(root / "scenes", ec);
        fs::create_directories(root / "assets", ec);

        std::ofstream cproj((root / "project.cproj").string(), std::ios::trunc);
        if (cproj)
        {
            cproj << "# Cosmic project manifest (Starforge)\n";
            cproj << "name = \"" << name << "\"\n";
            cproj << "startup_scene = \"scenes/Main.cscene\"\n";
            cproj << "fixed_dt_hz = 60\n";
        }

        // Seed an empty scene so the project opens into something.
        Cosmic::FileSystem::SetActiveProject(name);
        Cosmic::Ref<Cosmic::Scene> empty = Cosmic::Scene::Create();
        Cosmic::Entity sun = empty->CreateEntity("Sun");
        sun.AddComponent<Cosmic::DirectionalLightComponent>();
        Cosmic::SceneSerializer::Save(*empty, Cosmic::FileSystem::Resolve("project://scenes/Main.cscene"));

        OpenProject(name);
    }

    void StarforgeApp::CloseProject()
    {
        m_Ctx.ProjectOpen = false;
        m_Ctx.Scene.reset();
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Content.Reset();
    }

    void StarforgeApp::NewScene()
    {
        m_Ctx.Scene = Cosmic::Scene::Create();
        Cosmic::Entity sun = m_Ctx.Scene->CreateEntity("Sun");
        sun.AddComponent<Cosmic::DirectionalLightComponent>();

        m_Ctx.SceneName    = "Untitled";
        m_Ctx.SceneVfsPath = "";
        m_Ctx.Commands.Clear();
        m_Ctx.ClearSelection();
        m_Ctx.ClearDirty();
        m_Ctx.Log("[Scene] New scene.");
    }

    void StarforgeApp::OpenScene(const std::string& vfsPath)
    {
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
        m_Ctx.ClearDirty();
        m_Ctx.Log("[Scene] Opened '" + vfsPath + "'.");
    }

    bool StarforgeApp::SaveScene()
    {
        if (!m_Ctx.Scene) return true;
        if (m_Ctx.SceneVfsPath.empty())
            return false;   // needs a name — caller opens Save As
        SaveSceneToVfs(m_Ctx.SceneVfsPath);
        return true;
    }

    void StarforgeApp::SaveSceneToVfs(const std::string& vfsPath)
    {
        if (!m_Ctx.Scene) return;
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
    // Frame
    // =========================================================================
    void StarforgeApp::OnUpdate(float ts)
    {
        // Content-browser scene-open request.
        if (!m_Ctx.PendingOpenScene.empty())
        {
            const std::string p = m_Ctx.PendingOpenScene;
            m_Ctx.PendingOpenScene.clear();
            OpenScene(p);
        }

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
            m_Viewport.DrawSceneOverlay(m_Ctx, m_Camera.GetCamera());
        }
    }

    void StarforgeApp::Autosave(float ts)
    {
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
            if (m_ShowHierarchy) m_Hierarchy.OnImGuiRender(m_Ctx);
            if (m_ShowInspector) m_Inspector.OnImGuiRender(m_Ctx);
            if (m_ShowContent)   m_Content.OnImGuiRender(m_Ctx);
            if (m_ShowConsole)   m_Console.OnImGuiRender(m_Ctx);
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
            ImGui::TextDisabled("%s%s  |  %d selected",
                                m_Ctx.SceneName.c_str(), m_Ctx.Dirty ? " *" : "",
                                (int)m_Ctx.Selection.size());
            if (m_Ctx.ProjectOpen && m_Ctx.Scene)
                m_Viewport.DrawGizmo(m_Ctx, m_Camera);
        }
        if (ws)
            ws->EndViewportOverlay();

        // Input (ImGui frame is live + fresh here). Gizmo already drawn above, so
        // picking sees this frame's gizmo state.
        if (m_Ctx.ProjectOpen && m_Ctx.Scene)
            m_Viewport.OnUpdate(m_Ctx, m_Camera, ImGui::GetIO().DeltaTime);

        DrawSaveAsPopup();
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
            m_Viewport.DrawToolbar(m_Ctx, m_Camera);
        else
            ImGui::TextDisabled("No project open — see the Home panel.");
        ImGui::End();
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
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout"))
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->ResetLayout();
            ImGui::EndMenu();
        }
    }

    void StarforgeApp::DrawEntityMenu()
    {
        auto make = [&](const char* label, std::function<void(Cosmic::Entity)> build)
        {
            if (ImGui::MenuItem(label))
                Commands::Create(m_Ctx, label, Cosmic::Entity{}, build);
        };

        make("Empty", nullptr);
        if (ImGui::BeginMenu("Mesh"))
        {
            make("Cube",   [](Cosmic::Entity e) { e.AddComponent<Cosmic::MeshRendererComponent>(Cosmic::Mesh::CreateBox({ 1,1,1 })).Color = { 0.8f,0.8f,0.82f,1 }; });
            make("Sphere", [](Cosmic::Entity e) { e.AddComponent<Cosmic::MeshRendererComponent>(Cosmic::Mesh::CreateUVSphere(0.5f, 24, 32)).Color = { 0.8f,0.8f,0.82f,1 }; });
            make("Plane",  [](Cosmic::Entity e) { e.AddComponent<Cosmic::MeshRendererComponent>(Cosmic::Mesh::CreatePlane(10, 10)).Color = { 0.5f,0.5f,0.55f,1 }; });
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light"))
        {
            make("Directional Light", [](Cosmic::Entity e) { e.AddComponent<Cosmic::DirectionalLightComponent>(); });
            make("Point Light",       [](Cosmic::Entity e) { e.AddComponent<Cosmic::PointLightComponent>(); });
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
