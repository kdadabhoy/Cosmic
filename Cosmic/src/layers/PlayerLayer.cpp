// layers/PlayerLayer.cpp — standalone scene player (Phase 13 / E13). See header.

#include "layers/PlayerLayer.h"

#include "core/Application.h"
#include "core/Input.h"
#include "codes/KeyCodes.h"
#include "codes/MouseButtonCodes.h"
#include "layers/WorkspaceLayer.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"    // U5 — flow scene loader
#include "scene/ui/UiSystem.h"        // U1 — in-game UI overlay + interaction
#include "camera/Camera.h"
#include "renderer/RenderCommand.h"
#include "utils/Config.h"
#include "utils/FileSystem.h"
#include "utils/Branding.h"   // K1 — runtime app icon (manifest-aware)
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

        // Manifest — startup scene/flow + fixed-dt + window identity (all optional).
        float fixedHz = 60.0f;
        std::string title = m_ProjectName.empty() ? "Cosmic Player" : m_ProjectName;
        std::string startupFlow;
        std::string manifestIcon;   // K1 — the S5 `icon` key (project-root relative)
        int  winW = 0, winH = 0;   // 0 => keep the current window size
        if (Ref<Config> cfg = Config::Load("project://project.cproj"))
        {
            m_StartupScene = cfg->GetString("startup_scene", m_StartupScene);
            startupFlow    = cfg->GetString("startup_flow", "");   // U5 (empty => single scene)
            fixedHz        = static_cast<float>(cfg->GetInt("fixed_dt_hz", 60));
            manifestIcon   = cfg->GetString("icon", "");
            // [window] title/width/height (S5); fall back to the legacy top-level keys.
            title = cfg->GetString("window.title", cfg->GetString("window_title", cfg->GetString("name", title)));
            winW  = static_cast<int>(cfg->GetInt("window.width",  0));
            winH  = static_cast<int>(cfg->GetInt("window.height", 0));

            // U3 — pixel-art preset: point-filter every texture this app loads so
            // sprites stay crisp at integer zooms (set BEFORE any content loads).
            if (cfg->GetBool("pixel_art", false))
                AssetLibrary::SetDefaultTextureSampling(TextureFilter::Nearest,
                                                        TextureWrap::ClampToEdge);

            // U7 — mouse-look apps: capture the cursor from boot. Esc releases;
            // a click inside the window recaptures (see OnUpdate).
            m_CaptureCursor = cfg->GetBool("capture_cursor", false);
            if (m_CaptureCursor)
                Application::Get().GetWindow().SetCursorCaptured(true);
        }
        Application::Get().SetFixedTimestepHz(fixedHz);

        // Window identity (S5): a shipped app opens with its own name + size, not
        // "Cosmic Engine" at 1280x720. The custom title bar reads the workspace
        // project name; the OS/taskbar name comes from the GLFW title.
        Application::Get().GetWindow().SetTitle(title);
        if (winW > 0 && winH > 0)
            Application::Get().GetWindow().SetSize(winW, winH);
        if (auto* ws = Application::Get().GetWorkspaceLayer())
            ws->SetProjectName(title);

        // Runtime app icon (K1): re-resolve now that the project is mounted, so a
        // packaged app's taskbar shows the project icon at runtime (manifest
        // `icon` key / project://icon.png), not just on the exe file. The exe-dir
        // and user:// candidates keep priority (the documented order).
        {
            const std::string icon = Branding::ResolveProcessIcon(
                manifestIcon.empty() ? std::string() : ("project://" + manifestIcon),
                /*includeProjectIcon=*/true);
            if (!icon.empty())
                Application::Get().GetWindow().SetIcon(icon);
        }

        m_Physics.Init();   // J4 — one world for the layer; scenes bind/unbind to it

        // U5 — when a startup flow is named, it OWNS scene selection: its start
        // state's scene is loaded and adopted. Otherwise the single startup scene
        // loads exactly as before (shipped-app compat — no flow key => unchanged).
        if (!startupFlow.empty())
        {
            m_Flow.SetSceneLoader([this](const std::string& p) { return LoadSceneFile(p); });
            FlowAsset asset;
            std::string err;
            if (FlowAsset::Load(asset, "project://" + startupFlow, &err))
            {
                m_UseFlow = true;
                m_Flow.Start(asset);
                if (Ref<Scene> s = m_Flow.ActiveScene())
                {
                    m_Scenes.SetActiveScene(s);
                    RebindScripts();
                }
                else
                    CS_CORE_ERROR("PlayerLayer: startup flow '{0}' produced no active scene.", startupFlow);
            }
            else
                CS_CORE_ERROR("PlayerLayer: failed to load startup flow '{0}': {1}", startupFlow, err);
        }

        if (!m_UseFlow)
        {
            const std::string scenePath = FileSystem::Resolve("project://" + m_StartupScene);
            if (!m_Scenes.Load(scenePath))
                CS_CORE_ERROR("PlayerLayer: could not load startup scene '{0}'.", scenePath);
            RebindScripts();
        }

        CS_CORE_INFO("PlayerLayer: running project '{0}' ({1} '{2}', {3} Hz).",
                     m_ProjectName, m_UseFlow ? "flow" : "scene",
                     m_UseFlow ? startupFlow : m_StartupScene, fixedHz);
    }

    Ref<Scene> PlayerLayer::LoadSceneFile(const std::string& path)
    {
        Ref<Scene> s = Scene::Create();
        const std::string resolved = FileSystem::Resolve(path);
        if (!SceneSerializer::Load(*s, resolved))
        {
            CS_CORE_ERROR("PlayerLayer: flow could not load scene '{0}'.", path);
            return nullptr;
        }
        return s;
    }

    void PlayerLayer::OnDetach()
    {
        Application::Get().GetWindow().SetCursorCaptured(false);   // U7 — never leak capture
        m_Flow.Stop();   // U5 — unsubscribe from scene buses before scenes tear down
        if (m_TrackedScene)
        {
            m_TrackedScene->OnNavStop();                 // N4 — release the crowd first
            m_TrackedScene->OnPhysicsStop(m_Physics);
        }
        m_Scripts.Destroy();
        m_Physics.Shutdown();
        m_SceneRenderer.Shutdown();   // free GPU subsystems while the context is live (H2)
        m_TrackedScene.reset();
        m_Camera.reset();
    }

    void PlayerLayer::RebindScripts()
    {
        Ref<Scene> active = m_Scenes.GetActiveScene();
        if (active == m_TrackedScene)
            return;
        if (m_TrackedScene)
        {
            m_TrackedScene->OnNavStop();                 // N4 — release the old scene's crowd
            m_TrackedScene->OnPhysicsStop(m_Physics);    // tear down the old scene's bodies
        }
        m_Scripts.Destroy();          // tear down the old scene's instances first
        m_TrackedScene = active;
        if (m_TrackedScene)
        {
            m_Scripts.Instantiate(*m_TrackedScene);
            m_TrackedScene->SyncWorldSystems();          // build recipe terrain etc. first
            m_TrackedScene->OnPhysicsStart(m_Physics);   // build bodies from components (J4)
            m_TrackedScene->OnNavStart();                // bind the crowd to the navmesh (N4)
        }
    }

    void PlayerLayer::OnUpdate(float ts)
    {
        auto& app = Application::Get();

        // U7 — mouse-look capture lifecycle: Esc releases, a click recaptures.
        // (The Esc press ALSO reaches the flow below — releasing capture and
        // opening a pause overlay on the same press is the intended feel.)
        if (m_CaptureCursor)
        {
            auto& win = app.GetWindow();
            if (Input::IsKeyPressed(CS_KEY_ESCAPE))
                win.SetCursorCaptured(false);
            else if (!win.IsCursorCaptured() && !m_PrevMouseDown &&
                     Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT))
                win.SetCursorCaptured(true);
        }

        // Advance any queued scene transition; re-instantiate scripts on a swap.
        m_Scenes.OnUpdate(ts);
        RebindScripts();

        // U1 — UI pointer interaction FIRST, so button signals are on the bus
        // before the flow drains them this same frame.
        if (!app.IsPaused())
            UpdateUI(ts);

        // U5 — advance the screen flow (drains queued signals -> transitions).
        if (m_UseFlow)
        {
            const bool esc = Input::IsKeyPressed(CS_KEY_ESCAPE);
            if (esc && !m_PrevEscape) m_Flow.FeedSignal("key:Escape");
            m_PrevEscape = esc;

            m_Flow.OnUpdate(ts);
            if (m_Flow.QuitRequested())
            {
                app.TransitionToLauncher();
                return;
            }
            if (Ref<Scene> fs = m_Flow.ActiveScene(); fs && fs != m_Scenes.GetActiveScene())
            {
                m_Scenes.SetActiveScene(fs);
                RebindScripts();
            }
        }

        if (!app.IsPaused())
        {
            m_Scripts.Tick(ts);
            if (m_TrackedScene)
                m_TrackedScene->UpdateSpriteAnimations(ts);   // U4 — flipbook advance
        }

        RenderScene(ts);
    }

    void PlayerLayer::UpdateUI(float dt)
    {
        (void)dt;
        if (!m_TrackedScene) return;

        Ref<FrameBuffer> fb = Application::Get().GetFrameBuffer();
        if (!fb || fb->GetWidth() < 1 || fb->GetHeight() < 1) return;

        const UiRect viewport{ { 0.0f, 0.0f },
                               { (float)fb->GetWidth(), (float)fb->GetHeight() } };

        const glm::vec2 mouse = Input::GetMousePosition();
        const bool down = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);

        UiPointer p;
        p.Position     = mouse;
        p.Down         = down;
        p.PressedEdge  = down && !m_PrevMouseDown;
        p.ReleasedEdge = !down && m_PrevMouseDown;
        m_PrevMouseDown = down;

        UiSystem::Update(*m_TrackedScene, viewport, p);
    }

    void PlayerLayer::OnFixedUpdate(float fixedDt)
    {
        // Application skips this entirely while paused (Feature B) — so the sim
        // freezes without a guard here. Tick order contract (J4): scripts'
        // OnFixedUpdate -> physics step -> collision-event dispatch.
        m_Scripts.FixedTick(fixedDt);
        if (m_TrackedScene)
        {
            m_TrackedScene->OnPhysicsStep(fixedDt);
            m_TrackedScene->OnNavStep(fixedDt);          // N4 — advance the crowd (post-physics)
            m_TrackedScene->DispatchPhysicsEvents(m_Scripts);
        }
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

    void PlayerLayer::RenderScene(float dt)
    {
        auto& app = Application::Get();
        Ref<FrameBuffer> fb = app.GetFrameBuffer();
        if (!fb || fb->GetWidth() < 1 || fb->GetHeight() < 1)
            return;

        UpdateCamera(static_cast<float>(fb->GetWidth()) / static_cast<float>(fb->GetHeight()));

        const uint32_t vw = fb->GetWidth(), vh = fb->GetHeight();
        if (!m_SceneRenderer.IsInitialized())
            m_SceneRenderer.Init(vw, vh);
        m_SceneRenderer.SetViewportSize(vw, vh);

        fb->Bind();
        RenderCommand::SetViewport(0, 0, vw, vh);
        RenderCommand::SetClearColor({ 0.06f, 0.07f, 0.10f, 1.0f });
        RenderCommand::Clear();

        if (m_TrackedScene)
        {
            // H2 — render through the SAME SceneRenderer path as the editor viewport,
            // so a packaged app gets env/sky/shadows/HDR/post identical to Starforge.
            SceneRenderDesc desc;
            m_TrackedScene->BuildRenderDesc(*m_Camera, dt, desc);
            desc.Settings.ClearColor = { 0.06f, 0.07f, 0.10f, 1.0f };

            // U1 — canvas UI composites after post (LDR bound). No canvas => no-op,
            // so shipped 3D apps are unaffected.
            Scene* scenePtr = m_TrackedScene.get();
            const glm::mat4 camVP = desc.Projection * desc.View;   // X6 — world-anchor projector
            desc.DrawOverlay2D = [scenePtr, vw, vh, camVP]()
            {
                UiSystem::Render(*scenePtr, UiRect{ { 0.0f, 0.0f }, { (float)vw, (float)vh } }, &camVP);
            };

            // U3 — world-space sprites draw in the transparent phase (HDR bound,
            // scene depth live). A scene with no sprites makes no GL calls here.
            desc.DrawTransparent = [scenePtr, vw, vh](const SceneDrawContext& c)
            {
                scenePtr->OnRenderSprites(c.ViewProjection, vw, vh);
                // X5 — 2D lights multiply over the sprite output (no-op without lights).
                scenePtr->OnRender2DLights(c.ViewProjection, vw, vh);
            };

            if (auto* env = m_TrackedScene->FindEnvironment())
            {
                m_SceneRenderer.ApplyEnvironment(*env, desc);
            }
            else
            {
                desc.Settings.Skybox  = false;
                desc.Settings.IBL     = false;
                desc.Settings.Shadows = false;
            }

            m_SceneRenderer.Render(desc);
        }
    }

    void PlayerLayer::OnImGuiRender()
    {
        auto& app = Application::Get();

        // When a screen flow is active it OWNS Escape (key:Escape transitions /
        // its own pause overlay), so the built-in ImGui pause menu stands down.
        if (!m_UseFlow && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
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
