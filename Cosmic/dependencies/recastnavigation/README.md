# Recast & Detour Navigation (vendored)

Cosmic vendors [Recast & Detour](https://github.com/recastnavigation/recastnavigation)
for the Phase 26 navigation-mesh / agent-crowd tier (N1).

- **Upstream:** https://github.com/recastnavigation/recastnavigation
- **Pinned tag:** `v1.6.0`  (commit `6dc1667f580357e8a2154c28b7867bea7e8ad3a7`)
- **License:** Zlib (see `License.txt`)
- **Vendored on:** 2026-07-13

## What is here

Only the four **library** module source trees are vendored:

| Module | Purpose |
| --- | --- |
| `Recast/` | Voxel-rasterize input triangles → walkable heightfield → contour → poly mesh + detail mesh (the *bake*). |
| `Detour/` | Runtime navmesh (`dtNavMesh`) + query (`dtNavMeshQuery`): path find, raycast, nearest/random point, serialize. |
| `DetourCrowd/` | `dtCrowd` — many agents with local steering / obstacle avoidance over a `dtNavMesh`. |
| `DetourTileCache/` | Tiled, run-time-rebuildable navmesh compression (dirty-tile rebuild). |

**Not** vendored: the upstream `RecastDemo/` (SDL + OpenGL sample), `Tests/`,
`Docs/`, `DebugUtils/`, `Doxyfile`, the top-level `CMakeLists.txt`, or the CMake
install/package protocol. The engine compiles the sources directly via this
directory's own `CMakeLists.txt` (see it for the full rationale), so none of
Recast's own CMake option protocol is used. Nav debug draw is done through the
engine's `Renderer3D` line/triangle batch, so upstream `DebugUtils` (which needs
a draw backend) is deliberately excluded.

## Build configuration (see `CMakeLists.txt`)

- One **static** library `RecastNavigation`, linked **PRIVATE** into `Cosmic.dll`
  (no new runtime DLL — the Jolt/assimp firewall). Recast/Detour headers never
  appear in a public engine header, so game/project DLLs never see or link them:
  the service lives behind `nav/NavWorld` (pimpl), whose public header includes
  zero Recast types.
- `/std:c++20`, MSVC `/MD(d)` (inherited from the root toolchain), warnings
  silenced on the vendored target only (`/W0`).
- 32-bit poly refs (`DT_POLYREF64` off — the default); documented flip point in
  the CMake comment.
- No `/fp:fast` — the engine's default precise float codegen keeps the N4
  two-run determinism proof reproducible.

## Updating

1. Re-run the sparse checkout of the new tag's four module subtrees
   (`Recast/`, `Detour/`, `DetourCrowd/`, `DetourTileCache/` — each `Include/` +
   `Source/`) plus `License.txt` over this directory.
2. Update the **pinned tag / commit / date** above.
3. Reconfigure CMake (the glob is `CONFIGURE_DEPENDS`, but a clean reconfigure is
   safest) and run `CosmicTests` — `test_nav_*` are the guard.
