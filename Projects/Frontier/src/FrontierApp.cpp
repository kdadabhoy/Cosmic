// FrontierApp.cpp — root manager + homescreen. See FrontierApp.h for the overview.

#include "FrontierApp.h"

#include "worlds/IslandWorld.h"
#include "worlds/NightVolcanoWorld.h"
#include "worlds/BlizzardWorld.h"
#include "worlds/MirrorLakeWorld.h"
#include "worlds/StormOceanWorld.h"

#include "panels/GpuProfilerPanel.h"   // F3 — GPU-profiler HUD

#include "layers/WorkspaceLayer.h"   // dock-port registration + viewport queries
#include "ui/ThemeManager.h"         // accent colour for the homescreen tiles
#include "ui/Fonts.h"
#include "ui/Overlay.h"              // UI::Text
#include "ui/IconsLucide.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Frontier
{
    FrontierApp::FrontierApp() : Cosmic::Layer("Frontier") {}

    // =========================================================================
    void FrontierApp::OnAttach()
    {
        CS_INFO("Frontier: attaching the Phase 11 showcase.");

        Cosmic::FileSystem::SetActiveProject("Frontier");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        m_Worlds.emplace_back(std::make_unique<IslandWorld>());
        m_Worlds.emplace_back(std::make_unique<NightVolcanoWorld>());
        m_Worlds.emplace_back(std::make_unique<BlizzardWorld>());
        m_Worlds.emplace_back(std::make_unique<MirrorLakeWorld>());
        m_Worlds.emplace_back(std::make_unique<StormOceanWorld>());
        m_Attached.assign(m_Worlds.size(), false);

        m_Orbit.SetNavigationStyle(Cosmic::NavStyle::Classic);

        // One-time Phase 11 shader self-check: compile every shader this phase
        // ships so a contract drift (or a GLSL typo) surfaces at app boot
        // instead of mid-work-order. Cheap (compile-only; Refs drop right away).
        {
            static const char* kPhase11Shaders[] = {
                "assets/shaders/SkyDetail.glsl",
                "assets/shaders/FlowEmissive.glsl",
                "assets/shaders/WaterFlow.glsl",
                "assets/shaders/PBRInstanced.glsl",
                "assets/shaders/ShadowDepthInstanced.glsl",
                "assets/shaders/TerrainDepth.glsl",
                "assets/shaders/SnowAccum.glsl",
                "assets/shaders/LensFlare.glsl",
            };
            int failed = 0;
            for (const char* path : kPhase11Shaders)
                if (!Cosmic::Shader::Create(path))
                {
                    ++failed;
                    CS_ERROR("Frontier: Phase 11 shader self-check FAILED: {}", path);
                }
            if (failed == 0)
                CS_INFO("Frontier: Phase 11 shader self-check passed ({} shaders).",
                        (int)(sizeof(kPhase11Shaders) / sizeof(kPhase11Shaders[0])));
        }

        SetWorld(-1);
        CS_INFO("Frontier: {} worlds registered.", (int)m_Worlds.size());
    }

    // =========================================================================
    void FrontierApp::OnDetach()
    {
        for (size_t i = 0; i < m_Worlds.size(); ++i)
            if (m_Attached[i] && m_Worlds[i])
                m_Worlds[i]->OnDetach();
        m_Worlds.clear();
        m_Attached.clear();

        // Release the SceneRenderer's GPU resources (env/post/shadow) while the
        // GL context is still live (F2) — worlds released their own above first.
        m_SceneRenderer.Shutdown();

        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("Frontier: detached.");
    }

    // =========================================================================
    void FrontierApp::OnUpdate(float ts)
    {
        if (m_ActiveWorld < 0 || m_ActiveWorld >= (int)m_Worlds.size())
            return;

        auto& app = Cosmic::Application::Get();
        auto* ws  = app.GetWorkspaceLayer();

        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        // --- Camera: fly (F1, default) or orbit inspect fallback (nav-panel toggle).
        // Control only while the cursor is over the viewport (or mid-look/drag).
        const bool vpHovered = ws && ws->IsViewportHovered();
        if (m_FlyCamera)
        {
            m_Fly.OnResize(vpSize.x, vpSize.y);
            m_Fly.SetViewportRect(vpPos, vpSize);
            m_Fly.SetControlEnabled(vpHovered || m_Fly.IsLooking());
            m_Fly.OnUpdate(ts);
        }
        else
        {
            m_Orbit.OnResize(vpSize.x, vpSize.y);
            m_Orbit.SetViewportRect(vpPos, vpSize);
            m_Orbit.SetControlEnabled(vpHovered || m_Orbit.IsDragging());
            m_Orbit.OnUpdate(ts);
        }

        m_WorldTime += ts;

        // --- Render target: the app viewport FBO (WorkspaceLayer bound and
        // cleared it before client layers; we re-assert + clear to the world's
        // color so each world reads distinctly in the skeleton).
        auto vfb = app.GetFrameBuffer();
        if (!vfb)
            return;

        // Engine frame orchestrator (F2): lazily built on the first world entry
        // with the live viewport size, resized every frame. The world fills a
        // SceneRenderDesc and calls Renderer->Render(desc) with vfb bound.
        if (!m_SceneRenderer.IsInitialized())
            m_SceneRenderer.Init(vfb->GetWidth(), vfb->GetHeight());
        m_SceneRenderer.SetViewportSize(vfb->GetWidth(), vfb->GetHeight());

        vfb->Bind();
        Cosmic::RenderCommand::SetViewport(0, 0, vfb->GetWidth(), vfb->GetHeight());

        World& world = *m_Worlds[m_ActiveWorld];
        Cosmic::RenderCommand::SetClearColor(world.GetInfo().PlaceholderClear);
        Cosmic::RenderCommand::Clear();

        WorldContext ctx;
        ctx.DeltaTime      = ts;
        ctx.TimeSeconds    = m_WorldTime;
        ctx.ViewportWidth  = vfb->GetWidth();
        ctx.ViewportHeight = vfb->GetHeight();
        ctx.OrbitFallback  = &m_Orbit;
        ctx.Camera         = m_FlyCamera ? &m_Fly : nullptr;   // null => orbit is active
        ctx.Renderer       = &m_SceneRenderer;

        world.OnUpdate(ctx);
        m_LastCtx = ctx;
    }

    // =========================================================================
    void FrontierApp::OnImGuiRender()
    {
        if (m_AppliedDock != DockStateKey())
            ApplyDockLayout();

        if (m_ActiveWorld < 0)
        {
            DrawHomescreen();
            return;
        }

        DrawNavPanel();

        // Shared GPU-profiler HUD (F3): per-pass ms from the SceneRenderer's timer
        // zones. Drawn before the world panels so clicking Home (which may clear
        // m_ActiveWorld) doesn't skip it.
        GpuProfilerPanel::Draw();

        // DrawNavPanel's Home / world-switch buttons call SetWorld, which can
        // clear m_ActiveWorld to -1 (Home) or change it mid-frame — re-check
        // before indexing, or clicking Home dereferences m_Worlds[-1].
        if (m_ActiveWorld >= 0 && m_ActiveWorld < (int)m_Worlds.size())
            m_Worlds[m_ActiveWorld]->OnPanels(m_LastCtx);
    }

    // =========================================================================
    void FrontierApp::OnEvent(Cosmic::Event& e)
    {
        if (m_ActiveWorld < 0)
            return;

        // Forward scroll (speed/zoom) + resize to the active camera (handlers return
        // false, so the events still propagate).
        if (m_FlyCamera)
            m_Fly.OnEvent(e);
        else
            m_Orbit.OnEvent(e);
    }

    // =========================================================================
    void FrontierApp::SetWorld(int index)
    {
        if (index >= (int)m_Worlds.size())
            index = -1;

        m_ActiveWorld = index;
        m_WorldTime   = 0.0f;

        if (index >= 0)
        {
            if (!m_Attached[index])
            {
                m_Worlds[index]->OnAttach();   // lazy: heavy worlds build on first entry
                m_Attached[index] = true;
            }

            // Aim both cameras from the world's spawn pose. The fly camera gets the
            // pose directly; the orbit fallback frames the same spawn from a target.
            const WorldInfo& info = m_Worlds[index]->GetInfo();
            m_Fly.SetPose(info.SpawnPosition, info.SpawnYawDeg, info.SpawnPitchDeg);

            const glm::vec3 target{ 0.0f, info.SpawnPosition.y * 0.25f, 0.0f };
            m_Orbit.SetTarget(target);
            m_Orbit.SetYawPitch(info.SpawnYawDeg, info.SpawnPitchDeg);
            m_Orbit.SetDistance(std::max(glm::length(info.SpawnPosition - target), 10.0f));
        }
        // Dock layout re-applies automatically (DockStateKey changes).
    }

    int FrontierApp::DockStateKey() const
    {
        return m_ActiveWorld;   // home = -1, else the world index
    }

    // =========================================================================
    void FrontierApp::ApplyDockLayout()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws) { m_AppliedDock = DockStateKey(); return; }

        ws->ClearDockWindows();

        if (m_ActiveWorld < 0)
        {
            // Homescreen: one docked window in the central node (under the
            // engine chrome; lone tab auto-hidden — SF_Telem pattern).
            ws->SetViewportVisible(false);
            ws->DockWindow("Home", Cosmic::DockPort::Center);
            m_AppliedDock = DockStateKey();
            return;
        }

        // World view: 3D viewport center, nav left-top, world settings left-bottom.
        ws->SetViewportVisible(true);
        ws->SetEdgeRatios(0.18f, 0.20f, 0.16f, 0.20f);
        ws->DockWindow("Frontier",       Cosmic::DockPort::LeftTop);
        ws->DockWindow("World Settings", Cosmic::DockPort::LeftBottom);
        ws->DockWindow("GPU Profiler",   Cosmic::DockPort::RightBottom);   // F3

        m_AppliedDock = DockStateKey();
    }

    // =========================================================================
    // Shared nav panel: home + world switching + camera note + FPS.
    // =========================================================================
    void FrontierApp::DrawNavPanel()
    {
        ImGui::Begin("Frontier");

        ImGui::TextColored({ 1.0f, 0.75f, 0.35f, 1.0f }, "Frontier");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(ICON_LC_HOME "  Home", ImVec2(-1, 0)))
        {
            SetWorld(-1);
            ImGui::End();
            return;
        }
        ImGui::Spacing();

        for (int i = 0; i < (int)m_Worlds.size(); ++i)
        {
            const WorldInfo& info = m_Worlds[i]->GetInfo();
            const bool active = (m_ActiveWorld == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.15f, 1.0f));
            const std::string label = std::string(info.Icon) + "  " + info.Name;
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0)) && !active)
                SetWorld(i);
            if (active) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::SeparatorText("Camera");

        // Fly / Orbit toggle (fly is the default exploration camera).
        int mode = m_FlyCamera ? 0 : 1;
        ImGui::RadioButton("Fly", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Orbit", &mode, 1);
        m_FlyCamera = (mode == 0);

        if (m_FlyCamera)
        {
            ImGui::TextWrapped("Fly: RMB look, WASD move, E/Q up/down, Shift boost, "
                               "scroll speed (over the viewport).");
            ImGui::Text("Speed: %.1f m/s", m_Fly.GetMoveSpeed());
        }
        else
        {
            ImGui::TextWrapped("Orbit inspect: LMB orbit, RMB pan, scroll zoom "
                               "(over the viewport).");
        }

        ImGui::Spacing();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // =========================================================================
    // Homescreen — tile menu: 3 worlds on top, 2 centered below.
    // =========================================================================
    void FrontierApp::DrawHomescreen()
    {
        ImGui::Begin("Home", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 accent  = Cosmic::ThemeManager::Accent();
        const ImU32  accentU = ImGui::ColorConvertFloat4ToU32(accent);
        const ImU32  textU   = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32  subU    = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        ImFont* iconFont  = ImGui::GetFont();   // default font carries the Lucide glyphs
        ImFont* titleFont = Cosmic::UI::Fonts::Get("Roboto-Bold", 22.0f);
        ImFont* headFont  = Cosmic::UI::Fonts::Get("Roboto-Bold", 34.0f);

        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // ---- Heading ----
        Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 28.0f),
                         textU, "Frontier", headFont, 34.0f, Cosmic::UI::Align::TopCenter);
        Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 72.0f),
                         subU, "Cosmic Engine 3D showcase — choose a world", titleFont, 16.0f,
                         Cosmic::UI::Align::TopCenter);

        // ---- Tile grid: row of 3, then a centered row of 2 ----
        const int   count = (int)m_Worlds.size();
        const float gap   = 24.0f;
        const float gridW = std::min(avail.x * 0.88f, 1180.0f);
        const float tileW = (gridW - 2.0f * gap) / 3.0f;
        const float tileH = 170.0f;
        const float gridH = tileH * 2.0f + gap;
        const float offY  = origin.y + std::max(120.0f, (avail.y - gridH) * 0.5f);

        for (int i = 0; i < count; ++i)
        {
            const int  row     = (i < 3) ? 0 : 1;
            const int  coln    = (i < 3) ? i : (i - 3);
            const int  rowN    = (row == 0) ? std::min(count, 3) : (count - 3);
            const float rowW   = rowN * tileW + (rowN - 1) * gap;
            const float rowOffX = origin.x + (avail.x - rowW) * 0.5f;

            const ImVec2 p0(rowOffX + coln * (tileW + gap), offY + row * (tileH + gap));
            const ImVec2 p1(p0.x + tileW, p0.y + tileH);

            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
            const bool hov     = ImGui::IsItemHovered();
            ImGui::PopID();

            const ImU32 base   = ImGui::GetColorU32(hov ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            const ImU32 border = hov ? accentU : ImGui::GetColorU32(ImGuiCol_Border);
            dl->AddRectFilled(p0, p1, base, 12.0f);
            dl->AddRect(p0, p1, border, 12.0f, 0, hov ? 2.5f : 1.0f);

            const WorldInfo& info = m_Worlds[i]->GetInfo();
            const float cx = (p0.x + p1.x) * 0.5f;
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.28f), accentU, info.Icon,  iconFont,  44.0f, Cosmic::UI::Align::Center);
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.58f), textU,   info.Name,  titleFont, 22.0f, Cosmic::UI::Align::Center);
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.78f), subU,    info.Blurb, nullptr,   14.0f, Cosmic::UI::Align::Center);

            if (clicked) SetWorld(i);
        }

        ImGui::End();
    }

} // namespace Frontier

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
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
        return new Frontier::FrontierApp();
    }
}
