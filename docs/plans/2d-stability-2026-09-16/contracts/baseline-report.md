# Cosmic 2D — WO-02 fresh baseline report

**Work order:** WO-02 (Establish a fresh 2D baseline and the numbers the gates calibrate against) ·
**Gate:** G1 · **Depends on:** WO-01 · **Acceptance:** B02 + baseline reports.

**Run identity**
- Repo HEAD at execution: `afdfe213cb376661ab43a98826b398773fde0eb2` (`main`), clean tracked tree.
  Working tree carried only the protected untracked root file
  `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` (never staged/moved) plus this WO's new
  `docs/plans/2d-stability-2026-09-16/evidence/WO-02/` output.
- Packet base was `0e8894b` (support-matrix); HEAD is three doc-only commits above it
  (`967f174`, `c55d300`, `afdfe21`) — no engine/build source changed between `0e8894b` and `afdfe21`.
- Workflow: **main-only** (D-WORKFLOW) — configured/built/tested directly on `main`, no candidate
  worktree. `engine-3d` and the `cosmic-pre-2d-2026-09-16` tag were not touched.
- Configuration: **explicit `-DCOSMIC_2D_ONLY=ON`** for every build/test below.
- Date: 2026-09-17. Raw evidence: `../evidence/WO-02/` (index at the end).

> **Purpose.** A clean isolated 2D build + the honest current state, plus the **measured baselines**
> every provisional numeric bar in [`../03-Acceptance-Test-Catalog.md`](../03-Acceptance-Test-Catalog.md)
> is ratified from. Per [`numeric-bar-policy.md`](numeric-bar-policy.md), no bar gates a merge until a
> baseline is measured here on a named reference machine. Nothing below is "pass/fail" — measurements
> are recorded as **baseline**, and unmeasurable bars are **`ENVIRONMENT_BLOCKED`** with the exact
> missing prerequisite and owner WO named (never a phantom pass).

---

## 1. Reference machine identity (fills the support-matrix "pending fields")

| Field | Value |
| --- | --- |
| Machine identity | **DESKTOP-SEOA4BT** (ASUS desktop) |
| CPU | **AMD Ryzen 7 7800X3D** (Zen 4), 8 cores / 16 threads |
| CPU ISA (measured, `__cpuid` probe) | SSE2, SSE3, SSSE3, **SSE4.1, SSE4.2**, AVX, FMA, AVX2, AVX512F, BMI1, BMI2 — **D-CPU SSE4.2 floor MET** |
| RAM | 31.2 GiB (≥ 16 GB target ✓) |
| OS | **Windows 11 Education, 10.0.26200** (build 26200), x64 |
| GPU | **NVIDIA GeForce RTX 5070 Ti** (RTX 50-series — D-GPU qualified target) |
| GPU driver (WMI) | **32.0.16.1692**, dated 2026-09-03 |
| GL context (as the engine reports it) | **OpenGL 4.5 core** — `GL_RENDERER` = "NVIDIA GeForce RTX 5070 Ti/PCIe/SSE2" |
| Secondary adapter | AMD Radeon integrated (Ryzen iGPU) — not used for the GL tests |

This machine sits inside the **D-GPU** declared envelope (Win 10/11 x64, GL 4.5 core, ≥16 GB RAM,
NVIDIA incl. RTX 50-series) and meets the **D-CPU** floor. Per
[`support-matrix.md`](support-matrix.md) vocabulary it is the recorded **reference / baseline**
machine; full **qualified** status (mandatory catalog executed + passed) is a WO-13 deliverable.
The `/PCIe/SSE2` suffix in `GL_RENDERER` is NVIDIA's own driver string (the CPU capability the driver
detected for itself); it is unrelated to Cosmic's SSE4.2 floor.

**Still pending (owned downstream, not measurable on this one machine):** an exact **Windows 10** x64
edition/build (this reference is Windows 11 only) and its GPU/driver — WO-13 needs both Win10 and
Win11 qualification.

---

## 2. Build identity and toolchain

