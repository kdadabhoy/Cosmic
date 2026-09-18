# WO-10 execution report — 2026-09-18 (clocks, numerics and the synthetic analysis sample, N01–N04 + X01)

Only WO-10 was executed, directly on `main` (main-only campaign, D-WORKFLOW). All five
acceptance cases were implemented, driven through the WO-04 acceptance runner in **Debug and
Release** on the final binaries, and every one **PASSED** on the reference machine. The
production `Application` scheduler is now driven by the clock cases over an injected time
source (the `IFrameClock` seam — inert in shipping); four scheduler defects and the open KI-16
were found by the cases before any fix was written and are registered with failing-before /
passing-after evidence (**KI-51..KI-54, KI-16 fixed**; **KI-55, KI-56 registered open** as
documented limitations). The negative-global-scale policy is **reject** (written into
`contracts/contracts.md` §7). The analysis sample (`Projects/AnalysisSample`) builds **outside
the SDK checkout** against `COSMIC_SDK_DIR`, is packaged through the real editor path and runs
from a different working directory. **Real to-9km qualification remains deferred (D-9km):** X01
is a compatibility specimen on the synthetic F-TRAJECTORY / F-SERIES-LARGE fixtures — no
"qualified downstream consumer" claim is made. Nothing is `ENVIRONMENT_BLOCKED` in the final runs.
The two trunk source audits stay red for the pre-existing KI-39 reasons only (byte-identical
output to WO-09's; no WO-10 file adds a violation). The 17 goldens are byte-unchanged.

## Scope and provenance

- Initial `HEAD`: `a9789facc74cbc84eee381954fd870879af02e1d` ("Report WO-09 content/services
  results and evidence"). **The handoff said `main` was 3 ahead of `origin/main`; it was not —
  Kaden had pushed WO-09, `origin/main == main`, 0 ahead.** No branch, worktree, push,
  preservation-ref move or tag change was made; `engine-3d` and `cosmic-pre-2d-2026-09-16` were
  not touched; the registered worktree `.claude/worktrees/epic-clarke-338e7f` was left alone.
- The untracked root plan `Cosmic - 2D Trunk Consolidation & Acceptance Plan.md` and the
  untracked `recordings/` directory were never staged, moved or overwritten.
- Commits are authored **and** committed as `kdadabhoy <kdadabhoy28@gmail.com>` with no
  `Co-Authored-By`, AI or "Generated with" trailer; only explicit WO-10 paths were staged.
- The runner recorded `dirty=True` with `commit=a9789fa` in every `results.json` because the
  runs happened on the uncommitted WO-10 tree; the commit SHAs are given at the end.

## Toolchain and environment

- CMake `C:\Program Files\Microsoft Visual Studio\18\Community\…\CMake\bin\cmake.exe`, VS18 2026
  x64 (MSVC 19.51, dumpbin 14.44), configured `-DCOSMIC_2D_ONLY=ON` (cache pins
  `COSMIC_2D_ONLY:BOOL=ON`, `COSMIC_BUILD_TESTS=ON`, `COSMIC_BUILD_RENDER_TESTS=ON`;
  `configure.log`). Reconfigured after adding the new `.cpp` files (the test list is explicit)
  and again after the root `CMakeLists.txt` change (`COSMIC_SKIP_PROJECTS`).
- Full builds of every target: Release and Debug both **0 warnings / 0 errors**
  (`build-release.log`/`.exit`, `build-debug.log`/`.exit`).
- Acceptance runner: Windows PowerShell 5.1, absolute `-OutDir` under this directory,
  repository-local `-TempRoot C:\dev\Cosmic\build\wo10-accept-temp`, `-KeepArtifacts`,
  repository-local child `TEMP`/`TMP`, and `-GoldenDir C:\dev\Cosmic\tests\render\goldens`.
- Reference machine (from `results.json`): **DESKTOP-SEOA4BT**, Windows 11 Education 26200.9457,
  AMD Ryzen 7 7800X3D (SSE4.2), 31.2 GiB, **NVIDIA GeForce RTX 5070 Ti** driver 32.0.16.1692,
  OpenGL 4.5.

## Baseline facts recorded (and respected)

- The real clock at `a9789fa` (`Application.cpp:180-247`): `float time = (float)glfwGetTime()`,
  `float m_LastFrameTime`, `m_AbsoluteTime += rawTimestep` into a `float`; PASS 1A only
  `if (m_UseFixedTimestep && !m_Paused)`, frame time clamped to 0.25 s, `m_Accumulator +=
  frameTime * m_TimeScale`, `while (m_Accumulator >= fixedDeltaTime)` with a SIGNED delta;
  PASS 1B `scaledTimestep = paused ? 0 : raw * TimeScale` feeding `UpdateLayerTime` and
  `OnUpdate`; `SetFixedTimestepHz` clamps to 1..1000 with a warning; `Pause`/`Resume` orthogonal
  to `TimeScale`; **no clock injection seam**. A negative `TimeScale` drove the accumulator
  negative — no fixed tick, not reverse physics (confirmed, KI-54).
- `WorkspaceLayer` forwards a plugin `UpdateLayerTime(ts)`, `OnUpdate(ts * local)` and
  `OnFixedUpdate(fixed * local)`; a layer pushed directly on the `Application` gets `OnUpdate(ts)`
  and `OnFixedUpdate(fixed)` with its local scale reaching only its own `GetLocalTime()` —
  the two paths are distinguishable and both are observed (N01 `local-quarter` /
  `global-quarter`).
- `TimelineState` / `FixedSubstepper` (`test_timeline_state.cpp`, 10 cases incl. negative
  Speed) and `test_integrators.cpp` / `test_random.cpp` are the standalone units; N01 does not
  rely on them — it drives `Application::Run` end to end.
- `contracts.md` §5: the recorder / plot / display path stays **float**, scientific source values
  stay **double** outside it; the engine is not migrated to double. Numeric-bar policy: the
  "clock drift ≤ one 60-Hz step over 2 h" bar is measurement-derived and **not gating**.
- D-9km locked: synthetic fixtures only. `F-TRAJECTORY` is committed as
  `Projects/AnalysisSample/data/trajectory.csv` (SHA-256
  `ab0d5761845778eb37fdea44e3477f8b6e14b757778f736ae6694142493b9648` of the LF form git stores —
  a CRLF checkout (`core.autocrlf=true`) has a different on-disk hash but the same 1,201 values;
  generated independently by `tests/fixtures/wo10/gen_trajectory.py`; `*.csv` is gitignored, so
  it was staged explicitly with `git add -f`); `F-SERIES-LARGE` is generated from its metadata
  (equations + seed `0x0A105E21`), never committed as a blob.
- Reused primitives, not duplicated: `DataRecorder` (v1) → `DataPlayer` (load / seek /
  interpolate), `DataExport` (double CSV columns, the restricted numeric reader), `ImageIO::WritePNG`,
  `FrameBuffer::ReadPixels` (the WO-08 capture path), ImPlot (PUBLIC on the `Cosmic` target),
  the scaffold/SF_Telem external-project CMake shape, `StarforgeApp::PackageProject` →
  `BuildRunner` → `Packager::Stage/Finalize` (the editor packager; **no new packager**).

## What was built

**Engine — the clock seam (inert in shipping):** `Cosmic/src/core/IFrameClock.h` (`IFrameClock`
+ `GlfwFrameClock`, exported); `Application` gained a test-only constructor overload taking a
`std::unique_ptr<IFrameClock>` (the shipping constructor delegates with `GlfwFrameClock`),
`Run()` seeds and `RenderSingleFrame()` samples the clock through it. Nothing else about the
scheduler moved. Proof of inertness on the final Release `Cosmic.dll` (`dumpbin /EXPORTS`):
**0 exported or embedded `Fake*` symbols, 14 `IFrameClock`/`GlfwFrameClock` exports**; the
fake clock is `tests/FakeFrameClock.h` only, compiled into `CosmicTests.exe`, and the staged
`dist/AnalysisSample` carries no `FakeFrameClock` string in any binary.

**Engine — defect fixes (see the register):** `Application.h/.cpp` — the clock sample and
`m_LastFrameTime` in `double`, only the frame delta narrowed to `float` (KI-51);
`m_AbsoluteTime` a `double` accumulator behind the unchanged `float` getter (KI-51);
`SetTimeScale` rejects NaN / ±inf / negative (KI-52 / KI-54, the policy); `SetFixedTimestepHz`
rejects NaN, still clamps everything else (KI-53); the dead "signed fixed delta" branch removed
(the delta is always `+1/Hz`). `DataRecorder.h/.cpp` — `m_ElapsedTime` a `std::atomic<double>`
(single-writer load+store; lock-free), stored v1 timestamps still `float` (KI-16). Root
`CMakeLists.txt` — `AnalysisSample` on the project-scanner skip list in both modes, and the
skip-default migration's `CACHE INTERNAL` seed guarded with `if(NOT DEFINED …)` (INTERNAL
implies FORCE, so the seed overwrote the remembered default before the comparison and an
edited default could never migrate — found the moment the list changed).

**Headless tests (`CosmicTests`, tier U):** `tests/test_wo10_n02_clock.cpp` (headless half:
KI-16 over 432,000 ticks, negative replay through `DataPlayer`, the 24-h float-accumulator
arithmetic), `test_wo10_n03_numerics.cpp` (N03), `test_wo10_n04_determinism.cpp` (N04).

**Host tests (`CosmicTests`, tier W, `skip(true)`, one runner child per rung):**
`tests/test_wo10_n01_dispatch.cpp` (11 N01 rungs) and the host half of
`test_wo10_n02_clock.cpp` (8 N02 rungs incl. `drift-2h`), over `tests/WO10ClockHarness.h`
(the driver overlay + the integer-tick/double reference + the shared assertions),
`tests/FakeFrameClock.h`, `tests/WO10ClockReport.h` and the `WO10ClockFixture` runtime plugin
(`tests/WO10ClockFixture.cpp`, hosted by the real `WorkspaceLayer`).

**The analysis sample (X01):** `Projects/AnalysisSample/` — `CMakeLists.txt` (standalone
against `COSMIC_SDK_DIR`, `GAME_OUTPUT_DIR`, `COSMIC_2D_ONLY` option; refuses to be added
in-tree), `project.cproj`, `README.md`, `data/trajectory.csv`, `src/AnalysisFixtures.h` (the
two fixture generators + `LocalFrame`), `src/AnalysisSampleLayer.{h,cpp}` (the plugin layer),
`src/X01SelfTest.cpp` (the env-armed in-app harness), `src/AnalysisSample.cpp` (the plugin
exports). **Editor host:** `Projects/Starforge/src/X01PackageSelfTest.cpp` (armed by
`COSMIC_X01_PACKAGE` + `COSMIC_X01_PROJECT`; opens the external project and calls the real
`PackageProject()`), hooked at `OnAttach`/`OnUpdate`/`OnDetach` of `StarforgeApp`.

**Fixtures / oracles:** `tests/fixtures/wo10/gen_trajectory.py` (writes F-TRAJECTORY
independently of the engine), `tests/fixtures/wo10/pcg32_oracle.py` (an independent PCG32:
the 1,000,000-output FNV-1a-64 the N04 case pins).

**Runner:** `tests/acceptance/fixtures/Run-WO10Case.ps1` (WO-09's wrapper + `-IsolateCwd`
junction isolation for Application-hosting children, `-TestCaseExcludeMulti`, `-CompareRuns`
two-process byte comparison, `COSMIC_WO10_EVIDENCE_DIR`), `Run-WO10Sample.ps1` (X01: copy
outside the tree → editor package → run the package elsewhere → out-of-process numeric +
image + series oracles), manifests `wo10-units`, `wo10-host`, `wo10-sample`, `wo10-retained`,
`wo10-drift`.

**Docs:** `docs/reference/core.md` (`IFrameClock`, the constructor overload, `SetTimeScale`,
`SetFixedTimestepHz`, `GetAbsoluteTime`), `docs/reference/README.md` (manifest row),
`docs/guide/time-and-ticks.md` (the clock table, the pass diagram, the global-scale policy),
`contracts/contracts.md` (§5 record, new §7), `contracts/numeric-bar-policy.md` (WO-10
measurements, the clock-drift row, new spec-derived rows), `contracts/retained-feature-register.md`
(M9, M11), `contracts/known-issues.md` (KI-51..56, KI-16 disposition).

## Final-pass method

Every engine and test change was followed by a full `-m` rebuild of every target in both
configurations (0 warnings), the binaries were hashed (`binaries-sha256-final.txt`; the
pre-WO-10 hashes are in `binaries-sha256-at-a9789fa.txt`), and **all five manifests were then
run in Release and in Debug on those binaries**. No source changed after the hashes were taken;
nothing below is reported from a binary that differs from the final tree. The failing-before
evidence (`failing-before/`) was captured on the tree with only the inert seam added, before any
fix, and is not re-run.

## Acceptance-case status (final pass, final binaries, through the runner)

| Case | Tier | Debug | Release | Evidence |
| --- | --- | --- | --- | --- |
| N01 × 11 rungs (30hz, 60hz, 144hz, irregular, speed0, speed025, speed4, pause, stall, local-quarter, global-quarter) | W | PASSED 11/11 | PASSED 11/11 | `n01-<rung>-<cfg>/`, `wo10-host-runner-<cfg>/`, `host-runner-excerpts.txt` |
| N02 × 7 rungs (origin-0 / -2h / -24h, policy-hz, policy-scale-nan / -negative / -inf) | W | PASSED 7/7 | PASSED 7/7 | `n02-<rung>-<cfg>/` |
| N02-drift-2h (432,000 frames) | W | PASSED | PASSED | `n02-drift-2h-<cfg>/captures/n02-drift-2h.txt`, `wo10-drift-runner-<cfg>/` |
| N02-U (KI-16, negative replay, float-accumulator arithmetic) | U | PASSED (3 / 432,043) | PASSED (3 / 432,043) | `n02u-<cfg>/` |
| N03 numerics | U | PASSED (7 / 16,688) | PASSED (7 / 16,688) | `n03-<cfg>/` |
| N04 determinism (+ two-process byte compare) | U | PASSED (3 / 200,018 ×2), 4 files identical | PASSED (3 / 200,018 ×2), 4 files identical | `n04-<cfg>/same-build-compare.txt` |
| N04-FILTERS / N04-LOOKUP / N04-SCENE (retained suites) | U | PASSED | PASSED | `n04-*-<cfg>/` |
| X01 (package + packaged run + numeric oracle + image/series oracle) | I | PASSED (4 / 4) | PASSED (4 / 4) | `x01-<cfg>/` (`x01-package-result.json`, `x01-result.json`, `x01-trajectory-4.5s.png`, `series-large.meta.json`, `x01-package-payload.txt`, `x01-editor-console.txt`) |
| retained-units (`--test-case-exclude="WO-09 *,WO-10 *"`) | U | PASSED (401 / 23,184,880) | PASSED (401 / 23,199,635) | `retained-units-<cfg>/` |
| wo09-units (`WO-09 *`) | U | PASSED (40 / 6,190) | PASSED (40 / 6,190) | `wo09-units-<cfg>/` |
| wo10-units-all (`WO-10 *`, headless) | U | PASSED (13 / 216,703) | PASSED (13 / 216,703) | `wo10-units-all-<cfg>/` |
| retained-goldens (6 cases, 17 goldens) | G | PASSED (6 / 29), byte-exact | PASSED (6 / 29), byte-exact | `retained-goldens-<cfg>/goldens-sha256-*.txt` |
| retained-wo08-gpu (22 run + 2 R07 skipped) | G | PASSED (22 / 21,121,754) | PASSED (22 / 21,121,754) | `retained-wo08-gpu-<cfg>/` |
| retained-wo09-gpu (9 cases) | G | PASSED (9 / 1,606,171) | PASSED (9 / 1,606,171) | `retained-wo09-gpu-<cfg>/` |

`golden_mutated=False` in every `children.json`; no `.actual`/`.diff` diagnostics were produced;
the 17 goldens' SHA-256 before and after every GPU child are identical to WO-09's
`goldens-sha256-after.txt`. Counts: the unfiltered `CosmicTests` is now **467** cases = 401
retained + 40 `WO-09 *` + 13 `WO-10 *` headless + 13 `skip(true)` (10 native host, the WO-09
audio case, the 2 WO-10 host cases). (The handoff's "39 WO-09" is the same set the WO-09 runs
counted as 40 non-skipped `WO-09 *` cases; nothing was added to or removed from it.)
`CosmicRenderTests`: 6 goldens + 24 WO-08 (2 R07 `skip(true)`) + 9 WO-09, unchanged. Source
audits: `audit-gl-conformance.txt` (8 violations — KI-39's WO-07 fixtures) and
`audit-docs-coverage.txt` (2 unlisted headers — KI-39) are **byte-identical to WO-09's**;
`core/IFrameClock.h` is in the manifest and documented.

### N01 — the production dispatch over the injected clock

Each rung is a fresh `CosmicTests.exe` child: a real `Application` boots
`WO10ClockFixture.dll` through the real `WorkspaceLayer`, the driver overlay is pushed on the
`Application`, and `Application::Run` runs its normal loop — `PollEvents`, `RenderSingleFrame`
(accumulator, 0.25 s clamp, pause, `TimeScale`, layer dispatch, ImGui, `SwapBuffers`), the Safe
Zone — with only the value `RenderSingleFrame` samples from the clock scripted. Declared total
simulated time per rung: 10 s + 1/480 s (one eighth of a step clear of a tick boundary). Every
count below is asserted **exactly** against the integer-tick/double reference and was identical
in Debug and Release:

- **30 / 60 / 144 Hz / irregular (seeded 2–40 ms frames): 600 / 600 / 600 / 600 ticks** on both
  observers; per-frame variable dt within **1.8e-9 s** of the declared value (float bar
  1e-6 + 1e-5·dt); the 60-Hz schedule delivers **exactly one tick every frame** (before KI-51:
  140 zero-tick + 139 two-tick frames); the irregular schedule's largest frame yields 3.
- **speed 0 / 0.25 / 4: 0 / 150 / 2,400 ticks**; the fixed delta stays 1/60 (unscaled) while
  `OnUpdate` receives 0 / 1/240 / 4/60; uptime advances 10.002 s regardless.
- **pause** (frames 181..300 paused): **480 ticks**, 0 ticks and dt 0 in every paused frame, local
  time frozen while uptime runs, the resume frame earns **1** tick (no pause debt).
- **stall** (one 0.4 s frame): **591 ticks**; the stall frame delivers **15** (0.25 s) and the next
  frame **1** — 9 ticks (0.15 s) dropped and never repaid; `OnUpdate` received the unclamped
  0.4 s (pinned as the documented policy).
- **local-quarter vs global-quarter**: plugin-local 0.25 → the plugin sees `OnUpdate` 1/240 **and**
  `OnFixedUpdate` 1/240 over **600** ticks while the direct overlay sees 1/60 and 1/60; global
  0.25 → the plugin sees the same 1/240 `OnUpdate` but **150** ticks of an unscaled 1/60 fixed
  delta — the two scalings are distinguishable in the observed dt, as N01 requires.
- Stored-float accumulators (plugin `GetLocalTime()`, uptime): observed errors 4.5e-7 .. 1.7e-4 s
  over ≤ 1,441 frames, all inside the worst-case float-accumulation bound `N·ulp/2`.

### N02 — origins, policy, negative replay, drift, long uptime

- **Clock origins 0 / 7,200 / 86,400 s with 1/144 s (sub-step) frames:** 600 ticks at every
  origin, finite state, no local-time decrease, per-frame dt within **1.1e-10 s** of 1/144 at all
  three origins. **Before KI-51** the float sample gave a 3.8e-4 s error at 2 h (every frame over
  the bar) and **0 or 7.8 ms per 6.9 ms frame at 24 h, 599 of 600 ticks**.
- **policy-hz:** `SetFixedTimestepHz` 0 → 1 Hz, 1e9 → 1000, **NaN → rejected (stays 1000)**,
  +inf → 1000, −inf → 1, 60 → 60; 5,302 ticks = reference; the NaN window still delivers 1,500
  ticks and the first frame after the next valid rate earns 1 (before KI-53: 0 ticks, then a
  **91-tick burst**).
- **policy-scale-nan:** NaN rejected; 120 ticks during the request, 120 after `SetTimeScale(1)`,
  no non-finite dt anywhere (before KI-52: 0 and 0 — the accumulator was NaN for good, and NaN
  reached every `OnUpdate` and local time).
- **policy-scale-negative — the negative-global-scale policy:** `SetTimeScale(-1)` is **rejected
  with a warning and the previous scale kept**: **180 ticks during the 3 s it was requested and 180
  in the 3 s after +1 was set again**, never a signed fixed delta, local time monotonic (before
  KI-54: **0 and 0** — a 3 s restart debt, local time running backwards). Reverse playback stays a
  local-timeline feature (`Layer::SetTimeScale`, `DataPlayer::SetSpeed`, `TimelineState::Speed`).
- **policy-scale-inf:** +inf / −inf rejected, 360 ticks (before KI-52 the +inf run **never
  returned from the drain loop — the runner killed it at its 150 s deadline, TIMEOUT**).
- **Negative replay (headless):** a v1 recording of F-TRAJECTORY through the real recorder,
  `DataPlayer::SetSpeed(-1)` from t = 10 s plays back monotonically to 0 in 40 ticks of 0.25 s and
  auto-stops; `SampleAt` at exact and midpoint times matches the double equations (x within
  3.1e-5 m); NaN / inf speed and a NaN seek are ignored. `TimelineState` negative `Speed` stays
  covered by the retained `test_timeline_state`.
- **drift-2h (the proposed bar):** 432,000 frames of exactly 1/60 s through the production loop:
  **production ticks 432,000 vs reference 432,000 — drift 0 step(s)** against the proposed
  "≤ one 60-Hz step over 2 h" bar (measurement, **not gating**); uptime 7,200.0024 s vs
  7,200.0021 (+3.6e-4 s: the float `1/60f` frame deltas summed, not accumulator drift); the
  plugin's `Layer::GetLocalTime()` reads **7,183.15 s (−16.86 s)** — the stored-float
  quantisation the bar accounts separately (KI-56). Identical numbers in Debug (0 steps,
  7,183.15 / 7,200.0024); 0.45 ms/frame Release (193 s), 0.68 ms/frame Debug (293 s).
- **Long uptime / 24-h quantisation:** `float += 1/60` over 5,184,000 frames reads **83,794.8 s
  for 86,400 (−2,605 s, −3.0 %)**; over 432,000 frames 7,183.14 s (−16.86 s) — the exact number
  KI-16 showed in WO-06. `m_AbsoluteTime` and the recorder's time base no longer use that
  arithmetic; `Layer::m_LocalTime` still does (KI-56, open: an SDK-ABI change).
- **KI-16 (headless, passing-after):** 432,000 ticks of `1/60f` → duration **7,200.000488 s**
  (one float ulp above the exact float-tick sum 7,200.000376; 0.03 step from the nominal 7,200),
  every stored timestamp within one float ulp of the exact sum (max 2.4e-4 s); before: −16.8555 s.

### N03 — numerics at the catalog's parameters (both configs identical)

RK4 projectile dt = 1/480 for 1 s: **p rel err 1.37e-6, v 9.41e-6** (bound 1e-4); the same run
over a double state: 8.7e-9 — `IntegrateRK4`'s step arguments are `float` (dt/6 rounds to float),
a ~1e-8 floor recorded as a toolkit property, not a defect. Mass-spring-damper 2,000 steps at
1/1000: **8.81e-7** relative to the analytic envelope (bound 2e-3), energy decayed. RK4 halving
ratio **15.11** (10..24). Semi-implicit Euler max energy **1.015×** initial over 10 s (< 1.10).
F-TRAJECTORY: the generator equals the catalog equations bit for bit, the committed CSV
round-trips bit-exactly (1,201 rows), units are SI (apex t = 5.0986 s at y = 127.4645 m, `vy`
zero-crossing there, range x = 300 m / y = 9.6675 m at 10 s). **The float telemetry path vs the
double reference:** x max abs err **3.05e-5 m** (300 m span, rel 1.3e-7), y **4.61e-5 m**, `vy`
1.33e-5 m/s, every stored timestamp within 4.5e-7 s; every sample inside the domain bound
`2·ulp_f(value) + 2·|d/dt|·ulp_f(t)` (float storage + float interpolation + float time), the
large-value samples inside the generic float bar (6 of 4,804 samples near x = 0 exceed the
generic bar purely through the float time resolution — reported, not hidden). **Before KI-16**
this case failed: the 1,200th timestamp was 6.7e-5 s off = 2.0 mm x / 3.2 mm y. The 1e11
origin: double-subtract-then-float max err **7.6e-6 m** (half the double ulp at 1e11);
float-then-subtract **300 m** off.

### N04 — determinism (both configs identical)

Canonical PCG32 vector (seed 42 / stream 54) ✓. 1,000,000 outputs: two generators agree word
for word; **FNV-1a-64 `0x3654ce49c351b391` = the independent Python PCG32 oracle**
(`tests/fixtures/wo10/pcg32_oracle.py`), last word `0xef1e2afa`. Seeded Gaussian ×100,000
`0xea0fddffaacaf6b4` and a seeded sin/exp/log stream `0x9540463af85ebf45` — pinned under the
declared same-toolchain scope and **identical in Debug and Release**. Same-build
reproducibility: the wrapper ran the suite in **two processes** and the 4,000,000-byte PCG
stream, both 400,000-byte float streams and the hash file are **byte-identical** (SHA-256 in
`n04-<cfg>/same-build-compare.txt`; the `.bin` captures themselves are not committed — their
hashes are). The retained `Filters (E12)`, `LookupTable (E13)` and `2D scene determinism (W2)`
suites ran green as their own cases. No cross-GPU image or MP4 bit equality is required anywhere.

### X01 — the analysis sample (both configs PASSED 4/4)

1. **Copied outside the SDK source tree:** `Projects/AnalysisSample` →
   `build/wo10-sample-external/AnalysisSample` (fresh per run).
2. **Packaged through the real editor path:** `Starforge.exe` (armed) → `OpenProjectPath` →
   `PackageProject()` → `BuildRunner`: `cmake -S <external> -B <external>/build -A x64
   -DCOSMIC_SDK_DIR=C:/dev/Cosmic -DGAME_OUTPUT_DIR=<external>/build` + `--build --config
   Release` (**32.5 s**; every cmake line in `x01-editor-console.txt`) → `Packager::Stage` +
   `Finalize` → `dist/AnalysisSample/`: `AnalysisSample.exe` (CosmicApp renamed), `Cosmic.dll`,
   `AnalysisSample.dll` (size-identical to the external build output), `boot.cfg` naming the
   project, `assets/projects/AnalysisSample/{project.cproj,data/trajectory.csv}` (SHA-256 equal
   to the committed fixture), engine assets; no `src`/`build`/`CMakeLists.txt`/test exe in the
   payload (75 files, `x01-package-payload.txt`). In the Debug configuration the **Debug editor**
   packages; the packager's default (File ▸ Package) builds the project Release and stages from
   the Release runtime, so the app under test is the Release build in both configurations.
3. **Run from a different working directory** (a scratch dir; the exe re-homes to its own):
   the in-app harness loads **1,201 double rows == the equations**, generates F-SERIES-LARGE
   (100,000 × 8, finite; extrema where the equations put them — sin max 1 at an index ≡ 250 mod
   1,000, the 99 sawtooth wraps at every whole second, the step at 50,000; the two noise channels
   flagged seeded; metadata JSON written), records the local coordinates into a v1 recording
   (`DataRecorder`) and loads it (`DataPlayer`, duration 10 s); the 1e11 world origin subtracted
   in double reproduces every local value to **7.63e-6 m**; **Play** advances the head
   monotonically under the real frame clock (6 frames, 0 → 0.458 s), **Pause** freezes it for 10
   frames; **12 scrubs** (i = 0, 1, 119, 120, 600, 611, 1199, 1200 and the midpoints 0.5, 120.5,
   611.5, 1199.5 over 120): marker (float replay) vs plot (double series) **≤ 4.6e-5 m**, plot vs
   the equations **≤ 9.2e-5 m** (exact at a sample; at a midpoint the chord of the parabola,
   g/8·dt² = 8.5e-5 m, + the 1e11 double rounding); **Export** at t = 4.5 s: the viewport
   framebuffer (1022×504) read back and written as PNG, the marker pixel (463.09, 70.58) from the
   projection, the marker colour (255,64,25) at that pixel and the clear colour far away; a second
   copy through `user://exports` (portable `<exe>/user`). Verdict PASS, exit 0.
4. **Out-of-process oracle (PowerShell):** the 12 scrubs re-evaluated in double against its own
   `30t` / `50t − ½gt²`: worst marker-vs-oracle **4.15e-5 m**, plot-vs-equations **8.78e-5 m**, all
   scrub times present, velocities correct; the PNG decoded with System.Drawing: the marker colour
   at the pixel **derived from the double reference and the reported window** (463, 70) and 3 px
   beside it, the clear colour at (2, 2) and none of the marker colour four radii away, size
   1022×504, the `user://` copy byte-identical; the series metadata's analytic extrema,
   discontinuity markers and seed recomputed from the equations it declares.

The plots (position, speed, the 100k×8 series) are ImPlot panels docked in the sample's
workspace; their values are the double arrays asserted above (the selected sample is the plot's
double interpolation). They are not pixel-checked — the exported still is the marker/trail
scene. Play/pause/scrub are driven through the sample's transport functions (the ones the
buttons call), not through synthesized mouse input.

## Defects found (all registered before the fix; failing-before → passing-after)

| KI | What | Failing-before | Passing-after | Fix |
| --- | --- | --- | --- | --- |
| **KI-51** | The frame clock sampled as `float`: every delta quantised to the ulp of uptime (0/2-tick jitter at 60 Hz; 3.8e-4 s at 2 h; 0 or 7.8 ms per 6.9 ms frame at 24 h, 599/600 ticks); `m_AbsoluteTime` a float accumulator (−3 % after 24 h) | `failing-before/wo10-host-runner-Release/` (`N01-60hz`, `N01-speed4`, `N02-origin-2h`, `N02-origin-24h`) | `n01-60hz-<cfg>/`, `n02-origin-*-<cfg>/` | double sample, double `m_LastFrameTime`, float delta only; double uptime |
| **KI-52** | `SetTimeScale(NaN)` poisons the accumulator forever; `+inf` hangs (TIMEOUT) | `N02-policy-scale-nan` (119 of 360 ticks, NaN in every layer), `N02-policy-scale-inf` (TIMEOUT 150 s) | `n02-policy-scale-nan-<cfg>/`, `n02-policy-scale-inf-<cfg>/` | reject non-finite |
| **KI-53** | `SetFixedTimestepHz(NaN)` survives `std::clamp`: no ticks + a 91-tick burst on the next rate | `N02-policy-hz` | `n02-policy-hz-<cfg>/` | reject NaN, clamp the rest |
| **KI-54** | Negative global `TimeScale` = no fixed tick + a hidden restart debt | `N02-policy-scale-negative` (120 of 480 ticks; 0 in the 3 s after +1) | `n02-policy-scale-negative-<cfg>/` | **policy: reject negative**; the signed-delta branch removed |
| **KI-16** | Recorder float time base −16.86 s after 2 h (and 6.7e-5 s over the 10-s fixture = mm-level replay error) | `failing-before/wo10-units-runner-Release/` (`N02-U`, `N03`) | `n02u-<cfg>/`, `n03-<cfg>/` | double accumulator behind the float v1 timestamps |
| **KI-55** | A huge finite scale (1e30) = an astronomically long drain loop | — (documented; the case would only reproduce KI-52's TIMEOUT) | — | **open**: a ceiling is Kaden's call (proposed 1,000×) |
| **KI-56** | `Layer::m_LocalTime` float accumulator: −16.9 s after 2 h, −3 % after 24 h | `N02-U` arithmetic; `n02-drift-2h-<cfg>/` | — | **open**: documented; the fix changes `sizeof(Layer)` (SDK ABI) |

Every counterfactual was captured on the working tree before the fix (Release; the runner
JSON/JUnit + per-case logs under `failing-before/`) and re-run after it on rebuilt binaries in
both configurations; no destructive reset was used, no golden was regenerated, no tolerance or
deadline was loosened. Bounds that were corrected while writing the cases (not engine
behaviour) are recorded in the test sources: the RK4 double-state floor (float step arguments),
the 1e11 double-ulp term, and the float replay path's domain bound.

## The negative-global-scale policy (as written into `contracts/contracts.md` §7)

`Application::SetTimeScale` accepts only **finite values ≥ 0**; NaN, ±inf and **negative** values
are **rejected** with `CS_CORE_WARN` and the previous scale is kept — no hidden restart debt, no
poisoned accumulator, no unbounded drain loop; `OnFixedUpdate` always receives `+1/Hz`.
Reverse playback belongs to the local timelines: `Layer::SetTimeScale(<0)` (plugin-local, also
the `dt` a `WorkspaceLayer`-hosted plugin is handed), `DataPlayer::SetSpeed(<0)`,
`TimelineState::Speed < 0`. `SetFixedTimestepHz` rejects NaN and clamps everything else to
`[1, 1000]`. The 0.25 s clamp drops (never repays) the excess; pause carries no debt.

## Measured numbers vs the proposed bars

| Quantity | Proposed bar | Measured (Release / Debug identical unless noted) | Gating? |
| --- | --- | --- | --- |
| Clock drift over 2 h (432,000 frames) | ≤ 1 step (60 Hz) | **0 steps** (432,000 = 432,000) | no — measurement-derived, arms the bar for WO-13 |
| 24-h stored-float quantisation | "account separately" | float `+= 1/60` → **−2,605 s (−3.0 %)**; `Layer::GetLocalTime()` −16.86 s at 2 h; `m_AbsoluteTime` now double (7,200.0024 vs 7,200.0021 at 2 h) | no (caveat, KI-56) |
| Per-frame variable dt | float bar 1e-6 + 1e-5·dt | ≤ 8.7e-10 s at origins 0 / 2 h / 24 h | pinned by N01/N02 |
| Float replay path vs double reference | float bar / derived domain bound | x 3.05e-5 m, y 4.61e-5 m (F-TRAJECTORY) | pinned by N03 |
| RK4 / oscillator / order / energy | 1e-4 / 2e-3 / 10..24 / <1.10 | 1.37e-6 / 8.81e-7 / 15.11 / 1.015 | pinned by N03 |
| PCG 1,000,000 outputs | canonical + same build | oracle hash equal; two processes byte-identical | pinned by N04 |

## Notes, limits and honest caveats

- **To-9km qualification remains deferred (D-9km).** X01 is a compatibility specimen on synthetic
  fixtures; no real schema, units, epoch or volume was exercised and no "qualified downstream
  consumer" is claimed. The 1e11 offset case tests the sample's conversion only.
- The N01/N02 host rungs need a real window + GL context (the real `Application` boots
  `WorkspaceLayer` + ImGui); they are tier W with `requires: gpu-gl` — a machine without one is
  `ENVIRONMENT_BLOCKED`, never a pass. The GLFW clock itself is never measured by them (the seam
  replaces it); the drift number is the scheduler's, over exact frame deltas.
- The 60-Hz "exactly one tick per frame" is a property of an EXACT injected schedule; real frame
  times jitter far more than a float ulp, so 0/2-tick frames remain normal in production — the
  total over any window is what the reference pins.
- The RK4 double-state reference has a ~1e-8 floor because `IntegrateRK4`'s step arguments are
  `float`; documented in the M11 row, not changed (the toolkit's float API is the engine's).
- `wo09-units` counts 40 non-skipped `WO-09 *` cases (the WO-09 report's "39" + the crossbuild
  extension is named `2D engine (WO-09 C05)…` and lives in the retained 401); nothing in that
  set changed.
- The Debug X01 run packages with the Debug editor but, by the packager's own default, builds and
  stages the Release sample (`m_PkgOpt.ReleaseBuild`); packaging the Debug runtime from the
  editor's isolated directory is not a path the packager offers (it stages from the Release
  runtime dir).
- Evidence hygiene: the wrappers' scratch dirs and the junction-linked editor tree are removed per
  child; the runner's `_temp` lived under `build/`; the gitignored `*.log` files stay uncommitted;
  the N04 `.bin` captures (4 MB each) are deleted after hashing (`same-build-compare.txt` keeps
  the SHA-256s); `results.json` / `results.junit.xml` / `children.json` / `*-excerpts.txt` /
  the X01 JSON + PNG + metadata + payload listing + editor console / the drift captures / the
  failing-before runner JSON are committed. `dist/AnalysisSample` and
  `build/wo10-sample-external` are gitignored build outputs.
- Not done, by scope: WO-11 packaging/install beyond X01's own package, WO-12 docs cleanup,
  animated/MP4 export (A1), real to-9km data or schema, 3D content, the KI-39 audit fixes, the
  KI-55 ceiling and the KI-56 `Layer` change (both need a decision), ratifying the light ceiling.

## Files

- Engine: `Cosmic/src/core/{IFrameClock.h,Application.h,Application.cpp}`,
  `Cosmic/src/telemetry/{DataRecorder.h,DataRecorder.cpp}`, root `CMakeLists.txt`.
- Sample: `Projects/AnalysisSample/{CMakeLists.txt,project.cproj,README.md,data/trajectory.csv,
  src/AnalysisFixtures.h,src/AnalysisSampleLayer.h,src/AnalysisSampleLayer.cpp,src/X01SelfTest.cpp,
  src/AnalysisSample.cpp}`; editor host `Projects/Starforge/src/X01PackageSelfTest.cpp`,
  `StarforgeApp.{h,cpp}`.
- Tests: `tests/{test_wo10_n01_dispatch,test_wo10_n02_clock,test_wo10_n03_numerics,
  test_wo10_n04_determinism,WO10ClockFixture}.cpp`, `tests/{FakeFrameClock,WO10ClockReport,
  WO10ClockHarness}.h`, `tests/CMakeLists.txt`, `tests/fixtures/wo10/{gen_trajectory.py,pcg32_oracle.py}`.
- Runner: `tests/acceptance/fixtures/{Run-WO10Case,Run-WO10Sample}.ps1`,
  `tests/acceptance/manifests/wo10-{units,host,sample,retained,drift}.manifest.json`.
- Docs: `docs/reference/{core.md,README.md}`, `docs/guide/time-and-ticks.md`,
  `docs/plans/2d-stability-2026-09-16/contracts/{known-issues,contracts,numeric-bar-policy,
  retained-feature-register}.md`, this directory.

## Local commits (not pushed — Kaden pushes)

1. `7bf6934b76703e2a48bd1bdc2a01bb07fbc67d24` — Fix the frame-clock sample, time-scale/rate policy
   and recorder time base KI-51..KI-54 + KI-16; add the IFrameClock seam (WO-10): the engine
   changes, the root CMake skip list + migration guard, the register entries KI-51..KI-56, the
   three contract updates and the reference/guide chapters.
2. `156d9b468488fb34eb2589b85c1a7902cc3b71e9` — Add N01–N04 clock/numerics acceptance cases, the
   X01 analysis sample, its editor package harness and the wo10 runner manifests (WO-10): every
   test, fixture, generator/oracle, wrapper and manifest, the test CMake list, the sample project,
   the Starforge X01 harness + hooks.
3. The evidence commit that carries this report and `evidence/WO-10/**` (the gitignored `*.log`
   files and the N04 `.bin` captures are not committed).

Author and committer on all three: `kdadabhoy <kdadabhoy28@gmail.com>`, no trailers. `main` is
3 ahead of `origin/main` (`a9789fa`); Kaden's push = `git push origin main`.
