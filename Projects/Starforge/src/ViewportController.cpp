// ViewportController.cpp — see header.

#include "ViewportController.h"
#include "commands/EditorCommands.h"

#include "layers/WorkspaceLayer.h"

#include <imgui.h>

#include <algorithm>

using namespace Cosmic;

namespace Starforge
{
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

    void ViewportController::DrawSceneOverlay(EditorContext& ctx, const Camera& cam)
    {
        Renderer3D::BeginScene(cam);

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

        Renderer3D::EndScene();
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
