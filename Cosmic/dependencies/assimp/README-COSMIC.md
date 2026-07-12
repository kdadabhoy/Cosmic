# Open Asset Import Library — assimp (vendored)

Cosmic vendors [assimp](https://github.com/assimp/assimp) as the gated mesh-import
backend behind `assets/MeshImport` (E16 seam; flipped ON by Phase 20 A1).

- **Upstream:** https://github.com/assimp/assimp
- **Pinned tag:** `v5.4.3`  (commit `c35200e38ea8f058812b83de2ef32c6093b0ece2`)
- **License:** BSD-3-Clause (see `LICENSE`)
- **Vendored on:** 2026-07-12

## What is here (the trim)

The upstream release archive minus everything the engine never compiles:

- **Removed wholesale:** `test/` (114 MB of models + gtest suites), `doc/`,
  `samples/`, `tools/`, `port/`, `scripts/`, `packaging/`, `fuzz/`,
  `contrib/googletest`, `contrib/draco`, `contrib/android-cmake`.
- **`code/AssetLib/` trimmed to the five importers Cosmic enables:**
  `FBX`, `Obj`, `STL`, `Ply`, `Collada` (+ `STEPParser`, which the core source
  list compiles unconditionally, and `Step/STEPFile.h`, which STEPParser
  includes). Every other loader directory is deleted — safe because `ADD_ASSIMP_IMPORTER`/`ADD_ASSIMP_EXPORTER` only
  reference a loader's sources when its CMake flag enables it, and
  `ImporterRegistry.cpp` gates each `#include` behind the matching
  `ASSIMP_BUILD_NO_*_IMPORTER` define.
- **Kept:** `code/` common core, `include/`, `cmake-modules/`, and the contrib
  pieces the core list compiles unconditionally or the kept importers need
  (`zlib`, `unzip`, `pugixml` (Collada), `utf8cpp` (FBX), `clipper`, `poly2tri`,
  `openddlparser`, `Open3DGC`, `rapidjson`, `zip`, `stb`, `tinyusdz` stub).

## Build configuration (see `Cosmic/CMakeLists.txt`)

Unlike Jolt (curated CMakeLists), assimp is built through its **own** CMake with
options set before `add_subdirectory` (its `cmake_minimum_required` is 3.22, so
CMP0077 honors normal variables):

- Importers: `ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF` +
  `FBX/OBJ/STL/COLLADA/PLY` ON — glTF stays on the engine's cgltf path.
- Exporters OFF (`ASSIMP_NO_EXPORT=ON`), tests/tools/samples/install OFF,
  `ASSIMP_BUILD_ZLIB=ON` (vendored zlib — no system dep),
  `ASSIMP_WARNINGS_AS_ERRORS=OFF`, `ASSIMP_IGNORE_GIT_HASH=ON`.
- One **static** library linked **PRIVATE** into `Cosmic.dll` (the Jolt
  firewall pattern): only `assets/MeshImport.cpp` includes assimp headers, so
  the types never leak to game/project DLLs.
- `COSMIC_WITH_ASSIMP` is the engine-side gate define — default ON since A1;
  set `-DCOSMIC_WITH_ASSIMP=OFF` at configure time to fall back to OBJ-only.

## Local patches (re-apply on update)

- Root `CMakeLists.txt`: `ADD_COMPILE_OPTIONS(/source-charset:utf-8)` commented
  out (marked `[COSMIC PATCH]`) — Cosmic passes `/utf-8` globally and MSVC
  rejects the pair as incompatible (D8016).

## Updating

1. Download the new release archive, apply the same trim (list above).
2. Update the **pinned tag / commit / date** here.
3. Reconfigure CMake, rebuild, run `CosmicTests` — `test_meshimport.cpp`'s
   assimp-gated cases are the guard.
