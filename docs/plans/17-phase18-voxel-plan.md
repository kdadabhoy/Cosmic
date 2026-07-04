# Phase 18 Plan — Voxel Worlds (blocky 3D games)

> **Created 2026-07-04** (user-approved: voxel/blocky 3D games are in scope alongside 2D).
> This is the **most deferrable** of the new phases — it is a genuine new engine subsystem
> with no existing seam, so it runs LAST (after Phases 14–17) and its work orders are sized
> with more discovery room than the others. If priorities shift, everything here parks
> cleanly behind the FEATURE-MATRIX row.
>
> **Scope (v1):** editable chunked voxel volumes rendered fast, block edit tools in Starforge,
> Jolt collision, procedural generation via the engine `Noise` toolkit. **Anti-goals (v1):**
> infinite worlds with disk paging, per-voxel lighting/flood-fill light propagation, marching-
> cubes smooth voxels, multiplayer. Each is a parked row with an unlock.
>
> **Depends on:** Phase 15 (Jolt — V5), Phase 14 H2 (SceneRenderer path). Benefits from
> Phase 17 U-series only for game samples.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate (no shipped app attaches voxel
components). Meshing/storage/generation are pure CPU → headless-tested in `CosmicTests`.
JobSystem rules: GL uploads on the main thread only; workers produce `MeshData` (the E15
GL-free geometry type — reuse it, don't invent a second one).

## 1. Architecture

```
Cosmic/src/voxel/
├── VoxelVolume.h/.cpp    chunk store: 32³ chunks, dense uint16 block ids + a per-volume
│                         BlockPalette; Get/Set(x,y,z), dirty-chunk tracking
├── BlockPalette.h        block type table: name, atlas tile per face (top/side/bottom),
│                         flags (solid, transparent, emissive) — reflected struct, .cpal JSON
├── VoxelMesher.h/.cpp    chunk → MeshData (v1 culled faces, v2 greedy) — pure, tested
├── VoxelGenerator.h      recipe (reflected): noise stack → heightmap terrain + caves fill
└── scene glue            VoxelVolumeComponent { PalettePath, GeneratorRecipe, ViewRadius }
                          Scene::SyncVoxelVolumes() — the E15/E18 signature-rebuild pattern
```

Storage on disk: a `.cvox` sidecar per volume (chunked RLE — voxel data does NOT go in the
scene JSON; the scene stores the path + generator recipe, the E15 "params, not meshes" rule at
volume scale; hand-edited chunks serialize, generated-and-untouched chunks re-generate).

## 2. Work orders

### V1 — Volume core + palette + serialization
Chunk math (world↔chunk↔local), dense storage, palette load/save, `.cvox` RLE round-trip,
dirty tracking. **Acceptance:** headless — set/get across chunk borders; 1M-voxel RLE
round-trip byte-identical; palette JSON round-trip. **Status:** ☐

### V2 — Mesher (culled → greedy)
`VoxelMesher::BuildChunk(volume, chunkCoord, palette) -> MeshData` — v1 culled interior faces
w/ per-face atlas UVs + baked AO corners (cheap voxel AO); v2 (same work order if time, else
split) greedy merging. **Acceptance:** headless — known 3-block fixture produces exact
vertex/index counts; greedy ≤ culled vertex count on random volumes; normals unit; a
1-chunk solid cube = 6 quads greedy. **Status:** ☐

### V3 — Render integration
`Scene::SyncVoxelVolumes` builds/uploads dirty chunk meshes (JobSystem workers mesh, main
thread uploads ≤ N/frame budget); chunks submit through the S12 Renderer3D queue (frustum
culling free) with one atlas material (PBR-lite: albedo atlas + roughness constant; point
sampling per Phase-17 pixel rules). Chunk AABBs for culling. **Acceptance:** 16×8×16-chunk
world renders ≥60 fps in the editor with the Statistics window showing cull rates; edits show
next frame; zero GL errors. **Status:** ☐

### V4 — Edit tools + script API
Editor: block brush (raycast the voxel grid — DDA traversal, engine-side
`VoxelVolume::RayCast`), LMB place / RMB break, palette picker panel, undoable as coalesced
edit runs (E7 merge keys). Scripts: `Voxels().RayCast/Get/Set` proxy (mirrors `Physics()`).
**Acceptance:** sculpt + undo/redo in editor; save/reload preserves edits; a script digs a
tunnel in Play. **Status:** ☐

### V5 — Collision (Jolt)
Per-dirty-chunk static `MeshShape` from the mesher's collision variant (no AO/UVs), swapped
into the `PhysicsWorld` on rebuild; character (J6) walks the world. **Acceptance:** headless —
capsule cast against a fixture chunk; in-app — J6 character walks/steps/jumps on voxel ground,
edits under its feet update collision within one frame. **Status:** ☐

### V6 — Generation + streaming
`VoxelGenerator` recipe (reflected; `Noise::Ridged2D`/`Perlin` heightmap + threshold caves +
surface blocks by height/slope) runs per ungenerated chunk in `ViewRadius` around the camera
on the JobSystem; unloaded chunks free meshes, keep edits. **Acceptance:** fly across a
64×64-chunk world with no hitch >2 ms on the main thread (profiler evidence); regenerating
with the same seed is deterministic (headless). **Status:** ☐

### V7 — Sample + phase acceptance
"ForgeBlocks" template: generated voxel island, palette (grass/dirt/stone/sand/wood/leaves),
character walking (J6), place/break in Play via a script, flow-made menu (U5). Packaged and
run on a clean path — the recorded demo is the phase's definition of done. **Status:** ☐

## 3. Parked (unlocks)

| Item | Unlock |
| --- | --- |
| Disk chunk paging / infinite worlds | a project outgrows ~128×32×128 chunks resident |
| Voxel light propagation (torch light) | a project needs underground lighting moods |
| Smooth voxels (marching cubes / dual contouring) | a terrain-sculpt use case asks for it |
| Transparent blocks pass (water/glass sorting) | first project that needs glass/water blocks |

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/17-phase18-voxel-plan.md` in
> `C:\dev\Cosmic`. Read §0–§1 first. Reuse `MeshData` (E15), the JobSystem main-thread-GL
> rule, the S12 render queue, and the E7 undo patterns — this phase adds no new
> infrastructure categories. Meshing/storage/gen are headless-tested. Compat gate for shipped
> apps; roadmap cmake recipe; no git writes. Finish with Acceptance + status banner.
