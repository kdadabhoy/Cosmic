# WO-03 — Enforce 2D across every supported entry point and CI

**Gate:** G2 · **Acceptance:** B03–B05 · **Status:** done (local commits; not pushed) · **Date:** 2026-09-17

## Environment

| Field | Value |
| --- | --- |
| Branch | `main` (main-only campaign, D-WORKFLOW) |
| HEAD at start | `4217ac9` |
| Working-tree diff sha256 (at report time) | `2bef03218186…` |
| Host | DESKTOP-SEOA4BT (reference machine, WO-02) |
| CPU | Ryzen 7 7800X3D (SSE4.1/4.2 ✓ — meets D-CPU floor) |
| OS | Windows 11, build 10.0.26200.9457 |
| Generator | Visual Studio 18 2026, arch x64 |
| CMake | 4.3.1-msvc1 (VS-bundled) |
| MSVC | cl 19.51.36248.0 |

All B03/B04 checks below are **configure-only** into throwaway build dirs under the session
scratchpad, so the tree's real `build/` (a WO-02 2D Debug engine) was left intact. B05's external-DLL
build linked against that existing 2D Debug engine and its stray outputs were cleaned afterward.

---

## Entry points changed

Every place a build can originate now selects the supported 2D mode; the CMake layer makes OFF
impossible on this branch, so a script/CI/preset default can never silently ship 3D.

| Entry point | File | Change |
| --- | --- | --- |
| Root option + **enforcement gate** | `CMakeLists.txt` | `COSMIC_2D_ONLY` default **OFF→ON**; added a `FATAL_ERROR` that **rejects OFF at configure time** (the single gate every entry point funnels through). |
| Engine standalone default | `Cosmic/CMakeLists.txt` | option default **OFF→ON**. |
| Editor project default | `Projects/Starforge/CMakeLists.txt` | option default **OFF→ON**. |
| Scaffold template (external plugins) | `Cosmic/templates/ExampleProject/CMakeLists.txt` | declares `COSMIC_2D_ONLY` (default ON) and **stamps the define** onto the plugin, so an external plugin matches the 2D SDK's public-header fences across the DLL boundary. |
| Editor hot-reload | `Projects/Starforge/src/BuildRunner.cpp` | game-plugin configure now passes `-DCOSMIC_2D_ONLY=ON` explicitly. |
| PR CI | `.github/workflows/ci.yml` | explicit `-DCOSMIC_2D_ONLY=ON`; **mode+arch+toolchain cache key**; Debug **and** Release units; 2D-mode cache assertion; **test-discovery floor**; report upload. |
| Release staging | `.github/workflows/release.yml` | explicit `-DCOSMIC_2D_ONLY=ON`; 2D-mode cache assertion before staging; dropped the 3D `Frontier` example. |
| GPU qualification (new) | `.github/workflows/gpu-qualification.yml` | scheduled/dispatch golden-image job on a self-hosted `gpu` runner — **not** on the PR path. |
| Packaging | `package.bat` | configure now `-DCOSMIC_2D_ONLY=ON` (`package_installer.bat` inherits it). |
| Dev scripts | `build.bat`, `build_all.bat`, `build_all_release.bat`, `build_engine.bat` | explicit `-DCOSMIC_2D_ONLY=ON` + corrected "2D-only engine" messaging (they previously mislabelled the default as "full 3D engine"). |
| `build_3d.bat` | `build_3d.bat` | repurposed to a **redirect notice** to `engine-3d` (its old `-DCOSMIC_2D_ONLY=OFF` now hits the gate). |
| Cosmetic | `Cosmic/src/layers/LauncherLayer.cpp` | dropped the stale `ViperSim` example string. |

`build_2d.bat` / `build_all_2d.bat` already passed `-DCOSMIC_2D_ONLY=ON` — left as-is (now redundant
with the default but still correct). Jolt and the shared physics/camera/math helpers are untouched.

---

## B03 — enforcement through every entry point (clean **and** stale-cache)

Commands: `cmake -S . -B <scratch> …` with the VS-bundled cmake. Result must be ON, or a clear
rejection with no implicit full-engine tree generated.

