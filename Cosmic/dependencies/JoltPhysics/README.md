# Jolt Physics (vendored)

Cosmic vendors the [Jolt Physics](https://github.com/jrouwe/JoltPhysics) library
for the Phase 15 rigid-body / character / query tier.

- **Upstream:** https://github.com/jrouwe/JoltPhysics
- **Pinned tag:** `v5.5.0`  (commit `23dadd0e603f1b321142d4c74df07fce85064989`)
- **License:** MIT (see `LICENSE`)
- **Vendored on:** 2026-07-04

## What is here

Only the library source tree (`Jolt/`) is vendored — **not** the upstream
`Build/`, `Samples/`, `TestFramework/`, `HelloWorld/`, `PerformanceTest/`,
`UnitTests/`, or asset directories. The engine compiles the sources directly via
this directory's own `CMakeLists.txt` (see it for the full rationale), so none of
Jolt's own CMake option protocol is used.

## Build configuration (see `CMakeLists.txt`)

- One **static** library `Jolt`, linked **PRIVATE** into `Cosmic.dll` (no new
  runtime DLL — the assimp-style firewall). Jolt headers never appear in a public
  engine header, so game/project DLLs never see or link Jolt.
- `/std:c++20`, MSVC `/MD(d)` (inherited from the root toolchain), warnings
  silenced on the vendored target only.
- `JPH_CROSS_PLATFORM_DETERMINISTIC` — floating-point determinism for the J8
  two-run bit-match proof.
- SSE4.1/SSE4.2 at the **define** level; `/arch` stays at the MSVC x64 baseline
  (SSE2) so nothing propagates AVX codegen into the rest of the engine. See the
  CMake comment for the AVX2 flip-on note.
- `JPH_ENABLE_ASSERTS` + `JPH_DEBUG_RENDERER` in Debug only.

## Updating

1. Re-run the sparse clone of the new tag's `Jolt/` subtree over this directory.
2. Update the **pinned tag / commit / date** above.
3. Reconfigure CMake (the glob is `CONFIGURE_DEPENDS`, but a clean reconfigure is
   safest) and run `CosmicTests` — `test_physics_*` are the guard.
