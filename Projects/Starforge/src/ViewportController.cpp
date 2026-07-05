// ViewportController.cpp — see header.

#include "ViewportController.h"
#include "commands/EditorCommands.h"

#include "layers/WorkspaceLayer.h"
#include "physics/ScenePhysics.h"   // J8 — live physics debug draw during Play

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

    void ViewportController::OnUpdate(EditorContext& ctx, OrbitCameraController& cam, float ts)
    {
        (void)ts;
        auto& app = Application::Get();
        auto* ws  = app.GetWorkspaceLayer();
        const bool vpHover = ws && ws->IsViewportHovered();
        const glm::vec2 vpPos  = app.GetViewportPos();
        const glm::vec2 vpSize = app.GetViewportSize();

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

        // Click-pick — only on the click frame (an ID pre-pass is not free).
        const bool lmb     = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);
        const bool clicked = lmb && !m_LmbWasDown;
        m_LmbWasDown = lmb;

        if (clicked && vpHover && !m_GizmoActive && !m_GizmoOver &&
            m_Picker && ctx.Scene && vpSize.x > 1.0f && vpSize.y > 1.0f)
        {
            const glm::vec2 mouse = Input::GetMouseScreenPosition();
            const int px = (int)(mouse.x - vpPos.x);
            const int py = (int)(mouse.y - vpPos.y);
            if (px >= 0 && py >= 0 && px < (int)vpSize.x && py < (int)vpSize.y)
            {
                m_Picker->RenderIdPass(*ctx.Scene, cam.GetCamera(),
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

    void ViewportController::DrawGizmo(EditorContext& ctx, OrbitCameraController& cam)
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
            Gizmo::Manipulate(cam.GetCamera(), t, m_Op, m_Space, snap);

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
        auto consider = [&](Entity e)
        {
            if (!e || !e.HasComponent<MeshRendererComponent>()) return;
            const auto& mr = e.GetComponent<MeshRendererComponent>();
            if (!mr.MeshAsset) return;
            const glm::vec3 lmin = mr.MeshAsset->GetLocalMin();
            const glm::vec3 lmax = mr.MeshAsset->GetLocalMax();
            const glm::mat4 m = ctx.Scene->GetWorldTransform(e);
            for (int i = 0; i < 8; ++i)
            {
                glm::vec3 corner((i & 1) ? lmax.x : lmin.x, (i & 2) ? lmax.y : lmin.y, (i & 4) ? lmax.z : lmin.z);
                glm::vec3 wp = glm::vec3(m * glm::vec4(corner, 1.0f));
                if (!any) { mn = mx = wp; any = true; }
                else      { mn = glm::min(mn, wp); mx = glm::max(mx, wp); }
            }
        };

        if (ctx.HasSelection())
            for (entt::entity h : ctx.Selection) consider(Entity(h, ctx.Scene.get()));
        else
            for (auto h : ctx.Scene->View<TransformComponent, MeshRendererComponent>())
                consider(Entity(h, ctx.Scene.get()));
        return any;
    }

    void ViewportController::FrameSelection(EditorContext& ctx, OrbitCameraController& cam)
    {
        glm::vec3 mn, mx;
        if (SelectionBounds(ctx, mn, mx))
            cam.FrameBounds(mn, mx);
    }
}
