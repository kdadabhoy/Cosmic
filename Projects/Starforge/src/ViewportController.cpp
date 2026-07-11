// ViewportController.cpp — see header.

#include "ViewportController.h"
#include "commands/EditorCommands.h"

#include "layers/WorkspaceLayer.h"
#include "physics/ScenePhysics.h"   // J8 — live physics debug draw during Play
#include "scene/ui/UiSystem.h"      // U1 — canvas UI interaction + hit-test
#include "voxel/VoxelVolume.h"      // V4 — voxel brush raycast

#include <imgui.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

using namespace Cosmic;

namespace Starforge
{
    namespace
    {
        // Three orthogonal wire circles — a cheap "sphere" for light glyphs / radius.
        void DrawWireSphere(const glm::vec3& c, float r, const glm::vec4& col, int seg)
        {
            const float step = glm::two_pi<float>() / (float)seg;
            glm::vec3 pxy{}, pxz{}, pyz{};
            for (int i = 0; i <= seg; ++i)
            {
                const float a = i * step, ca = std::cos(a), sa = std::sin(a);
                const glm::vec3 xy = c + glm::vec3(ca, sa, 0.0f) * r;
                const glm::vec3 xz = c + glm::vec3(ca, 0.0f, sa) * r;
                const glm::vec3 yz = c + glm::vec3(0.0f, ca, sa) * r;
                if (i > 0)
                {
                    Renderer3D::DrawLine(pxy, xy, col);
                    Renderer3D::DrawLine(pxz, xz, col);
                    Renderer3D::DrawLine(pyz, yz, col);
                }
                pxy = xy; pxz = xz; pyz = yz;
            }
        }

        // A Y-axis capsule wireframe under `xform` (rings at ±halfHeight, 4 verticals,
        // and hemispherical cap arcs) — the collider gizmo for CapsuleCollider (J8).
        void DrawWireCapsule(const glm::mat4& xform, float radius, float halfHeight,
                             const glm::vec4& col, int seg = 20)
        {
            auto P = [&](const glm::vec3& local) { return glm::vec3(xform * glm::vec4(local, 1.0f)); };
            const float step = glm::two_pi<float>() / (float)seg;

            glm::vec3 topPrev{}, botPrev{};
            for (int i = 0; i <= seg; ++i)
            {
                const float a = i * step, ca = std::cos(a), sa = std::sin(a);
                const glm::vec3 top = P({ ca * radius,  halfHeight, sa * radius });
                const glm::vec3 bot = P({ ca * radius, -halfHeight, sa * radius });
                if (i > 0)
                {
                    Renderer3D::DrawLine(topPrev, top, col);
                    Renderer3D::DrawLine(botPrev, bot, col);
                }
                topPrev = top; botPrev = bot;
                if (i % (seg / 4) == 0)   // 4 vertical body lines
                    Renderer3D::DrawLine(top, bot, col);
            }

            // Cap arcs (XY + ZY half-circles at each end).
            glm::vec3 tpx{}, tpz{}, bpx{}, bpz{};
            for (int i = 0; i <= seg / 2; ++i)
            {
                const float a = i * step, ca = std::cos(a), sa = std::sin(a);
                const glm::vec3 tx = P({ ca * radius,  halfHeight + sa * radius, 0 });
                const glm::vec3 tz = P({ 0,            halfHeight + sa * radius, ca * radius });
                const glm::vec3 bx = P({ ca * radius, -halfHeight - sa * radius, 0 });
                const glm::vec3 bz = P({ 0,           -halfHeight - sa * radius, ca * radius });
                if (i > 0)
                {
                    Renderer3D::DrawLine(tpx, tx, col); Renderer3D::DrawLine(tpz, tz, col);
                    Renderer3D::DrawLine(bpx, bx, col); Renderer3D::DrawLine(bpz, bz, col);
                }
                tpx = tx; tpz = tz; bpx = bx; bpz = bz;
            }
        }