| Item | Value |
| --- | --- |
| CMake / CTest | **4.3.1-msvc1** (VS-bundled: `…\18\Community\…\CMake\bin\cmake.exe`; not on PATH) |
| Generator | **Visual Studio 18 2026**, platform **x64** |
| Compiler | **MSVC 19.51.36248.0** (toolset 14.51.36231; 14.44.35207 also installed, 14.51 selected) |
| Windows SDK | **10.0.26100.0** (targeting Windows 10.0.26200) |
| C++ standard | C++20 (`/std:c++20 /utf-8`), global `/MP` |
| Configure | exit **0** (`configure.log`) |

**Effective cache (key entries, `build/CMakeCache.txt`):**
`COSMIC_2D_ONLY=ON` · `COSMIC_WITH_JOLT=ON` · `COSMIC_BUILD_TESTS=ON` · `COSMIC_BUILD_RENDER_TESTS=ON`
· `COSMIC_BUILD_ENGINE_ONLY=OFF` · `COSMIC_WITH_ASSIMP=ON` (see note) ·
`COSMIC_SKIP_PROJECTS=Frontier;Engine3DDemo;ForgeIsle;ViperSim`.

**PUBLIC compile definitions.** `COSMIC_2D_ONLY` is the **only** engine define declared `PUBLIC`
([`Cosmic/CMakeLists.txt:234`](../../../../Cosmic/CMakeLists.txt)), so it — and only it — crosses the
target boundary into `CosmicTests`, `CosmicRenderTests`, `SF_Telem` and `Starforge`. Verified in the
generated `CosmicTests.vcxproj` (the define is present in every config section). Everything else is
`PRIVATE` to the engine:
- Engine (`Cosmic`) Debug: `COSMIC_BUILD_DLL, COSMIC_2D_ONLY, COSMIC_WITH_JOLT, JPH_USE_SSE4_1,
  JPH_USE_SSE4_2, JPH_CROSS_PLATFORM_DETERMINISTIC, JPH_ENABLE_ASSERTS, JPH_DEBUG_RENDERER, _DEBUG`.
- Engine (`Cosmic`) Release: adds `COSMIC_DIST` + `NDEBUG`, drops the two Debug-only JPH defines
  (so **any Release build is a distribution build**, per the root CMake comment).
- `JPH_USE_SSE4_1` / `JPH_USE_SSE4_2` compiled into the engine — this is the concrete origin of the
  **D-CPU SSE4.2 floor**.

> **Note — assimp.** `COSMIC_WITH_ASSIMP=ON` in the cache is a no-op in 2D: the engine gates it
> `if(COSMIC_WITH_ASSIMP AND NOT COSMIC_2D_ONLY)`, so **no `COSMIC_WITH_ASSIMP` define is emitted** and
> assimp/model-import is not compiled or linked. Confirmed absent from every generated
> `PreprocessorDefinitions`. The 2D project scanner also skips the four 3D flagships
> (Frontier, Engine3DDemo, ForgeIsle, ViperSim); `SF_Telem` and `Starforge` build in 2D.

---

## 3. Builds and warnings

| Build | Result | Wall time | Warnings |
| --- | --- | --- | --- |
| ALL (Debug, 2D) | exit **0** | 63.5 s | **0** (`build-debug.log`) |
| ALL (Release, 2D) | exit **0** | 112.1 s | **0** (`build-release.log`) |

Both configs produced `CosmicApp.exe` (launcher), `CosmicTests.exe`, `CosmicRenderTests.exe`,
`Starforge.exe`+`Starforge.dll`, and `SF_Telem.dll`. Zero compiler/linker warnings in either config.

---

## 4. Discovered 2D test surface (the "340" claim, checked not assumed)

Discovered by running the built binaries (`--list-test-cases` then a full run), **not** by trusting
the historical shorthand.

### 4.1 Unit tests — `CosmicTests` (headless, shared tier only in 2D)

