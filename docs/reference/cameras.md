# API Reference — Cameras & Navigation

> **STATUS: SKELETON** — to be filled by work order **D14** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/camera/Camera.h`,
`camera/OrthographicCamera.h`, `camera/OrthographicCameraController.h`,
`camera/PerspectiveCamera.h`, `camera/OrbitCameraController.h` (+ `NavStyle`/`ViewPreset`),
`camera/FlyCameraController.h`, `camera/NavigationCube.h`, `graphics/Gizmo.h`,
**`camera/Camera2DController.h`** — the 2D pan/zoom rig, which D52 found was the only controller
missing from the manifest in
[reference/README.md](README.md#coverage-manifest--every-public-header-maps-to-a-chapter); add its
row when D5 runs.

**Read first:** the guide chapter [`../guide/cameras.md`](../guide/cameras.md) — written from source
by D53, it covers every class in scope here (README §16, which used to cover only
`OrthographicCamera` + its controller, is now an overview pointing at it). Systems explainer:
[cameras-navigation](../systems/cameras-navigation.md). For `Camera2DController` specifically —
focus/zoom conventions, `ScreenToWorld`, `PanBy`, `ZoomAboutPoint`, `FrameBounds` — see
[`../guide/cameras.md`](../guide/cameras.md#set-up-a-2d-panzoom-view) and, for the authoring context,
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md#drive-the-2d-camera).

**Configuration note for D14:** `NavigationCube` is **3D-only** (`Cosmic/CMakeLists.txt:198`) and so
is `ScenePicker` (`:202`), but `Cosmic.h` fences only the `ScenePicker` include — a 2D build
compiles a `NavigationCube` call and fails at link time. Mark the 3D-only rows with the manifest's
³ᴰ marker.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Camera` base (S4.1) — the unified interface (`GetViewProjection` etc.), what both renderers accept
- [ ] `OrthographicCamera` + `OrthographicCameraController` — bounds, zoom, rotation, `OnUpdate`/`OnEvent`/`OnResize` wiring
- [ ] `PerspectiveCamera` — FOV/aspect/near-far, position/orientation setters
- [ ] `OrbitCameraController` — CAD navigation (S5.1/S5.2): `NavStyle` bindings, `ViewPreset` snap views, zoom-to-cursor, orbit-about-cursor, frame (F) / Home behaviors, hotkey table
- [ ] `FlyCameraController` (F1) — WASD + mouse-look, speed modifiers, capture/release contract (when the mouse is captured vs UI)
- [ ] `NavigationCube` (S5.3) — draw + hit-test API, dock corner, DPI notes
- [ ] `Gizmo` (S5.5, ImGuizmo) — begin-frame call, operation/mode enums (translate/rotate/scale, local/world), snapping, `IsOver`/`IsUsing` interaction guards, W/E/R hotkey convention

## Sections to write

1. "Which camera/controller when" decision table (2D tool → ortho controller; CAD inspect → orbit; world exploration → fly). <!-- TODO(D14) -->
2. Entries per checklist. <!-- TODO(D14) -->
3. Event/update wiring pattern common to all controllers (forward `OnEvent`, call `OnUpdate(ts)`, respect `e.Handled`). <!-- TODO(D14) -->

---
*Changelog:*
