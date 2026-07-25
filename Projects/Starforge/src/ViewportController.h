#pragma once

// ViewportController.h — picking, gizmo+undo, grid/axes, framing (E9).
//
// Wires the S5 viewport investment into the editor: ScenePicker click-select
// (feeding the multi-selection), ImGuizmo transform manipulation whose drags
// become coalesced TransformEdit undo commands, a ground grid + origin axes +
// selection outline drawn through Renderer3D's batched lines, camera snap views,
// frame-selection, camera bookmarks, and the view modes (R8: Lit / Unlit /
// Wireframe via the engine's SetPolygonMode-backed SceneRendererSettings flag /
// Entity-ID flat-color debug view). Owned + driven by StarforgeApp.
//
// (The old deviation note about a missing polygon-mode verb is closed — doc 18
// R8 shipped RenderCommand::SetPolygonMode + these view modes, 2026-07-11.)
// The grid/axes here reuse the existing Renderer3D line batch rather than a
// new, duplicate renderer/DebugDraw module (which would only re-implement it).
//
// THE 2D CONFIGURATION (Phase 29 / W7). Renderer3D, ScenePicker and
// NavigationCube are all out of the 2D build, so every overlay that draws
// through them fences out: the infinite grid + axis tripod, the mesh selection
// wire boxes, the 3D light glyphs, the nav-poly overlay, the entity-ID debug
// view, the ID-pass click-pick and the nav cube. What SURVIVES is the 2D
// authoring surface, re-expressed on Renderer2D's own line batch — the pixel
// grid, sprite selection rects, the tile-painter visuals, the gizmo, and (new
// in W7, §6.4) a collider overlay that projects Box/Sphere/Capsule colliders
// onto XY, because PhysicsWorld::DebugDraw is a no-op without Renderer3D.

#include "EditorContext.h"
#include "EditorCameraRig.h"
#include "EditorPrefs.h"
#ifndef COSMIC_2D_ONLY
#include "nav/NavTypes.h"            // N3 — NavDebugTri (nav-overlay draw scratch)
#endif

#include <Cosmic.h>

namespace Starforge
{
    class ViewportController
    {
    public:
        // Viewport view mode (doc 18 R8; the selector lives on the K6 strip +
        // View menu). Lit = the full SceneRenderer path; Unlit = lights/IBL/
        // shadows neutralized (flat albedo); Wireframe = the engine
        // SceneRendererSettings::Wireframe line rasterization; EntityID = every
        // mesh flat-colored by a hash of its entity id (the ID-buffer visualize).
        enum class ViewMode : int { Lit = 0, Unlit, Wireframe, EntityID };

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
        void OnUpdate(EditorContext& ctx, EditorCameraRig& rig, float ts,
                      bool playing, Cosmic::Camera2DController* cam2d = nullptr,
                      const Cosmic::Camera* renderCamOverride = nullptr,
                      const glm::vec4& uiBandUv = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));

#ifndef COSMIC_2D_ONLY
        // Grid + axes + selection outline, drawn via Renderer3D into the bound
        // viewport FBO. Call after Scene::OnRender3D, before unbinding. Wraps
        // DrawOverlayContent in its own BeginScene/EndScene.
        void DrawSceneOverlay(EditorContext& ctx, const Cosmic::Camera& cam);

        // The overlay draw calls WITHOUT a BeginScene/EndScene wrap — for the
        // SceneRenderer DrawTransparent hook (H2), which is already inside a scene
        // with the HDR target + scene depth bound.
        void DrawOverlayContent(EditorContext& ctx);
#endif

        // 2D-mode overlay (U3): the pixel grid (1-unit minors, 10-unit majors,
        // XY axes) sized to the 2D rig's visible rect, plus wire-rect selection
        // outlines for sprites. Same no-wrap contract as DrawOverlayContent.
        //
        // W7: the 3D build draws it on the Renderer3D line batch the caller's
        // scene already owns; the 2D build has its own Renderer2D twin that
        // opens a PushRenderPass around the same geometry and additionally
        // draws the collider overlay (§6.4). Both produce the same picture.
        void DrawOverlayContent2D(EditorContext& ctx, const Cosmic::Camera2DController& cam);

