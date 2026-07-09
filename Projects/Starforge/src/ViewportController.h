#pragma once

// ViewportController.h — picking, gizmo+undo, grid/axes, framing (E9).
//
// Wires the S5 viewport investment into the editor: ScenePicker click-select
// (feeding the multi-selection), ImGuizmo transform manipulation whose drags
// become coalesced TransformEdit undo commands, a ground grid + origin axes +
// selection outline drawn through Renderer3D's batched lines, camera snap views,
// frame-selection, and camera bookmarks. Owned + driven by StarforgeApp.
//
// DEVIATION (documented): a true polygon-fill "Wireframe" view mode needs an
// engine polygon-mode verb that does not exist yet (renderer/ has no fill-mode
// API); that + ID-buffer visualize are a small additive engine follow-up. The
// grid/axes here reuse the existing Renderer3D line batch rather than a new,
// duplicate renderer/DebugDraw module (which would only re-implement it).

#include "EditorContext.h"

#include <Cosmic.h>

namespace Starforge
{
    class ViewportController
    {
    public:
        void Init();

        // Input: gizmo hotkeys (W/E/R/F), camera bookmarks, and click-picking.
        // Call from StarforgeApp::OnUpdate (after the scene is rendered).
        void OnUpdate(EditorContext& ctx, Cosmic::OrbitCameraController& cam, float ts);

        // Grid + axes + selection outline, drawn via Renderer3D into the bound
        // viewport FBO. Call after Scene::OnRender3D, before unbinding. Wraps
        // DrawOverlayContent in its own BeginScene/EndScene.
        void DrawSceneOverlay(EditorContext& ctx, const Cosmic::Camera& cam);

        // The overlay draw calls WITHOUT a BeginScene/EndScene wrap — for the
        // SceneRenderer DrawTransparent hook (H2), which is already inside a scene
        // with the HDR target + scene depth bound.
        void DrawOverlayContent(EditorContext& ctx);

        // The gizmo, drawn inside the viewport overlay window (between
        // WorkspaceLayer::BeginViewportOverlay/EndViewportOverlay).
        void DrawGizmo(EditorContext& ctx, Cosmic::OrbitCameraController& cam);

        // The tool strip (gizmo mode, snap, grid, snap-views) for the top bar.
        void DrawToolbar(EditorContext& ctx, Cosmic::OrbitCameraController& cam);

        // Depth probe for orbit-about-surface (H1): renders a one-off ID pass at the
        // current camera pose and reconstructs the world point under the given SCREEN
        // pixel. StarforgeApp wires this into OrbitCameraController::SetPivotProbe so a
        // CAD orbit pivots about the actual surface under the cursor (falls back to the
        // controller's ray/target-plane pivot when it misses geometry).
        bool ProbeWorldPoint(EditorContext& ctx, const Cosmic::Camera& cam,
                             const glm::vec2& screenMouse, glm::vec3& out);

        // Last-frame gizmo state — StarforgeApp gates the camera on it.
        bool GizmoBusy() const { return m_GizmoActive || m_GizmoOver; }

    private:
        void FrameSelection(EditorContext& ctx, Cosmic::OrbitCameraController& cam);
        bool SelectionBounds(EditorContext& ctx, glm::vec3& mn, glm::vec3& mx) const;

        Cosmic::Ref<Cosmic::ScenePicker> m_Picker;

        Cosmic::Gizmo::Operation m_Op    = Cosmic::Gizmo::Operation::Translate;
        Cosmic::Gizmo::Space     m_Space = Cosmic::Gizmo::Space::World;
        bool  m_Snap      = false;
        float m_SnapValue = 0.5f;
        bool  m_ShowGrid  = true;
        bool  m_ShowColliders    = true;    // J8 — collider wireframe gizmos
        bool  m_ShowPhysicsDebug = false;   // J8 — live Jolt body outlines during Play

        bool  m_GizmoActive = false;
        bool  m_GizmoOver   = false;
        bool  m_GizmoWasUsing = false;
        Cosmic::TransformComponent m_DragBefore;   // gizmo drag-start pose (undo)

        bool  m_LmbWasDown = false;

        // Voxel brush press-edge latches (V4): one edit per click.
        bool  m_VoxelLmbWas = false;
        bool  m_VoxelRmbWas = false;

        struct Bookmark { bool Set = false; float Yaw = 0, Pitch = 0, Dist = 0; glm::vec3 Target{ 0 }; };
        Bookmark m_Bookmarks[9];
    };
}
