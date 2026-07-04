# Starforge — editor user guide

> Starforge is the Cosmic editor: assemble scenes visually, attach C++ simulation
> logic, press Play, and package the result as a standalone app. It ships as a
> normal plugin — launch it from the Launcher, or `CosmicApp --project Starforge`.
> This guide covers the Stage-D authoring workflow (primitives, import, materials,
> environment, packaging). Plan of record: [`docs/plans/archive/11-phase13-starforge-plan.md`](../plans/archive/11-phase13-starforge-plan.md)
> (Phase 13 complete — current work: [`docs/plans/13-phase14-starforge-hardening-plan.md`](../plans/13-phase14-starforge-hardening-plan.md)).
> This quick guide will be absorbed into the full Starforge manual (doc 12 D39).

## Layout

The editor docks a top bar (menus + Play/build toolbar), a **Viewport** (CAD
navigation + gizmos), and panels: **Hierarchy**, **Inspector**, **Content
Browser**, **Console**, plus (View menu, off by default) **Environment**,
**Material Editor**, and **Statistics**.

## Creating & editing objects

- **Entity ▸ Primitive** (or the Hierarchy **+ Create ▸ Primitive**) adds a
  parametric **Cube / Sphere / Plane / Cylinder / Cone / Torus**. A primitive
  stores only its *shape + parameters* — the mesh is rebuilt whenever you change a
  parameter in the Inspector (undoable) and regenerated on load, so scenes stay
  tiny and text-diffable. Edit `Radius`, `Segments`, `Size`, etc. live.
- The **Inspector** is reflection-driven: every registered component's fields get
  an appropriate widget automatically, with per-edit undo (Ctrl+Z / Ctrl+Y).
- **Import Model** (File ▸ Import Model…): point at a source file; it is copied
  into `project://models/`, a `<file>.cmeta` records the unit scale + up-axis
  (STL→mm, FBX→cm presets), and an entity is spawned. **OBJ imports today**;
  FBX/STL/DAE/PLY need the assimp backend (build with `COSMIC_WITH_ASSIMP` — see
  the header of `Cosmic/src/assets/MeshImport.cpp`). Edit the `.cmeta` and re-open
  to re-import at a different scale.

## Materials & environment

- **Material Editor** (View ▸ Material Editor): author a PBR `.cmat`
  (albedo / metallic / roughness / AO / emissive + optional texture-map slots),
  **Save**, and **Assign to Selection**. The live viewport is the preview.
  (Material edits apply live but are not undoable in v1 — save then re-assign.)
- **Environment** (View ▸ Environment): edit the scene's single Environment entity
  — sun, sky mode, time-of-day, fog, IBL and post — with per-edit undo. Drives the
  renderer for the whole scene.

## Play & ship

- **▶ / ⏸ / ⏭ / ⏹** run the scene: Play snapshots the edit scene, builds a runtime
  copy, instantiates scripts, and ticks a fixed step; Stop restores the untouched
  edit scene. **Ctrl+B** builds the project's C++ scripts and hot-reloads them.
- **File ▸ Package…** stages `dist/<Project>/` — `<Project>.exe` (a renamed
  `CosmicApp.exe`), `Cosmic.dll`, the project DLL, its assets, and a `boot.cfg`.
  Copy that folder to any machine and double-click: `boot.cfg` names the startup
  project so it runs straight into the scene with no Launcher. (v1 stages the
  current build config — build Release first for a shipping app.)

## Keyboard shortcuts

Help ▸ Keyboard Shortcuts lists them in-app. The essentials:

| Keys | Action |
| --- | --- |
| `Ctrl+N` / `Ctrl+S` | New / Save scene |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+D` / `Del` | Duplicate / Delete selection |
| `Ctrl+B` | Build scripts (hot reload) |
| `F` | Frame selection |
| `W` / `E` / `R` | Gizmo translate / rotate / scale |
| MMB drag / Ctrl+MMB / scroll | Orbit / pan / zoom |
| `1`–`9` / `Ctrl+1`–`9` | Recall / save camera bookmark |

## Known v1 limits (Stage D)

- FBX/STL/DAE/PLY import is gated behind the assimp backend (OBJ + glTF work now).
- Material previews are the live viewport; no offscreen preview-sphere / thumbnails yet.
- **World-systems authoring (terrain/water/particles, E18)** and the **telemetry
  recording panel (E20)** are not yet in this build — see the plan doc.
- Packaging stages the current build config (no forced Release rebuild / zip yet).
