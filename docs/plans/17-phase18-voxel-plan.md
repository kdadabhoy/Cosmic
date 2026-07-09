# Phase 18 Plan — Voxel Worlds (blocky 3D games)

> **STATUS 2026-07-08 (UNcommitted) — engine + editor foundation CODE-COMPLETE.**
> All seven work orders landed. New engine module `Cosmic/src/voxel/` (`VoxelVolume`
> chunk store, `BlockPalette`, `VoxelMesher` culled+greedy, `VoxelGenerator`,
> `VoxelRender` atlas/recipe runtime); `VoxelVolumeComponent` (reflected, flattened
> recipe) + `Scene::SyncVoxelVolumes` (JobSystem meshing, streaming generation,
> S12-queue chunk draws); script `Voxels()` proxy (RayCast/Get/Set/Break/Place);
> per-chunk static Jolt `MeshShape` collision in `ScenePhysics` (rebuilt on edit);
> Starforge **Voxels panel** (palette picker + brush toggle + Regenerate/Clear) +
> undoable viewport **place/break brush** (`Commands::VoxelEdit`, coalesced) + World▸
> Voxel Volume menu; template **VoxelDigger** script + **ForgeBlocks** sample builder
> (baked island `.cvox` + WalkController character + camera-child digger + HUD) offered
> via the homescreen "Voxel Sample" button. Build green Debug 5 projects **zero
> warnings**, `CosmicTests` **262/262** (241→262, +21 voxel/collision); compat gate held
> (no shipped app attaches a VoxelVolumeComponent → `SyncVoxelVolumes` no-ops). REMAINING
> = the user's on-GPU acceptance + recorded ForgeBlocks demo (V7 DoD), a few documented
> follow-ups (per-vertex AO baking — the shared MeshVertex layout has no colour channel;
> streaming chunk UNLOAD / disk paging = the parked "infinite worlds" unlock; image-atlas
> greedy repeat; a `.cflow` title menu for the sample).
>
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
round-trip byte-identical; palette JSON round-trip. **Status:** ✅ 2026-07-08 — `voxel/VoxelVolume` (32³ chunks, `x>>5`/`x&31` coord math, lazy chunks, seam-aware dirty), `voxel/BlockPalette` (+`.cpal` JSON, default grass/dirt/stone/sand/wood/leaves), `.cvox` chunked-RLE (sorted → byte-identical re-save). Tests in `test_voxel.cpp`.

### V2 — Mesher (culled → greedy)
`VoxelMesher::BuildChunk(volume, chunkCoord, palette) -> MeshData` — v1 culled interior faces
w/ per-face atlas UVs + baked AO corners (cheap voxel AO); v2 (same work order if time, else
split) greedy merging. **Acceptance:** headless — known 3-block fixture produces exact
vertex/index counts; greedy ≤ culled vertex count on random volumes; normals unit; a
1-chunk solid cube = 6 quads greedy. **Status:** ✅ 2026-07-08 — `voxel/VoxelMesher::BuildChunk` (shared greedy-slice builder, `allowMerge`/`mergeByBlock`/`withUV` flags → Culled/Greedy/Collision) into `MeshData` (E15, reused). Programmatic face defs (u×v=n) → winding verified against the stored normal in-test. `BuildCollision` merges by solidity only. `VertexAO` helper tested (bake awaits a voxel vertex-colour path). Tests: 3-in-a-row=14 culled quads, greedy row=6, solid cube=6, greedy≤culled.

### V3 — Render integration
`Scene::SyncVoxelVolumes` builds/uploads dirty chunk meshes (JobSystem workers mesh, main
thread uploads ≤ N/frame budget); chunks submit through the S12 Renderer3D queue (frustum
culling free) with one atlas material (PBR-lite: albedo atlas + roughness constant; point
sampling per Phase-17 pixel rules). Chunk AABBs for culling. **Acceptance:** 16×8×16-chunk
world renders ≥60 fps in the editor with the Statistics window showing cull rates; edits show
next frame; zero GL errors. **Status:** ✅ 2026-07-08 — `VoxelVolumeComponent` (reflected; runtime `Ref<VoxelVolume>`/`Ref<BlockPalette>`/`Ref<VoxelRenderData>`), `Scene::SyncVoxelVolumes(cameraPos)` (JobSystem workers build `MeshData`, main-thread upload ≤24 chunks/call), `voxel/VoxelRender` procedural solid-colour atlas + PBR-lite material, chunk meshes submitted through `SubmitOpaqueMeshes` → the S12 queue (per-chunk AABB frustum cull free), wired into `OnRender3D` + `BuildRenderDesc`. On-GPU fps/cull evidence = user-run.