| Config | Test cases | Assertions | Failed | Skipped | Exit |
| --- | --- | --- | --- | --- | --- |
| Debug | **340** | **116,476** | 0 | 0 | 0 |
| Release | **340** | **116,476** | 0 | 0 | 0 |

**Verdict on "340/340":** the 340 figure is the **real current 2D unit test-*case* count** — verified
by fresh discovery on both configs, not used as a placeholder. The shorthand hides that those 340
cases carry **116,476 assertions**, and that the count is *cases*, not assertions. In 2D mode the
suite is the shared tier of `tests/CMakeLists.txt`; the 3D-only tier (~22 files: terrain/voxel/nav/
mesh/skeletal/3D-renderer, added only `if(NOT COSMIC_2D_ONLY)`) leaves the build entirely, so a
missing 3D case cannot masquerade as a skip. Full case list: `../evidence/WO-02/2d-unit-testcases.txt`.

### 4.2 GPU render tests — `CosmicRenderTests` (real window + GL context, 320×180 offscreen goldens)

| Config | Test cases | Assertions | Failed | Exit |
| --- | --- | --- | --- | --- |
| Debug | **6** | 29 | 0 | 0 |
| Release | **6** | 29 | 0 | 0 |

The **current 2D GPU suite is 6 cases**: `sprites`, `tilemap`, `light2d`, `light2d A/B` (no-lights ==
byte-identical), `ui`, `scene2d` (full 2D frame through `SceneRenderer::RenderToTexture`). Confirmed
run on the RTX 5070 Ti (the run log shows the GL 4.5 context + `Renderer2D` init). There are **14**
committed golden PNGs total; the other 8 are 3D goldens that gate out in 2D.

**Goldens not regenerated (honest-gate check):** `COSMIC_UPDATE_GOLDENS` confirmed unset; the 14
golden PNG SHA-256 hashes are **byte-identical before and after** the render runs, and **0**
`*.actual.png` / `*.diff.png` artifacts were produced (`goldens-sha256-before.txt` ==
`goldens-sha256-after.txt`).

---

## 5. Audits

| Audit | Exit | Result |
| --- | --- | --- |
| `tests/check_gl_conformance.ps1` | **0** | Clean — no raw `gl*`/`GL_*` tokens outside `platform/OpenGL/`. |
| `tests/check_docs_coverage.ps1` | **0** | Clean — 147 public headers, 144 manifest rows, 7 skeleton reference chapters, 7 chapters outside `docs/reference/` (exempt from strict mode). |

The docs-coverage run prints a **WARNING** listing 4 `COSMIC_API` symbols not yet mentioned in three
still-skeleton chapters (`assets-io.md`, `jobs.md`, `rendering-pipeline.md`). Skeleton chapters WARN
only — the script exits **0**; these are documentation-completeness notes for the docs effort, not
build/test failures, and are out of WO-02 scope.

---

## 6. SF_Telem workflow baseline (current `main` vs SF-Stable)

### 6.1 Source parity vs SF-Stable — ESTABLISHED

`SF-Stable` = `fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e` ("New View Option on Main Telem",
**2026-06-28**), a strict ancestor of `main` (`main` is now **80** commits ahead, 0 behind).

Diffing `Projects/SF_Telem` from SF-Stable → `main`, the **entire tree is byte-identical except two
files**: `src/TelemHub.cpp` (+11/−1) and `src/TelemHub.h` (+12). That single change is the Phase 29
**W9 `IngestChunk` extraction** — a **behavior-preserving testability refactor**: the line-framing +
tag-routing + decode code moved verbatim out of `PumpSerial()`, which now calls
`IngestChunk(m_Link->Poll())`; the moved code's own comment states *"Behaviour is unchanged: an empty
chunk still returns before the accumulator is touched."* The header adds the public `IngestChunk`
declaration and three read-only counters (`GoodFrames`/`BadFrames`/`PacketCount`). The app's runtime
path is unchanged: `OnUpdate() → PumpSerial() → IngestChunk()`.

