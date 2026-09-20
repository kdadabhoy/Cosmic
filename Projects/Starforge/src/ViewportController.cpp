// ViewportController.cpp — see header.

#include "ViewportController.h"
#include "commands/EditorCommands.h"
#include "Prefabs.h"                // K13 — viewport prefab drops

#include "layers/WorkspaceLayer.h"
// STAYS in both configurations (plan §8.5): ScenePhysics is dimension-agnostic
// and the W7 2D collider overlay reads the very same collider components.
#include "physics/ScenePhysics.h"   // J8 — live physics debug draw during Play
#include "scene/ui/UiSystem.h"      // U1 — canvas UI interaction + hit-test

#include "ui/IconsLucide.h"         // K6 — strip glyphs

#include <imgui.h>
#include <imgui_internal.h>         // K13 — BeginDragDropTargetCustom (viewport rect)

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

using namespace Cosmic;

namespace Starforge
{
    // WO-07 KI-1 regression probe (see ViewportController.h). Null in every normal
    // run; the editor self-test points it at its own buffer to learn the real
    // snap-chip rects.
    ViewportController::Ki1ChipProbe* ViewportController::s_Ki1Probe = nullptr;

    namespace
    {
        // ---- 2D overlay primitives (W7) --------------------------------------
        // Renderer2D::DrawRect takes a CENTER + size; DrawLine takes two points.
        // Everything here draws on the sprite plane at the given z.
        void Rect2D(const glm::vec2& center, const glm::vec2& size, float z,
                    const glm::vec4& col)
        {
            Renderer2D::DrawRect({ center.x, center.y, z }, size, col);
        }

        // A closed polyline through `seg` samples of an ellipse — the 2D stand-in
        // for the 3D wire sphere (Renderer2D has no wire-circle primitive; its
        // DrawCircle is a filled disc).
        void WireEllipse2D(const glm::vec2& c, float rx, float ry, float z,
                           const glm::vec4& col, int seg = 32)
        {
            glm::vec3 prev{ c.x + rx, c.y, z };
            for (int i = 1; i <= seg; ++i)
            {
                const float a = (float)i / (float)seg * glm::two_pi<float>();
                const glm::vec3 cur{ c.x + std::cos(a) * rx, c.y + std::sin(a) * ry, z };
                Renderer2D::DrawLine(prev, cur, col);
                prev = cur;
            }
        }
    }

    void ViewportController::Init()
    {
    }

    void ViewportController::OnUpdate(EditorContext& ctx, EditorCameraRig& rig, float ts,
                                      bool playing, Camera2DController* cam2d,
                                      const Camera* renderCamOverride, const glm::vec4& uiBandUv)
    {
        (void)ts;
        m_Cam2D = cam2d;   // U3 — 2D mode routing for F-frame + toolbar Frame
        auto& app = Application::Get();
        auto* ws  = app.GetWorkspaceLayer();
        const bool vpHover = ws && ws->IsViewportHovered();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();

        // The camera every pick/probe below sees: the viewport's ACTUAL render
        // camera — the Play game camera when overridden (U7), else the 2D rig
        // in 2D mode, else the rig's active camera (orbit / fly / possess, K7).
        const Camera& renderCam = renderCamOverride ? *renderCamOverride
            : (cam2d ? static_cast<const Camera&>(cam2d->GetCamera())
                     : rig.ActiveCamera());

        ImGuiIO& io = ImGui::GetIO();
        // Gizmo hotkeys yield while a fly-look drag is on (WASD/QE are MOVEMENT
        // there; W must not flip the gizmo to Translate mid-flight).
        const bool canKey = vpHover && !io.WantTextInput && !io.WantCaptureKeyboard &&
                            !rig.IsFlying();

        if (canKey)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_Op = Gizmo::Operation::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_Op = Gizmo::Operation::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_Op = Gizmo::Operation::Scale;
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) m_Op = Gizmo::Operation::Universal;   // K11
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameSelection(ctx, rig);
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_ShowGrid = !m_ShowGrid;

