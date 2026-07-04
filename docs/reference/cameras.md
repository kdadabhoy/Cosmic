# API Reference — Cameras & Navigation

> **STATUS: SKELETON** — to be filled by work order **D14** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/camera/Camera.h`,
`camera/OrthographicCamera.h`, `camera/OrthographicCameraController.h`,
`camera/PerspectiveCamera.h`, `camera/OrbitCameraController.h` (+ `NavStyle`/`ViewPreset`),
`camera/FlyCameraController.h`, `camera/NavigationCube.h`, `graphics/Gizmo.h`.

**Read first:** root README §16 (camera system); systems explainer
[cameras-navigation](../systems/cameras-navigation.md).

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
