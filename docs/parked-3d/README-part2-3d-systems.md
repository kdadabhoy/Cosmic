# Root README Part II — the 3D system sections (moved verbatim)

> **PARKED 3D — not on the trunk.** This chapter documents code that lives only on the `engine-3d` branch (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`). The 2D trunk (`main`) no longer builds or ships it (D-PURGE, 2026-09-18). Kept for when 3D resumes.

> Moved here 2026-09-20 by App Platform AP-D1 from the root `README.md` Part II (sections 30, 35 and 40).
> Text is verbatim; line references were true at Phase 29 and describe the `engine-3d` tree. The live root
> README keeps a one-paragraph pointer in §30.

## README Part II §30 — "The 2D partition" (verbatim)

### The 2D partition

Since Phase 29 the same source tree builds **two engines**. The 2D configuration is produced by
`list(FILTER … EXCLUDE REGEX …)` calls in `Cosmic/CMakeLists.txt` (lines 178–210) — one per row of
the partition table, in table order, so the two stay auditable against each other. Nothing is
deleted and no file differs between the branches; the difference is entirely which files reach the
compiler.

| Excluded in the 2D build | What goes |
| --- | --- |
| Whole subsystem trees | `terrain/`, `voxel/`, `water/`, `nav/`, `particles/` |
| `renderer/` | `Renderer3D`, `EnvironmentMap`, `ShadowMap`, `CoverageCapture`, `InstanceSet` |
| `graphics/` | `Model`, `Skeleton`, `AnimationClip`, `CgltfImpl` |
| `camera/` | `NavigationCube` (its `Render()` issues direct `Renderer3D` calls) |
| `scene/` | `Scene3D`, `Components3D`, `SceneNav`, `ScenePicker`, `WorldSystemRecipes` |
| `reflect/` | `TypeRegistry3D` |
| `assets/` | `MeshImport.cpp` (the header stays, so the fences read the same on both) |
| Vendored | **assimp** (159 TUs) and **recastnavigation** (26) are never configured |

`physics/` is **shared and unfenced** — Jolt ships on both configurations. `SceneRenderer` and
`PostProcessStack` ship on both too: a 2D frame runs the same HDR → tonemap → overlay spine. The
authoritative exclusion table, the classification rule for new code, and the recorded build times
are in [`docs/systems/build-2d-3d-split.md`](docs/systems/build-2d-3d-split.md); the client-facing
summary is [§1.6](#16-the-two-engine-configurations).


## README Part II §30 — the 3D lines of the source file map (verbatim)

```
│   ├── Renderer3D.h/.cpp         3D  Sorted queue: submit → cull → sort → auto-instance → flush
│   ├── ShadowMap.*  EnvironmentMap.*  CoverageCapture.*  InstanceSet.*                      3D
│   └── Model.*  Skeleton.*  AnimationClip.*  CgltfImpl.cpp                                  3D
│   ├── Scene3D.cpp               3D  the 3D half of Scene (split out in Phase 29 W5)
│   ├── Components3D.h            3D  mesh renderer, lights, environment, animator, world systems
│   ├── WorldSystemRecipes.*      3D  scene-authored terrain/water/emitter → spec
│   ├── SceneNav.*  ScenePicker.*  3D  navmesh bake + .cnav; 3D viewport picking
├── nav/                          3D  NavWorld.* (Recast/Detour behind a pimpl), NavTypes.h
├── terrain/                      3D  Terrain.* — heightmap composition, quadtree LOD
├── water/                        3D  Water.*, GerstnerWave.h, Presets.h
├── particles/                    3D  ParticleSystem.* (GPU compute), Presets.h
├── voxel/                        3D  VoxelVolume, BlockPalette, VoxelMesher/Generator/Render
```

## README Part II §35 DG-6 — the Renderer3D nodes and edges (verbatim)

```mermaid
    class Renderer3D {
        <<static — 3D build only>>
        +DrawMesh()
        +Flush()
        -RenderQueue m_Queue
    }
    SceneRenderer ..> Renderer3D : routes opaque/transparent
    Renderer3D ..> RenderCommand
```

## README Part II §40 — the build-flag paragraph (verbatim)

Two flags shape what gets built. **`COSMIC_2D_ONLY`** selects the engine configuration — it filters
the source glob, skips the assimp and recastnavigation dependencies entirely, and changes the
project skip-list; it is the only engine define exported `PUBLIC`, because public headers carry
`#ifndef COSMIC_2D_ONLY` fences that must resolve identically in the engine and in every consumer.
**`COSMIC_BUILD_ENGINE_ONLY`** skips the project scanner. Everything else — `COSMIC_WITH_JOLT`,
`COSMIC_WITH_ASSIMP`, `COSMIC_BUILD_TESTS`, `COSMIC_BUILD_RENDER_TESTS`, `COSMIC_SKIP_PROJECTS` — is
a narrower switch on one subsystem or target.