        // A little sun: a small ring with radiating spokes, drawn at a light's origin.
        void DrawSunGlyph(const glm::vec3& c, const glm::vec4& col)
        {
            const float r = 0.30f;
            DrawWireSphere(c, r, col, 12);
            for (int i = 0; i < 8; ++i)
            {
                const float a = i * glm::two_pi<float>() / 8.0f;
                const glm::vec3 d(std::cos(a), std::sin(a), 0.0f);
                Renderer3D::DrawLine(c + d * (r * 1.3f), c + d * (r * 2.0f), col);
            }
        }
    }

    void ViewportController::Init()
    {
        m_Picker = ScenePicker::Create();
    }

    void ViewportController::OnUpdate(EditorContext& ctx, OrbitCameraController& cam, float ts,
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
        // in 2D mode, else the orbit camera.
        const Camera& renderCam = renderCamOverride ? *renderCamOverride
            : (cam2d ? static_cast<const Camera&>(cam2d->GetCamera())
                     : static_cast<const Camera&>(cam.GetCamera()));

        ImGuiIO& io = ImGui::GetIO();
        const bool canKey = vpHover && !io.WantTextInput && !io.WantCaptureKeyboard;

        if (canKey)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_Op = Gizmo::Operation::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_Op = Gizmo::Operation::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_Op = Gizmo::Operation::Scale;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameSelection(ctx, cam);
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_ShowGrid = !m_ShowGrid;

            // Camera bookmarks: Ctrl+1..9 save, 1..9 recall.
            for (int i = 0; i < 9; ++i)
            {
                if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) continue;
                if (io.KeyCtrl)
                    m_Bookmarks[i] = { true, cam.GetYaw(), cam.GetPitch(), cam.GetDistance(), cam.GetTarget() };
                else if (m_Bookmarks[i].Set)
                {
                    cam.SetTarget(m_Bookmarks[i].Target);
                    cam.SetYawPitch(m_Bookmarks[i].Yaw, m_Bookmarks[i].Pitch);
                    cam.SetDistance(m_Bookmarks[i].Dist);
                }
            }
        }

        // Voxel brush (V4): when editing a selected voxel volume, LMB places the
        // active block / RMB breaks — one undoable edit per click (raycast the grid).
        // In the editor's default CAD nav the camera uses MMB, so LMB/RMB are free.
        bool voxelBrushConsumed = false;
        if (ctx.VoxelBrush.Editing && vpHover && !m_GizmoActive && !m_GizmoOver &&
            ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            Entity prim = ctx.PrimaryEntity();
            if (prim && prim.HasComponent<VoxelVolumeComponent>())
            {
                voxelBrushConsumed = true;   // suppress select while brushing
                auto& vc = prim.GetComponent<VoxelVolumeComponent>();

                const bool blmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
                const bool brmb = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_RIGHT);
                const bool placeClick = blmb && !m_VoxelLmbWas;
                const bool breakClick = brmb && !m_VoxelRmbWas;
                m_VoxelLmbWas = blmb; m_VoxelRmbWas = brmb;

                if ((placeClick || breakClick) && vc.Volume && vc.Palette)
                {
                    const glm::vec2 mouse = Input::GetMouseScreenPosition();
                    const float lx = mouse.x - vpPos.x, ly = mouse.y - vpPos.y;
                    if (lx >= 0 && ly >= 0 && lx < vpSize.x && ly < vpSize.y)
                    {
                        // Unproject the mouse pixel into a world ray through the camera.
                        const Camera& c = renderCam;
                        const glm::mat4 invVP = glm::inverse(c.GetViewProjectionMatrix());
                        const float nx = 2.0f * (lx / vpSize.x) - 1.0f;
                        const float ny = 1.0f - 2.0f * (ly / vpSize.y);
                        glm::vec4 pn = invVP * glm::vec4(nx, ny, -1.0f, 1.0f); pn /= pn.w;
                        glm::vec4 pf = invVP * glm::vec4(nx, ny,  1.0f, 1.0f); pf /= pf.w;
                        // Near-plane origin works for BOTH projections (an ortho
                        // camera's position is not on the pixel's parallel ray).
                        const glm::vec3 origin = glm::vec3(pn);
                        const glm::vec3 dir = glm::normalize(glm::vec3(pf) - glm::vec3(pn));

                        VoxelRayHit hit = vc.Volume->RayCast(origin, dir, ctx.VoxelBrush.Reach, *vc.Palette);
                        if (hit.Hit)
                        {
                            ctx.VoxelBrush.Stroke++;   // each click = its own undo step
                            if (placeClick)
                                Commands::VoxelEdit(ctx, prim, hit.Place, ctx.VoxelBrush.ActiveBlock, ctx.VoxelBrush.Stroke);
                            else
                                Commands::VoxelEdit(ctx, prim, hit.Voxel, 0, ctx.VoxelBrush.Stroke);
                        }
                    }
                }
            }
        }
        if (!ctx.VoxelBrush.Editing) { m_VoxelLmbWas = false; m_VoxelRmbWas = false; }

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
                    !m_GizmoActive && !m_GizmoOver && vpRect.Contains(local))
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
            !m_GizmoActive && !m_GizmoOver && ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
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
        if (clicked && !voxelBrushConsumed && !tileBrushConsumed && !uiConsumed && !spriteConsumed &&
            vpHover && !m_GizmoActive && !m_GizmoOver &&
            m_Picker && ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            const glm::vec2 mouse = Input::GetMouseScreenPosition();
            const int px = (int)(mouse.x - vpPos.x);
            const int py = (int)(mouse.y - vpPos.y);
            if (px >= 0 && py >= 0 && px < (int)vpSize.x && py < (int)vpSize.y)
            {
                m_Picker->RenderIdPass(*ctx.Scene, renderCam,
                                       (uint32_t)vpSize.x, (uint32_t)vpSize.y);
                Entity hit = m_Picker->Pick(*ctx.Scene, px, py);
                if (hit)
                {
                    if (io.KeyCtrl) ctx.ToggleSelect(hit);
                    else            ctx.SelectOnly(hit);
                }
                else if (!io.KeyCtrl)
                {
                    ctx.ClearSelection();
                }
            }
        }
    }

    bool ViewportController::ProbeWorldPoint(EditorContext& ctx, const Camera& cam,
                                             const glm::vec2& screenMouse, glm::vec3& out)
    {
        auto& app = Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (!m_Picker || !ctx.Scene || vpSize.x < 1.0f || vpSize.y < 1.0f)
            return false;

        const int px = (int)(screenMouse.x - vpPos.x);
        const int py = (int)(screenMouse.y - vpPos.y);
        if (px < 0 || py < 0 || px >= (int)vpSize.x || py >= (int)vpSize.y)
            return false;

        // One self-contained ID pass at the live pose (RenderIdPass restores the
        // default target). Invoked only on the frame an orbit drag begins — cheap.
        m_Picker->RenderIdPass(*ctx.Scene, cam, (uint32_t)vpSize.x, (uint32_t)vpSize.y);
        return m_Picker->WorldPoint(cam, px, py, out);
    }

    void ViewportController::DrawSceneOverlay(EditorContext& ctx, const Camera& cam)
    {
        Renderer3D::BeginScene(cam);
        DrawOverlayContent(ctx);
        Renderer3D::EndScene();
    }

    void ViewportController::DrawOverlayContent2D(EditorContext& ctx, const Camera2DController& cam)
    {
        // NO BeginScene/EndScene — same contract as DrawOverlayContent. Lines
        // sit at z=0 (the sprite plane's conventional depth).
        glm::vec2 mn, mx;
        cam.VisibleRect(mn, mx);

        if (m_ShowGrid)
        {
            const float pxPerUnit = (cam.GetZoom() > 0.0f)
                ? (Application::Get().GetViewportSize().y / (2.0f * cam.GetZoom()))
                : 0.0f;

            const glm::vec4 minor(0.30f, 0.32f, 0.36f, 0.35f);
            const glm::vec4 major(0.45f, 0.47f, 0.52f, 0.7f);

            // 1-unit minors only when they resolve (>= ~6 px apart); 10-unit majors.
            auto drawLines = [&](float step, const glm::vec4& col)
            {
                const float x0 = std::floor(mn.x / step) * step;
                const float y0 = std::floor(mn.y / step) * step;
                for (float x = x0; x <= mx.x; x += step)
                    Renderer3D::DrawLine({ x, mn.y, 0.0f }, { x, mx.y, 0.0f }, col);
                for (float y = y0; y <= mx.y; y += step)
                    Renderer3D::DrawLine({ mn.x, y, 0.0f }, { mx.x, y, 0.0f }, col);
            };
            if (pxPerUnit >= 6.0f)          drawLines(1.0f,  minor);
            if (pxPerUnit * 10.0f >= 6.0f)  drawLines(10.0f, major);

            // XY axes through the origin (X red, Y green — matches the 3D axes).
            Renderer3D::DrawLine({ mn.x, 0.0f, 0.0f }, { mx.x, 0.0f, 0.0f }, { 0.86f, 0.24f, 0.24f, 0.9f });
            Renderer3D::DrawLine({ 0.0f, mn.y, 0.0f }, { 0.0f, mx.y, 0.0f }, { 0.35f, 0.80f, 0.30f, 0.9f });
        }

        // Selection outlines: a wire rect around each selected sprite (meshes in
        // a 2.5D scene keep their 3D box from the shared sizing rule below).
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
                const float hx = std::abs(half.x), hy = std::abs(half.y);
                const glm::vec3 p = t.Position;
                Renderer3D::DrawLine({ p.x - hx, p.y - hy, p.z }, { p.x + hx, p.y - hy, p.z }, sel);
                Renderer3D::DrawLine({ p.x + hx, p.y - hy, p.z }, { p.x + hx, p.y + hy, p.z }, sel);
                Renderer3D::DrawLine({ p.x + hx, p.y + hy, p.z }, { p.x - hx, p.y + hy, p.z }, sel);
                Renderer3D::DrawLine({ p.x - hx, p.y + hy, p.z }, { p.x - hx, p.y - hy, p.z }, sel);
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

                auto rect = [&](float x0, float y0, float x1, float y1, const glm::vec4& col)
                {
                    Renderer3D::DrawLine({ o.x + x0, o.y + y0, o.z }, { o.x + x1, o.y + y0, o.z }, col);
                    Renderer3D::DrawLine({ o.x + x1, o.y + y0, o.z }, { o.x + x1, o.y + y1, o.z }, col);
                    Renderer3D::DrawLine({ o.x + x1, o.y + y1, o.z }, { o.x + x0, o.y + y1, o.z }, col);
                    Renderer3D::DrawLine({ o.x + x0, o.y + y1, o.z }, { o.x + x0, o.y + y0, o.z }, col);
                };

                rect(0.0f, 0.0f, (float)tm.GridW, (float)tm.GridH,
                     { 0.35f, 0.65f, 0.95f, 0.8f });   // map bounds

                if (ctx.TileBrush.Editing)
                {
                    const glm::vec4 hot{ 1.0f, 0.85f, 0.25f, 0.9f };
                    if (tm.InBounds(m_TileLastCell.x, m_TileLastCell.y))
                        rect((float)m_TileLastCell.x,       (float)m_TileLastCell.y,
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
    }

    void ViewportController::DrawOverlayContent(EditorContext& ctx)
    {
        // NO BeginScene/EndScene — the caller owns the scene. In the SceneRenderer
        // path (H2) this runs from the DrawTransparent hook with the HDR target +
        // scene depth still bound, so grid/selection lines occlude correctly.
        if (m_ShowGrid)
        {
            Renderer3D::DrawGrid(50.0f, 1.0f,
                                 glm::vec4(0.30f, 0.32f, 0.36f, 0.5f),
                                 glm::vec4(0.45f, 0.47f, 0.52f, 0.8f), 10);
            Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);
        }

        // Selection outline: an oriented wire box around each selected mesh.
        if (ctx.Scene)
        {
            for (entt::entity h : ctx.Selection)
            {
                Entity e(h, ctx.Scene.get());
                if (!e || !e.HasComponent<MeshRendererComponent>()) continue;
                const auto& mr = e.GetComponent<MeshRendererComponent>();
                if (!mr.MeshAsset) continue;

                const glm::vec3 lmin = mr.MeshAsset->GetLocalMin();
                const glm::vec3 lmax = mr.MeshAsset->GetLocalMax();
                const glm::vec3 c = 0.5f * (lmin + lmax);
                const glm::vec3 s = (lmax - lmin) * 1.03f;
                const glm::mat4 box = ctx.Scene->GetWorldTransform(e)
                    * glm::translate(glm::mat4(1.0f), c)
                    * glm::scale(glm::mat4(1.0f), s);
                Renderer3D::DrawWireBox(box, glm::vec4(1.0f, 0.62f, 0.11f, 1.0f));
            }
        }

        // Light glyphs (H3): make lights visible objects. Directional lights show a
        // sun + a travel-direction arrow; point lights show a small bulb glyph, and a
        // selected one shows its full falloff radius as a translucent wire sphere.
        if (ctx.Scene)
        {
            auto& reg = ctx.Scene->GetRegistry();
            auto selected = [&](entt::entity h)
            {
                for (entt::entity s : ctx.Selection) if (s == h) return true;
                return false;
            };

            for (auto e : reg.view<TransformComponent, DirectionalLightComponent>())
            {
                const auto& t  = reg.get<TransformComponent>(e);
                const auto& dl = reg.get<DirectionalLightComponent>(e);
                const glm::vec4 col(dl.Color, 1.0f);
                DrawSunGlyph(t.Position, col);
                const glm::vec3 dir = glm::length(dl.Direction) > 1e-4f
                    ? glm::normalize(dl.Direction) : glm::vec3(0.0f, -1.0f, 0.0f);
                const glm::vec3 tip = t.Position + dir * 2.0f;
                Renderer3D::DrawLine(t.Position, tip, col);
                // arrowhead: two short back-spokes off the tip
                const glm::vec3 ref = std::abs(dir.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                const glm::vec3 side = glm::normalize(glm::cross(dir, ref)) * 0.25f;
                Renderer3D::DrawLine(tip, tip - dir * 0.4f + side, col);
                Renderer3D::DrawLine(tip, tip - dir * 0.4f - side, col);
            }

            for (auto e : reg.view<TransformComponent, PointLightComponent>())
            {
                const auto& t  = reg.get<TransformComponent>(e);
                const auto& pl = reg.get<PointLightComponent>(e);
                DrawWireSphere(t.Position, 0.35f, glm::vec4(pl.Color, 1.0f), 16);
                if (selected(e))
                    DrawWireSphere(t.Position, pl.Radius, glm::vec4(pl.Color, 0.5f), 24);
            }
        }

        // Collider gizmos (J8): a wireframe per collider in the SAME world transform
        // the runtime bakes (mesh-space geometry x world matrix), so a Fit-to-mesh
        // box overlays its mesh exactly. Selected entities draw bright; others dim.
        if (m_ShowColliders && ctx.Scene)
        {
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

                if (const auto* c = reg.try_get<BoxColliderComponent>(e))
                {
                    const glm::mat4 m = world
                        * glm::translate(glm::mat4(1.0f), c->Offset)
                        * glm::scale(glm::mat4(1.0f), c->HalfExtents * 2.0f);
                    Renderer3D::DrawWireBox(m, col);
                }
                if (const auto* c = reg.try_get<SphereColliderComponent>(e))
                {
                    const glm::vec3 center = glm::vec3(world * glm::vec4(c->Offset, 1.0f));
                    const float sx = glm::length(glm::vec3(world[0]));   // world scale (uniform assumed)
                    DrawWireSphere(center, c->Radius * sx, col, 24);
                }
                if (const auto* c = reg.try_get<CapsuleColliderComponent>(e))
                {
                    const glm::mat4 m = world * glm::translate(glm::mat4(1.0f), c->Offset);
                    DrawWireCapsule(m, c->Radius, c->HalfHeight, col);
                }
            }
        }

        // Live Jolt state during Play (J8): body outlines coloured by sleep state,
        // contact points. Debug-config only (JPH_DEBUG_RENDERER) — a no-op in Release.
        if (m_ShowPhysicsDebug && ctx.Scene && ctx.Scene->GetPhysics())
            ctx.Scene->GetPhysics()->World().DebugDraw();
    }

    void ViewportController::DrawGizmo(EditorContext& ctx, const Camera& cam)
    {
        auto& app = Application::Get();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();
        if (vpSize.x < 1.0f || vpSize.y < 1.0f) { m_GizmoActive = m_GizmoOver = false; return; }

        Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);

        Entity sel = ctx.PrimaryEntity();
        if (sel && sel.HasComponent<TransformComponent>())
        {
            auto& t = sel.GetComponent<TransformComponent>();
            const TransformComponent beforeThisFrame = t;   // pre-manipulate pose
            const float snap = m_Snap ? m_SnapValue : 0.0f;
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

    void ViewportController::DrawToolbar(EditorContext& ctx, OrbitCameraController& cam)
    {
        (void)ctx;
        int op = (int)m_Op;
        ImGui::TextDisabled("Tool:"); ImGui::SameLine();
        ImGui::RadioButton("Move", &op, 0);   ImGui::SameLine();
        ImGui::RadioButton("Rotate", &op, 1); ImGui::SameLine();
        ImGui::RadioButton("Scale", &op, 2);  ImGui::SameLine();
        m_Op = (Gizmo::Operation)op;

        ImGui::TextDisabled("|"); ImGui::SameLine();
        int space = (int)m_Space;
        ImGui::RadioButton("World", &space, (int)Gizmo::Space::World); ImGui::SameLine();
        ImGui::RadioButton("Local", &space, (int)Gizmo::Space::Local); ImGui::SameLine();
        m_Space = (Gizmo::Space)space;

        ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_Snap); ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("##snapv", &m_SnapValue, 0.05f, 0.01f, 90.0f, "%.2f"); ImGui::SameLine();
        ImGui::Checkbox("Grid", &m_ShowGrid); ImGui::SameLine();
        ImGui::Checkbox("Colliders", &m_ShowColliders); ImGui::SameLine();
        ImGui::Checkbox("Physics", &m_ShowPhysicsDebug); ImGui::SameLine();

        ImGui::TextDisabled("|"); ImGui::SameLine();
        if (ImGui::SmallButton("Front")) cam.SnapView(ViewPreset::Front); ImGui::SameLine();
        if (ImGui::SmallButton("Top"))   cam.SnapView(ViewPreset::Top);   ImGui::SameLine();
        if (ImGui::SmallButton("Iso"))   cam.SnapView(ViewPreset::Iso);   ImGui::SameLine();
        if (ImGui::SmallButton("Frame")) FrameSelection(ctx, cam);
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
            if (e.HasComponent<MeshRendererComponent>())
            {
                const auto& mr = e.GetComponent<MeshRendererComponent>();
                if (mr.MeshAsset)
                {
                    const glm::vec3 lmin = mr.MeshAsset->GetLocalMin();
                    const glm::vec3 lmax = mr.MeshAsset->GetLocalMax();
                    const glm::mat4 m = ctx.Scene->GetWorldTransform(e);
                    for (int i = 0; i < 8; ++i)
                    {
                        glm::vec3 corner((i & 1) ? lmax.x : lmin.x, (i & 2) ? lmax.y : lmin.y, (i & 4) ? lmax.z : lmin.z);
                        grow(glm::vec3(m * glm::vec4(corner, 1.0f)));
                    }
                }
            }
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
            for (auto h : ctx.Scene->View<TransformComponent, MeshRendererComponent>())
                consider(Entity(h, ctx.Scene.get()));
            for (auto h : ctx.Scene->View<TransformComponent, SpriteRendererComponent>())
                consider(Entity(h, ctx.Scene.get()));
        }
        return any;
    }

    void ViewportController::FrameSelection(EditorContext& ctx, OrbitCameraController& cam)
    {
        glm::vec3 mn, mx;
        if (!SelectionBounds(ctx, mn, mx))
            return;
        if (m_Cam2D)   // U3 — 2D mode frames the XY extent on the 2D rig
            m_Cam2D->FrameBounds({ mn.x, mn.y }, { mx.x, mx.y });
        else
            cam.FrameBounds(mn, mx);
    }
}