### V4 — Edit tools + script API
Editor: block brush (raycast the voxel grid — DDA traversal, engine-side
`VoxelVolume::RayCast`), LMB place / RMB break, palette picker panel, undoable as coalesced
edit runs (E7 merge keys). Scripts: `Voxels().RayCast/Get/Set` proxy (mirrors `Physics()`).
**Acceptance:** sculpt + undo/redo in editor; save/reload preserves edits; a script digs a
tunnel in Play. **Status:** ✅ 2026-07-08 — engine `VoxelVolume::RayCast` (Amanatides-Woo DDA,
world ray → hit voxel + face normal + place cell; headless-tested). Script `Voxels()` proxy on
`ScriptableEntity` (RayCast/Get/Set/Break/Place, mirrors `Physics()`). Editor: Starforge **Voxels
panel** (block-swatch palette picker, Edit toggle, Reach, Greedy toggle, Regenerate/Clear/Save-
`.cvox`) + viewport **brush** (LMB place / RMB break via camera-unproject ray, CAD-nav-safe) →
`Commands::VoxelEdit` (coalesced per click, full undo/redo). On-GPU sculpt pass = user-run.

### V5 — Collision (Jolt)
Per-dirty-chunk static `MeshShape` from the mesher's collision variant (no AO/UVs), swapped
into the `PhysicsWorld` on rebuild; character (J6) walks the world. **Acceptance:** headless —
capsule cast against a fixture chunk; in-app — J6 character walks/steps/jumps on voxel ground,
edits under its feet update collision within one frame. **Status:** ✅ 2026-07-08 — `ScenePhysics`
builds one static `CollisionShapeDesc::Kind::Mesh` body per resident chunk (`BuildCollision`
variant, world-baked verts), rebuilt each fixed step from `VoxelRenderData::CollisionDirty`
(populated when a chunk re-meshes). Headless `test_voxel_collision.cpp`: ray hits the voxel floor,
a dynamic box rests on it, digging a column drops the ray through within one step. J6 walk on
voxel ground = user-run (the character controller collides with the Static-layer chunk bodies).

### V6 — Generation + streaming
`VoxelGenerator` recipe (reflected; `Noise::Ridged2D`/`Perlin` heightmap + threshold caves +
surface blocks by height/slope) runs per ungenerated chunk in `ViewRadius` around the camera
on the JobSystem; unloaded chunks free meshes, keep edits. **Acceptance:** fly across a
64×64-chunk world with no hitch >2 ms on the main thread (profiler evidence); regenerating
with the same seed is deterministic (headless). **Status:** ✅ 2026-07-08 — `voxel/VoxelGenerator`
(reflected recipe flattened onto `VoxelVolumeComponent`; `Noise` fBm/Ridged heightmap + `Fbm3D`
threshold caves + surface/dirt/stone/sand by height). `SyncVoxelVolumes` streams ungenerated
chunks in `ViewRadius` nearest-first (budget/frame, main-thread — concurrent chunk-map insert is
unsafe, so generation is main-thread + budgeted; meshing is the parallel JobSystem cost), re-culls
resident-neighbour seams. Determinism headless-tested (same seed → byte-identical `.cvox`). Chunk
UNLOAD / disk paging = the parked "infinite worlds" unlock (bounded worlds keep every chunk
resident); the ForgeBlocks sample bakes a finite island so nothing streams unbounded.

### V7 — Sample + phase acceptance
"ForgeBlocks" template: generated voxel island, palette (grass/dirt/stone/sand/wood/leaves),
character walking (J6), place/break in Play via a script, flow-made menu (U5). Packaged and
run on a clean path — the recorded demo is the phase's definition of done. **Status:** ◑ 2026-07-08 —
template **VoxelDigger** script (place/break via `Voxels()`) + registration shipped in the scaffold;
`StarforgeApp::BuildForgeBlocks` scaffolds the C++ project, bakes an 8×2×8-chunk island to
`project://voxels/world.cvox`, and authors the scene (voxel world loading the `.cvox` for
play-start collision, a `CharacterController`+`WalkController` player, a camera child with
`VoxelDigger`, sun/environment, a `Canvas`+`UiText` HUD), offered via the homescreen "Voxel
Sample" button. REMAINING (user-run): Build Scripts + Play the sample, the packaged clean-path
**recorded demo** (the DoD), and an optional `.cflow` title menu (U5) over the HUD.

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
