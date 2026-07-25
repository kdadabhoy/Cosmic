// ViewportController.cpp — see header.

#include "ViewportController.h"
#include "commands/EditorCommands.h"
#include "Prefabs.h"                // K13 — viewport prefab drops

#include "layers/WorkspaceLayer.h"
// STAYS in both configurations (plan §8.5): ScenePhysics is dimension-agnostic
// and the W7 2D collider overlay reads the very same collider components.
#include "physics/ScenePhysics.h"   // J8 — live physics debug draw during Play
#include "scene/ui/UiSystem.h"      // U1 — canvas UI interaction + hit-test
#ifndef COSMIC_2D_ONLY
#include "voxel/VoxelVolume.h"      // V4 — voxel brush raycast
#include "voxel/VoxelRender.h"      // R8 — entity-ID view draws voxel chunk meshes
#include "nav/NavWorld.h"           // N3 — nav-poly overlay (GetDebugTriangles)
#endif

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
    namespace
    {
#ifndef COSMIC_2D_ONLY
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

        // Entity-ID view (R8): a stable, well-separated flat color per entity id.
        // Golden-ratio hue spread + a small hash-driven value wobble so neighbors
        // in creation order never share a hue.
        glm::vec4 IdColor(uint32_t id)
        {
            const float h = std::fmod((float)id * 0.61803398875f, 1.0f) * 6.0f;
            const float v = 0.75f + 0.20f * std::fmod((float)id * 0.2971f, 1.0f);
            const float c = v * 0.85f;                       // s = 0.85
            const float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            const float m = v - c;
            glm::vec3 rgb(0.0f);
            if      (h < 1.0f) rgb = { c, x, 0 };
            else if (h < 2.0f) rgb = { x, c, 0 };
            else if (h < 3.0f) rgb = { 0, c, x };
            else if (h < 4.0f) rgb = { 0, x, c };
            else if (h < 5.0f) rgb = { x, 0, c };
            else               rgb = { c, 0, x };
            return glm::vec4(rgb + m, 1.0f);
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
#else
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
#endif
    }

    void ViewportController::Init()
    {
#ifndef COSMIC_2D_ONLY
        m_Picker = ScenePicker::Create();
#endif
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
#ifndef COSMIC_2D_ONLY
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
#endif   // COSMIC_2D_ONLY — the voxel brush

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
        // 3D only: the picker renders a mesh ID pass. In the 2D build the sprite
        // rect-pick above IS the pick path, and a miss clears the selection.
#ifndef COSMIC_2D_ONLY
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
#else
        // 2D: a click on empty space (no sprite, no UI, no tile stroke) clears
        // the selection — the same "click-away deselects" contract the ID pass
        // provides in the 3D build.
        if (clicked && !tileBrushConsumed && !uiConsumed && !spriteConsumed &&
            vpHover && !m_GizmoActive && !m_GizmoOver && !io.KeyCtrl && ctx.Scene)
        {
            ctx.ClearSelection();
        }
        (void)voxelBrushConsumed;
#endif
    }

#ifndef COSMIC_2D_ONLY
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

    void ViewportController::DrawEntityIdView(EditorContext& ctx, const Camera& cam)
    {
        if (!ctx.Scene)
            return;

        // Meshes stored by params/path must be live before we can draw them —
        // the same top-of-frame syncs BuildRenderDesc runs.
        ctx.Scene->SyncPrimitiveMeshes();
        ctx.Scene->SyncVoxelVolumes(cam.GetPosition());

        // Flat shading: lift the Lambert ambient floor to 1 so the per-draw color
        // IS the pixel (restored after — sticky global).
        const float prevAmbient = Renderer3D::GetAmbient();
        Renderer3D::SetAmbient(1.0f);

        Renderer3D::BeginScene(cam);

        auto& reg = ctx.Scene->GetRegistry();

        for (auto e : reg.view<TransformComponent, MeshRendererComponent>())
        {
            const auto& mr = reg.get<MeshRendererComponent>(e);
            if (!mr.MeshAsset) continue;
            const glm::mat4 xf = ctx.Scene->GetWorldTransform(Entity(e, ctx.Scene.get()));
            Renderer3D::DrawMesh(mr.MeshAsset, xf, IdColor((uint32_t)e), (int)(uint32_t)e);
        }

        // LOD groups: same camera-distance level the lit pass would pick (S12.4).
        for (auto e : reg.view<TransformComponent, LODGroupComponent>())
        {
            const auto& t   = reg.get<TransformComponent>(e);
            const auto& lod = reg.get<LODGroupComponent>(e);
            const int level = LODGroupComponent::SelectLevel(
                lod.Levels, glm::distance(cam.GetPosition(), t.Position));
            if (level < 0 || !lod.Levels[level].MeshAsset) continue;
            const glm::mat4 xf = ctx.Scene->GetWorldTransform(Entity(e, ctx.Scene.get()));
            Renderer3D::DrawMesh(lod.Levels[level].MeshAsset, xf, IdColor((uint32_t)e), (int)(uint32_t)e);
        }

        // Voxel volumes: every uploaded chunk mesh in the volume's color.
        for (auto e : reg.view<VoxelVolumeComponent>())
        {
            const auto& vc = reg.get<VoxelVolumeComponent>(e);
            if (!vc.Volume || !vc.Render) continue;
            const glm::mat4 xf =
                glm::translate(glm::mat4(1.0f), vc.Volume->GetOrigin()) *
                glm::scale(glm::mat4(1.0f), glm::vec3(vc.Volume->GetVoxelSize()));
            for (const auto& kv : vc.Render->ChunkMeshes)
                if (kv.second)
                    Renderer3D::DrawMesh(kv.second, xf, IdColor((uint32_t)e), (int)(uint32_t)e);
        }

        // Grid + selection wire boxes stay useful in the debug view.
        DrawOverlayContent(ctx);

        Renderer3D::EndScene();
        Renderer3D::SetAmbient(prevAmbient);
    }
#endif   // COSMIC_2D_ONLY — ProbeWorldPoint + DrawSceneOverlay + DrawEntityIdView

#ifndef COSMIC_2D_ONLY
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
#else   // COSMIC_2D_ONLY
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
#endif   // COSMIC_2D_ONLY — DrawOverlayContent2D + DrawColliderOverlay2D

#ifndef COSMIC_2D_ONLY
    void ViewportController::DrawOverlayContent(EditorContext& ctx)
    {
        // NO BeginScene/EndScene — the caller owns the scene. In the SceneRenderer
        // path (H2) this runs from the DrawTransparent hook with the HDR target +
        // scene depth still bound, so grid/selection lines occlude correctly.
        if (m_ShowGrid)
        {
            // K10 — the infinite grid (ray-plane fragment shader, decade steps,
            // distance fade) replaces the fixed 50 m DrawGrid; the axis tripod
            // stays for the Y direction the plane can't show.
            Renderer3D::DrawInfiniteGrid({});
            Renderer3D::DrawAxes(glm::mat4(1.0f), 2.0f);
        }

        // Selection wire boxes — the FALLBACK when the K12 outline pass is off
        // (bypass view modes); the silhouette ring owns meshed selections
        // otherwise. Lights/colliders below keep their glyphs either way.
        if (ctx.Scene && !m_OutlinePassActive)
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

            // X5 — 2D lights: a center cross glyph + a flat radius RING in the XY
            // plane (they live in the 2D plane, so a sphere would read wrong).
            for (auto e : reg.view<TransformComponent, Light2DComponent>())
            {
                const auto& t  = reg.get<TransformComponent>(e);
                const auto& lc = reg.get<Light2DComponent>(e);
                const bool sel = selected(e);
                const glm::vec4 col(lc.Color, sel ? 1.0f : 0.6f);
                Renderer3D::DrawLine(t.Position - glm::vec3(0.2f, 0.0f, 0.0f),
                                     t.Position + glm::vec3(0.2f, 0.0f, 0.0f), col);
                Renderer3D::DrawLine(t.Position - glm::vec3(0.0f, 0.2f, 0.0f),
                                     t.Position + glm::vec3(0.0f, 0.2f, 0.0f), col);
                const int   seg = 32;
                const float r   = lc.Radius;
                glm::vec3   prev = t.Position + glm::vec3(r, 0.0f, 0.0f);
                for (int i = 1; i <= seg; ++i)
                {
                    const float a = (float)i / (float)seg * 6.2831853f;
                    const glm::vec3 cur = t.Position + glm::vec3(std::cos(a) * r, std::sin(a) * r, 0.0f);
                    Renderer3D::DrawLine(prev, cur, glm::vec4(lc.Color, sel ? 0.7f : 0.35f));
                    prev = cur;
                }
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

        // Navmesh overlay (N3): the walkable poly soup as a translucent wireframe
        // (the J8 line-batch precedent — Renderer3D has no filled-tri primitive). A
        // navmesh draws when SELECTED (bright) or when its AlwaysRenderHelper is set
        // (dim); the strip toggle (m_ShowNavMesh) is the master switch.
        if (m_ShowNavMesh && ctx.Scene)
        {
            auto& reg = ctx.Scene->GetRegistry();
            auto selected = [&](entt::entity h)
            {
                for (entt::entity s : ctx.Selection) if (s == h) return true;
                return false;
            };
            const glm::vec4 dim { 0.20f, 0.85f, 0.55f, 0.35f };   // resting teal-green
            const glm::vec4 hot { 0.40f, 1.00f, 0.70f, 0.95f };   // selected

            for (auto e : reg.view<NavMeshComponent>())
            {
                const auto& nm = reg.get<NavMeshComponent>(e);
                if (!nm.Nav || !nm.Nav->IsBuilt())
                    continue;
                const bool sel = selected(e);
                if (!sel && !nm.AlwaysRenderHelper)
                    continue;
                const glm::vec4 col = sel ? hot : dim;

                m_NavTriScratch.clear();
                nm.Nav->GetDebugTriangles(m_NavTriScratch);
                for (const Cosmic::NavDebugTri& tri : m_NavTriScratch)
                {
                    Renderer3D::DrawLine(tri.A, tri.B, col);
                    Renderer3D::DrawLine(tri.B, tri.C, col);
                    Renderer3D::DrawLine(tri.C, tri.A, col);
                }
            }
        }
    }
#endif   // COSMIC_2D_ONLY — DrawOverlayContent (the 3D overlay)

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

#ifndef COSMIC_2D_ONLY
    void ViewportController::PrerenderNavCube(const Camera& cam, bool playing, bool mode2D)
    {
        // K8 — the cube pre-pass renders into its own FBO (outside the main
        // scene); skipped where the widget is hidden (Play / 2D mode).
        m_NavCubeFresh = false;
        if (playing || mode2D)
            return;
        if (!m_NavCube)
            m_NavCube = NavigationCube::Create(120);
        m_NavCube->Render(cam.GetViewMatrix());
        m_NavCubeFresh = true;
    }
#endif

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

            // World/Local.
            {
                const bool world = m_Space == Gizmo::Space::World;
                if (ImGui::Button(world ? ICON_LC_GLOBE : ICON_LC_BOX, ImVec2(sq, sq)))
                    m_Space = world ? Gizmo::Space::Local : Gizmo::Space::World;
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
                if (on)
                {
                    const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
                }
                if (ImGui::Button(icon, ImVec2(sq, sq)))
                    on = !on;
                if (on)
                    ImGui::PopStyleColor();
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
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0.0f, 3.0f);
            };
            toggle(ICON_LC_GRID_3X3, m_ShowGrid,         "Grid (G)");
#ifndef COSMIC_2D_ONLY
            toggle(ICON_LC_BOXES,    m_ShowColliders,    "Collider gizmos (J8)");
            toggle(ICON_LC_ACTIVITY, m_ShowPhysicsDebug, "Live physics debug draw during Play (J8)");
            toggle(ICON_LC_WAYPOINTS, m_ShowNavMesh,     "Nav-mesh overlay (N3): walkable polys of selected /\nAlways-render-helper navmeshes");
#else
            // W7 — the collider chip drives the Renderer2D collider overlay
            // (§6.4). The physics-debug and nav chips have nothing behind them
            // in a 2D build (Jolt's debug renderer needs Renderer3D; there is
            // no navmesh), so they are absent rather than dead.
            toggle(ICON_LC_BOXES,    m_ShowColliders,
                   "Collider overlay (J8/W7): Box/Sphere/Capsule projected onto XY");
#endif
            ImGui::SameLine(0.0f, 8.0f);

            // R8 — view-mode dropdown (Lit · Unlit · Wireframe · Entity ID).
            // The 2D build keeps Lit and Wireframe only: Unlit neutralizes 3D
            // lights and Entity ID renders the mesh ID pass, neither of which
            // exists there.
            {
#ifndef COSMIC_2D_ONLY
                static const char* kModes[] = { "Lit", "Unlit", "Wireframe", "Entity ID" };
                const int kModeCount = 4;
#else
                static const char* kModes[] = { "Lit", "Wireframe" };
                const int kModeCount = 2;
#endif
                int vm = (int)m_ViewMode;
#ifdef COSMIC_2D_ONLY
                vm = (m_ViewMode == ViewMode::Wireframe) ? 1 : 0;
#endif
                ImGui::SetNextItemWidth(96.0f);
                if (ImGui::Combo("##k6viewmode", &vm, kModes, kModeCount))
#ifndef COSMIC_2D_ONLY
                    m_ViewMode = (ViewMode)vm;
#else
                    m_ViewMode = (vm == 1) ? ViewMode::Wireframe : ViewMode::Lit;
#endif
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
#ifndef COSMIC_2D_ONLY
        if (m_NavCubeFresh && m_NavCube && !playing && !mode2D)
        {
            const float cube = (float)m_NavCube->GetSize();
            const ImVec2 pos(vpPos.x + 10.0f, vpPos.y + vpSize.y - cube - 10.0f);
            ImGui::SetCursorScreenPos(pos);
            ImGui::Image((ImTextureID)(intptr_t)m_NavCube->GetTextureID(),
                         ImVec2(cube, cube), ImVec2(0, 1), ImVec2(1, 0));
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Click a face to snap the view");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const ImVec2 mouse = ImGui::GetMousePos();
                    const float u = (mouse.x - pos.x) / cube;
                    const float v = (mouse.y - pos.y) / cube;
                    ViewPreset preset;
                    if (m_NavCube->PickFace(u, v, preset))
                        rig.SnapView(preset);
                }
            }
        }