        // The gizmo, drawn inside the viewport overlay window (between
        // WorkspaceLayer::BeginViewportOverlay/EndViewportOverlay), AFTER
        // DrawViewportOverlays (it yields while a strip widget is hovered or
        // active). Takes the ACTIVE render camera — ImGuizmo detects ortho vs
        // perspective itself.
        void DrawGizmo(EditorContext& ctx, const Cosmic::Camera& cam);

        // Arm 1-unit MOVE snapping (the 2D pixel-grid convention) — called by
        // the shell when 2D mode turns on.
        void ArmPixelSnap() { m_SnapMoveOn = true; m_SnapMove = 1.0f; }

        // K6 — the in-viewport header strip (translucent, top-left): gizmo op
        // icons incl. Universal (K11), World/Local, three per-operation snap
        // chips, grid/collider/physics toggles, the R8 view-mode dropdown, and
        // the K7 camera dropdown (+ fly speed while flying). Also draws the K8
        // axis navigator (bottom-left) and the K9 stats chips (bottom-right).
        // Call between BeginViewportOverlay/EndViewportOverlay, BEFORE DrawGizmo.
        // Hidden while playing (same rule as the old toolbar); 2D mode hides the
        // 3D-only pieces (camera dropdown, nav cube).
        void DrawViewportOverlays(EditorContext& ctx, EditorCameraRig& rig,
                                  bool playing, bool mode2D);

#ifndef COSMIC_2D_ONLY
        // K8 — render the navigation cube's offscreen pass for this frame's
        // camera. Call from RenderViewport BEFORE binding the viewport FBO (the
        // cube binds + unbinds its own target).
        void PrerenderNavCube(const Cosmic::Camera& cam, bool playing, bool mode2D);
#endif

        // K6 — snap prefs round-trip (EditorPrefs persistence).
        void LoadSnapPrefs(const Prefs::EditorSettings& s);
        void SaveSnapPrefs(Prefs::EditorSettings& s) const;

        // K9 — stats-chip row toggle (View menu).
        bool& ShowStatsChips() { return m_ShowStatsChips; }

        // K12 — when the SceneRenderer outline pass is on this frame, the mesh
        // wire boxes stand down (lights/colliders keep their glyphs; the boxes
        // return automatically in the bypass view modes where the pass is off).
        void SetOutlinePassActive(bool active) { m_OutlinePassActive = active; }

        // K13 — drag-and-drop into the viewport: accepts the Content Browser's
        // ASSET_PATH payload over the whole viewport rect. Meshes/prefabs spawn
        // at the probed world point under the cursor (fallback: 10 m along the
        // camera ray); a .cmat assigns the ID-picked entity's MaterialPath; an
        // image assigns a hit SpriteRenderer's TexturePath. Every drop is ONE
        // undo step; drops are refused while playing or mid-gizmo-drag. Call
        // between BeginViewportOverlay/EndViewportOverlay. `cam2d` non-null =
        // 2D mode (drop points land on the XY plane at z = 0).
        void UpdateViewportDragDrop(EditorContext& ctx, const Cosmic::Camera& renderCam,
                                    Cosmic::Camera2DController* cam2d, bool playing);

        // View mode (R8). StarforgeApp::RenderViewport routes on it each frame.
        ViewMode GetViewMode() const          { return m_ViewMode; }
        void     SetViewMode(ViewMode m)      { m_ViewMode = m; }

#ifndef COSMIC_2D_ONLY
        // The Entity-ID debug view (R8): draws every mesh (MeshRenderer, LOD
        // groups, voxel chunks) flat-colored by a hash of its entt id — the
        // human-readable form of the picker's integer ID buffer. Owns its own
        // BeginScene/EndScene; call with the viewport FBO bound + cleared
        // INSTEAD of the SceneRenderer path.
        void DrawEntityIdView(EditorContext& ctx, const Cosmic::Camera& cam);

        // Depth probe for orbit-about-surface (H1): renders a one-off ID pass at the
        // current camera pose and reconstructs the world point under the given SCREEN
        // pixel. StarforgeApp wires this into OrbitCameraController::SetPivotProbe so a
        // CAD orbit pivots about the actual surface under the cursor (falls back to the
        // controller's ray/target-plane pivot when it misses geometry).
        bool ProbeWorldPoint(EditorContext& ctx, const Cosmic::Camera& cam,
                             const glm::vec2& screenMouse, glm::vec3& out);
#endif

