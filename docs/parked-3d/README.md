# Parked 3D documentation

> **PARKED 3D — not on the trunk.** Everything in this directory documents code that lives only on the `engine-3d` branch (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`). The 2D trunk (`main`) no longer builds or ships it (D-PURGE, 2026-09-18). Kept for when 3D resumes.

**Why it is here.** On 2026-09-18 the App Platform campaign decided (D-PURGE, D-DOCS) that `main` is the 2D-only
trunk: `Renderer3D`, terrain, water, particles, voxels, navigation, skeletal animation and model import, their
vendored dependencies (assimp, recastnavigation, cgltf), their tests, goldens, editor panels and template scripts
were deleted from `main` by AP-05. The full pre-purge tree is preserved, byte for byte, at:

| What | Value |
| --- | --- |
| Branch | `engine-3d` |
| Commit | `0e8894b8540029ac57e68540aa9774cf5cf77ebe` |
| Tag | `cosmic-pre-2d-2026-09-16` |

Every file below carries the same banner at line 3. Nothing here is maintained; `file:line` references describe the
`engine-3d` tree, not `main`. Live documentation may link into this directory only with the visible label
"(parked 3D)" (checked by `evidence/AP-D1/check_parked.py`). The link checker treats this tier as warn-only.

## Index

| Parked chapter | Was | What it documents |
| --- | --- | --- |
| [`guide/rendering-3d.md`](guide/rendering-3d.md) | `docs/guide/rendering-3d.md` | `Renderer3D` submit/cull/sort/instance queue, meshes, models, LOD, the material-read-at-flush rule |
| [`guide/lighting-and-environment.md`](guide/lighting-and-environment.md) | `docs/guide/lighting-and-environment.md` | The full 3D frame: sun/point lights, PBR + IBL, sky modes, time of day, shadows, coverage capture, the post chain (the 2D part lives on as [`../guide/lighting-2d.md`](../guide/lighting-2d.md)) |
| [`guide/world-systems.md`](guide/world-systems.md) | `docs/guide/world-systems.md` | Terrain, water, particles as scene content |
| [`guide/voxels.md`](guide/voxels.md) | `docs/guide/voxels.md` | Voxel volumes, palettes, meshing, generation, editing |
| [`guide/navigation-and-ai.md`](guide/navigation-and-ai.md) | `docs/guide/navigation-and-ai.md` | Recast/Detour navmesh bake, `.cnav`, agents, the script `Nav()` proxy |
| [`guide/animation.md`](guide/animation.md) | `docs/guide/animation.md` | Skeletons, clips, GPU skinning, `AnimatorComponent`, sockets, the Animation Editor |
| [`reference/rendering-3d.md`](reference/rendering-3d.md) | `docs/reference/rendering-3d.md` | API skeleton: `Renderer3D`, `Model`, `InstanceSet` (`graphics/Mesh.h` still exists on `main` and is now routed to [`../reference/graphics-resources.md`](../reference/graphics-resources.md)) |
| [`reference/world-systems.md`](reference/world-systems.md) | `docs/reference/world-systems.md` | API skeleton: `Terrain`, `Water` + `GerstnerWave`, `ParticleEmitter`/`RibbonEmitter` + `Presets` |
| [`reference/rendering-pipeline-3d.md`](reference/rendering-pipeline-3d.md) | the 3D half of `docs/reference/rendering-pipeline.md` | API skeleton: `EnvironmentMap`, `ShadowMap`, `CoverageCapture` |
| [`systems/rendering-3d.md`](systems/rendering-3d.md) | `docs/systems/rendering-3d.md` | How the sorted queue works |
| [`systems/rendering-pipeline-3d.md`](systems/rendering-pipeline-3d.md) | the 3D half of `docs/systems/rendering-pipeline.md` | Shadow, reflection, environment and sky passes; lighting theory |
| [`systems/cameras-navigation-3d.md`](systems/cameras-navigation-3d.md) | the 3D half of `docs/systems/cameras-navigation.md` | CAD orbit/fly navigation, `NavigationCube`, `ScenePicker` |
| [`systems/terrain.md`](systems/terrain.md) | `docs/systems/terrain.md` | Heightmap composition, quadtree LOD, splat/triplanar materials |
| [`systems/water.md`](systems/water.md) | `docs/systems/water.md` | Gerstner water, reflections, buoyancy queries |
| [`systems/particles.md`](systems/particles.md) | `docs/systems/particles.md` | GPU-compute particles, ribbons, presets |
| [`systems/build-2d-3d-split.md`](systems/build-2d-3d-split.md) | `docs/systems/build-2d-3d-split.md` | The Phase 29 two-configuration build (`COSMIC_2D_ONLY` filter, fences, `engine-2d` branch) — superseded by the trunk policy |
| [`README-part2-3d-systems.md`](README-part2-3d-systems.md) | root `README.md` Part II §30/§35/§40 | The 2D-partition table, the 3D lines of the source map, DG-6’s `Renderer3D` nodes, the build-flag paragraph |

Archived 3D *plans* (Phases 18, 20, 24, 26, 28) are in [`../plans/archive/`](../plans/archive/README.md); the
3D rows of the feature matrix are under [`../plans/FEATURE-MATRIX.md`](../plans/FEATURE-MATRIX.md) "Parked (engine-3d)".

## How to resume 3D

1. `git fetch --tags` and start from the preserved tree: `git switch -c 3d-resume cosmic-pre-2d-2026-09-16`
   (or branch from `engine-3d`). Do **not** cherry-pick 3D source back onto `main`; the trunk policy is one
   engine, 2D-only.
2. Configure that tree as it documents itself: the pre-purge `README.md` §1.6 and
   [`systems/build-2d-3d-split.md`](systems/build-2d-3d-split.md) describe `COSMIC_2D_ONLY=OFF`, `build_3d.bat`
   and the vendored assimp/recastnavigation.
3. Bring the 2D trunk’s later work across by merging `main` into the 3D branch, not the other way round; the
   `COSMIC_2D_ONLY` fences were removed from `main` (AP-05 Part B, `evidence/AP-05/unfence.py`), so expect
   conflicts in the files that script rewrote (listed in `evidence/AP-05/report.md`).
4. Move the chapters here back to their `Was` paths and drop the banners; the coverage manifest rows that
   pointed at them are the `../parked-3d/…` rows in `docs/reference/README.md`.
