// StarforgeApp.cpp — root editor layer + plugin exports. See StarforgeApp.h.

#include "StarforgeApp.h"

#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "graphics/Mesh.h"
#include "utils/FileSystem.h"

#include <imgui.h>
#include <implot.h>

namespace Starforge
{
    StarforgeApp::StarforgeApp() : Cosmic::Layer("Starforge") {}

    // =========================================================================
    void StarforgeApp::OnAttach()
    {
        CS_INFO("Starforge: attaching the Cosmic editor (Phase 13 skeleton).");

        // TODO(E6): becomes SetActiveProject(<open project name>) when a real
        // project opens; "Starforge" mounts the editor's own assets until then.
        Cosmic::FileSystem::SetActiveProject("Starforge");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        // CAD navigation is the editor default (S5.1): MMB orbit about the point
        // under the cursor, Ctrl+MMB pan, scroll zoom-to-cursor, LMB free.
        m_Camera.SetNavigationStyle(Cosmic::NavStyle::CAD);
        m_Camera.SnapView(Cosmic::ViewPreset::Iso, /*animate=*/false);

        BuildSandboxScene();

        m_Ctx.Log("[Starforge] Editor skeleton attached.");
        m_Ctx.Log("[Starforge] Viewport: MMB orbit | Ctrl+MMB pan | scroll zoom-to-cursor.");
        m_Ctx.Log("[Starforge] Gizmos/picking arrive with E9; Save/Open with E2/E6.");
    }

    // =========================================================================
    void StarforgeApp::OnDetach()
    {
        // Drop the scene (and its GPU mesh refs) while the GL context is live.
        m_Ctx.Selected = entt::null;
        m_Ctx.Scene.reset();

        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Starforge: detached.");
    }

    // =========================================================================
    // TODO(E6): replaced by project open/create; TODO(E2): loaded from .cscene.
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
            auto& mr = ground.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreatePlane(24.0f, 24.0f));
            mr.Color = { 0.32f, 0.34f, 0.38f, 1.0f };
        }
        {
            Cosmic::Entity cube = m_Ctx.Scene->CreateEntity("Forge Cube");
            cube.GetComponent<Cosmic::TransformComponent>().Position = { 0.0f, 0.75f, 0.0f };
            cube.GetComponent<Cosmic::TransformComponent>().Scale    = { 1.5f, 1.5f, 1.5f };
            auto& mr = cube.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreateBox({ 1.0f, 1.0f, 1.0f }));
            mr.Color = { 0.92f, 0.45f, 0.14f, 1.0f };   // forge ember
        }
        {
            Cosmic::Entity orb = m_Ctx.Scene->CreateEntity("Anvil Orb");
            orb.GetComponent<Cosmic::TransformComponent>().Position = { 2.6f, 0.6f, -1.2f };
            auto& mr = orb.AddComponent<Cosmic::MeshRendererComponent>(
                Cosmic::Mesh::CreateUVSphere(0.6f, 24, 32));
            mr.Color = { 0.55f, 0.62f, 0.75f, 1.0f };
        }

        m_Ctx.SceneName = "Sandbox";
        m_Ctx.Log("[Scene] Sandbox scene created (Sun, Ground, Forge Cube, Anvil Orb).");
    }

    // =========================================================================
    void StarforgeApp::OnUpdate(float ts)
    {
        auto& app = Cosmic::Application::Get();
        auto* ws  = app.GetWorkspaceLayer();

        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        // Editor camera: control only while the cursor is over the viewport (or
        // mid-drag), so panel interaction never orbits the camera (S5 lesson).
        const bool vpHovered = ws && ws->IsViewportHovered();
        m_Camera.OnResize(vpSize.x, vpSize.y);
        m_Camera.SetViewportRect(vpPos, vpSize);
        m_Camera.SetControlEnabled(vpHovered || m_Camera.IsDragging());
        m_Camera.OnUpdate(ts);

        // NOTE (edit mode): Scene::OnUpdate/OnFixedUpdate are intentionally NOT
        // ticked — systems/scripts only run in Play mode. TODO(E13).

        // Render the scene into the workspace viewport FBO (the engine displays
        // it in the docked "Viewport" window). TODO(E9): switch to the engine
        // SceneRenderer (PBR/shadows/post) + DebugDraw grid once tools land.
        auto vfb = app.GetFrameBuffer();
        if (!vfb)
            return;

        vfb->Bind();
        Cosmic::RenderCommand::SetViewport(0, 0, vfb->GetWidth(), vfb->GetHeight());
        Cosmic::RenderCommand::SetClearColor({ 0.086f, 0.098f, 0.129f, 1.0f });
        Cosmic::RenderCommand::Clear();

        if (m_Ctx.Scene)
            m_Ctx.Scene->OnRender3D(m_Camera.GetCamera());
    }

    // =========================================================================
    void StarforgeApp::ApplyDockLayout()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws)
            return;

        ws->ClearDockWindows();
        ws->DockWindow("Hierarchy",       Cosmic::DockPort::LeftTop);
        ws->DockWindow("Inspector",       Cosmic::DockPort::RightTop);
        ws->DockWindow("Content Browser", Cosmic::DockPort::BottomCenter);
        ws->DockWindow("Console",         Cosmic::DockPort::BottomRight);

        m_DockApplied = true;
    }

    // =========================================================================
    void StarforgeApp::OnImGuiRender()
    {
        if (!m_DockApplied)
            ApplyDockLayout();

        m_Hierarchy.OnImGuiRender(m_Ctx);
        m_Inspector.OnImGuiRender(m_Ctx);
        m_Content.OnImGuiRender(m_Ctx);
        m_Console.OnImGuiRender(m_Ctx);

        // Viewport overlay chip — the E9 wiring point (gizmos, ViewCube, and the
        // play toolbar draw here, INSIDE the viewport window's draw list).
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (ws && ws->BeginViewportOverlay())
        {
            const glm::vec2 pos = Cosmic::Application::Get().GetViewportPos();
            ImGui::SetCursorScreenPos(ImVec2(pos.x + 10.0f, pos.y + 10.0f));
            ImGui::TextDisabled("Starforge — edit mode (skeleton) | scene: %s",
                                m_Ctx.SceneName.c_str());
        }
        if (ws)
            ws->EndViewportOverlay();
    }

    // =========================================================================
    void StarforgeApp::OnEvent(Cosmic::Event& e)
    {
        // Scroll zoom / resize for the editor camera (handlers return false, so
        // events keep propagating). TODO(E9): route W/E/R gizmo hotkeys +
        // ScenePicker clicks here, gated on viewport hover.
        m_Camera.OnEvent(e);
    }

} // namespace Starforge

// =============================================================================
// Plugin exports — the existing CosmicApp contract (plan §0.6). The Launcher
// lists Starforge like any project; `CosmicApp --project Starforge` boots it.
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