#endif   // COSMIC_2D_ONLY — the K8 nav cube

        // ---- K9: stats chips (bottom-right; View-menu toggle) -----------------
        if (m_ShowStatsChips)
        {
            char text[256];
#ifndef COSMIC_2D_ONLY
            const Renderer3D::Statistics s = Renderer3D::GetStats();
            const float dist = rig.Orbit().GetDistance();
            std::snprintf(text, sizeof(text),
                          "%dx%d   " ICON_LC_BOXES " %u draws  %u submitted  %u culled  %u instanced   "
                          ICON_LC_RULER " %.1f m   %.2f ms",
                          (int)vpSize.x, (int)vpSize.y,
                          s.DrawCalls, s.MeshesSubmitted, s.MeshesCulled,
                          s.AutoInstancedMeshes + s.ExplicitInstances,
                          dist, 1000.0f / std::max(1.0f, ImGui::GetIO().Framerate));
#else
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
#endif
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
#ifndef COSMIC_2D_ONLY
            else if (!ProbeWorldPoint(ctx, renderCam, mouse, dropPoint))
#else
            else   // no depth probe without the picker — always the camera-ray fallback
#endif
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
#ifndef COSMIC_2D_ONLY
                if (!m_Picker || px < 0 || py < 0 || px >= vpSize.x || py >= vpSize.y)
                    return {};
                m_Picker->RenderIdPass(*ctx.Scene, renderCam,
                                       (uint32_t)vpSize.x, (uint32_t)vpSize.y);
                return m_Picker->Pick(*ctx.Scene, (int)px, (int)py);
#else
                return {};
#endif
            };