**Conclusion:** SF_Telem's supported workflows (wire protocol, decode, screens, recorder/replay
integration) are **source-identical** between SF-Stable and current `main` apart from this one
non-behavioral refactor that made the framing path drivable headlessly (`test_sftelem_hub.cpp`).

### 6.2 SF-Stable runtime baseline — NOT APPLICABLE in 2D (structural blocker), configure smoke recorded

Per the WO's escape hatch ("if the older build needs unavailable tooling, record the blocker and keep
source-parity evidence instead of claiming runtime parity"):

- **SF-Stable predates the engine split entirely** — its root `CMakeLists.txt` has **zero**
  `COSMIC_2D_ONLY` (it is a monolithic 3D-default tree). Therefore a **like-for-like 2D-only runtime
  baseline on SF-Stable cannot exist by construction.**
- Concrete data point: a throwaway detached worktree at `fa6ed9f` **configures successfully (exit 0)**
  with the current toolchain (VS 18 2026 / MSVC 19.51 / cmake 4.3.1) — but in its native monolithic
  mode: it auto-detects **SF_Telem *and* a separate `SF_TelemTest` project** (later folded into main's
  in-app "Testing" screen), and `COSMIC_BUILD_TESTS` is reported *"not used by the project"* (SF-Stable
  predates that option / test gating). `sfstable-configure.log`. The worktree was removed after;
  `main` and `engine-3d` were untouched; no `main` DLLs were copied into SF-Stable.
- A native **3D-monolith** build of SF-Stable is therefore *possible* but is **out of WO-02 scope**: it
  is not 2D-comparable, uses older test gating, and no such stream/COM hardware baseline is required for
  this milestone. The **current-main 2D runtime baselines in §7 stand as the reference** the gates
  calibrate against. No runtime parity is *claimed* for SF-Stable (it was not run) — only source parity.

---

## 7. Measured numeric baselines (arming the `../03` provisional bars)

Recorded as **baseline** on **DESKTOP-SEOA4BT** (identity in §1), `COSMIC_2D_ONLY=ON`. Per
[`numeric-bar-policy.md`](numeric-bar-policy.md) these arm the *measurement-derived* bars; a bar with
no armed baseline is "not yet gating," never a silent pass or a phantom failure.

| Bar (`../03`) | Provisional target | WO-02 baseline on this machine | State |
| --- | --- | --- | --- |
| **Normal close** (idle/open/streaming) | ≤ 2 s | SF_Telem (Release, `--project SF_Telem`, no COM): n=5 close min **0.131 s** / median **0.142 s** / max **0.149 s**; plateau-instance close 0.144 s; first probe 0.118 s. All exit 0, graceful `WM_CLOSE`→exit. | **MEASURED / armed** (idle+open only) |
| **Non-recording memory plateau** | Δ ≤ max(32 MiB, 5%); trend ≤ 1 MiB/10 min | SF_Telem idle, 60 s warmup + 180 s @5 s: initial **234.9 MB** → plateau median **225.3 MB** private (working set ~97 MB, handles 460–466, threads 23–27). Δ = **−9.6 MB**; **no growth** (see caveat). | **MEASURED / armed** (short cycle) |
| **SF two-hour recording peak** | private-bytes ceiling 2 GiB | Non-recording base **~225 MB**; recorded-series growth is bounded and small (§7.1): ~48 MB (60 Hz) / ~32 MB (40 fps/ESC) of columnar float data for 3 ESCs over 2 h, +~1× that as the `Flush` snapshot copy + vector-capacity slack. Live-recording curve not measurable (no stream — §7.2). | **PARTIAL** — base + increment bounded; **live curve `ENVIRONMENT_BLOCKED`** |
| **UI heartbeat during a connection op** | no gap > 250 ms; p95/p99 | Requires an actual connection operation (COM open) + a heartbeat probe. No COM device/virtual port, no instrumented harness yet. | **`ENVIRONMENT_BLOCKED`** — needs COM + WO-04 harness |
| **10,000-instance frame time** | p95 ≤ 16.67 ms, p99 ≤ 33.33 ms @1080p Release vsync-off, 10 s+60 s | No 1080p / vsync-off / 10k-instance perf harness exists yet (render suite is 320×180 offscreen). Machine is ready (RTX 5070 Ti). | **`ENVIRONMENT_BLOCKED`** — needs the WO-08 perf harness (R07) |
| **Stalled-open close / delayed-connect cancel / close-with-pending-export** (§1) | 2 s / bounded / 30 s | Need a stalled Bluetooth-SPP port, a live connect, and a 2-h recording fixture respectively. | **`ENVIRONMENT_BLOCKED`** — WO-05/WO-05a/WO-06 + COM |