            // Camera bookmarks: Ctrl+1..9 save, 1..9 recall (seamless in every
            // rig mode — RecallPose re-seeds the fly pose when flying).
            for (int i = 0; i < 9; ++i)
            {
                if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) continue;
                auto& orbit = rig.Orbit();
                if (io.KeyCtrl)
                    m_Bookmarks[i] = { true, orbit.GetYaw(), orbit.GetPitch(),
                                       orbit.GetDistance(), orbit.GetTarget() };
                else if (m_Bookmarks[i].Set)
                    rig.RecallPose(m_Bookmarks[i].Target, m_Bookmarks[i].Yaw,
                                   m_Bookmarks[i].Pitch, m_Bookmarks[i].Dist);
            }
        }

        // Voxel brush (V4): when editing a selected voxel volume, LMB places the
        // active block / RMB breaks — one undoable edit per click (raycast the grid).
        // In the editor's default CAD nav the camera uses MMB, so LMB/RMB are free.
        bool voxelBrushConsumed = false;

        // Tile painter (U4): Tile Palette "Paint" on + 2D mode + a Tilemap on the
        // primary selection. LMB applies the tool (Paint drag = one undo stroke,
        // Flood on click, Rect on press→release), RMB erases cells. Selection
        // clicks are suppressed while painting (like the voxel brush).
        bool tileBrushConsumed = false;
        if (ctx.TileBrush.Editing && cam2d && ctx.Scene &&
            vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            Entity prim = ctx.PrimaryEntity();
            if (prim && prim.HasComponent<TilemapComponent>() &&
                prim.HasComponent<TransformComponent>())
            {
                auto& tm = prim.GetComponent<TilemapComponent>();
                tm.EnsureCells();
                const auto& tt = prim.GetComponent<TransformComponent>();

                if (vpHover)
                {
                    const glm::vec2 world = Camera2DController::ScreenToWorld(
                        Input::GetMouseScreenPosition(), vpPos, vpSize,
                        cam2d->GetFocus(), cam2d->GetZoom());
                    m_TileLastCell = { (int)std::floor(world.x - tt.Position.x),
                                       (int)std::floor(world.y - tt.Position.y) };
                    tileBrushConsumed = true;   // suppress select/pick while painting
                }

                const bool blmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
                const bool brmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
                const bool lPress   = blmb && !m_TileLmbWas && vpHover;
                const bool lRelease = !blmb && m_TileLmbWas;
                const bool rPress   = brmb && !m_TileRmbWas && vpHover;
                m_TileLmbWas = blmb; m_TileRmbWas = brmb;

                using Tool = EditorContext::TileBrushState::ToolKind;
                const int cx = m_TileLastCell.x, cy = m_TileLastCell.y;

                switch (ctx.TileBrush.Tool)
                {
                case Tool::Paint:
                    if (lPress) ctx.TileBrush.Stroke++;
                    if (blmb && vpHover)
                        Commands::TileEdit(ctx, prim, cx, cy, ctx.TileBrush.Tile,
                                           ctx.TileBrush.Stroke);
                    break;

                case Tool::Flood:
                    if (lPress && tm.InBounds(cx, cy))
                    {
                        // Fill on a scratch copy; commit the changed set as ONE step.
                        std::vector<uint16_t> scratch = tm.Cells;
                        const auto changed = TilemapComponent::FloodFill(
                            scratch, tm.GridW, tm.GridH, cx, cy, ctx.TileBrush.Tile);
                        std::vector<std::pair<uint32_t, uint16_t>> writes;
                        writes.reserve(changed.size());
                        for (uint32_t i : changed) writes.push_back({ i, ctx.TileBrush.Tile });
                        Commands::TileEditRun(ctx, prim, writes, "Flood Fill Tiles");
                    }
                    break;

                case Tool::Rect:
                    if (lPress)
                    {
                        ctx.TileBrush.RectDragging = true;
                        ctx.TileBrush.RectAnchor   = { cx, cy };
                    }
                    if (lRelease && ctx.TileBrush.RectDragging)
                    {
                        ctx.TileBrush.RectDragging = false;
                        const int x0 = std::max(0, std::min(ctx.TileBrush.RectAnchor.x, m_TileLastCell.x));
                        const int x1 = std::min(tm.GridW - 1, std::max(ctx.TileBrush.RectAnchor.x, m_TileLastCell.x));
                        const int y0 = std::max(0, std::min(ctx.TileBrush.RectAnchor.y, m_TileLastCell.y));
                        const int y1 = std::min(tm.GridH - 1, std::max(ctx.TileBrush.RectAnchor.y, m_TileLastCell.y));
                        std::vector<std::pair<uint32_t, uint16_t>> writes;
                        for (int y = y0; y <= y1; ++y)
                            for (int x = x0; x <= x1; ++x)
                                writes.push_back({ (uint32_t)(y * tm.GridW + x), ctx.TileBrush.Tile });
                        Commands::TileEditRun(ctx, prim, writes, "Rect Fill Tiles");
                    }
                    break;
                }

                // RMB: single-cell erase drag, any tool.
                if (rPress) ctx.TileBrush.Stroke++;
                if (brmb && vpHover)
                    Commands::TileEdit(ctx, prim, cx, cy, 0, ctx.TileBrush.Stroke);
            }
        }
        if (!ctx.TileBrush.Editing || !cam2d)
        {
            m_TileLmbWas = false; m_TileRmbWas = false;
            ctx.TileBrush.RectDragging = false;
        }

        // Click edge — shared by the UI block below and the click-pick after it.
        const bool lmb     = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
        const bool clicked = lmb && !m_LmbWasDown;
        m_LmbWasDown = lmb;

        // U1 — in-game UI in the viewport. While PLAYING the canvas is live:
        // hover/press tints step and buttons emit on the scene EventBus (same
        // UiSystem::Update the PlayerLayer runs), and a pointer over interactable
        // UI consumes the click so it never leaks to 3D picking. While EDITING,
        // a click on any UI element selects it instead — the ScenePicker's ID
        // pass renders meshes only and cannot see rect-based UI.
        bool uiConsumed = false;
        if (ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            // The canvas layout rect: the letterbox band (U7) in viewport-local
            // pixels — the full viewport whenever no band is active.
            const UiRect vpRect{
                { uiBandUv.x * vpSize.x,                  uiBandUv.y * vpSize.y },
                { (uiBandUv.x + uiBandUv.z) * vpSize.x,   (uiBandUv.y + uiBandUv.w) * vpSize.y } };
            const glm::vec2 mouse = Input::GetMouseScreenPosition();
            const glm::vec2 local = { mouse.x - vpPos.x, mouse.y - vpPos.y };

            if (playing)
            {
                // Park the pointer far away when the cursor is outside the
                // viewport so hover clears and an armed press cancels on release.
                UiPointer p;
                p.Position     = vpHover ? local : glm::vec2(-1.0e6f, -1.0e6f);
                p.Down         = lmb;
                p.PressedEdge  = lmb && !m_UiMouseWas;
                p.ReleasedEdge = !lmb && m_UiMouseWas;
                m_UiMouseWas   = lmb;

                const bool overUi = UiSystem::Update(*ctx.Scene, vpRect, p);
                uiConsumed = overUi && vpHover;
            }
            else
            {
                m_UiMouseWas = false;
                if (clicked && !voxelBrushConsumed && !tileBrushConsumed && vpHover &&
                    !m_GizmoActive && !m_GizmoOver && !m_ExternalGizmoBusy && vpRect.Contains(local))
                {
                    uint32_t hit = 0;
                    if (UiSystem::HitTest(*ctx.Scene, vpRect, local, hit))
                    {
                        Entity e(static_cast<entt::entity>(hit), ctx.Scene.get());
                        if (io.KeyCtrl) ctx.ToggleSelect(e);
                        else            ctx.SelectOnly(e);
                        uiConsumed = true;
                    }
                }
            }
        }

        // 2D sprite pick (U3): sprites have no mesh, so the ID pass can't see
        // them — rect-test the topmost sprite (desc ZOrder, then the Y/Z sort
        // key) under the cursor's world XY point first. Falls through to the ID
        // pass on a miss so meshes in a 2.5D scene stay pickable.
        bool spriteConsumed = false;
        if (clicked && !voxelBrushConsumed && !tileBrushConsumed && !uiConsumed && vpHover && cam2d &&
            !m_GizmoActive && !m_GizmoOver && !m_ExternalGizmoBusy && ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            const glm::vec2 world = Camera2DController::ScreenToWorld(
                Input::GetMouseScreenPosition(), vpPos, vpSize,
                cam2d->GetFocus(), cam2d->GetZoom());

            struct Hit { entt::entity E; int32_t Z; float Key; };
            std::vector<Hit> hits;
            auto view = ctx.Scene->GetRegistry().view<TransformComponent, SpriteRendererComponent>();
            for (auto e : view)
            {
                const auto& t = view.get<TransformComponent>(e);
                const auto& s = view.get<SpriteRendererComponent>(e);
                const glm::vec2 half = SpriteRendererComponent::WorldSize(
                    s, { t.Scale.x, t.Scale.y },
                    s.Resolved ? (int)s.Resolved->GetWidth() : 0,
                    s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f;
                if (std::abs(world.x - t.Position.x) <= std::abs(half.x) &&
                    std::abs(world.y - t.Position.y) <= std::abs(half.y))
                    hits.push_back({ e, s.ZOrder, s.YSort ? -t.Position.y : t.Position.z });
            }
            if (!hits.empty())
            {
                auto top = std::max_element(hits.begin(), hits.end(),
                    [](const Hit& a, const Hit& b)
                    {
                        if (a.Z   != b.Z)   return a.Z   < b.Z;
                        if (a.Key != b.Key) return a.Key < b.Key;
                        return a.E < b.E;
                    });
                Entity e(top->E, ctx.Scene.get());
                if (io.KeyCtrl) ctx.ToggleSelect(e);
                else            ctx.SelectOnly(e);
                spriteConsumed = true;
            }
        }

        // Click-pick — only on the click frame (an ID pre-pass is not free).
        // 3D only: the picker renders a mesh ID pass. In the 2D build the sprite
        // rect-pick above IS the pick path, and a miss clears the selection.
        // 2D: a click on empty space (no sprite, no UI, no tile stroke) clears
        // the selection — the same "click-away deselects" contract the ID pass
        // provides in the 3D build.
        if (clicked && !tileBrushConsumed && !uiConsumed && !spriteConsumed &&
            vpHover && !m_GizmoActive && !m_GizmoOver && !m_ExternalGizmoBusy && !io.KeyCtrl && ctx.Scene)
        {
            ctx.ClearSelection();
        }
        (void)voxelBrushConsumed;
    }

    // =========================================================================
    // The 2D authoring overlay, on Renderer2D (W7).
    //
    // Same picture as the 3D twin above — pixel grid, XY axes, sprite selection
    // rects, tile-painter visuals — plus the §6.4 collider overlay. The only
    // structural difference is the render pass: the 3D build inherits the
    // caller's Renderer3D scene, but a 2D frame's DrawTransparent hook has no
    // open batch (SceneRenderer's Renderer3D::BeginScene/EndScene pair fenced
    // out in W6), so this opens its own PushRenderPass. That is the same verb
    // Scene::OnRenderSprites uses two calls later in the same hook, with the
    // same view-projection and viewport bounds — it nests cleanly, and Pop
    // flushes the lines before the sprites batch.
    // =========================================================================
    void ViewportController::DrawOverlayContent2D(EditorContext& ctx, const Camera2DController& cam)
    {
        glm::vec2 mn, mx;
        cam.VisibleRect(mn, mx);

        const glm::vec2 vpSize = Application::Get().GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        // Transparent-queue contract, matching OnRenderSprites: depth test ON
        // (so overlay lines sit correctly against sprite depth), writes OFF,
        // straight alpha. The engine default is depth-write ON, so restore it.
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);

        Renderer2D::PushRenderPass(cam.GetCamera().GetViewProjectionMatrix(),
                                   { 0.0f, 0.0f, vpSize.x, vpSize.y });

        if (m_ShowGrid)
        {
            const float pxPerUnit = (cam.GetZoom() > 0.0f)
                ? (vpSize.y / (2.0f * cam.GetZoom()))
                : 0.0f;

            const glm::vec4 minor(0.30f, 0.32f, 0.36f, 0.35f);
            const glm::vec4 major(0.45f, 0.47f, 0.52f, 0.7f);

            // 1-unit minors only when they resolve (>= ~6 px apart); 10-unit majors.
            auto drawLines = [&](float step, const glm::vec4& col)
            {
                const float x0 = std::floor(mn.x / step) * step;
                const float y0 = std::floor(mn.y / step) * step;
                for (float x = x0; x <= mx.x; x += step)
                    Renderer2D::DrawLine({ x, mn.y, 0.0f }, { x, mx.y, 0.0f }, col);
                for (float y = y0; y <= mx.y; y += step)
                    Renderer2D::DrawLine({ mn.x, y, 0.0f }, { mx.x, y, 0.0f }, col);
            };
            if (pxPerUnit >= 6.0f)          drawLines(1.0f,  minor);
            if (pxPerUnit * 10.0f >= 6.0f)  drawLines(10.0f, major);

            // XY axes through the origin (X red, Y green — matches the 3D axes).
            Renderer2D::DrawLine({ mn.x, 0.0f, 0.0f }, { mx.x, 0.0f, 0.0f }, { 0.86f, 0.24f, 0.24f, 0.9f });
            Renderer2D::DrawLine({ 0.0f, mn.y, 0.0f }, { 0.0f, mx.y, 0.0f }, { 0.35f, 0.80f, 0.30f, 0.9f });
        }

        // Selection outlines: a wire rect around each selected sprite. This is
        // the ONLY selection affordance in the 2D build — the K12 outline pass
        // rides on ScenePicker and fenced out with it.
        if (ctx.Scene)
        {
            const glm::vec4 sel(1.0f, 0.62f, 0.11f, 1.0f);
            for (entt::entity h : ctx.Selection)
            {
                Entity e(h, ctx.Scene.get());
                if (!e || !e.HasComponent<TransformComponent>() ||
                    !e.HasComponent<SpriteRendererComponent>())
                    continue;
                const auto& t = e.GetComponent<TransformComponent>();
                const auto& s = e.GetComponent<SpriteRendererComponent>();
                const glm::vec2 half = SpriteRendererComponent::WorldSize(
                    s, { t.Scale.x, t.Scale.y },
                    s.Resolved ? (int)s.Resolved->GetWidth() : 0,
                    s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f * 1.03f;
                Rect2D({ t.Position.x, t.Position.y },
                       { std::abs(half.x) * 2.0f, std::abs(half.y) * 2.0f },
                       t.Position.z, sel);
            }
        }

        // Tile painter visuals (U4): map bounds of the selected tilemap, the
        // hovered cell, and the pending rect-fill preview.
        if (ctx.Scene)
        {
            Entity prim = ctx.PrimaryEntity();
            if (prim && prim.HasComponent<TilemapComponent>() &&
                prim.HasComponent<TransformComponent>())
            {
                auto& tm = prim.GetComponent<TilemapComponent>();
                tm.EnsureCells();
                const auto& t = prim.GetComponent<TransformComponent>();
                const glm::vec3 o = t.Position;

                // Same (x0,y0)-(x1,y1) corner convention as the 3D twin, mapped
                // onto DrawRect's centre+size form.
                auto rect = [&](float x0, float y0, float x1, float y1, const glm::vec4& col)
                {
                    Rect2D({ o.x + 0.5f * (x0 + x1), o.y + 0.5f * (y0 + y1) },
                           { x1 - x0, y1 - y0 }, o.z, col);
                };

                rect(0.0f, 0.0f, (float)tm.GridW, (float)tm.GridH,
                     { 0.35f, 0.65f, 0.95f, 0.8f });   // map bounds

                if (ctx.TileBrush.Editing)
                {
                    const glm::vec4 hot{ 1.0f, 0.85f, 0.25f, 0.9f };
                    if (tm.InBounds(m_TileLastCell.x, m_TileLastCell.y))
                        rect((float)m_TileLastCell.x,        (float)m_TileLastCell.y,
                             (float)m_TileLastCell.x + 1.0f, (float)m_TileLastCell.y + 1.0f, hot);

                    if (ctx.TileBrush.RectDragging)
                    {
                        const glm::ivec2 a = ctx.TileBrush.RectAnchor, b = m_TileLastCell;
                        rect((float)std::min(a.x, b.x),        (float)std::min(a.y, b.y),
                             (float)std::max(a.x, b.x) + 1.0f, (float)std::max(a.y, b.y) + 1.0f, hot);
                    }
                }
            }
        }

        // X5 — 2D light glyphs: the centre cross + radius ring the 3D overlay
        // draws for Light2DComponent, re-expressed on the 2D batch.
        if (ctx.Scene)
        {
            auto& reg = ctx.Scene->GetRegistry();
            auto selected = [&](entt::entity h)
            {
                for (entt::entity s : ctx.Selection) if (s == h) return true;
                return false;
            };
            for (auto e : reg.view<TransformComponent, Light2DComponent>())
            {
                const auto& t  = reg.get<TransformComponent>(e);
                const auto& lc = reg.get<Light2DComponent>(e);
                const bool sel = selected(e);
                const glm::vec4 col(lc.Color, sel ? 1.0f : 0.6f);
                Renderer2D::DrawLine(t.Position - glm::vec3(0.2f, 0.0f, 0.0f),
                                     t.Position + glm::vec3(0.2f, 0.0f, 0.0f), col);
                Renderer2D::DrawLine(t.Position - glm::vec3(0.0f, 0.2f, 0.0f),
                                     t.Position + glm::vec3(0.0f, 0.2f, 0.0f), col);
                WireEllipse2D({ t.Position.x, t.Position.y }, lc.Radius, lc.Radius,
                              t.Position.z, glm::vec4(lc.Color, sel ? 0.7f : 0.35f));
            }
        }

        Renderer2D::PopRenderPass();   // flushes the overlay batch
        RenderCommand::SetDepthWrite(true);
    }

    // §6.4 — colliders projected onto XY. Each shape is drawn in the SAME world
    // transform ScenePhysics bakes it with, then flattened: a box becomes its
    // XY footprint, a sphere its great circle, a capsule the classic stadium
    // (two side lines + two end arcs) of the Y-axis capsule the runtime builds.
    // Same toggle (m_ShowColliders) and same palette as the 3D gizmos.
    //
    // Its own pass, called AFTER the sprites (see the header): a collider
    // normally sits exactly on the sprite it belongs to, so drawing it with the
    // rest of the overlay — which must stay UNDER the art, or the grid would
    // paint over it — left the wireframe buried and invisible.
    void ViewportController::DrawColliderOverlay2D(EditorContext& ctx,
                                                   const Camera2DController& cam)
    {
        if (!m_ShowColliders || !ctx.Scene)
            return;

        const glm::vec2 vpSize = Application::Get().GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(false);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);

        Renderer2D::PushRenderPass(cam.GetCamera().GetViewProjectionMatrix(),
                                   { 0.0f, 0.0f, vpSize.x, vpSize.y });

        auto& reg = ctx.Scene->GetRegistry();
        auto selected = [&](entt::entity h)
        {
            for (entt::entity s : ctx.Selection) if (s == h) return true;
            return false;
        };
        const glm::vec4 dim { 0.20f, 0.85f, 0.45f, 0.55f };   // resting green
        const glm::vec4 hot { 0.35f, 1.00f, 0.55f, 1.00f };   // selected

        for (auto e : reg.view<TransformComponent>())
        {
            const bool anyCol = reg.any_of<BoxColliderComponent, SphereColliderComponent,
                                           CapsuleColliderComponent>(e);
            if (!anyCol) continue;
            const glm::vec4 col = selected(e) ? hot : dim;
            const glm::mat4 world = ctx.Scene->GetWorldTransform(Entity(e, ctx.Scene.get()));

            // World scale per axis (the 2D rig has no rotation about X/Y, so the
            // axis lengths are the honest footprint scale).
            const float sx = glm::length(glm::vec3(world[0]));
            const float sy = glm::length(glm::vec3(world[1]));

            if (const auto* c = reg.try_get<BoxColliderComponent>(e))
            {
                const glm::vec3 ctr = glm::vec3(world * glm::vec4(c->Offset, 1.0f));
                Rect2D({ ctr.x, ctr.y },
                       { c->HalfExtents.x * 2.0f * sx, c->HalfExtents.y * 2.0f * sy },
                       ctr.z, col);
            }
            if (const auto* c = reg.try_get<SphereColliderComponent>(e))
            {
                const glm::vec3 ctr = glm::vec3(world * glm::vec4(c->Offset, 1.0f));
                WireEllipse2D({ ctr.x, ctr.y }, c->Radius * sx, c->Radius * sy, ctr.z, col, 32);
            }
            if (const auto* c = reg.try_get<CapsuleColliderComponent>(e))
            {
                const glm::vec3 ctr = glm::vec3(world * glm::vec4(c->Offset, 1.0f));
                const float r  = c->Radius * sx;
                const float hh = c->HalfHeight * sy;
                const float z  = ctr.z;

                // Straight sides.
                Renderer2D::DrawLine({ ctr.x - r, ctr.y - hh, z }, { ctr.x - r, ctr.y + hh, z }, col);
                Renderer2D::DrawLine({ ctr.x + r, ctr.y - hh, z }, { ctr.x + r, ctr.y + hh, z }, col);

                // Hemispherical caps: half-circles about the two end centres.
                const int seg = 16;
                glm::vec3 prevTop{ ctr.x + r, ctr.y + hh, z };
                glm::vec3 prevBot{ ctr.x - r, ctr.y - hh, z };
                for (int i = 1; i <= seg; ++i)
                {
                    const float a = (float)i / (float)seg * glm::pi<float>();
                    const glm::vec3 top{ ctr.x + std::cos(a) * r, ctr.y + hh + std::sin(a) * r, z };
                    const glm::vec3 bot{ ctr.x - std::cos(a) * r, ctr.y - hh - std::sin(a) * r, z };
                    Renderer2D::DrawLine(prevTop, top, col);
                    Renderer2D::DrawLine(prevBot, bot, col);
                    prevTop = top; prevBot = bot;
                }
            }
        }

        Renderer2D::PopRenderPass();   // flushes the collider batch
        RenderCommand::SetDepthWrite(true);
    }

    void ViewportController::DrawGizmo(EditorContext& ctx, const Camera& cam)
    {
        auto& app = Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f) { m_GizmoActive = m_GizmoOver = false; return; }

        // Strip etiquette (K6): the overlay widgets draw first in this window —
        // while one is hovered/active (a snap chip drag, a dropdown), the gizmo
        // must not also grab the mouse. Skipping Manipulate mid-drag is safe:
        // ImGuizmo's own drag holds no ImGui ActiveId, so this never interrupts
        // a gizmo drag in progress.
        if ((ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive()) && !Gizmo::IsUsing())
        {
            m_GizmoActive = false;
            m_GizmoOver   = false;
            m_GizmoWasUsing = false;
            return;
        }

        Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);

        Entity sel = ctx.PrimaryEntity();
        if (sel && sel.HasComponent<TransformComponent>())
        {
            auto& t = sel.GetComponent<TransformComponent>();
            const TransformComponent beforeThisFrame = t;   // pre-manipulate pose

            // K6 — per-operation snap; Universal rides the MOVE snap (ImGuizmo
            // accepts a single snap value per call — documented limitation).
            float snap = 0.0f;
            switch (m_Op)
            {
                case Gizmo::Operation::Rotate:    snap = m_SnapRotateOn ? m_SnapRotate : 0.0f; break;
                case Gizmo::Operation::Scale:     snap = m_SnapScaleOn  ? m_SnapScale  : 0.0f; break;
                case Gizmo::Operation::Universal:
                case Gizmo::Operation::Translate: snap = m_SnapMoveOn   ? m_SnapMove   : 0.0f; break;
            }
            Gizmo::Manipulate(cam, t, m_Op, m_Space, snap);

            const bool usingNow = Gizmo::IsUsing();
            if (usingNow && !m_GizmoWasUsing)
                m_DragBefore = beforeThisFrame;             // drag just started
            if (!usingNow && m_GizmoWasUsing)
            {
                Commands::CommitTransform(ctx, sel, m_DragBefore);
                ctx.Commands.SetMergeBarrier();
            }
            m_GizmoWasUsing = usingNow;
        }
        else
        {
            m_GizmoWasUsing = false;
        }

        m_GizmoActive = Gizmo::IsUsing();
        m_GizmoOver   = Gizmo::IsOver();
    }

    void ViewportController::LoadSnapPrefs(const Prefs::EditorSettings& s)
    {
        m_SnapMoveOn   = s.SnapMoveOn;
        m_SnapRotateOn = s.SnapRotateOn;
        m_SnapScaleOn  = s.SnapScaleOn;
        m_SnapMove     = s.SnapMove;
        m_SnapRotate   = s.SnapRotate;
        m_SnapScale    = s.SnapScale;
    }

    void ViewportController::SaveSnapPrefs(Prefs::EditorSettings& s) const
    {
        s.SnapMoveOn   = m_SnapMoveOn;
        s.SnapRotateOn = m_SnapRotateOn;
        s.SnapScaleOn  = m_SnapScaleOn;
        s.SnapMove     = m_SnapMove;
        s.SnapRotate   = m_SnapRotate;
        s.SnapScale    = m_SnapScale;
    }

    void ViewportController::DrawViewportOverlays(EditorContext& ctx, EditorCameraRig& rig,
                                                  bool playing, bool mode2D)
    {
        auto& app = Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 40.0f || vpSize.y < 40.0f)
            return;

        const ImGuiStyle& style = ImGui::GetStyle();
        const float sq = ImGui::GetFrameHeight();

        // ---- K6: the header strip (hidden while playing, like the old bar) ----
        if (!playing)
        {
            ImGui::SetCursorScreenPos(ImVec2(vpPos.x + 8.0f, vpPos.y + 8.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.09f, 0.72f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
            ImGui::BeginChild("##k6strip", ImVec2(0, sq + 12.0f),
                              ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar);

            auto opButton = [&](const char* icon, Gizmo::Operation op, const char* tip)
            {
                const bool active = m_Op == op;
                if (active)
                {
                    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
                }
                if (ImGui::Button(icon, ImVec2(sq, sq)))
                    m_Op = op;
                if (active)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0.0f, 3.0f);
            };
            opButton(ICON_LC_MOVE_3D,   Gizmo::Operation::Translate, "Move (W)");
            opButton(ICON_LC_ROTATE_3D, Gizmo::Operation::Rotate,    "Rotate (E)");
            opButton(ICON_LC_SCALE_3D,  Gizmo::Operation::Scale,     "Scale (R)");
            opButton(ICON_LC_MAXIMIZE,  Gizmo::Operation::Universal,
                     "Universal (Q): move + rotate + scale in one gizmo.\n"
                     "Snapping uses the MOVE increment.");

            // WO-07 test probe (gated): record the last item's centre + the colour-stack
            // delta across it, in strip order. Null in every normal editor run.
            auto probeChip = [&](int colorBefore)
            {
                if (!s_Ki1Probe || s_Ki1Probe->count >= Ki1ChipProbe::kMax) return;
                const int i = s_Ki1Probe->count;
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                s_Ki1Probe->cx[i] = (mn.x + mx.x) * 0.5f;
                s_Ki1Probe->cy[i] = (mn.y + mx.y) * 0.5f;
                // Net colour push/pop of THIS chip, measured at the widget — the KI-1
                // signal, immune to ImGui's end-of-window stack recovery.
                s_Ki1Probe->colorDelta[i] = ImGui::GetCurrentContext()->ColorStack.Size - colorBefore;
                ++s_Ki1Probe->count;
            };
            auto colorNow = [&]() { return s_Ki1Probe ? ImGui::GetCurrentContext()->ColorStack.Size : 0; };

            // World/Local.
            int wlColorBefore = 0;   // recorded in fixed slot 5 so the KI-1 snap chips keep 0-2
            {
                const bool world = m_Space == Gizmo::Space::World;
                wlColorBefore = colorNow();
                if (ImGui::Button(world ? ICON_LC_GLOBE : ICON_LC_BOX, ImVec2(sq, sq)))
                    m_Space = world ? Gizmo::Space::Local : Gizmo::Space::World;
                if (s_Ki1Probe)   // fixed slot 5 (count stays for the snap chips 0-2 / toggles 3-4)
                {
                    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                    s_Ki1Probe->cx[5] = (mn.x + mx.x) * 0.5f;
                    s_Ki1Probe->cy[5] = (mn.y + mx.y) * 0.5f;
                    s_Ki1Probe->colorDelta[5] = ImGui::GetCurrentContext()->ColorStack.Size - wlColorBefore;
                    s_Ki1Probe->haveWorldLocal = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(world ? "Gizmo space: World (click for Local)"
                                            : "Gizmo space: Local (click for World)");
                ImGui::SameLine(0.0f, 8.0f);
            }

            // Three per-operation snap chips: toggle + editable value (2208's
            // "10 | 15° | 0.25" row).
            auto snapChip = [&](const char* icon, bool& on, float& value,
                                const char* fmt, float speed, float mn, float mx,
                                const char* tip)
            {
                ImGui::PushID(icon);
                const int ki1ColorBefore =            // WO-07 probe (gated): stack size
                    s_Ki1Probe ? ImGui::GetCurrentContext()->ColorStack.Size : 0;
                // KI-1 FIX (WO-07): latch the pushed state BEFORE the button flips
                // `on`. The old code guarded the pop on the post-click value, so every
                // click left ImGui's colour stack off by one — a Debug abort
                // ("PopStyleColor() too many times") and silent Release corruption.
                // This is the same latched-push pattern the `toggle` lambda below
                // already uses. The chip ships in the 2D editor, so this shipped
                // to users.
                const bool pushed = on;
                if (pushed)
                {
                    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
                }
                if (ImGui::Button(icon, ImVec2(sq, sq)))
                    on = !on;
                if (pushed)
                    ImGui::PopStyleColor();
                probeChip(ki1ColorBefore);   // WO-07 test probe (gated): snap chips are 0-2
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::SetNextItemWidth(52.0f);
                ImGui::BeginDisabled(!on);
                ImGui::DragFloat("##v", &value, speed, mn, mx, fmt);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", tip);
                ImGui::PopID();
                ImGui::SameLine(0.0f, 6.0f);
            };
            snapChip(ICON_LC_MOVE,      m_SnapMoveOn,   m_SnapMove,   "%.2f",  0.05f, 0.01f, 100.0f,
                     "Move snap (m) — also the Universal gizmo's snap");
            snapChip(ICON_LC_ROTATE_CW, m_SnapRotateOn, m_SnapRotate, "%.0f°", 1.0f,  1.0f,  90.0f,
                     "Rotate snap (degrees)");
            snapChip(ICON_LC_SCALING,   m_SnapScaleOn,  m_SnapScale,  "%.2f",  0.01f, 0.01f, 10.0f,
                     "Scale snap (increment)");

            // View toggles.
            auto toggle = [&](const char* icon, bool& on, const char* tip)
            {
                // PRE-EXISTING BUG, fixed here (found by the W7 on-GPU pass):
                // the pop used to be guarded on `on` AFTER the button had
                // already flipped it, so EVERY click on a chip left ImGui's
                // style-colour stack unbalanced by one — an assert + abort() in
                // Debug, silent corruption in Release. Latch the pushed state
                // instead of re-reading the flag.
                const int toggleColorBefore = colorNow();
                const bool pushed = on;
                if (pushed)
                {
                    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
                }
                if (ImGui::Button(icon, ImVec2(sq, sq)))
                    on = !on;
                if (pushed)
                    ImGui::PopStyleColor();
                probeChip(toggleColorBefore);   // WO-07 test probe (gated): toggles are 3-4
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0.0f, 3.0f);
            };
            toggle(ICON_LC_GRID_3X3, m_ShowGrid,         "Grid (G)");
            // W7 — the collider chip drives the Renderer2D collider overlay
            // (§6.4). The physics-debug and nav chips have nothing behind them
            // in a 2D build (Jolt's debug renderer needs Renderer3D; there is
            // no navmesh), so they are absent rather than dead.
            toggle(ICON_LC_BOXES,    m_ShowColliders,
                   "Collider overlay (J8/W7): Box/Sphere/Capsule projected onto XY");
            ImGui::SameLine(0.0f, 8.0f);

            // AP-03 — the UI rect gizmo's snap chips (2D mode). Same latched-push
            // toggle as above (the KI-1-fixed pattern); NOT probed, so the WO-07
            // harnesses' fixed chip slots (0-2 snap, 3-4 toggles, 5 World/Local)
            // are unchanged.
            if (mode2D && m_RectSnap)
            {
                auto rectChip = [&](const char* icon, bool& on, const char* tip)
                {
                    const bool pushed = on;
                    if (pushed)
                    {
                        const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
                    }
                    if (ImGui::Button(icon, ImVec2(sq * 1.9f, sq)))
                        on = !on;
                    if (pushed)
                        ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", tip);
                    ImGui::SameLine(0.0f, 3.0f);
                };
                ImGui::PushID("ap03rect");
                rectChip(ICON_LC_MOVE " 1/8", m_RectSnap->PixelEighth,
                         "UI rect gizmo: snap edges to 1/8 px (AP-03)");
                rectChip(ICON_LC_GRID_3X3 " 16", m_RectSnap->Grid16,
                         "UI rect gizmo: snap edges to a 16 px grid (wins over 1/8 px)");
                ImGui::PopID();
                ImGui::SameLine(0.0f, 8.0f);
            }

            // R8 — view-mode dropdown (Lit · Unlit · Wireframe · Entity ID).
            // The 2D build keeps Lit and Wireframe only: Unlit neutralizes 3D
            // lights and Entity ID renders the mesh ID pass, neither of which
            // exists there.
            {
                static const char* kModes[] = { "Lit", "Wireframe" };
                const int kModeCount = 2;
                int vm = (int)m_ViewMode;
                vm = (m_ViewMode == ViewMode::Wireframe) ? 1 : 0;
                ImGui::SetNextItemWidth(96.0f);
                if (ImGui::Combo("##k6viewmode", &vm, kModes, kModeCount))
                    m_ViewMode = (vm == 1) ? ViewMode::Wireframe : ViewMode::Lit;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("View mode (R8): Unlit = flat albedo, Wireframe = line\n"
                                      "rasterization, Entity ID = flat per-entity hash colors.");
                ImGui::SameLine(0.0f, 8.0f);
            }

            // K7 — camera dropdown: Free (Orbit) / Free (Fly) / cameras by Tag.
            if (!mode2D)
            {
                std::string label = "Free (Orbit)";
                if (rig.GetMode() == EditorCameraRig::Mode::Fly)     label = "Free (Fly)";
                if (rig.GetMode() == EditorCameraRig::Mode::Possess) label = "Possessed";
                if (rig.GetMode() == EditorCameraRig::Mode::Possess && ctx.Scene)
                    if (Entity e = ctx.Scene->FindByUUID(rig.PossessedEntity());
                        e && e.HasComponent<TagComponent>())
                        label = e.GetComponent<TagComponent>().Tag;

                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::BeginCombo("##k6camera", (ICON_LC_CAMERA + (" " + label)).c_str()))
                {
                    if (ImGui::Selectable("Free (Orbit)", rig.GetMode() == EditorCameraRig::Mode::Orbit))
                        rig.SetMode(EditorCameraRig::Mode::Orbit);
                    if (ImGui::Selectable("Free (Fly)", rig.GetMode() == EditorCameraRig::Mode::Fly))
                        rig.SetMode(EditorCameraRig::Mode::Fly);
                    if (ctx.Scene)
                    {
                        bool sep = false;
                        auto view = ctx.Scene->GetRegistry()
                            .view<CameraComponent, TransformComponent>();
                        for (auto e : view)
                        {
                            Entity ent(e, ctx.Scene.get());
                            if (!ent.HasComponent<IDComponent>()) continue;
                            if (!sep) { ImGui::Separator(); sep = true; }
                            const std::string tag = ent.HasComponent<TagComponent>()
                                ? ent.GetComponent<TagComponent>().Tag : std::string("Camera");
                            const UUID id = ent.GetComponent<IDComponent>().ID;
                            const bool selected = rig.GetMode() == EditorCameraRig::Mode::Possess &&
                                                  (uint64_t)rig.PossessedEntity() == (uint64_t)id;
                            if (ImGui::Selectable((tag + "##" + std::to_string((uint64_t)id)).c_str(), selected))
                                rig.Possess(id);
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Viewport camera (K7). RMB-hold in the viewport = temporary\n"
                                      "fly (WASD+QE, LShift boost, scroll = speed). Possess renders\n"
                                      "a scene camera's pose read-only.");

                // Fly speed chip (scroll adjusts it while flying).
                if (rig.IsFlying())
                {
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled(ICON_LC_GAUGE " %.0f m/s", rig.Fly().GetMoveSpeed());
                }
                ImGui::SameLine(0.0f, 8.0f);
            }

            // Frame selection (the old toolbar's Frame; Front/Top/Iso now live
            // on the K8 cube).
            if (ImGui::Button(ICON_LC_FOCUS, ImVec2(sq, sq)))
                FrameSelection(ctx, rig);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Frame selection (F)");

            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
        }

        // ---- K8: axis navigator (bottom-left; hidden in 2D/Play) --------------

        // ---- K9: stats chips (bottom-right; View-menu toggle) -----------------
        if (m_ShowStatsChips)
        {
            char text[256];
            // W7 — the 2D chip row reads the Renderer2D batch instead, and the
            // "distance" slot becomes the 2D rig's zoom (its scale readout).
            const Renderer2D::Statistics s = Renderer2D::GetStats();
            const float zoom = m_Cam2D ? m_Cam2D->GetZoom() : 0.0f;
            std::snprintf(text, sizeof(text),
                          "%dx%d   " ICON_LC_BOXES " %u draws  %u quads  %u lines   "
                          ICON_LC_RULER " %.1f u   %.2f ms",
                          (int)vpSize.x, (int)vpSize.y,
                          s.DrawCalls, s.QuadCount, s.LineCount,
                          zoom, 1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
            const ImVec2 ts = ImGui::CalcTextSize(text);
            const ImVec2 pad(8.0f, 4.0f);
            const ImVec2 p0(vpPos.x + vpSize.x - ts.x - pad.x * 2.0f - 10.0f,
                            vpPos.y + vpSize.y - ts.y - pad.y * 2.0f - 10.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p0, ImVec2(p0.x + ts.x + pad.x * 2.0f, p0.y + ts.y + pad.y * 2.0f),
                              IM_COL32(15, 17, 23, 185), 6.0f);
            dl->AddText(ImVec2(p0.x + pad.x, p0.y + pad.y),
                        ImGui::GetColorU32(ImGuiCol_Text), text);
        }
    }

    void ViewportController::UpdateViewportDragDrop(EditorContext& ctx, const Camera& renderCam,
                                                    Camera2DController* cam2d, bool playing)
    {
        // Drops are an EDIT operation: refused while playing (runtime scene) and
        // while a gizmo drag is active (no accidental spawns mid-manipulation).
        if (playing || !ctx.Scene || m_GizmoActive)
            return;

        auto& app = Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f)
            return;

        const ImRect rect(ImVec2(vpPos.x, vpPos.y),
                          ImVec2(vpPos.x + vpSize.x, vpPos.y + vpSize.y));
        if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID("##k13viewport_drop")))
            return;

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
        {
            const std::string vfs((const char*)payload->Data);
            std::string ext = fs::path(vfs).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            const std::string stem = fs::path(vfs).stem().string();

            const glm::vec2 mouse = Input::GetMouseScreenPosition();
            const float px = mouse.x - vpPos.x, py = mouse.y - vpPos.y;

            // The drop's world point: 2D mode = the XY plane point under the
            // cursor; 3D = the ID-pass depth probe, falling back to 10 m along
            // the camera ray through the cursor when it misses geometry.
            glm::vec3 dropPoint(0.0f);
            if (cam2d)
            {
                const glm::vec2 w = Camera2DController::ScreenToWorld(
                    mouse, vpPos, vpSize, cam2d->GetFocus(), cam2d->GetZoom());
                dropPoint = { w.x, w.y, 0.0f };
            }
            else   // no depth probe without the picker — always the camera-ray fallback
            {
                const glm::mat4 invVP = glm::inverse(renderCam.GetViewProjectionMatrix());
                const float nx = 2.0f * (px / vpSize.x) - 1.0f;
                const float ny = 1.0f - 2.0f * (py / vpSize.y);
                glm::vec4 pn = invVP * glm::vec4(nx, ny, -1.0f, 1.0f); pn /= pn.w;
                glm::vec4 pf = invVP * glm::vec4(nx, ny,  1.0f, 1.0f); pf /= pf.w;
                dropPoint = glm::vec3(pn) + glm::normalize(glm::vec3(pf) - glm::vec3(pn)) * 10.0f;
            }

            // The entity under the cursor (material/image assignment targets).
            // 3D: the ID pass. 2D: nothing — the sprite rect walk below is the
            // only hit-test, and it covers every 2D drop target.
            auto pickUnderCursor = [&]() -> Entity
            {
                return {};
            };

            static const char* kImageExts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
            const bool isImage = std::any_of(std::begin(kImageExts), std::end(kImageExts),
                                             [&](const char* e) { return ext == e; });

            if (ext == ".cprefab")
            {
                // Instantiate live, place at the drop point, record ONE create
                // step (the snapshot carries the position, so redo lands there).
                Entity root = Prefabs::Instantiate(ctx, vfs);
                if (root && root.HasComponent<TransformComponent>())
                    root.GetComponent<TransformComponent>().Position = dropPoint;
                if (root)
                    Commands::RecordSpawn(ctx, root, "Drop Prefab " + stem);
            }
            else if (isImage)
            {
                // 2D-first: prefer the topmost sprite under the cursor's world
                // XY point in 2D mode; otherwise the ID-picked entity.
                Entity target;
                if (cam2d)
                {
                    auto view = ctx.Scene->GetRegistry()
                        .view<TransformComponent, SpriteRendererComponent>();
                    for (auto e : view)
                    {
                        const auto& t = view.get<TransformComponent>(e);
                        const auto& s = view.get<SpriteRendererComponent>(e);
                        const glm::vec2 half = SpriteRendererComponent::WorldSize(
                            s, { t.Scale.x, t.Scale.y },
                            s.Resolved ? (int)s.Resolved->GetWidth() : 0,
                            s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f;
                        if (std::abs(dropPoint.x - t.Position.x) <= std::abs(half.x) &&
                            std::abs(dropPoint.y - t.Position.y) <= std::abs(half.y))
                            target = Entity(e, ctx.Scene.get());
                    }
                }
                if (!target)
                    target = pickUnderCursor();

                if (target && target.HasComponent<SpriteRendererComponent>())
                {
                    Commands::SetField(ctx, target,
                                       entt::type_hash<SpriteRendererComponent>::value(),
                                       "TexturePath",
                                       Cosmic::Reflect::FieldValue{ vfs });
                    ctx.Log("[Drop] Assigned sprite image '" + vfs + "'.");
                }
                else
                    ctx.Log("[Drop] No SpriteRenderer under the cursor for '" + vfs + "'.",
                            LogSeverity::Warn);
            }
            else
            {
                ctx.Log("[Drop] '" + ext + "' has no viewport drop action (use the Inspector slots).",
                        LogSeverity::Warn);
            }
        }
        ImGui::EndDragDropTarget();
    }

    bool ViewportController::SelectionBounds(EditorContext& ctx, glm::vec3& mn, glm::vec3& mx) const
    {
        if (!ctx.Scene) return false;
        bool any = false;
        auto grow = [&](const glm::vec3& wp)
        {
            if (!any) { mn = mx = wp; any = true; }
            else      { mn = glm::min(mn, wp); mx = glm::max(mx, wp); }
        };
        auto consider = [&](Entity e)
        {
            if (!e) return;
            // Sprites (U3): their world rect from the shared sizing rule, so F
            // frames a 2D scene the same way it frames meshes.
            if (e.HasComponent<TransformComponent>() && e.HasComponent<SpriteRendererComponent>())
            {
                const auto& t = e.GetComponent<TransformComponent>();
                const auto& s = e.GetComponent<SpriteRendererComponent>();
                const glm::vec2 half = SpriteRendererComponent::WorldSize(
                    s, { t.Scale.x, t.Scale.y },
                    s.Resolved ? (int)s.Resolved->GetWidth() : 0,
                    s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f;
                const float hx = std::abs(half.x), hy = std::abs(half.y);
                grow({ t.Position.x - hx, t.Position.y - hy, t.Position.z });
                grow({ t.Position.x + hx, t.Position.y + hy, t.Position.z });
            }
        };

        if (ctx.HasSelection())
        {
            for (entt::entity h : ctx.Selection) consider(Entity(h, ctx.Scene.get()));
        }
        else
        {
            for (auto h : ctx.Scene->View<TransformComponent, SpriteRendererComponent>())
                consider(Entity(h, ctx.Scene.get()));
        }
        return any;
    }

    void ViewportController::FrameSelection(EditorContext& ctx, EditorCameraRig& rig)
    {
        glm::vec3 mn, mx;
        if (!SelectionBounds(ctx, mn, mx))
            return;
        if (m_Cam2D)   // U3 — 2D mode frames the XY extent on the 2D rig
            m_Cam2D->FrameBounds({ mn.x, mn.y }, { mx.x, mx.y });
        else
            rig.FrameBounds(mn, mx);   // K7 — seamless in fly mode too
    }
}