#ifndef COSMIC_2D_ONLY
            static const char* kMeshExts[] = { ".obj", ".gltf", ".glb", ".fbx", ".stl", ".dae", ".ply" };
#endif
            static const char* kImageExts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
#ifndef COSMIC_2D_ONLY
            const bool isMesh  = std::any_of(std::begin(kMeshExts),  std::end(kMeshExts),
                                             [&](const char* e) { return ext == e; });
#endif
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
#ifndef COSMIC_2D_ONLY
            else if (isMesh)
            {
                Commands::Create(ctx, stem, Entity{}, [&](Entity e)
                {
                    e.GetComponent<TransformComponent>().Position = dropPoint;
                    e.AddComponent<MeshRendererComponent>().MeshPath = vfs;   // sync resolves
                });
                ctx.Log("[Drop] Spawned '" + stem + "' from " + vfs + ".");
            }
            else if (ext == ".cmat")
            {
                Entity hit = pickUnderCursor();
                if (hit && hit.HasComponent<MeshRendererComponent>())
                {
                    Commands::AssignMaterial(ctx, hit, vfs);
                    ctx.Log("[Drop] Assigned material '" + vfs + "'.");
                }
                else
                    ctx.Log("[Drop] No mesh under the cursor for '" + vfs + "'.",
                            LogSeverity::Warn);
            }
#endif
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
#ifndef COSMIC_2D_ONLY
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
#endif
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
#ifndef COSMIC_2D_ONLY
            for (auto h : ctx.Scene->View<TransformComponent, MeshRendererComponent>())
                consider(Entity(h, ctx.Scene.get()));
#endif
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
