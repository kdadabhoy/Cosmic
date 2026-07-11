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

        // Input: gizmo hotkeys (W/E/R/F), camera bookmarks, UI interaction (U1),
        // and click-picking. Call from StarforgeApp::OnUpdate (after the scene is
        // rendered). `playing` = editor Play mode: canvas UI goes LIVE (hover/
        // press tints + EventBus signals) and consumes clicks over it; while
        // editing, a click on a UI element selects it instead (rect hit-test —
        // the ScenePicker ID pass can't see UI entities). `cam2d` non-null =
        // 2D authoring mode (U3): picking runs through the ortho camera, a click
        // rect-picks the topmost sprite before falling back to the mesh ID pass,
        // and F frames the selection in XY on the 2D rig.
        // U7: `renderCamOverride` = the camera the viewport actually rendered
        // with this frame when it isn't the orbit/2D rig (the Play game camera)
        // — picking must unproject through it; `uiBandUv` = the letterbox band
        // as viewport fractions {x,y,w,h} ((0,0,1,1) = full) — the canvas UI is
        // laid out inside it, so pointer hit-testing uses the same rect.
        void OnUpdate(EditorContext& ctx, Cosmic::OrbitCameraController& cam, float ts,
                      bool playing, Cosmic::Camera2DController* cam2d = nullptr,
                      const Cosmic::Camera* renderCamOverride = nullptr,
                      const glm::vec4& uiBandUv = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

        // Grid + axes + selection outline, drawn via Renderer3D into the bound
        // viewport FBO. Call after Scene::OnRender3D, before unbinding. Wraps
        // DrawOverlayContent in its own BeginScene/EndScene.
        void DrawSceneOverlay(EditorContext& ctx, const Cosmic::Camera& cam);

        // The overlay draw calls WITHOUT a BeginScene/EndScene wrap — for the
        // SceneRenderer DrawTransparent hook (H2), which is already inside a scene
        // with the HDR target + scene depth bound.
        void DrawOverlayContent(EditorContext& ctx);

        // 2D-mode overlay (U3): the pixel grid (1-unit minors, 10-unit majors,
        // XY axes) sized to the 2D rig's visible rect, plus wire-rect selection
        // outlines for sprites. Same no-wrap contract as DrawOverlayContent.
        void DrawOverlayContent2D(EditorContext& ctx, const Cosmic::Camera2DController& cam);

        // The gizmo, drawn inside the viewport overlay window (between
        // WorkspaceLayer::BeginViewportOverlay/EndViewportOverlay). Takes the
        // ACTIVE render camera — ImGuizmo detects ortho vs perspective itself.
        void DrawGizmo(EditorContext& ctx, const Cosmic::Camera& cam);

        // Arm 1-unit snapping (the 2D pixel-grid convention) — called by the
        // shell when 2D mode turns on.
        void ArmPixelSnap() { m_Snap = true; m_SnapValue = 1.0f; }

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

        // U3 — the 2D rig while 2D mode is active (set by OnUpdate each frame,
        // null in 3D mode). FrameSelection + the toolbar Frame button route
        // through it so F frames in XY.
        Cosmic::Camera2DController* m_Cam2D = nullptr;

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

        // U1 — UI pointer latch for Play-mode canvas interaction in the viewport.
        bool  m_UiMouseWas = false;

        // Voxel brush press-edge latches (V4): one edit per click.
        bool  m_VoxelLmbWas = false;
        bool  m_VoxelRmbWas = false;

        // Tile painter latches (U4): stroke edges + the last hovered cell (the
        // rect tool finalizes with it even when the release lands off-viewport).
        bool       m_TileLmbWas = false;
        bool       m_TileRmbWas = false;
        glm::ivec2 m_TileLastCell{ 0, 0 };

        struct Bookmark { bool Set = false; float Yaw = 0, Pitch = 0, Dist = 0; glm::vec3 Target{ 0 }; };
        Bookmark m_Bookmarks[9];
    };
}