**Caveat on the plateau "trend."** A straight-line fit over the whole post-warmup window is
**−43 MiB/10 min**, but that slope is an artifact of a **single one-time release** at t≈65 s (warmup
staging freed and the worker pool trimmed 27→24 threads, dropping ~235→~225 MB). Steady state
(t ≥ 75 s) is **flat within ~0.3 MB**. Either reading satisfies the leak-gate direction (no growth);
the honest statement is *"no measurable growth over the cycle,"* not *"−43 MiB/10 min drain."* This is
a WO-02 **baseline** over a 60 s+180 s cycle — **shorter** than the S01 soak oracle (10 min warmup +
5+5 min medians over a 2 h run), which is armed later (WO-13).

### 7.1 Recording private-bytes ceiling — extrapolation input (spec-derived)

`DataRecorder` stores **columnar** float history: per entity, `timestamps[frame]` + `columns[ch][frame]`
(`DataRecorder.h:221-232`), pre-reserved (zero-malloc hot path). In-memory cost per recorded frame:
`(channel_count + 1) × 4 bytes`. SF_Telem records **3 ESC entities**: `ESC_Right`, `ESC_Left`
(drive, 8 channels → 36 B/frame each) and `ESC_Weapon` (9 channels → 40 B/frame) = **112 B per
frame-set**.

| Cadence | Frames/entity over 2 h | Series data (3 ESCs) |
| --- | --- | --- |
| 60 Hz (doc 03 soak assumption; 432,000 samples/entity) | 432,000 | **~48.4 MB** |
| ~40 fps/ESC (F-SF-LONG, 25 ms; 288,000 samples/entity) | 288,000 | **~32.3 MB** |

So the recorded time-series itself is **tens of MB**, not gigabytes. The 2 GiB provisional ceiling is
dominated by the **non-recording base (~225 MB)** + the `Flush` full-snapshot copy (~+1× the series) +
vector-capacity doubling (avoided if `ReserveCapacity` covers the session) + CSV-`double` export
temporaries + ImPlot display rings. This makes the **non-recording plateau (§7) the dominant input**;
the recording increment is small and boundable. WO-06 measures the live curve + snapshot/flush
duration to set the real ceiling and loss window.

### 7.2 Why the connection/recording bars are blocked here (named prerequisite)

SF_Telem's only data source is a **Bluetooth-SPP COM port from an ESP32** (the `sf_telem_sim_test`
"simulator" is an ESP32 `.ino` sketch that runs on hardware, not the PC). `TestHub`/`TestingManager`
are bench-test *screens* that still consume the same real `SerialLink` — there is **no in-app
synthetic feeder**. No COM device, no paired virtual-COM pair (e.g. com0com — a kernel-driver install,
a system-settings change, out of scope), and no PC-side protocol feeder are present. Every bar that
needs a live/stalled connection is therefore `ENVIRONMENT_BLOCKED` and owned by the COM work orders
(WO-05a repro, WO-04 seam, WO-05 matrix, WO-06 recording), which are chartered to run "with real
Windows corroboration where hardware allows."

---

## 8. Existing failures and known issues (left visible)

**Automated surface: no failures.** Units (Debug+Release), the GPU render suite (Debug+Release), and
both audits are all exit 0 with 0 warnings. No new crash/hang/data-loss/leak was discovered by this
baseline pass, so **no new KI is added.**