| Scenario | Command | Result | Exit |
| --- | --- | --- | --- |
| **Clean, default** (no flag) | `cmake -S . -B b03_default -A x64 -DCOSMIC_BUILD_TESTS=ON` | `COSMIC_2D_ONLY:BOOL=ON`, configured+generated | **0** ✓ |
| **Clean, preset** | `cmake -S . --preset 2d` | `COSMIC_2D_ONLY:BOOL=ON`, configured+generated | **0** ✓ |
| **Explicit OFF** (unsupported request) | `cmake -S . -B b03_off -A x64 -DCOSMIC_2D_ONLY=OFF` | `FATAL_ERROR` at `CMakeLists.txt:82` with the "not supported on this branch / use engine-3d" message; **no build files generated** | **1** ✓ (rejected clearly) |
| **Stale OFF cache** (seeded `COSMIC_2D_ONLY:BOOL=OFF`, reconfigured with **no** flag) | `cmake -S . -B b03_stale` | Sticky OFF cache caught → same `FATAL_ERROR`; **no `.slnx` generated** → stale opposite-mode tree **not** silently reused | **1** ✓ (enforced, not reused) |

### Failing-before (the KI-3 bug, reproduced)

With the **pre-WO-03** root `CMakeLists.txt` (`git show HEAD:CMakeLists.txt`, default OFF, no gate)
temporarily swapped in and then restored:

```
cmake -S . -B before_off -A x64 -DCOSMIC_2D_ONLY=OFF   →   exit 0
  COSMIC_2D_ONLY:BOOL=OFF, CosmicRoot.slnx generated, assimp + Recast configured (a 3D tree)
```

So **before** the fix an OFF request produced a full 3D/mixed tree; **after** the fix the identical
request is rejected (row 3/4 above). Rejection message verbatim:

```
CMake Error at CMakeLists.txt:82 (message):
  COSMIC_2D_ONLY=OFF is not supported on this branch.
    main is the 2D-only stability trunk — it builds and ships the 2D engine only.
    A stale build/ cache from a previous 3D configure is the usual cause: delete the
    build directory and reconfigure (it will default to the 2D engine).
    For a full 3D build, check out the 'engine-3d' branch, where 3D is supported.
```

### CI cache-key stale-restore (the ci.yml:36 half of KI-3)

The old key `cmake-build-${{ runner.os }}-${{ hashFiles('CMakeLists.txt','Cosmic/CMakeLists.txt',
'Runtime/CMakeLists.txt','tests/CMakeLists.txt') }}` hashes only files that are **byte-identical
between a 2D and a 3D configure** (mode is a `-D` flag, not a file edit), so the 2D and 3D keys were
the **same string** → `actions/cache` could restore a stale opposite-mode `build/`. The new key,

```
cmake-build-${{ runner.os }}-2d-x64-msvc<VER>-<hash(… + Starforge/CMakeLists + CMakePresets + ci.yml)>
```

carries the **mode token** (`2d` vs `3d`), **arch** (`x64`) and **MSVC toolset version**, so 2D/3D,
different arch, or a compiler bump can never collide on one cache entry.

---

## B04 — generated target/source graph and manifest (2D configure)

From the clean 2D tree `b03_default` (VS18 uses the `.slnx` solution format):

**Solution projects** — `Cosmic, ImGuizmo, Jolt, glfw, glad, imgui, implot, SF_Telem, Starforge,
CosmicApp, StarforgeEditor, CosmicTests`.

- 3D-only deps **assimp / RecastNavigation / zlibstatic**: **absent** ✓ (never configured in 2D).
- Shared physics **Jolt**: **present** ✓ (preserved on both configurations).

**`Cosmic.vcxproj` compiled source set (227 entries):**

- Designated 3D paths **absent** ✓: subsystem dirs `terrain|voxel|water|nav|particles` (0 matches),
  and every named 3D file — `Renderer3D, EnvironmentMap, ShadowMap, CoverageCapture, InstanceSet,
  NavigationCube, Scene3D, SceneNav, Components3D, TypeRegistry3D, Model, Skeleton, AnimationClip,
  CgltfImpl, MeshImport.cpp`.
