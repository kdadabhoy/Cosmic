// layers/PlayerLayer.cpp — standalone scene player (Phase 13 / E13). See header.

#include "layers/PlayerLayer.h"

#include "core/Application.h"
#include "core/Input.h"
#include "codes/KeyCodes.h"
#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "camera/Camera.h"
#include "renderer/RenderCommand.h"
#include "utils/Config.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
    // A trivial concrete camera: the PlayerLayer computes view/projection from the
    // scene's Primary CameraComponent each frame and pushes them in.
    class PlayerCamera : public Camera
    {
    public:
        void Set(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& pos)
        {
            m_View = view; m_Proj = proj; m_Pos = pos; m_ViewProj = proj * view;
        }
        const glm::mat4& GetViewMatrix() const override           { return m_View; }
        const glm::mat4& GetProjectionMatrix() const override     { return m_Proj; }
        const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProj; }
        const glm::vec3& GetPosition() const override             { return m_Pos; }
    private:
        glm::mat4 m_View{ 1.0f }, m_Proj{ 1.0f }, m_ViewProj{ 1.0f };
        glm::vec3 m_Pos{ 0.0f, 3.0f, 8.0f };
    };

    PlayerLayer::PlayerLayer(const std::string& projectName)
        : Layer("PlayerLayer"), m_ProjectName(projectName)
    {
    }

    PlayerLayer::~PlayerLayer() = default;

    void PlayerLayer::OnAttach()
    {
        if (!m_ProjectName.empty())
            FileSystem::SetActiveProject(m_ProjectName);

        m_Camera = CreateScope<PlayerCamera>();

        // Manifest — startup scene + fixed-dt + window title (all optional).
        float fixedHz = 60.0f;
        std::string title = m_ProjectName.empty() ? "Cosmic Player" : m_ProjectName;
        if (Ref<Config> cfg = Config::Load("project://project.cproj"))
        {
            m_StartupScene = cfg->GetString("startup_scene", m_StartupScene);
            fixedHz        = static_cast<float>(cfg->GetInt("fixed_dt_hz", 60));
            title          = cfg->GetString("window_title", cfg->GetString("name", title));
        }
        Application::Get().SetFixedTimestepHz(fixedHz);
        if (auto* ws = Application::Get().GetWorkspaceLayer())
            ws->SetProjectName(title);

        const std::string scenePath = FileSystem::Resolve("project://" + m_StartupScene);
        if (!m_Scenes.Load(scenePath))
            CS_CORE_ERROR("PlayerLayer: could not load startup scene '{0}'.", scenePath);
        RebindScripts();

        CS_CORE_INFO("PlayerLayer: running project '{0}' (scene '{1}', {2} Hz).",
                     m_ProjectName, m_StartupScene, fixedHz);
    }

    void PlayerLayer::OnDetach()
    {
        m_Scripts.Destroy();
        m_TrackedScene.reset();
        m_Camera.reset();
    }

    void PlayerLayer::RebindScripts()
    {
        Ref<Scene> active = m_Scenes.GetActiveScene();
        if (active == m_TrackedScene)
            return;
        m_Scripts.Destroy();          // tear down the old scene's instances first
        m_TrackedScene = active;
        if (m_TrackedScene)
            m_Scripts.Instantiate(*m_TrackedScene);
    }

    void PlayerLayer::OnUpdate(float ts)
    {
        // Advance any queued scene transition; re-instantiate scripts on a swap.
        m_Scenes.OnUpdate(ts);
        RebindScripts();

        if (!Application::Get().IsPaused())
            m_Scripts.Tick(ts);

        RenderScene();
    }

    void PlayerLayer::OnFixedUpdate(float fixedDt)
    {
        // Application skips this entirely while paused (Feature B) — so the sim
        // freezes without a guard here.
        m_Scripts.FixedTick(fixedDt);
    }

    void PlayerLayer::UpdateCamera(float aspect)
    {
        if (!m_TrackedScene) return;

        // First Primary CameraComponent wins; view = inverse(world transform).
        auto& reg = m_TrackedScene->GetRegistry();
        for (auto e : reg.view<CameraComponent, TransformComponent>())
        {
            const auto& cam = reg.get<CameraComponent>(e);
            if (!cam.Primary) continue;
            const glm::mat4 world = m_TrackedScene->GetWorldTransform(Entity(e, m_TrackedScene.get()));
            m_Camera->Set(glm::inverse(world), cam.GetProjection(aspect), glm::vec3(world[3]));
            return;
        }

        // No primary camera — fall back to a fixed 3/4 view and warn once.
        if (!m_MissingCameraWarned)
        {
            CS_CORE_WARN("PlayerLayer: no Primary CameraComponent — using a default view.");
            m_MissingCameraWarned = true;
        }
        const glm::vec3 eye{ 0.0f, 4.0f, 10.0f };
        m_Camera->Set(glm::lookAt(eye, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0, 1, 0)),
                      glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f), eye);
    }

    void PlayerLayer::RenderScene()
    {
        auto& app = Application::Get();
        Ref<FrameBuffer> fb = app.GetFrameBuffer();
        if (!fb || fb->GetWidth() < 1 || fb->GetHeight() < 1)
            return;

        UpdateCamera(static_cast<float>(fb->GetWidth()) / static_cast<float>(fb->GetHeight()));

        fb->Bind();
        RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());
        RenderCommand::SetClearColor({ 0.06f, 0.07f, 0.10f, 1.0f });
        RenderCommand::Clear();

        if (m_TrackedScene)
            m_TrackedScene->OnRender3D(*m_Camera);
    }

    void PlayerLayer::OnImGuiRender()
    {
        auto& app = Application::Get();

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            app.TogglePause();

        if (!app.IsPaused())
            return;

        // Minimal pause menu (centered).
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Paused", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Paused");
            ImGui::Separator();
            if (ImGui::Button("Resume", ImVec2(180, 0)))
                app.Resume();
            if (ImGui::Button("Quit to Launcher", ImVec2(180, 0)))
            {
                app.Resume();
                app.TransitionToLauncher();
            }
        }
        ImGui::End();
    }

    void PlayerLayer::OnEvent(Event& e)
    {
        if (!Application::Get().IsPaused())
            m_Scripts.DispatchEvent(e);
    }
}
