# Cameras & CAD Navigation — How It Works

> **STATUS: SKELETON** — to be filled by work order **D27** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** one `Camera` interface feeds both renderers; on top sit controllers — 2D
pan/zoom, SolidWorks-style orbit (zoom-to-cursor, orbit-about-cursor, snap views, ViewCube),
and a WASD fly camera — plus ID-buffer picking and ImGuizmo transform gizmos.
**Source:** `Cosmic/src/camera/*`, `scene/ScenePicker.*`, `graphics/Gizmo.*`
**API Reference:** [../reference/cameras.md](../reference/cameras.md) · **Guide:** root README §16

## Section plan

1. **Overview** — why "camera" and "controller" are separate objects; the three navigation personas (2D tool, CAD inspection, world exploration). <!-- TODO(D27) -->
2. **Mental model** — view/projection in one approachable paragraph + a small frames diagram. <!-- TODO(D27) -->
3. **Step-by-step** — an orbit drag: cursor → depth read under cursor (`FrameBuffer::ReadDepth`) → pivot reconstruction → orbit; a pick click: viewport-space px → entity-ID attachment `ReadPixel` → `Entity`. <!-- TODO(D27) -->
4. **Technical implementation** — `NavStyle` binding table, `ViewPreset` snap math, frame-selected (local AABB), `NavigationCube` render + hit-test, ImGuizmo integration (vendored lib, screen-space contract, DPI), fly-camera capture rules (F1). <!-- TODO(D27) -->
5. **Design decisions** — "feels like SolidWorks" acceptance (S5), why picking is ID-buffer not raycast. <!-- TODO(D27) -->
6. **Limits & future work** — selection outline = wire-AABB deviation note (S5.4). <!-- TODO(D27) -->

**Truth sources:** doc 05 §4 (S5 banners), `OrbitCameraController.cpp`, `ScenePicker.cpp`,
Engine3DDemo "CAD Tools (S5)" panel code.