- Shared/2D paths **present** ✓ (the allowed exceptions — *not* a blanket "no 3D-looking symbol"
  rule): `camera/Camera2D` ×2, `camera/Camera.` ×1, `camera/OrthographicCamera` ×4, `/physics/` ×12,
  `physics/backends/JoltBackend` ×1, `renderer/Renderer2D` ×2, `renderer/SceneRenderer` ×2,
  `renderer/Light2DRenderer` ×2, `assets/AssetLibrary` ×2, `math/` ×7.

**Consumer compile definitions match** ✓: `COSMIC_2D_ONLY` appears as a preprocessor definition in
every consumer's `.vcxproj` — `Cosmic`, `Starforge`, `CosmicTests`, `CosmicApp`, `SF_Telem` (8 config
blocks each), confirming the PUBLIC define propagates identically across the DLL boundary.

*Package manifest:* the install rules install `Cosmic.dll` + engine `assets/` + each scanned project
DLL. Because the 3D targets are never generated (above), no 3D binary can enter the staged tree; the
release job additionally asserts `COSMIC_2D_ONLY:BOOL=ON` before staging (B05 / release proof below).

---

## B05 — audits, external DLL, test discovery, mode/SDK consistency

| Check | Result |
| --- | --- |
| GL conformance audit (`tests/check_gl_conformance.ps1`) | **clean**, exit 0 ✓ |
| Docs coverage audit (`tests/check_docs_coverage.ps1`) | **clean**, exit 0 ✓ (147 headers / 144 rows; pre-existing non-fatal skeleton warnings only) |
| External minimal DLL, **correct mode** | `ExampleProject` configured standalone against the 2D SDK → stamps `COSMIC_2D_ONLY` (8 config blocks); **built Debug and linked** against the existing 2D `Cosmic.lib` → `TemplateProject.dll` (9.1 MB), exit 0 ✓ |
| Test-discovery contract | `CosmicTests.exe --count` → **340** cases; CI fails if any config drops below the floor (300) or the binary is renamed/missing ✓ |
| Release cannot bypass 2D | `release.yml` configures `-DCOSMIC_2D_ONLY=ON` **and** the root gate rejects OFF, so editing the flag to OFF fails the configure; a `COSMIC_2D_ONLY:BOOL=ON` cache assertion runs before any staging ✓ |

### Residual (honest gate) — mismatched-mode plugin is *prevented*, not compiler-*rejected*

B05 asks that a wrong-mode fixture be "rejected **or** prevented by the build/package contract." I
built the template plugin with an explicit `-DCOSMIC_2D_ONLY=OFF` against the 2D engine: it
**compiled and linked (exit 0)** — this particular plugin touches no header region whose layout
diverges between modes, so neither the compiler nor the linker caught the ABI mismatch.

The mismatch is therefore closed by **prevention, not detection**: on the supported path the template
defaults `COSMIC_2D_ONLY=ON` and `BuildRunner` forces `-DCOSMIC_2D_ONLY=ON`, so a plugin built the
supported way always matches the SDK. A hand build that deliberately overrides the flag is off the
supported path.

**Recommended follow-up (not in WO-03 scope):** add a mode-tagged exported ABI symbol to a public
engine header (e.g. the 2D build exports `CosmicAbiTag_2D`, the 3D build `CosmicAbiTag_3D`, referenced
from a public inline) so a mismatched plugin fails to **link**. That touches the engine ABI + needs an
engine rebuild/test, so it belongs to WO-11 (packaging hardening) or a new KI, not this work order.

---

## Done-when (DoD) status

- B03–B05 pass from clean **and** stale-cache scenarios — ✓ (above).
- A release/package job cannot produce a 3D or mixed binary — ✓ (explicit flag + root gate + cache
  assertion; the gate is the CMake layer, so a script flag cannot bypass it).
- Jolt + shared helpers remain — ✓ (B04).
- The CI cache key carries the mode — ✓ (mode+arch+toolchain).

Residual: plugin-boundary mode mismatch is prevented by the build contract but not link-rejected;
tracked as a recommended follow-up above.

## Not run here (out of WO-03 scope / environment)

- GPU golden-image qualification — no GPU CI runner exists yet; `gpu-qualification.yml` establishes the
  scheduled/dispatch job on a self-hosted `gpu` runner (WO-13 qualifies on real hardware). Not a pass,
  not blocking PRs.