The three pre-registered issues in [`known-issues.md`](known-issues.md) remain **open** (owned by later
WOs); WO-02 changes none of them:
- **KI-1** (snap-chip ImGui style-stack `abort()`, `ViewportController.cpp:1280-1310`) **ships in the
  2D editor** but is **not exercised** by the headless unit suite or the offscreen render suite (no
  `ViewportController` UI interaction runs in either), so it does not surface as a failure here. It is a
  live defect awaiting WO-07 — not a WO-02 regression, and explicitly **not** silently "green."
- **KI-2** (SerialLink connected-state unreachable headlessly) — this is exactly why §7's
  connection/recording bars are blocked; owned by WO-04/WO-05.
- **KI-3** (trunk defaults to 3D; CI cache key omits build mode) — unchanged; owned by WO-03. Note
  WO-02 had to pass `-DCOSMIC_2D_ONLY=ON` **explicitly** precisely because the default is still OFF.

---

## 9. DoD check (WO-02)

- [x] Clean isolated 2D Debug + Release build (fresh `build/`, no stale cache), exit 0, 0 warnings.
- [x] Current GPU suite built + run on a qualified GPU; **count recorded accurately (6 cases / 29
  assertions, 2D)**; goldens **not** regenerated (hashes identical).
- [x] Full environment captured: CMake/compiler/SDK/dependency versions, CMakeCache, PUBLIC compile
  definitions, GL renderer/version/driver, Windows build, CPU ISA features, RAM, machine identity.
- [x] `check_gl_conformance.ps1` (exit 0) and `check_docs_coverage.ps1` (exit 0) recorded.
- [x] The historical **340** claim **checked** (verified current, reported with its 116,476 assertions
  and per-config runs), **not** used as a success placeholder.
- [x] SF_Telem baselined on current `main`; SF-Stable **source-parity** established and the runtime
  blocker recorded (pre-split, no 2D mode) rather than claiming runtime parity.
- [x] Every provisional numeric bar has a **measured baseline** (normal-close, non-recording plateau) or
  an explicit **`ENVIRONMENT_BLOCKED`** with the named prerequisite + owner WO (heartbeat, 10k-instance,
  stalled-open/cancel/pending-export, live recording curve).
- [x] Existing failures recorded, not hidden (none in the automated surface; KI-1/2/3 remain open).

**Rollback:** reporting/build only — discard `build/`; no engine change to revert.

---

## 10. Evidence index (`../evidence/WO-02/`)

| File | Contents |
| --- | --- |
| `configure.log` | 2D-only CMake configure (exit 0) |
| `build-debug.log` / `build-debug.exit` | Debug ALL build + exit/time (0 warnings) |
| `build-release.log` / `build-release.exit` | Release ALL build + exit/time (0 warnings) |
| `units-debug-list.txt` / `units-debug-run.txt` | Debug `CosmicTests` list + full run (340/116,476) |
| `units-release-list.txt` / `units-release-run.txt` | Release `CosmicTests` list + full run |
| `2d-unit-testcases.txt` | Clean list of all 340 2D unit test-case names |
| `render-debug-list.txt` / `render-debug-run.txt` | Debug `CosmicRenderTests` (6 cases, GL log) |
| `render-release-list.txt` / `render-release-run.txt` | Release `CosmicRenderTests` (6 cases) |
| `goldens-sha256-before.txt` / `goldens-sha256-after.txt` | 14 golden PNG hashes, identical (no regen) |
| `audit-gl-conformance.txt` | GL conformance audit (exit 0) |
| `audit-docs-coverage.txt` | Docs coverage audit (exit 0) |
| `cpu-isa-probe.txt` | `__cpuid` ISA probe (SSE4.2 floor met) |
| `runtime-baselines.txt` | SF_Telem memory plateau + close-time samples and computed stats |
| `sfstable-configure.log` | SF-Stable (`fa6ed9f`) configure smoke (exit 0, native 3D-monolith) |
