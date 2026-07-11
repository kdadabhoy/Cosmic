# Phase 26 Plan — Navigation & AI (NavMesh, Agents)

> **Created 2026-07-11.** Editor-vision phase 5 of 7 (spec of record:
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) §10).
> This phase flips FEATURE-MATRIX's former "Navmesh / AI pathfinding — ✖ unplanned" verdict:
> the reference screenshots + the Phase 28 flagship (companion/creature AI) are the unlock.
> Pattern of record throughout: **the Jolt vendoring playbook** (doc 14 J1 — PRIVATE-static,
> pimpl service, no third-party types in public headers, play-session lifetime).
>
> **Depends on:** Phase 15 (shipped — collision sources), Phase 10 terrain + Phase 18 voxel
> (shipped — geometry sources). Order: N1 → N2 → {N3, N4} → N5.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate (no shipped app attaches nav
components — every sync is a no-op for them). Bake + query math headless-tested against a known
greybox. Debug draw uses `Renderer3D` line/tri verbs (the J8 collider-gizmo pattern). Big
binary data stays out of scene JSON (the `.cvox` sidecar rule). No git writes.

## 1. Work orders

### N1 — Vendor Recast/Detour + `NavWorld` service *(gap §10.1)*
**Files:** VENDOR `Cosmic/dependencies/recastnavigation` (pin release; Recast + Detour +
DetourCrowd + DetourTileCache; demo/tools OFF; zlib license header retained); NEW engine
`nav/NavWorld.h/.cpp` (pimpl — no Recast types public), CMake PRIVATE-static into `Cosmic.dll`.
**Spec:** service API v1: `Build(NavBuildDesc{cells, agent, regions, tiles}, geometry spans)` →
serializable nav data; `FindPath(a, b)`, `Raycast`, `RandomPointAround`; tile-cache rebuild of
dirty tiles. **Acceptance:** headless: bake a hand-authored two-platform greybox, assert a
known path length ±ε and an unreachable pair fails cleanly; zero new public-header includes;
build green Debug+Release. **Status:** ☐

### N2 — `NavMeshComponent` + scene bake pipeline + `.cnav` *(gap §10.2; dep N1)*
**Files:** engine `scene/Components.h` (reflected recipe: cell size/height, agent
radius/height/max-climb/max-slope, region min/merge, max edge len/error, tile size,
`AutoGenerate`, `SourceMode{FromChildren, WholeScene}`, `AlwaysRenderHelper`; runtime nav data
NOT reflected), NEW `scene/SceneNav.h/.cpp` or `Scene::SyncNavMeshes` (the `SyncVoxelVolumes`
shape): gathers **collision-view geometry** (collider shapes via the ScenePhysics bake
enumeration, terrain heightfield samples, voxel chunk meshes — filtered to children when
`FromChildren`), builds via N1 on the `JobSystem` (the WorldSystemsPanel one-shot async
precedent), `.cnav` sidecar save/load beside the scene, `BuiltSignature` regen gating (the E18
recipe rule).
**Acceptance:** headless: recipe → bake → `.cnav` round-trip identical; editing a wall +
rebake changes paths; ForgePlayground bakes without stalls (async). **Status:** ☐

### N3 — Editor authoring + debug draw *(gap §10.4; deps N2)*
**Files:** Starforge `ViewportController.cpp` (translucent nav-poly overlay via
`Renderer3D` — dim always when `AlwaysRenderHelper`, bright when selected; K6 strip toggle),
`panels/InspectorPanel.cpp` (a **Regenerate now** button row for `NavMeshComponent` — the
J8 "Fit to mesh" per-component-button precedent), `HierarchyPanel`/Entity menu
(World ▸ Nav Mesh), `AssetTypes.h` row (`.cnav`).
**Spec:** recipe fields auto-UI via reflection (T10 metadata welcome but not required).
**Acceptance:** author → Regenerate → walkable polys visibly match the level (ramps in, steep
roofs out); toggle + selection highlighting behave; undo works on recipe edits. **Status:** ☐

### N4 — Agents: `NavAgentComponent` + crowd + script API *(gap §10.3; deps N2)*
**Files:** engine `scene/Components.h` (reflected
`NavAgentComponent{ Radius, Height, MaxSpeed, MaxAccel, StoppingDistance, AutoRepath }`),
play-session integration (DetourCrowd stepped in the Scene play tick — one dated line added to
the doc 14 J4 tick-order contract), `scripting/ScriptableEntity` proxy `Nav()` →
`SetTarget(pos)`, `Stop()`, `FindPath`, `RandomPointAround`, `Raycast`, arrival events via the
U2 EventBus (`nav.arrived`).
**Spec:** agents exist only while a play session runs (the physics-body lifetime rule);
transforms write back like physics sync. **Acceptance:** headless: an agent walks a baked
greybox to a target within tolerance, repaths around a moved blocker; two crowd agents don't
interpenetrate; determinism: two runs bit-match (the J9 proof pattern). **Status:** ☐

### N5 — Nav sample + docs *(deps N3, N4)*
**Files:** ForgePlayground (or a small template scene): 3 patrol/chase critters on a baked
mesh; docs plan D40 row for the phase.
**Spec:** patrol = waypoint loop via `Nav()`; chase = target the player character when near
(SystemScript tier, H9). **Acceptance:** recorded editor session: bake → Play → critters
navigate ramps and avoid each other → packaged exe behaves identically. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/25-phase26-navigation-ai-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, its cited § in
> `docs/design/example-images-gap-analysis.md`, and doc 14 J1/J4 (the vendoring + tick-order
> patterns of record). Pimpl the vendor; headless-test bake/path math; play-session lifetime;
> compat gate; roadmap cmake recipe; no git writes. Finish with Acceptance + status banner.
