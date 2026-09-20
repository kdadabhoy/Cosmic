# Cameras & 2D Navigation — How It Works

> **2D trunk (2026-09-20):** `Camera`, `OrthographicCamera`, `Camera2DController` and the orthographic controller are the live surface (the perspective camera and the orbit/fly controllers still compile for the editor viewport). The `NavigationCube`, `ScenePicker` and 3D viewport picking parts are parked at [`../parked-3d/systems/cameras-navigation-3d.md`](../parked-3d/systems/cameras-navigation-3d.md) (parked 3D).

> **STATUS: SKELETON** — to be filled by work order **D27** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** one `Camera` interface feeds the renderer; on top sit controllers — 2D pan/zoom
(`Camera2DController`), the orthographic controller, and the editor's orbit/fly rig — plus ImGuizmo
transform gizmos.
**Source:** `Cosmic/src/camera/*`, `graphics/Gizmo.*`
**API Reference:** [../reference/cameras.md](../reference/cameras.md) ·
**Guide:** [../guide/cameras.md](../guide/cameras.md)

## Section plan

1. **Overview** — why "camera" and "controller" are separate objects; the 2D-tool navigation persona (pan, zoom-to-cursor, pixel grid). <!-- TODO(D27) -->
2. **Mental model** — view/projection in one approachable paragraph + a small frames diagram. <!-- TODO(D27) -->
3. **Step-by-step** — one Camera2DController frame: input → target → smoothing → view matrix → RenderPass. <!-- TODO(D27) -->
4. **Technical implementation** — `OrthographicCamera` bounds and aspect handling, `Camera2DController` zoom-about-cursor math, the editor `EditorCameraRig`. <!-- TODO(D27) -->
6. **Limits & future work** — selection outline = wire-AABB deviation note (S5.4). <!-- TODO(D27) -->

**Truth sources:** `Camera2DController.cpp`, `OrthographicCameraController.cpp`,
`Projects/Starforge/src/EditorCameraRig.cpp`.