        // Last-frame gizmo state — StarforgeApp gates the camera on it.
        bool GizmoBusy() const { return m_GizmoActive || m_GizmoOver; }

    private:
        void FrameSelection(EditorContext& ctx, EditorCameraRig& rig);
        bool SelectionBounds(EditorContext& ctx, glm::vec3& mn, glm::vec3& mx) const;

#ifdef COSMIC_2D_ONLY
        // W7 / §6.4 — the 2D collider overlay. With Renderer3D gone,
        // PhysicsWorld::DebugDraw is a no-op and the J8 collider wireframes have
        // no line batch, so 2D physics would be invisible. This draws each
        // Box/Sphere/Capsule collider PROJECTED ONTO XY (the sprite plane) with
        // Renderer2D::DrawRect/DrawLine, honouring the same m_ShowColliders
        // strip toggle and the same selected-bright / resting-dim palette.
        // Called from DrawOverlayContent2D, inside its render pass.
        void DrawColliderOverlay2D(EditorContext& ctx);
#endif

#ifndef COSMIC_2D_ONLY
        Cosmic::Ref<Cosmic::ScenePicker> m_Picker;

        // K8 — engine navigation cube (bottom-left overlay).
        Cosmic::Ref<Cosmic::NavigationCube> m_NavCube;
        bool m_NavCubeFresh = false;   // rendered this frame (2D/play skip it)
#endif

        bool m_ShowStatsChips = true;  // K9 — chip row toggle (View menu)
        bool m_OutlinePassActive = false;   // K12 — wire boxes yield to the pass

        // U3 — the 2D rig while 2D mode is active (set by OnUpdate each frame,
        // null in 3D mode). FrameSelection + the toolbar Frame button route
        // through it so F frames in XY.
        Cosmic::Camera2DController* m_Cam2D = nullptr;

        Cosmic::Gizmo::Operation m_Op    = Cosmic::Gizmo::Operation::Translate;
        Cosmic::Gizmo::Space     m_Space = Cosmic::Gizmo::Space::World;
        ViewMode                 m_ViewMode = ViewMode::Lit;   // R8 view modes

        // K6 — per-operation snapping (persisted via EditorPrefs). Universal
        // uses the MOVE snap (ImGuizmo takes one snap per call — documented).
        bool  m_SnapMoveOn   = false;
        bool  m_SnapRotateOn = false;
        bool  m_SnapScaleOn  = false;
        float m_SnapMove     = 0.25f;   // m
        float m_SnapRotate   = 15.0f;   // deg
        float m_SnapScale    = 0.1f;

        bool  m_ShowGrid  = true;
        bool  m_ShowColliders    = true;    // J8 — collider wireframe gizmos (W7: the 2D overlay reads it too)
#ifndef COSMIC_2D_ONLY
        bool  m_ShowPhysicsDebug = false;   // J8 — live Jolt body outlines during Play
        bool  m_ShowNavMesh      = true;    // N3 — translucent nav-poly overlay (respects AlwaysRenderHelper + selection)
        std::vector<Cosmic::NavDebugTri> m_NavTriScratch;   // N3 — reused per-frame nav-poly draw buffer
#endif

        bool  m_GizmoActive = false;
        bool  m_GizmoOver   = false;
        bool  m_GizmoWasUsing = false;
        Cosmic::TransformComponent m_DragBefore;   // gizmo drag-start pose (undo)

        bool  m_LmbWasDown = false;

        // U1 — UI pointer latch for Play-mode canvas interaction in the viewport.
        bool  m_UiMouseWas = false;

#ifndef COSMIC_2D_ONLY
        // Voxel brush press-edge latches (V4): one edit per click.
        bool  m_VoxelLmbWas = false;
        bool  m_VoxelRmbWas = false;
#endif

        // Tile painter latches (U4): stroke edges + the last hovered cell (the
        // rect tool finalizes with it even when the release lands off-viewport).
        bool       m_TileLmbWas = false;
        bool       m_TileRmbWas = false;
        glm::ivec2 m_TileLastCell{ 0, 0 };

        struct Bookmark { bool Set = false; float Yaw = 0, Pitch = 0, Dist = 0; glm::vec3 Target{ 0 }; };
        Bookmark m_Bookmarks[9];
    };
}
