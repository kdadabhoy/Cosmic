# AP-05 part A execution report — 2026-09-19 (purge the filtered 3D source, deps, tests, goldens and editor TUs from `main`)

Only AP-05 **Part A** was executed, alone on `main` in `C:\dev\Cosmic` (serial WO; no worktree, no
other session in this tree). Everything the 2D build already excluded is now physically gone: the
five engine trees, the 30 engine files of the former `list(FILTER)` block, the three vendored
dependencies (674 files), the 22 shaders and 1 model only the deleted code loaded, the 22 3D-only
test TUs plus `render_3d.cpp` and its 9 goldens, the three already-excluded editor TUs, the two 3D
template scripts and `build_3d.bat` — **790 tracked files, 298,289 lines**. The CMake partition
machinery, the 3D preset, the 27 stale manifest rows and the checker's 3D logic went with them.
**Both configs rebuilt from a clean configure with 0 warnings; `CosmicTests --count` is 454 before
and after in both configs (the deleted TUs contributed 0 cases to the 2D suite — reconciled below);
the 8 retained 2D goldens are byte-identical; both audits exit 0; the B06 grep/file oracle
(`Verify-AP05Purge.ps1`) PASSES in part-A mode (5/5) through the runner in Release and Debug and,
as designed, FAILS strict (3 of 5) until part B removes the 240 remaining fences.** One pre-existing
test-hygiene defect surfaced (**KI-57**: `test_wo06.cpp` D01 poisons its own `%TEMP%` scratch —
not a purge effect, not fixed, outside this WO's files); with a clean per-run TEMP the retained
suites pass **454/454 Debug, 454/454 Release**. The fences themselves are untouched (part B).

## Scope and provenance

- Base `HEAD` at start: `747992702b00e5042ced9e43b3030bf93964fc9a` (the AP-00 follow-up; AP-00
  itself is `726ae35208ad7b1906350b13989840360bcae850`, already committed — nothing to commit first).
  `git status --short` at start: only the untracked root plan
  `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` and `recordings/`; both were never staged,
  moved or overwritten. The registered worktree `.claude/worktrees/epic-clarke-338e7f` and the
  concurrent lane worktree `C:\dev\Cosmic-ap-p1` were not touched; `engine-3d` and the
  `cosmic-pre-2d-2026-09-16` tag were not touched (everything deleted here is preserved there).
- Prompt vs packet: no conflict found. The prompt's "Delete every docs/reference/README.md row whose
  header no longer exists (do NOT touch any other row)" was followed literally — the prose above the
  table (`docs/reference/README.md:95-127`, which still describes the markers and the CMake filter) is
  left for AP-D1.
- Commit: authored and committed as `kdadabhoy <kdadabhoy28@gmail.com>`, no `Co-Authored-By`, no AI
  trailer, no "Generated with" line; only explicit AP-05 paths staged. Not pushed.
- The session was interrupted once by an API usage limit (HTTP 429) after the deletions, edits, both
  builds and the audits; it resumed on the same tree (reconciled by `git status`: 806 status lines ==
  790 staged deletions + 12 modified + 4 untracked). Nothing was reset or restored; the baseline count
  and golden hashes were still in the session record and the pre-purge hashes were re-derived from
  `git show HEAD:` for `golden-hashes.txt` anyway.

## Toolchain and environment

- VS-bundled CMake `4.3.1-msvc1` (`C:\Program Files\Microsoft Visual Studio\18\Community\...\CMake\bin\cmake.exe`,
  not on PATH), generator `Visual Studio 18 2026`, `-A x64`, MSVC `19.51.36248.0`, MSBuild `18.7.8`.
- Every configure passed `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON`; the post-purge clean
  configure added `-DCOSMIC_BUILD_RENDER_TESTS=ON` so `tests/render/CMakeLists.txt` (which lost
  `render_3d.cpp`) is proven to configure and compile. Effective cache: `COSMIC_2D_ONLY=ON`,
  `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=ON`, `COSMIC_WITH_JOLT=ON`,
  `COSMIC_SKIP_PROJECTS=AnalysisSample`; `COSMIC_WITH_ASSIMP` no longer exists (`build-excerpts.txt`).
- Reference machine DESKTOP-SEOA4BT (Windows 11 26200, Ryzen 7 7800X3D, RTX 5070 Ti) — the runner's
  `results.json` records the environment. Windows PowerShell 5.1 for every script.
## What was built / changed

**Deleted (`git rm`, 790 tracked files, 298,289 lines — full list in `deleted-paths.txt`):**

| Section-9 group | Paths | Files |
| --- | --- | --- |
| Engine trees | `Cosmic/src/{terrain,voxel,water,nav,particles}/` | 22 |
| Engine files | `renderer/{Renderer3D,EnvironmentMap,ShadowMap,CoverageCapture,InstanceSet}.{h,cpp}`, `graphics/{Model,Skeleton,AnimationClip}.{h,cpp}` + `CgltfImpl.cpp`, `camera/NavigationCube.{h,cpp}`, `scene/{Scene3D.cpp,Components3D.h,SceneNav.{h,cpp},ScenePicker.{h,cpp},WorldSystemRecipes.{h,cpp}}`, `reflect/TypeRegistry3D.cpp`, `assets/MeshImport.{h,cpp}` — exactly the former `list(FILTER)` block | 30 |
| Vendored deps | `Cosmic/dependencies/recastnavigation/` (49), `assimp/` (624), `cgltf/` (1) — consumer greps run before each `git rm`, in `dependency-consumer-greps.txt`: **no 2D consumer** for any of the three | 674 |
| Shaders/assets | `Cosmic/assets/models/Duck.glb`; `Cosmic/assets/shaders/{BrdfLut,DemoChecker3D,EnvSky,EquirectToCube,InfiniteGrid,IrradianceConvolve,Line3D,Mesh3D,PBRInstanced,ParticleBillboards,ParticleUpdate,PrefilterEnv,Ribbon,ShadowDepth,ShadowDepthInstanced,ShadowDepthSkinned,SkyDetail,Skybox,SnowAccum,Terrain,TerrainDepth,Water}.glsl` — every load site (`Shader::Create("assets/shaders/...")` / `engine://...`) of each is in a deleted TU | 23 |
| Tests | the 22 TUs of the `if(NOT COSMIC_2D_ONLY)` block (`test_phase10_world, test_flycamera, test_frustum, test_presets, test_particle_noise, test_render_queue, test_primitives, test_meshimport, test_animation, test_sockets, test_crossfade, test_material_slots, test_worldsystems, test_physics_terrain, test_nav_world, test_nav_bake, test_nav_agents, test_voxel, test_voxel_collision, test_forgeisle_content, test_render_desc, test_components3d_registry`), `tests/render/render_3d.cpp`, goldens `instancing, mesh_pbr, outline, particles, postchain_off, postchain_on, sky_ibl, terrain, water` (each referenced only by `render_3d.cpp`; `instancing2d.png` and the other seven 2D goldens kept) | 32 |
| Editor | `Projects/Starforge/src/panels/{WorldSystemsPanel,VoxelPanel}.{h,cpp}`, `editors/AnimationEditor.{h,cpp}` | 6 |
| Template scripts | `Projects/Starforge/assets/templates/src/scripts/{VoxelDigger,NavCritter}.h` | 2 |
| Build/scripts | `build_3d.bat` | 1 |

**Edited (12 files, +61/-407):**

- `Cosmic/CMakeLists.txt`: the `if(NOT COSMIC_2D_ONLY)` recast block, the `COSMIC_WITH_ASSIMP` option +
  block, the whole `if(COSMIC_2D_ONLY) list(FILTER ...)` partition block, `dependencies/cgltf` from the
  PRIVATE include dirs, the `RecastNavigation` link genex and the assimp link/define block. Kept
  untouched: `option(COSMIC_2D_ONLY ... ON)`, the PUBLIC `COSMIC_2D_ONLY` define, `COSMIC_WITH_JOLT` and
  its `list(FILTER)` (physics is dimension-agnostic and stays). Root `CMakeLists.txt` (reject-OFF gate)
  untouched.
- `tests/CMakeLists.txt`: the 3D-only TU block removed (+ the comment that pointed at
  `test_physics_terrain`); `tests/render/CMakeLists.txt`: `render_3d.cpp` removed.
- `Projects/Starforge/CMakeLists.txt`: the three `list(FILTER STARFORGE_SOURCES ...)` exclusions removed;
  its `option(COSMIC_2D_ONLY ... ON)` stays, with a comment saying why.
- `CMakePresets.json`: the 3D `"default"` preset deleted; `"2d"` is the only configure preset (name
  kept), description updated.
- `Projects/Starforge/assets/templates/src/Module.cpp`: the three `#ifndef COSMIC_2D_ONLY` blocks
  (includes, `CS_SCRIPT(VoxelDigger)`, `CS_SYSTEM(NavCritter)`) removed; `tests/test_template_scripts.cpp`:
  the fenced `NavCritter.h` include and the N5 case removed.
- `Cosmic/src/Cosmic.h:91` and `tests/test_s5_navigation.cpp:8,15` — see "Contract deviations".
- `docs/reference/README.md`: exactly the 27 rows whose header no longer exists deleted
  (`renderer/{Renderer3D,InstanceSet,EnvironmentMap,ShadowMap,CoverageCapture}.h`, `graphics/{Model,Skeleton,AnimationClip}.h`,
  `terrain/Terrain.h`, `water/{Water,Presets,GerstnerWave}.h`, `particles/{ParticleSystem,Presets}.h`,
  `scene/{WorldSystemRecipes,Components3D,ScenePicker,SceneNav}.h`, `camera/NavigationCube.h`,
  `assets/MeshImport.h`, `nav/{NavWorld,NavTypes}.h`, `voxel/{VoxelVolume,BlockPalette,VoxelMesher,VoxelGenerator,VoxelRender}.h`).
  146 -> 119 table rows (+2 footnote rows = 121 for the checker). No other row touched.
- `tests/check_docs_coverage.ps1`: the CMake `list(FILTER)` parse (which exited 1 when no rules
  existed), the fence-aware `Cosmic.h` walk and the 3D / 3D-plus marker mode (old failure mode 4) removed;
  the include walk is now flat (an include whose file is absent is skipped, so leftover fences around
  deleted includes are harmless until part B). Kept: unlisted-header, stale-row, malformed-row,
  missing-chapter and strict-mode failures, the `$internalHeaders` and footnote-row lists, pure ASCII.
- `docs/plans/2d-stability-2026-09-16/contracts/known-issues.md`: KI-39 disposition -> `fix landed 726ae35...`
  (AP-00 had not written it); new **KI-57** appended.
- New: `tests/acceptance/fixtures/Verify-AP05Purge.ps1`, `tests/acceptance/manifests/ap05-purge.manifest.json`,
  this evidence directory.
## Acceptance-case status

| ID | Result | Evidence |
| --- | --- | --- |
| B06-A (part-A mode, `-AllowFences`) | **PASSED** Release and Debug through `Run-Acceptance.ps1` — 5/5 checks (`paths-absent`, `build-files-clean`, `includes-of-deleted` PASS; `fence-uses` 240 and `identifiers` INFO) | `b06a-Release/results.json`, `b06a-Debug/results.json`, `verify-ap05-partA-excerpts.txt` |
| B06 strict (no `-AllowFences`) | **FAILS as designed** until part B: `fence-uses` 240, `includes-of-deleted` 71 fenced dead includes, `identifiers` 234 (150 fenced / 68 comment / 16 code) — 3 of 5 FAIL, exit 1 | `verify-ap05-strict-excerpts.txt` |
| Oracle self-test | re-creating one purged path (`Cosmic/src/nav/NavWorld.h`, a probe, removed again) flips `paths-absent` to FAIL (4/5, exit 1) — the oracle is not vacuous | this report, "Final-pass method" |
| Both configs 0 warnings | **PASS** — clean configure, Debug 0 / Release 0 (`'warning [A-Z]'` grep of the full MSBuild logs) | `build-excerpts.txt` |
| Test count reconciles | **PASS** — 454 = 454 - 0 (see "Measured numbers") | `test-counts.txt` |
| Retained goldens byte-identical | **PASS** — 8/8 SHA-256 equal to `git show HEAD:` | `golden-hashes.txt` |
| GL conformance audit | **PASS** exit 0, "clean" | `audit-gl-conformance.txt` |
| Docs coverage audit | **PASS** exit 0, "clean (123 public headers, 121 manifest rows, 6 skeleton chapters, 4 off-tier)" (151/148 before the purge) | `audit-docs-coverage.txt` |
| Retained unit suites | **PASS** 454/454 Debug, 454/454 Release, each with a clean per-run TEMP (see KI-57 for the direct-run trap) | `test-counts.txt` |

## Final-pass method

1. Baseline on the untouched tree: the existing `build/` reconfigured in place (`-DCOSMIC_2D_ONLY=ON
   -DCOSMIC_BUILD_TESTS=ON`; the cache already carried `COSMIC_BUILD_RENDER_TESTS=ON`), Debug built
   incrementally (0 warnings), `CosmicTests.exe --count` -> **454** (`--count --no-skip` -> 467); the 17
   goldens hashed (`golden-hashes.txt`; the pre side recomputed from `git show HEAD:` for the record).
2. A fence-aware scan (preprocessor-depth stack over every `#include` of a to-be-deleted header in every
   non-deleted file under `Cosmic/src`, `Projects`, `tests`, `Runtime`, `Cosmic/templates`) found **74
   includes inside excluded fences (safe), 2 unfenced (`Cosmic.h:91`, `test_s5_navigation.cpp:8`) and
   one compound condition** (`JoltBackend.cpp:1027`, `#if defined(JPH_DEBUG_RENDERER) && !defined(COSMIC_2D_ONLY)`,
   left for part B's exception list). The consumer greps for the three deps were run before their
   `git rm` (`dependency-consumer-greps.txt`).
3. Shader/asset scope: every `.../shaders/X.glsl` / `engine://` literal in the tree was mapped to the file
   that loads it; a shader was deleted only when every load site is in a deleted TU (`Duck.glb`'s two
   references are `Model.cpp`/`Model.h`). Word-level mentions (`Skybox`, `Terrain`, `Water` as component
   or identifier names) were not treated as references.
4. `git rm` of the groups, the 12 edits (exact single-occurrence replacements, CRLF preserved), `build/`
   deleted, clean configure + Debug + Release builds, counts, audits, hashes, fixture + manifest through
   the runner (Release and Debug, `-GoldenDir tests\render\goldens`, repository-local `-TempRoot`).
5. Retained suites: `CosmicTests.exe` (no filter) in Debug and Release, sequentially, each with
   `TEMP`/`TMP` pointed at a fresh scratch directory (the runner's own isolation, reproduced by hand) —
   the first direct Debug run had reused `%TEMP%\wo06` and failed D01 (-> KI-57).

## Defects found

- **KI-57** (registered before any rerun): `tests/test_wo06.cpp` D01 "pinned independent v1 and recorder
  exact storage" is not idempotent outside the runner — `Scratch()` (`:28-33`) never clears
  `<TEMP>/wo06/<name>`, the case's last step (`:213-215`) leaves `bad-version.bin` as
  `fallback/scene.bin`, and the next direct run fails at `:208` (`REQUIRE( p.Load(fallback.string()) )`).
  Failing-before: direct Debug run 453/454 (only D01), `-tc="WO-06 D01*"` twice -> 1/1 then 0/1;
  `rd /s /q %TEMP%\wo06` -> 1/1 again. Not a product defect (`DataPlayer::Load` behaves as specified) and
  not caused by the purge (the case touches no purged code); WO-06..WO-10 never saw it because the runner
  redirects `TEMP` per run. Not fixed here — `tests/test_wo06.cpp` is outside AP-05's ownership; disposition
  open, one-line fix recorded in the register.
- No crash, hang or data loss was found. No golden regenerated, no tolerance loosened.

## Measured numbers

- `CosmicTests.exe --count`: **454 before, 454 after** (Debug and Release identical; `--no-skip` 467/467).
  Reconciliation: the 22 deleted test TUs hold **167** `TEST_CASE`s (19+9+6+7+8+7+7+13+9+6+4+3+7+2+5+5+5+18+3+3+7+14)
  but sat under `if(NOT COSMIC_2D_ONLY)` and were **never compiled on this trunk** -> 0 cases in the 2D
  count; the removed `N5: NavCritter` case (`test_template_scripts.cpp`) and the two `NavigationCube` cases
  still in `test_s5_navigation.cpp` were `#ifndef COSMIC_2D_ONLY`-fenced -> 0; `render_3d.cpp` (8 cases) was
  fenced as a whole -> 0 in `CosmicRenderTests`. **454 - 0 = 454.** (`test-counts.txt`)
- Full suites with a clean per-run TEMP: Debug 454/454 passed (13 skipped by design: the host/runner-only
  cases), Release 454/454 (`test-counts.txt`).
- Goldens: 8 retained, all byte-identical; 9 deleted (`golden-hashes.txt`).
- Manifest: 146 -> 119 table rows; checker 151/148 -> 123/121 (headers/rows), 0 violations.
- B06 residue for part B: 240 `COSMIC_2D_ONLY` preprocessor uses under `Cosmic/src`, `Projects`, `tests`;
  71 dead includes of purged headers inside excluded fences; 234 3D-identifier mentions in `Cosmic/src`
  (150 fenced, 68 unfenced comments, 16 unfenced code lines — listed in `verify-ap05-partA-excerpts.txt`,
  the seed of part B's "now-dead 3D-only" list: `PhysicsWorld.h:49` `Renderer3DDebugSink` fwd,
  `BindingPoints.h:88` `TexUnitShadowMap`, `PostProcessStack.{h,cpp}` sun-shaft shadow-map inputs,
  `SceneRenderer.h:82,165,283,284` (`class Terrain;`, `TerrainCastsShadows`, `Init(shadowMapSize)`),
  `Scene.h:297,307` `SyncVoxelVolumes`/`SyncNavMeshes` decls).
- Tree: 790 files / 298,289 lines deleted; `Cosmic/dependencies` now has 17 entries.

## Contract deviations and notes for the integrator

1. **`Cosmic/src/Cosmic.h:91` — the one unfenced include of a deleted header removed** (`#include
   "camera/NavigationCube.h"`). §9 lists `camera/NavigationCube.*` in part A, and the header was included
   unfenced (the D53 finding the manifest's `³ᴰ⁺` row recorded); nothing 2D-compiled used the class (its
   `.cpp` was already filtered, so any use would have failed to link). Removed rather than fenced so part B
   has one fence fewer. Likewise `tests/test_s5_navigation.cpp:8` (`#include "camera/NavigationCube.h"`) and
   `:15` (`using Cosmic::NavigationCube;`) — its two `NavigationCube` cases were already fenced and stay
   fenced for part B to drop. No other line under `Cosmic/src` was edited.
2. **Part-A oracle semantics** (`Verify-AP05Purge.ps1 -AllowFences`): `fence-uses` and `identifiers`
   are INFO (counted, not failed) and `includes-of-deleted` ignores includes inside `#ifndef COSMIC_2D_ONLY`
   regions; `paths-absent`, `build-files-clean` and any *compiled* include of a purged header are hard
   FAILs in both modes. Without the switch the script is the literal B06 (zero fence uses, zero purged
   includes, zero 3D identifiers in `Cosmic/src` outside a `History:` comment note; the physics verb
   `SphereCast` is masked as a documented substring false positive of `Recast`). Part B drops
   `-AllowFences` from the manifest. The path list is parameterised (`-DeletedPaths`, `-DeletedPathsFile`,
   `-NoDefaultPaths`) so part B / AP-Q1 can extend it.
3. **Kept on purpose (not "referenced only by deleted code")**: `Cosmic/assets/shaders/Outline.glsl` (loaded
   by retained `SceneRenderer.cpp:790` inside a fence — part B decides), `GodRays/Ssao/SsaoBlur/LensFlare/
   Bloom*/Fxaa/Tonemap/BlitCopy` (retained `PostProcessStack`/`Light2DRenderer`), `PBR.glsl` and
   `PBRSkinned.glsl` (retained, unfenced `AssetLibrary.cpp:159,168`), and the six shaders with **no reference
   anywhere** (`ComputeParticles, FlatColor, FlowEmissive, MeshLit, ParticlePoints, WaterFlow`) plus
   `textures/Galaxy.png` — for AP-Q1.
4. Stale prose left for its owner: `Cosmic/CMakeLists.txt:7-11` (the `COSMIC_2D_ONLY` option comment still
   describes the excluded trees and deps — part B rewrites it as the compatibility no-op),
   `docs/reference/README.md:95-127` (marker/CMake-filter prose — AP-D1), `.github/workflows/ci.yml:34`
   (comment says the audit checks "the 2D/3D markers" — AP-P1; the audit itself is green), the
   `docs/guide` chapters that cite `build_3d.bat` / `cmake --preset default` / the deleted tests (AP-D1).
5. `JoltBackend.cpp:1027` (`#if defined(JPH_DEBUG_RENDERER) && !defined(COSMIC_2D_ONLY)`) is a compound
   condition and `SceneRenderer.cpp:214` sits in an `#else` of a fence — part B's manual-edit candidates;
   nothing in part A depends on them.
6. The runner's `results.json` for B06-A records `commit=7479927 dirty=True` because it ran on the
   uncommitted part-A tree (the WO-10 precedent); the per-case `*.log` files it wrote are gitignored.
7. Nothing could not be deleted: every §9 part-A path was tracked and `git rm` removed it; no
   `ENVIRONMENT_BLOCKED` in this WO.

## Files

- Evidence (this directory): `report.md`, `deleted-paths.txt` (790), `dependency-consumer-greps.txt`,
  `build-excerpts.txt`, `test-counts.txt`, `golden-hashes.txt`, `audit-gl-conformance.txt`,
  `audit-docs-coverage.txt`, `verify-ap05-partA-excerpts.txt`, `verify-ap05-strict-excerpts.txt`,
  `b06a-Release/{results.json,results.junit.xml}`, `b06a-Debug/{results.json,results.junit.xml}`.
- Source: the 790 deletions and 12 edits listed above; `tests/acceptance/fixtures/Verify-AP05Purge.ps1`;
  `tests/acceptance/manifests/ap05-purge.manifest.json`.

## Local commits (not pushed — Kaden pushes)

- Part A: one commit on `main`, "Purge the filtered 3D source, deps, tests, goldens and editor TUs from
  main (AP-05 part A)" — `1bedfa49c09ec3e734853746a651c7bc25ef88e2`.

---

# AP-05 part B execution report — 2026-09-19 (remove the `COSMIC_2D_ONLY` fences; the trunk is 2D-only source)

Only AP-05 **Part B** was executed, alone on `main` in `C:\dev\Cosmic` on top of the part-A commit
`1bedfa4`. `evidence/AP-05/unfence.py` (Python 3.14 via `py -3`, committed) rewrote the **40** C/C++
files that carried fences — **235 marked blocks, 4,624 lines dropped, 0 added, 271 hunks every one of
which starts and ends on a fence directive** — and left exactly **one exception** for hand editing
(`JoltBackend.cpp:1027`, a compound condition). The CMake `if(COSMIC_2D_ONLY)` block in the root
CMakeLists was collapsed by hand; `option(COSMIC_2D_ONLY … ON)`, the reject-OFF `FATAL_ERROR` gate, the
PUBLIC define, every project CMakeLists' option and BuildRunner's `-DCOSMIC_2D_ONLY=ON` stay as a
documented compatibility no-op. Because B06's `identifiers` oracle is strict (zero 3D identifiers in
`Cosmic/src` outside a `History:` note), the 16 unfenced code lines and 68 unfenced comment lines part A
listed were resolved too (dead declarations removed, the unreachable sun-shaft pass removed, comments
reworded or moved under `History:` notes) — nothing that any 2D code path reached. **Both configs rebuilt
from a clean configure with 0 warnings (421 TUs each); `CosmicTests --count` is 454 in both configs =
part A's post count; the full suites pass 454/454 Debug and 454/454 Release; both audits exit 0; the 8
retained goldens are byte-identical; B06 PASSES strict (5/5) through the runner in Release and Debug;
the retained stability manifests wo07-l01 (1/1), wo09-units (8/8), wo10-units (6/6) pass and wo08-gpu
ran on the RTX 5070 Ti (see "Retained suites").** No crash, hang or data loss was found; no KI entry
was added. The trunk now contains **no preprocessor use of `COSMIC_2D_ONLY` anywhere under
`Cosmic/src`, `Projects`, `tests`, `Cosmic/templates`**.

## Scope and provenance

- Base `HEAD` at start: `1bedfa49c09ec3e734853746a651c7bc25ef88e2` (part A). `git status --short` at
  start: only the untracked root plan `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` and
  `recordings/`; both were never staged, moved or overwritten. Worktrees `C:\dev\Cosmic-ap-p1` (ap/p1, the
  concurrent lane) and `.claude/worktrees/epic-clarke-338e7f` were not touched; `engine-3d` and the
  `cosmic-pre-2d-2026-09-16` tag were not touched.
- Prompt vs packet: no conflict. Two things the prompt does not spell out were needed to make B06 pass
  and are recorded under "Contract deviations": the identifier cleanup in `Cosmic/src` files that carried
  no fence, and a bug fix in the part-A oracle's file enumeration.
- The session was interrupted once by an API usage limit (HTTP 429) after both clean builds while the
  full suites ran in the background; it resumed on the same tree (reconciled by `git status`: 80 status
  lines, HEAD still `1bedfa4`, no build process running, both `CosmicTests.exe` newer than every source
  edit — proven by an incremental `cmake --build` per config that compiled **0** TUs). Nothing was reset
  or restored.
- Commit: authored and committed as `kdadabhoy <kdadabhoy28@gmail.com>`, no `Co-Authored-By`, no AI
  trailer, no "Generated with" line; only explicit AP-05 paths staged. Not pushed.

## Toolchain and environment

- VS-bundled CMake `4.3.1-msvc1`, generator `Visual Studio 18 2026`, `-A x64`, MSVC `19.51.36248.0`;
  Python 3.14.0 (`py -3`); Windows PowerShell 5.1 for every script and the runner.
- Clean configure: `build/` deleted, then `-DCOSMIC_2D_ONLY=ON -DCOSMIC_BUILD_TESTS=ON
  -DCOSMIC_BUILD_RENDER_TESTS=ON`. Effective cache: `COSMIC_2D_ONLY=ON`, `COSMIC_BUILD_TESTS=ON`,
  `COSMIC_BUILD_RENDER_TESTS=ON`, `COSMIC_WITH_JOLT=ON`, `COSMIC_SKIP_PROJECTS=AnalysisSample`,
  `COSMIC_SKIP_PROJECTS_APPLIED=AnalysisSample` (`build-excerpts.txt`).
- Reference machine DESKTOP-SEOA4BT (Windows 11 26200, Ryzen 7 7800X3D, RTX 5070 Ti + Radeon iGPU) —
  the runner's `results.json` files record the environment.

## Counts — before and after (the four roots `Cosmic/src`, `Projects`, `tests`, `Cosmic/templates`)

| Spelling | Before (HEAD `1bedfa4`) | After |
| --- | --- | --- |
| `#ifndef COSMIC_2D_ONLY` | **225** | 0 |
| `#ifdef COSMIC_2D_ONLY` | **10** | 0 |
| `#if defined(COSMIC_2D_ONLY)` | 0 | 0 |
| `#if !defined(COSMIC_2D_ONLY)` | 0 | 0 |
| `#elif` lines mentioning it | 0 | 0 |
| `COSMIC_2D_ONLY` inside a compound condition | **1** (`Cosmic/src/physics/backends/JoltBackend.cpp:1027`, `#if defined(JPH_DEBUG_RENDERER) && !defined(COSMIC_2D_ONLY)`) | 0 |
| `#else` / `#endif` trailing `// COSMIC_2D_ONLY` comments | 43 | 0 |
| non-directive mentions in C/C++ (comments, one string) | 6 | 3 (`SceneRenderer.h:48` inside a `History:` note; `BuildRunner.cpp:54,62` — the `-DCOSMIC_2D_ONLY=ON` the hot-reload configure passes, kept on purpose) |
| files mentioning the token (any type) | 51 (41 C/C++ + 10 CMake/script/doc) | 12 (0 C/C++ fence files; the CMake options/defines/comments, the oracle, the manifest, `Capture-WO06.py`, `check_docs_coverage.ps1`, `AnalysisSample/README.md`, the 2 files above) |
| B06 `fence-uses` (oracle, `Cosmic/src`+`Projects`+`tests`) | 240 = 236 directives + 4 lines of the oracle's own `.ps1` (see the oracle fix) | **0** |
| B06 `includes-of-deleted` | 71 (all inside excluded fences) | **0** |
| B06 `identifiers` in `Cosmic/src` | 234 (150 fenced / 68 comment / 16 code) | **0** (19 lines under `History:` notes) |

Marked blocks by outcome: **196** `#ifndef` blocks dropped whole, **29** `#ifndef … #else … #endif`
blocks reduced to their `#else` (2D) branch, **10** `#ifdef COSMIC_2D_ONLY` blocks reduced to their body
(= 235). Per-file counts and the dry-run diff stat: `unfence-dryrun.txt`; every hunk's first/last removed
line: `unfence-hunks.txt`.

## What was built / changed

1. **`evidence/AP-05/unfence.py`** — the nesting-aware rewriter (two passes: block structure +
   exceptions, then emit; bytes in / bytes out, CRLF and the final-newline state preserved, nothing ever
   added; a fully dropped block sitting between two blank lines collapses one blank). Dry run by default,
   `--write` to rewrite. Run order: dry run (saved as `unfence-dryrun.txt`) → `--write` (40 files) →
   dry run again (0 marked blocks, 1 exception, idempotent) → `git diff --stat` (40 files, 4,624
   deletions, 0 insertions) → the per-hunk review (`unfence-hunks.txt`, 271 hunks, 0 needing a look:
   each begins on `#ifndef/#ifdef COSMIC_2D_ONLY`, `#else` or `#endif` and ends on `#endif`/`#else`) →
   the 29 `#else` promotions and 10 `#ifdef` bodies read file by file (`ViewportController.cpp` 9+1,
   `StarforgeApp.cpp` 9+3, `SceneRenderer.cpp` 2, `HierarchyPanel.cpp` 2, `test_reflect.cpp` 2,
   `Scene.cpp` 0+2, one each in `ContentBrowserPanel`, `EnvironmentPanel`, `MaterialEditorPanel`,
   `ProfilerPanel`, `test_scene_serializer`, `PreviewRig.cpp`, `StarforgeApp.h`, `ViewportController.h`,
   `test_crossbuild_scene.cpp`). The kept text is exactly what the 2D preprocessor already selected, which
   the unchanged test count and the incremental compile check (0 warnings) confirm.
2. **The exception, by hand** — `JoltBackend.cpp` `DebugDraw()`: the compound-fenced Jolt→Renderer3D
   line-batch bridge (always false on this trunk) removed; the method is an explicit no-op with a `History:`
   note, and the file-header/`PhysicsWorld.h` prose about "the single 3D coupling" reworded.
3. **CMake, by hand** (`build-files-clean` stays green): root `CMakeLists.txt` — option comment rewritten
   as the compatibility no-op, the gate comment updated, the mode-derived `if(COSMIC_2D_ONLY)`/`else()`
   skip-list block collapsed to one `SKIP_DEFAULT` (behaviour identical for existing caches: both lists
   were `AnalysisSample`), the stale "Starforge … fenced out" prose fixed; `Cosmic/CMakeLists.txt` —
   the `:7-11` option comment (part A's hand-off) rewritten, the PUBLIC define kept with a no-op comment,
   the Jolt comments no longer speak of "both configurations"; `tests/CMakeLists.txt`,
   `Projects/Starforge/CMakeLists.txt`, `Projects/AnalysisSample/CMakeLists.txt`,
   `Projects/Starforge/assets/templates/CMakeLists.txt`, `Cosmic/templates/ExampleProject/CMakeLists.txt`
   — comments only; every `option(COSMIC_2D_ONLY … ON)` and the projects' `if(COSMIC_2D_ONLY)
   target_compile_definitions(...)` blocks kept (harmless; external projects keep configuring).
   `CMakePresets.json` untouched (the `2d` preset already passes ON).
4. **Identifier cleanup for B06 strict** (the 16 code lines → 0, the 68 comment lines → 0 outside
   `History:`), all provably unreachable from 2D code:
   - `PhysicsWorld.h:49` `class Renderer3DDebugSink;` — removed (dead forward declaration).
   - `BindingPoints.h:87-88` `TexUnitShadowMap = 11` — removed; unit 11 documented as reserved under a
     `History:` note because `PBR.glsl` / `PBRSkinned.glsl` / `MeshLit.glsl` still declare `u_ShadowMap`.
   - `PostProcessStack.{h,cpp}` (6 lines: `SetSunShaftInputs`, `m_ShaftShadowMapID` ×3, `u_ShadowMap`,
     the `RenderEffects` guard) — the S10.3 sun-shaft / god-rays pass removed whole: its only feeder
     `SetSunShaftInputs` was called from the fenced 3D half of `SceneRenderer::PassPostAndComposite`, so
     `m_ShaftShadowMapID` could never be non-zero and `RenderGodRays` could never run. Gone:
     `SetGodRaysEnabled/IsGodRaysEnabled/SetGodRaysParams/SetSunShaftInputs`, `RenderGodRays`, 11
     members, the `GodRays.glsl` load/reset and the half-res shaft FBO; `Composite` now always sets
     `u_UseShafts = 0` (the value it always set on this trunk — byte-identical, the goldens agree).
     With it: `SceneRendererSettings::GodRays/GodRaysIntensity/GodRaysDensity` and their two setter calls
     in `SceneRenderer.cpp`, the `desc.Settings.GodRays = false` wireframe override in
     `StarforgeApp.cpp`, and **`Cosmic/assets/shaders/GodRays.glsl`** (its only load site is gone —
     part A's "every load site deleted" rule).
   - `SceneRenderer.h:283` / `.cpp:201,214` — `Init(width, height, shadowMapSize = 2048)` →
     `Init(width, height)`; the two render-test callers that passed a size (`render_2d.cpp:545`,
     `render_wo08_rtt.cpp:199`) updated; PlayerLayer and Starforge already passed two arguments.
   - `SceneRenderer.h:82` `class Terrain;` — removed with the other forward declarations of purged
     types (`Model`, `InstanceSet`, `Water`, `ParticleEmitter`, `RibbonEmitter`, `ScenePicker`,
     `CoverageCapture`) and the unused `Mesh`/`Material`/`Shader` ones; `:165` `TerrainCastsShadows` —
     removed (its only reader was the fenced `PassShadow`); `:284` comment reworded.
   - `Scene.h:297,307` `SyncVoxelVolumes` / `SyncNavMeshes` — removed together with the five other
     declarations whose definitions lived in the purged `Scene3D.cpp` (`OnRender3D`, `UpdateAnimators`,
     `SyncPrimitiveMeshes`, `SyncWorldSystems`, `OnRenderWorldFX`; 84 lines with their docs; zero callers).
   - The 68 comment lines: reworded where the subject is live (`Camera.h`, `FlyCameraController.h`,
     `PerspectiveCamera.h`, `Mesh.h:69`, `UniformBuffer.h`, `Noise.h` ×3, `Spatial.h`, `RendererAPI.h`,
     `Cosmic.h` — the 4-line "3D component half" note removed, `Scene.h` BuildRenderDesc doc,
     `SceneRenderer.{h,cpp}` — the 50-line class header rewritten around the 2D spine with a `History:`
     paragraph for the 3D past, `Components.h` header, `ScenePhysics.cpp:189` — an orphaned "Voxel
     collision" section header removed), or moved under a `History:` note where the text explains why a
     3D-era remnant exists (`Material.h` ×4, `Mesh.h:32`, `TextureCube.h`, `PhysicsWorld.h:136`,
     `ScenePhysics.{h,cpp}` navmesh-bake notes, `TypeRegistry.cpp`, `BindingPoints.h` ×8,
     `CameraUniforms.h`, `RenderQueue.h`, `Components.h:437`, `Scene.cpp` BuildRenderDesc,
     `PostProcessStack.h` ×2, `SceneRenderer.h` ScenePass). The oracle counts 19 lines under `History:`.
   - Stale fence prose outside `Cosmic/src`: `StarforgeApp.h:18`, `ViewportController.cpp:749`,
     `render_main.cpp:100`, `tests/check_docs_coverage.ps1:39` reworded.
5. **Oracle fix** — `tests/acceptance/fixtures/Verify-AP05Purge.ps1` `Get-CodeFiles`: Windows
   PowerShell 5.1 ignores `-Include` on `Get-ChildItem -LiteralPath <dir> -Recurse` (every file came
   back — `.ps1`, `.md`, `.json`, `.gitignore`), which is why part A's 240 `fence-uses` carried four of the
   script's own comment lines (`:17,176-178`, the `defined(COSMIC_2D_ONLY)` regex matched them) and why
   strict mode could never have reached zero. The extension filter is now applied explicitly; the
   `includes-of-deleted` and `identifiers` walks were affected the same way (no false hits there).
6. **Manifest** — `ap05-purge.manifest.json`: `-AllowFences` dropped, case id `B06-A` → `B06`,
   description rewritten as the strict oracle.

## Acceptance-case status

| ID | Result | Evidence |
| --- | --- | --- |
| **B06** strict (no `-AllowFences`) through `Run-Acceptance.ps1` | **PASSED** Release and Debug — 5/5 (`paths-absent`, `build-files-clean`, `fence-uses` 0, `includes-of-deleted` 0, `identifiers` 0 outside 19 History lines) | `b06-Release/results.json`, `b06-Debug/results.json` (+junit), `verify-ap05-strict-after-excerpts.txt` |
| B06 strict on the part-A tree (control) | FAILED as designed, 3 of 5 — the full 240/71/234 lists | `verify-ap05-strict-before-excerpts.txt` |
| Both configs 0 warnings, clean configure | **PASS** — Debug 421 TUs 0 warnings, Release 421 TUs 0 warnings (`'warning [A-Z]'` grep of the full MSBuild logs) | `build-excerpts.txt` |
| Test count = part A's post count | **PASS** — 454 Debug, 454 Release (`--no-skip` 467/467; `CosmicRenderTests --count` 37/37) | `test-counts.txt` |
| Retained goldens byte-identical | **PASS** — 8/8 SHA-256 equal to `git show HEAD:`; `git status` of the golden dir empty | `golden-hashes.txt` |
| GL conformance audit | **PASS** exit 0, "clean" | `audit-gl-conformance.txt` |
| Docs coverage audit | **PASS** exit 0, "clean (123 public headers, 121 manifest rows, 6 skeleton chapters, 4 off-tier)" — unchanged from part A (no header deleted, no row touched) | `audit-docs-coverage.txt` |
| Full unit suites | **PASS** 454/454 Debug, 454/454 Release, each with a fresh per-run TEMP | `test-counts.txt` |
| Retained manifests | see "Retained suites" | `retained-*-Release/results.json` |

## Retained suites (Release, `Run-Acceptance.ps1`, per-manifest `-TempRoot build\_temp\<name>`)

| Manifest | Result |
| --- | --- |
| `wo07-l01` (L01 runtime-plugin teardown host, 110+ cases, `gpu-gl`) | **PASSED** 1/1 |
| `wo08-gpu` (R01, R02, R03, R04-G, R05, R06-G — hidden-window GL on the RTX 5070 Ti) | **PASSED** 6/6 (GPU present, nothing blocked) |
| `wo09-units` (C01-U … C06-U, C05-XB, C06-AUDIO) | **PASSED** 8/8 (audio device present) |
| `wo10-units` (N02-U, N03, N04, N04-FILTERS, N04-LOOKUP, N04-SCENE) | **PASSED** 6/6 |

The fixtures behind these manifests write their own child records into the stability packet's tracked
evidence directories (`docs/plans/2d-stability-2026-09-16/evidence/WO-07|08|09|10/*-Release/`, the
`-Output` argument baked into each manifest). Those directories are outside this WO's ownership and
WO-10's own commit never touched other WOs' evidence, so the side-effect rewrites (child records, golden
hash snapshots, captures — identical verdicts, new timestamps) were **reverted to HEAD after the runs**
and are not part of the commit; the runner's `results.json` / `results.junit.xml` for every retained run
live under `evidence/AP-05/retained-<manifest>-Release/` instead.

## Defects found

- None: no crash, hang or data loss; no golden regenerated, no tolerance loosened, no test skipped. The
  KI register was not appended (next entry stays **KI-57**). Two tooling issues were fixed in flight and
  are not product defects: the oracle's `-Include` enumeration (above), and a transient compile break of
  my own making (a nested `/* … */` inside the rewritten `SceneRenderer.h` header doc closed the block
  comment early — caught by the incremental compile check, fixed, and every changed C/C++ file was then
  scanned for nested/unterminated block comments: 0).

## Now-dead 3D-only surface (survives only because it was never fenced — for AP-Q1 to tidy)

Engine, `Cosmic/src` (all still exported through `Cosmic.h`, rows still in `docs/reference/README.md`):

- `camera/OrbitCameraController.h:51` `OrbitCameraController` (+ NavStyle / ViewPreset) and
  `camera/FlyCameraController.h:44` `FlyCameraController` — the editor rig's Orbit / Fly modes; the only
  consumers are `Projects/Starforge/src/EditorCameraRig.{h,cpp}` and `tests/test_s5_navigation.cpp`.
  `Camera2DController` only *mirrors* their architecture (comments).
- `camera/PerspectiveCamera.h:59` `PerspectiveCamera` — a pinhole camera with no perspective renderer;
  kept alive by the two controllers above and by the render fixtures' camera choice
  (`tests/render/render_2d.cpp:514`, `render_wo08_rtt.cpp:188`), which could use `OrthographicCamera`.
- `renderer/RenderQueue.h:44` `Key`, `:58` `OpaqueLess`, `:68` `TransparentLess`, `:76` `Run`, `:89`
  `FindInstancableRuns` — the 3D mesh-queue sorter; zero consumers (its test left in part A).
- `math/Frustum.h:32` `Frustum` — zero consumers (only `Cosmic.h:72` and a `RenderQueue.h` comment).
- `renderer/CameraUniforms.h:42` `GpuCameraBlock` — the camera UBO mirror; nothing uploads it.
- `graphics/TextureCube.h:48` `TextureCube` (+ `platform/OpenGL/OpenGLTextureCube.{h,cpp}` and the
  cube verbs in `RendererAPI.h` / `RenderCommand.h`) — IBL-only; zero consumers.
- `graphics/UniformBuffer.h:43` `UniformBuffer`, `graphics/StorageBuffer.h:37` `StorageBuffer` (+ their
  OpenGL implementations) — no 2D consumer (Renderer2D / Light2DRenderer use plain uniforms).
- `graphics/Mesh.h:128` `Mesh` with `MeshVertex`, `MeshData`, `SkinVertex`, `Submesh` (`:60-125`) —
  primitives and OBJ loading with no renderer that draws them; `Scene.h:21` fwd-declares `Material` only
  as the bucket key of the live 2D `OnRender` path.
- `graphics/Material.h:12` `Material` render-queue hints (`SetTransparent`, `SetInstancingShader`,
  `SetSkinnedShader`, `BindFullTo`) and the whole `.cmat` pipeline — `graphics/MaterialAsset.h`,
  `AssetLibrary::GetMaterial/BuildMaterial` (`assets/AssetLibrary.cpp:101-125`, which loads
  `PBR.glsl` / `PBRSkinned.glsl`), `reflect/TypeRegistry.cpp:250-260`, the editor's
  `MaterialEditorPanel`, `AssetTypes.cpp` `.cmat` and `ContentBrowserPanel` `.cmat` rows — authoring PBR
  materials for meshes nothing renders (`tests/test_wo09_c06_services.cpp:312` covers the null path).
- `renderer/SceneRenderer.h:79` `ScenePass::{ShadowDepth, Reflection, TopDownDepth}` + `:94`
  `SceneDrawContext::IsDepthOnly`; `SceneRendererSettings` `Skybox/IBL/Shadows/WaterReflections` (`:108`),
  `ShadowCenter/ShadowRadius/ShadowBias` (`:114`), the `Underwater*` block (`:125-133`), `LensFlare*`
  (`:136`), `OutlineEnabled/OutlineColor/OutlineWidthPx` (`:152-154`) — no reader; the hosts still assign
  `Skybox/IBL/Shadows = false` (`PlayerLayer.cpp:378-380`, `StarforgeApp.cpp:1179-1181,1189`,
  `render_2d.cpp:527-529`, `render_wo08_rtt.cpp:195`) and `ApplyEnvironment` still maps `Skybox`/`IBL`
  (`SceneRenderer.cpp:88-89`, asserted by `tests/test_scene_components.cpp:57-58`).
- `scene/Components.h:434-444` `EnvironmentComponent` sun + sky fields (`SunDirection`, `SunColor`,
  `SunIntensity`, `Sky`/`SkyMode`, `HdriPath`; reflected at `reflect/TypeRegistry.cpp:96-101`, so the
  Inspector still shows them and scenes still serialize them) — kept for scene compatibility.
- `renderer/PostProcessStack.h` — SSAO (`RenderEffects(projection)` reconstructs view-space position
  from a perspective projection), height fog, underwater medium, lens flare and heat-haze are 3D-scene
  effects still reachable from `EnvironmentComponent` (SSAO/Fog/LensFlare toggles); `Tonemap.glsl` keeps
  the `u_Shafts`/`u_UseShafts` input that is now always off.
- `physics/PhysicsWorld.h:138` `PhysicsWorld::DebugDraw` and `physics/PhysicsBackend.h:134`
  `IPhysicsBackend::DebugDraw` + `JoltBackend::DebugDraw` — explicit no-ops now (the editor draws its own
  Renderer2D collider overlay).
- `scene/Components.h:397-413` `CameraComponent::Projection::Perspective` (default!) — the perspective
  branch of the play camera feed (`Projects/Starforge/src/StarforgeApp.h:63` `PoseCamera`, and
  PlayerLayer's twin) has no perspective renderer behind it.
- Assets with no load site left: `Cosmic/assets/shaders/{ComputeParticles,FlatColor,FlowEmissive,
  ParticlePoints,WaterFlow}.glsl`, `textures/Galaxy.png` (part A's list); loaded but 3D-only:
  `PBR.glsl`, `PBRSkinned.glsl` (AssetLibrary), `MeshLit.glsl`, `Outline.glsl` (no loader after the
  fence drop — `SceneRenderer.cpp:790` went with `PassOutline`).
- Docs manifest rows that will go stale when the headers above are deleted (the checker's stale-row
  mode enforces it): `renderer/RenderQueue.h` → `rendering-3d.md` (a chapter still named for 3D — AP-D1),
  `camera/{PerspectiveCamera,OrbitCameraController,FlyCameraController}.h` → `cameras.md`,
  `graphics/TextureCube.h`, `renderer/BindingPoints.h` → `graphics-resources.md`.

Editor, `Projects/Starforge/src`: `EditorCameraRig.{h,cpp}` (`EditorCameraRig` `:49`, `PossessCamera`
`:33` — Orbit/Fly/Possess for a perspective viewport; `ViewportController.cpp:108` `rig.Orbit()`, `:881`
`rig.Fly().GetMoveSpeed()`), the `Settings.Skybox/IBL/Shadows` assignments above, `EditorSnapshot.h:12`
(a comment naming the purged `MeshRendererComponent`), `PreviewRig.h:40` prose about `GetMesh`.

## Measured numbers

- Unfence: 40 files, 235 blocks (196 dropped / 29 else-kept / 10 body-kept), 4,624 lines removed, 271
  hunks; identifier cleanup + CMake + oracle + manifest + the two test callers: 29 more files. Whole
  source change outside `docs/`: **69 files, +290 / −5,162 lines**, plus `GodRays.glsl` deleted
  (78 lines); the evidence files come on top.
- Builds: Debug 421 TUs, 0 warnings (10:38:30–10:40:04Z); Release 421 TUs, 0 warnings
  (10:40:51–10:43:20Z); incremental re-check after the interruption: 0 TUs both configs.
- `CosmicTests.exe --count`: 454 / 454 (Debug / Release); `--no-skip` 467 / 467; full suites 454/454
  passed both (23,400,718 / 23,486,893 assertions, 13 skipped by design each); `CosmicRenderTests
  --count` 37 / 37.
- Goldens: 8 retained, 8 identical. Docs checker 123 headers / 121 rows (unchanged).
- Line endings: 35 of the 40 rewritten files are CRLF and stayed CRLF; `Scene.cpp`, `SceneSerializer.cpp`,
  `StarforgeApp.cpp`, `StarforgeApp.h`, `test_crossbuild_scene.cpp` were already LF-only in the working
  copy before part B (the script provably preserves endings; git stores LF either way).

## Contract deviations and notes for the integrator

1. **Edits outside the fence sites in `Cosmic/src`** (the identifier cleanup in group 4 above) touch 20
   engine files that carried no fence (`Camera.h`, `FlyCameraController.h`, `PerspectiveCamera.h`,
   `Material.h`, `Mesh.h`, `TextureCube.h`, `UniformBuffer.h`, `Noise.h`, `Spatial.h`, `PhysicsWorld.h`,
   `BindingPoints.h`, `CameraUniforms.h`, `PostProcessStack.{h,cpp}`, `RendererAPI.h`, `RenderQueue.h`,
   `Components.h`, `Scene.{h,cpp}`, `TypeRegistry.cpp`, `Cosmic.h`). B06's `identifiers` oracle is the
   packet's own bar (§9: "zero identifiers … comments that explain history may mention 3D only under a
   History: note") and the prompt says B06 must PASS; nothing was deleted that any 2D code path reached,
   and the "deleting dead-but-compiled classes" exclusion was respected — the classes stay and are listed
   above. The god-rays pass is the one *feature* removed; it was unreachable by construction (its input
   was the purged `ShadowMap`).
2. **`SceneRenderer::Init` lost its third parameter** (public ABI of the engine DLL); the two render-test
   call sites were updated. External consumers pass two arguments (PlayerLayer/Starforge did).
3. **Comment-only edits to two files outside the Owns list**: `Projects/AnalysisSample/CMakeLists.txt`
   (`:32-36`) and `Projects/Starforge/assets/templates/CMakeLists.txt` (`:29-38`), whose prose claimed the
   SDK headers carry fences; the prompt names "every project CMakeLists'" option as kept, so their
   comments were brought in line. Options and blocks unchanged.
4. **Reverted side effects**: the retained manifests' fixtures rewrote 52+ tracked files under
   `docs/plans/2d-stability-2026-09-16/evidence/WO-09|WO-10` (and WO-07/WO-08's `-Release` dirs); reverted
   to HEAD after the runs (see "Retained suites") — not part of this commit.
5. Left for their owners: `docs/reference/README.md:95-127` and the `docs/guide` chapters that still
   describe the markers / `build_3d.bat` / `cmake --preset default` (AP-D1); `.github/workflows/ci.yml:34`
   (AP-P1); `docs/reference/rendering-3d.md` as a chapter name (AP-D1); everything in the dead list
   (AP-Q1). `tests/test_wo06.cpp` D01 (KI-57) still needs a clean TEMP per direct run — unchanged.
6. The runner's `results.json` files record `commit=1bedfa4 dirty=True` because every run happened on the
   uncommitted part-B tree (the WO-10 / part-A precedent); the per-case `*.log` files are gitignored.

## Files

- Evidence (this directory, part B): `unfence.py`, `unfence-dryrun.txt`, `unfence-hunks.txt`,
  `verify-ap05-strict-before-excerpts.txt`, `verify-ap05-strict-after-excerpts.txt`,
  `build-excerpts.txt`, `test-counts.txt` (part B section appended), `golden-hashes.txt`,
  `audit-gl-conformance.txt`, `audit-docs-coverage.txt` (the last five rewritten for part B),
  `b06-Release/`, `b06-Debug/`, `retained-wo07-l01-Release/`, `retained-wo08-gpu-Release/`,
  `retained-wo09-units-Release/`, `retained-wo10-units-Release/` (`results.json` + `results.junit.xml`
  each). Part A's files are untouched except this report.
- Source: the 69 files above (including `tests/acceptance/fixtures/Verify-AP05Purge.ps1` and
  `tests/acceptance/manifests/ap05-purge.manifest.json`), `Cosmic/assets/shaders/GodRays.glsl` deleted.

## Local commits (not pushed — Kaden pushes)

- Part B: one commit on `main`, "Remove the COSMIC_2D_ONLY fences; the trunk is 2D-only source (AP-05
  part B)" — the SHA is in the session's final report and in `git log`.
