# Cosmic 2D — known-issue register

Status: WO-00 decision record, 2026-09-16. Gate G0.
Revalidated at HEAD `72b47771c869666f3a645a47bfbfae917d3167f2`.

**This is the running register. Every later work order appends to it.** Add every newly discovered
crash, data-loss, hang, deadlock, use-after-free, stale callback, or silent-corruption defect here,
each with a minimal regression and a disposition. A missing fixture, a skipped test, or missing
equipment is **never** logged here as a pass — it is `ENVIRONMENT_BLOCKED` in the acceptance run.

Append format (copy the block below for a new entry):

```
### KI-N — <one-line title>
- Status: <Confirmed defect | Coverage gap | Enforcement gap | Suspected / not reproduced>
- Owner WO: <WO-xx>
- Anchor: <file:line at HEAD sha>
- Repro: <minimal steps / command / fixture>
- Regression: <test id/path once written; "none yet" until then>
- Disposition: <open | fix landed <sha> | won't-fix (reason)>
```

---

### KI-1 — Editor viewport snap-chip aborts on click (ImGui style-stack imbalance)

- **Status:** Confirmed defect.
- **Owner WO:** WO-07 (fix; L05 asserts ImGui stack balance).
- **Anchor:** `Projects/Starforge/src/ViewportController.cpp:1280-1310` — the `snapChip` lambda.
  The imbalance is the `PushStyleColor` at `:1288` (guarded by `if (on)` at `:1286-1289`) paired
  with the `PopStyleColor` at `:1293` (guarded by `if (on)` at `:1292`), while the button in
  between **flips `on`** (`:1290-1291 if (ImGui::Button(...)) on = !on;`). On the click that
  toggles a chip, the push and pop see opposite values of `on` → the ImGui colour stack is left
  unbalanced by one.
- **Reachability:** the three `snapChip(...)` calls (`:1306-1313`, Move/Rotate/Scale) render
  **before** the `#ifndef COSMIC_2D_ONLY` fence at `:1337`, so the bug **ships in the 2D editor**.
- **Contrast (proof it is a real, already-understood pattern):** the sibling `toggle` lambda at
  `:1314-1332` was already fixed — it latches `const bool pushed = on;` at `:1322` and guards both
  push (`:1326`) and pop (`:1331`) on `pushed`, so the flip cannot unbalance it. `snapChip` never
  received the same fix.
- **Repro:** launch Starforge (Debug, `COSMIC_2D_ONLY=ON`), open a scene, click any of the three
  snap chips in the viewport strip. Debug: assert + `abort()`. Release: silent style-stack
  corruption. The Phase 29 W7 on-GPU pass already observed this in both 2D and 3D editors.
- **Regression (WO-07):** `Projects/Starforge/src/Ki1SnapChipSelfTest.cpp` — a gated in-editor
  harness (armed by `COSMIC_KI1_SELFTEST`) that opens a real 2D edit scene and actuates the **real**
  `ViewportController::DrawViewportOverlays` snap chip through Dear ImGui for both toggle directions,
  reading the colour-stack size **at the widget** (before ImGui 1.92's end-of-window error recovery
  masks it — the naive after-frame check nets to zero in Release). Driven through the WO-04 runner as
  case `KI-1` (`tests/acceptance/manifests/wo07-ki1.manifest.json` +
  `tests/acceptance/fixtures/Run-Ki1SelfTest.ps1`). Evidence in `evidence/WO-07/ki1/`:
  - **Failing-before Debug** — `abort()`, exit 3, ImGui `IM_ASSERT` "Calling PopStyleColor() too many
    times!" in window `Untitled###Viewport` (`failing-before-Debug.stdout.log`).
  - **Failing-before Release** — no crash, but `colorDelta=-1` (OFF→ON spurious pop) and `colorDelta=+1`
    (ON→OFF leaked push) at the chip, verdict FAIL exit 1 (`failing-before-Release.result.json`) —
    the silent case a whole-frame no-crash check misses.
  - **Passing-after Debug + Release** — 4/4 actuations registered, imbalance 0, verdict PASS exit 0
    (`passing-after-*.result.json`); runner PASSED both configs (`evidence/WO-07/runner-*/results/`).
- **Fix:** mirrors the `toggle` lambda — latches `const bool pushed = on;` before the button and
  guards both the `PushStyleColor` and `PopStyleColor` on `pushed`
  (`Projects/Starforge/src/ViewportController.cpp`, the `snapChip` lambda).
- **Disposition:** **fix landed (WO-07 local commit, 2026-09-17).** Both configs build 0-warn; all
  385 retained headless tests still pass. Reproduce via the runner: see `evidence/WO-07/report.md`.

### KI-2 — SerialLink connected-state behaviour is unreachable headlessly (coverage gap)

- **Status:** Coverage gap (not a proven defect — a hole where the reported COM crashes could hide).
- **Owner WO:** WO-04 (build the injectable transport seam), WO-05 (use it for the connected-state
  matrix). WO-05a reproduces the *reported* close/link-loss crash early.
- **Anchor:** `Cosmic/src/serial/SerialLink.cpp:56-70` — auto-reconnect: it calls
  `m_Port.BeginOpen(...)` at `:67` on the reconnect interval — and the one-shot edge
  `ConsumeJustConnected` at `:115`. Neither is reachable without a port that actually opens.
- **Stated in source:** `tests/test_serial_lifecycle.cpp:14-20` (the "COVERAGE NOTE") — SerialLink
  has no injectable byte transport and no public port setter, so connected-state (auto-reconnect
  timing, the `ConsumeJustConnected` edge) is *not reachable headlessly*; the injectable transport
  is explicitly deferred (plan doc 28 §9.6). A rapidly-failing `COM999` proves the unreachable-port
  policy at the `SerialPort` level but does **not** reproduce a stalled Bluetooth open, a pending
  read, a mid-stream link loss, or close-with-live-port.
- **Related source facts (not defects, but the surface WO-05 must test):**
  `SerialPort::Close` joins the connection worker (`SerialPort.cpp:11,26,40`), and `BeginOpen`
  joins any prior connect thread before starting a new one (`SerialPort.cpp:66`) — a Bluetooth SPP
  port can sit inside `CreateFileA` for 10–20 s, which is why bounded cancellation + caller/thread
  ownership need an explicit contract (see [`contracts.md`](contracts.md) §1).
- **Repro:** cannot be reproduced headlessly today — that is the gap. WO-04 adds a narrow
  fake-transport seam (controls byte delivery / open outcome / timing; does **not** reimplement the
  parser or the connection state machine); WO-05/WO-05a then drive delayed-open, abandoned-open,
  pending-read, link-loss, and close-with-live-port with completion barriers, corroborated on real
  Windows virtual-COM + USB/Bluetooth hardware where available (T03/T04).
- **Regression:** none yet (owner WO-04 seam → WO-05 matrix).
- **Disposition:** open (coverage). Any actual crash found via the new seam becomes its own
  Confirmed-defect KI entry.

### KI-3 — Trunk does not enforce 2D; CI cache key omits build mode (enforcement gap)

- **Status:** Enforcement gap.
- **Owner WO:** WO-03.
- **Anchors (all at HEAD):**
  - `CMakeLists.txt:67` — `option(COSMIC_2D_ONLY "Build the 2D-only engine (excludes all 3D subsystems)" OFF)`: the default is **OFF** (3D).
  - `.github/workflows/ci.yml:41` — `cmake -S . -B build -A x64 -DCOSMIC_BUILD_TESTS=ON`: CI configures **without** `-DCOSMIC_2D_ONLY=ON`, so it builds the default 3D engine.
  - `.github/workflows/ci.yml:36` — cache key `cmake-build-${{ runner.os }}-${{ hashFiles('CMakeLists.txt', 'Cosmic/CMakeLists.txt', 'Runtime/CMakeLists.txt', 'tests/CMakeLists.txt') }}`: the key is keyed on CMake **file hashes only** — it omits the build mode / the `COSMIC_2D_ONLY` value. Flipping the flag without editing those files reuses a **stale, opposite-mode** `build/`.
  - `.github/workflows/release.yml:24` — `cmake -S . -B build -A x64 -DCOSMIC_BUILD_TESTS=OFF`: release staging also omits the 2D flag.
  - `package.bat:48` — `if exist build rmdir /s /q build` discards the cache, then `:54/:56`
    `cmake .. [-A x64] -DCOSMIC_BUILD_ENGINE_ONLY=OFF` configures **without** the 2D flag → a clean
    package can ship 3D.
- **Repro:** on a checkout with a locally-selected 2D cache, run a fresh default configure (or CI /
  `package.bat`): the result is the 3D engine, because none of the default entry points sets the
  flag. Then flip the flag in CI without touching the four hashed CMake files: the cache restore
  serves a stale opposite-mode `build/`.
- **Regression:** none yet. WO-03 makes default/CI/scaffold/packaging/release explicitly select the
  supported 2D mode (or reject an unsupported OFF on the trunk), and folds build **mode** +
  architecture + toolchain into the cache key. B03–B05 prove it from both clean and stale-cache
  scenarios; a release job cannot bypass the 2D contract.
- **Policy tie-in:** the recommended enforcement is *default ON + configure-time rejection of OFF on
  the supported trunk, OFF retained only on `engine-3d`* — see [`contracts.md`](contracts.md) §6 and
  [`support-matrix.md`](support-matrix.md).
- **Disposition:** open.

---

### KI-4 — Delayed open stalls shared-root teardown
- Owner: WO-05; diagnosis: WO-05a.
- Before: real SF_Telem OnDetach with a barrier-confirmed 2500-ms open took 2507 ms,
  exceeding the approved 2000-ms budget. `evidence/WO-05/before/`.
- Fix/regression landed first in local commit `552ef45`; shared-owned late cleanup,
  cancellation before waiting, and owner-thread session adoption. T04 qualification
  remains hardware-blocked; software proof does not close the physical gate.

### KI-5 — Close releases a session with an app-owned writer still active
- Owner: WO-05. Before: isolated WO-04 SerialPort source with only a signature adapter
  passing null cancellation to the extended Write seam; root OnDetach returned with
  one active writer. `evidence/WO-05/write-before/`; writer joined before root destruction.
- The WO-05 fix serializes writes/device release and drains cancellation. Regression:
  `WO-05 T03: close drains an app-owned in-flight writer...`.

### KI-6 — A non-polling screen allows an unbounded serial receive queue
- Confirmed before fix: `evidence/WO-05/queue-before/`; 1049088 bytes remained queued
  when the consumer stopped polling. Main's 4096-byte parser purge cannot bound the
  upstream serial queue while Home/Analysis/loaded Replay does not poll it.
- Owner: WO-05 / T06. Fix: 1-MiB queue preserving the accepted prefix, with
  explicit overflow and close-discard byte counters. Parser/firmware remain unchanged.
  Passing Debug/Release T06 evidence is in `evidence/WO-05/`; follow-on local commit
  recorded by the WO-05 report. Physical qualification remains blocked.

### KI-7 — A rapid silent-stall reconnect retains the previous partial text frame
- Confirmed before fix: `evidence/WO-05/generation-before/`; a reconnect completes
  between policy ticks, so the old open/closed edge detector never emits
  ConsumeJustConnected. The real root's TelemHub retains six old bytes and rejects
  the following otherwise valid frame. Owner: WO-05 / T06.
- Fix: monotonic adopted-session generation instead of a sampled boolean
  open edge, preserving one notification per session even without a closed frame.
  Passing Debug/Release T06 regression evidence is in `evidence/WO-05/`.

### WO-05 update — KI-2 deterministic coverage gap
- The real SF_Telem root, shared screens/services and host unload now use the seam:
  225 crossed root schedules x100, plus 18 fresh host close/launcher schedules x100.
- Both previously reserved WO-05a H1/H3 cases are active. Virtual-COM, representative
  USB/SPP and the second Windows version remain T04 ENVIRONMENT_BLOCKED; software
  coverage does not certify driver cancellation.

### KI-8 — PS5.1 acceptance minTests aborts on an empty stdout file
- Owner: WO-05 (discovered while auditing T05). Before:
  `evidence/WO-05/runner-before.txt`; minimal empty-output nonzero child causes
  Regex.Match(null) in the WO-04 runner and aborts the parent before JSON/JUnit.
- Regression: `wo05-runner-repro.manifest.json` self-test must classify FAILED,
  expected minimum1 / observed-1, and preserve complete evidence. Fix: skip the
  regex call for empty stdout, leaving the missing-count failure unchanged.
  `evidence/WO-05/runner-after-fixed/` classifies FAILED and self-test succeeds;
  no count requirement or verdict is weakened.

### KI-9 - Cancelling a held open exceeds the UI connection-service budget
- Owner: WO-05 / T03. Before: `evidence/WO-05/cancel-before/`; all 100
  barrier-confirmed non-cooperative opens require about 501 ms for Disconnect,
  exceeding the approved 250-ms owner responsiveness budget. Root lifetime and
  eventual late cleanup still balance; this is a cancellation grace defect.
- Regression: `WO-05 T03: cancel a non-cooperative open keeps the owner responsive...`.
  Reduce the grace to 100 ms; retain independent job/transport ownership for late
  completion. Passing-after: `evidence/WO-05/current-debug/` and
  `current-release/`, all 100 iterations/config <=250 ms, maxima 121.911/116.552 ms.
  Software regression is proven; physical driver qualification remains blocked.

## WO-06 findings (2026-09-17)

### KI-25 — Held reverse playback at a zero-duration recording misses its lower endpoint
- Status: Confirmed defect in the endpoint fix. Owner WO: WO-06.
- Anchor: Tick's upper branch takes priority when duration and position are both zero.
- Regression: `WO-06 D02: reverse and zero transport at held endpoints`, independent
  one-sample timestamp-zero specimen, held reverse Tick(0) must stop at the lower endpoint.
- Failing-before: evidence/WO-06/endpoint-zero-before/D02.out.log; one armed test,
  one failed assertion for reverse speed at coincident endpoints.
- Disposition: fixed in the local WO-06 commit; D02 passes in accepted-Debug/Release.
  Handle the coincident endpoints by stopping nonzero travel intent,
  while zero speed retains Play intent.

### KI-24 — Disabling ordinary auto-export bypasses mandatory ceiling finalization
- Status: Confirmed defect in the WO-06 limit guard. Owner WO: WO-06.
- Anchor: RecordFixed calls StopRecording, which respects the ordinary auto-export checkbox.
- Regression: `WO-06 D05: recording ceiling stops and finalizes` now crosses both public
  auto-export policy values and requires final scene.bin before shutdown can mask the omission.
- Failing-before: evidence/WO-06/policy-before/D05.out.log; one armed test, two failed
  assertions: final scene.bin absent and intentional data still dirty before shutdown.
- Disposition: fixed in the local WO-06 commit; D05 passes in accepted-Debug/Release.
  Ratified stop-and-finalize exports at the ceiling regardless of
  the ordinary manual-stop preference. Preserve the preference for manual Stop.

### KI-22 — A manual monitoring export failure does not remain dirty
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: TelemHub.cpp/ExportRecording never marks monitoring data as intentional.
- Regression: `WO-06 D05: manual monitoring export failure remains dirty`;
  one monitoring tick, write failure, owner update, shutdown. No Start command.
- Failing-before: evidence/WO-06/monitor-before/D05.out.log; dirty=false after failed
  manual export and shutdown skips finalization, leaving no recording.
- Disposition: fixed in the local WO-06 commit; D05 passes in debug-complete/release-complete.
  The explicit Export command establishes keep/save intent before
  starting/queuing its write. Clean only after verified successful publication.

### KI-23 — Upper endpoint stops reverse and zero-speed playback on a held tick
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: DataPlayer.cpp/Tick upper endpoint unconditionally clears Playing.
- Regression: `WO-06 D02: reverse and zero transport at held endpoints`;
  reverse at duration with dt=0, zero speed at both endpoints.
- Failing-before: evidence/WO-06/endpoint-before/D02.out.log.
- Disposition: fixed in the local WO-06 commit; D02 passes in debug-complete/release-complete.
  Clamp upper bound but stop only for forward speed, retaining
  reverse/zero intent until reaching the endpoint in the travel direction.

### KI-21 — Counterfactual source restore preserves an older mtime and stale object
- Status: Confirmed harness defect. Owner WO: WO-06.
- Repro: Copy-Item restores correct DataPlayer.cpp bytes with the saved older mtime;
  MSBuild does not rebuild the newer original-source object. The purported after
  case still fails 4007 assertions. Source hash is correct, binary is stale.
- Failing-before: evidence/WO-06/player-after/D03.out.log and build-debug-restored.log
  (no DataPlayer.cpp compile). No result was accepted as a pass.
- Disposition: corrected; player-after-fixed passes after explicitly invalidating restored source mtime,
  rebuilding and rerunning the
  armed D03 regression and capture source/binary hashes after restoring.

### KI-20 — v1 fixed-width metadata is used as an unbounded C string
- Status: Confirmed defect (analysis and counterfactual reproduction). Owner WO: WO-06.
- Anchor: DataPlayer.cpp at 0435d3c constructs strings from 64/32-byte arrays
  without checking for a terminating NUL; malformed metadata can read beyond them.
- Regression: F-CORRUPT mutations 4/6/7 in `WO-06 D03: bounded independent...`;
  full-width non-NUL names/tags/channels must reject and clear load state.
- Before evidence: evidence/WO-06/player-before/D03.out.log, isolated original HEAD
  DataPlayer.cpp (restore in finally); 4007 failing assertions include full-width
  metadata acceptance, invalid sample rate/timestamps and mixed-file fallback state.
- Disposition: fixed guard; evidence/WO-06/player-after-fixed has both D03 tests
  passing after explicit source-mtime invalidation and rebuild. Source restored byte-exactly.

### KI-19 — Independent long-fixture auditor confuses wire IDs with metadata tags
- Status: Confirmed harness defect. Owner WO: WO-06.
- Repro/regression: Verify-WO06.py expects R/L/W metadata tags, although v1 SF
  descriptors use Drive/Weapon (`TelemHub.cpp/TagFor`). The first independent audit
  fails before numeric checking. Wire IDs and descriptor categories are separate fields.
- Failing-before: evidence/WO-06/audit-debug/D05-independent-audit.err.log.
- Disposition: corrected in the local WO-06 commit; audit-complete-Debug/Release pass.
  Assert the correct documented Drive/Weapon descriptor categories;
  retain exact names/channels/counts/length and every numeric sample/CSV assertion.

### KI-18 — Concurrent writers can append decreasing timestamps to one entity
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: DataRecorder.cpp/RecordImpl reads elapsed time before acquiring entity mutex.
- Regression: `WO-06 D04: shared entity writers retain ordered append times and all calls`;
  barrier-started four writers x10000 calls to one entity, owner Tick/query interleavings.
  All 40000 calls accounted independently; the first execution detects timestamp reversals
  and DataPlayer rejects the recorder's own published snapshot.
- Failing-before: evidence/WO-06/shared-before/D04.out.log.
- Disposition: fixed in the local WO-06 commit; D04 passes in debug-complete/release-complete,
  accounting for all 40000 calls and zero reversals. Sample recorder time inside the entity append lock, preserving
  nondecreasing timestamps under the supported concurrent Record/owner Tick contract.

### KI-17 — Stop waits synchronously for an in-progress autosave
- Status: Confirmed defect in initial WO-06 fix. Owner WO: WO-06.
- Anchor: TelemHub.cpp/StopRecording unconditional WaitForFlush in proposed finalization fix.
- Regression: `WO-06 D05: stop queues final export without blocking on pending autosave`;
  root created/called/destroyed on one owner thread, write held behind a promise barrier.
- Failing-before: evidence/WO-06/queued-before/D05.out.log; Stop does not return
  within 250 ms while write barrier remains held. Barrier released and owner joined.
- Disposition: fixed in the local WO-06 commit; D05 passes in debug-complete/release-complete.
  Queue final export for owner updates, keep Stop/Export responsive;
  shutdown still joins the pending writer then finalizes the newest dirty prefix.

### KI-15 — Two-hour Debug export misses the approved 30-second limit
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: DataExport.cpp/WriteCSV per-value iostream formatting, WO-06 working tree.
- Regression: `WO-06 D05: two hour production fixture final export memory and sample accounting`.
- Failing-before: evidence/WO-06/debug-initial/D05-two-hour.out.log; 432000 samples
  in each of three entities; 30.6457 seconds final export wait >30. No deadline extended.
- Disposition: fixed in the local WO-06 commit; buffer locale-invariant max_digits10 decimal formatting.
  debug-complete/release-complete export takes 5.481/0.741 seconds, below the unchanged 30-second limit.

### KI-16 — Float telemetry accumulator drifts over two nominal hours
- Status: Confirmed numerical limitation. Owner WO: WO-10 (clock work).
- Anchor: DataRecorder.cpp/Tick float atomic accumulation at 0435d3c.
- Repro: WO-06 two-hour fixture records last timestamp 7183.12793 for nominal
  last sample 7199.983333; drift about -16.8554 seconds, beyond one fixed step.
- Evidence: evidence/WO-06/debug-initial/D05-two-hour.out.log.
- Disposition: open for WO-10; WO-06 does not change the v1 float time representation.

### KI-14 — Ratified two-hour stop-and-finalize limit is unenforced
- Status: Confirmed enforcement defect. Owner WO: WO-06.
- Anchor: TelemHub.cpp/RecordFixed at 0435d3c; continues appending past session ceiling.
- Repro/regression: `WO-06 D05: recording ceiling stops and finalizes` against
  original HEAD TelemHub.cpp with the additive test/header; source restored in finally.
- Failing-before: evidence/WO-06/ceiling-before/D05.out.log (still recording and
  second post-limit tick increases frame count). Separate counterfactual build log retained.
- Disposition: fixed in the local WO-06 commit; named 7200-second/432000-frame stop-and-finalize guard.
  D05 passes in debug-complete/release-complete.

### KI-10 — Replay accepts invalid timestamps and NaN seeks
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: DataPlayer.cpp/SetPosition, SampleAt, LoadBinaryFile at 0435d3c.
- Repro/regression: `WO-06 D03: reject decreasing and nonfinite timestamps` and
  `WO-06 D02: invalid seek leaves state and output unchanged` in tests/test_wo06.cpp.
- Failing-before: evidence/WO-06/before-complete/D02.out.log and D03.out.log;
  decreasing/negative/NaN times load successfully, NaN seek contaminates position/output.
- Disposition: fixed in the local WO-06 commit; D02/D03 pass in debug-complete/release-complete.
  Timestamps must be finite, nonnegative, nondecreasing;
  invalid query/control time rejected without changing state.

### KI-11 — CSV silently accepts blank/nonfinite cells and unsafe writer parameters
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: utils/DataExport.cpp at 0435d3c: strtod does not require conversion,
  check range or finite values; writers do not validate header grammar/circular parameters.
- Repro/regression: both WO-06 D06 tests in tests/test_wo06.cpp.
- Failing-before: evidence/WO-06/before-complete/D06.out.log; whitespace becomes zero,
  nan/inf/overflow/underflow succeed; bad header and negative count overwrite output;
  failed load retains headers.
- Disposition: fixed in the local WO-06 commit; locale-invariant restricted finite decimal grammar
  and empty failure outputs. All five D06 tests pass in debug-complete/release-complete.

### KI-12 — Flush replaces binary before checking CSV or stream completion
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: telemetry/DataRecorder.cpp and utils/DataExport.cpp at 0435d3c.
- Repro: publish a sample, replace A.csv with a directory, append sample, Flush again.
  `WO-06 D05: CSV failure preserves last complete snapshot` fails byte equality.
- Failing-before: evidence/WO-06/before-complete/D05.out.log; Flush complete logged
  although WriteCSV cannot open; previous scene.bin replaced. Streams also omit
  write/flush/close status checking (analysis; fault regressions added with OS seam).
- Disposition: fixed in the local WO-06 commit; D05/D06 pass in debug-complete/release-complete.
  Stage/validate exports, publish scene.bin last atomically,
  expose completion/failure and retain last published binary on failure.

### KI-13 — Shutdown skips final records while an autosave is pending
- Status: Confirmed defect. Owner WO: WO-06.
- Anchor: TelemHub.cpp/Shutdown, StopRecording, ExportRecording at 0435d3c.
- Repro: start production recording, snapshot at first tick behind write barrier,
  record second tick, release and OnDetach. Shutdown skips final export.
- Regression: `WO-06 D05: shutdown finalizes records newer than pending autosave`.
- Failing-before: evidence/WO-06/before-complete/D05.out.log; final recording missing.
- Disposition: fixed in the local WO-06 commit; D05 passes in debug-complete/release-complete.
  Drain pending snapshot then finalize dirty records; mark clean
  only after successful intentional export, surface errors.

## WO-07 findings (2026-09-18) — editor game-module lifecycle (L02)

All four were found by the L02 harness (`Projects/Starforge/src/L02ModuleReloadSelfTest.cpp`,
driven through the real `BuildScripts -> BuildRunner -> ReloadModule -> GameModule` path) on the
first real rebuild/reload cycles of a project scaffolded by the 2D editor itself. Failing-before
evidence is under `evidence/WO-07/l02/`; each entry names its file.

### KI-26 — A project scaffolded by the 2D editor cannot build its game module
- Status: Confirmed defect (shipped: every Ctrl+B in the 2D editor fails). Owner WO: WO-07 (L02).
- Anchor: `Projects/Starforge/assets/templates/CMakeLists.txt` at `fe3d807` — the scaffold template
  never consumes the `-DCOSMIC_2D_ONLY=ON` that `BuildRunner::Start` passes ("Manually-specified
  variables were not used by the project: COSMIC_2D_ONLY"), so every module TU compiles the 3D side
  of the public-header fences against the 2D `Cosmic.lib`; and `templates/src/Module.cpp` registers
  the 3D-only `VoxelDigger` / `NavCritter` samples unconditionally. WO-03 fixed the runtime-plugin
  template (`Cosmic/templates/ExampleProject`) but not this one.
- Repro: scaffold any project from the 2D editor's homescreen, press Ctrl+B — or copy the template
  and run the exact `BuildRunner` cmake lines: 13 unresolved externals
  (`Scene::GetNav`, `VoxelVolumeComponent`, `NavAgentComponent`, `SceneNavRuntime::*`,
  `ScriptableEntity::Voxels`), `LNK1120`.
- Failing-before: `evidence/WO-07/l02/scaffold-template-failing-before-Debug.txt`.
- Regression: L02 harness cycle 0 (the initial build of the real scaffold) via
  `tests/acceptance/manifests/wo07-l02.manifest.json`.
- Disposition: fix landed (WO-07 local commit `3e1c2be`) — the scaffold CMakeLists gains the same
  `option(COSMIC_2D_ONLY …)` + `target_compile_definitions` block the runtime template carries since
  WO-03, and the two 3D-only samples are fenced (`#ifndef COSMIC_2D_ONLY`) in `Module.cpp` and in
  their own headers. Passing-after: the L02 runner evidence (`l02-Debug/`, `l02-Release/`).

### KI-27 — Re-registering a reflected component accumulates its fields on every reload
- Status: Confirmed defect. Owner WO: WO-07 (L02, "registry entries neither accumulate nor vanish").
- Anchor: `Cosmic/src/reflect/TypeRegistry.h` — `ClassIn<T>` → `TypeRegistry::GetOrCreate` reuses
  the existing `TypeDescriptor` and `ClassBuilder::Field` **appends** to `d.Fields`; nothing clears
  the list (unlike `ModuleRegistry::AddScript`, which does). `RegisterEngineTypes`' "Idempotent:
  re-registering just overwrites" comment is false for fields.
- Repro: any `CS_COMPONENT` module reloaded N times: the descriptor lists its fields N+1 times
  (Inspector shows duplicates, the serializer writes each key N+1 times, and the first N copies'
  Read/Write thunks are code in the **unmapped** previous DLL — see KI-29).
- Failing-before: `evidence/WO-07/l02/failing-before-Debug-registry-accumulation-play-leak.result.json`
  (`descriptor 'L02Component' has 10 fields, expected 5` … 15 … 20 across cycles 1–3).
- Regression: L02 harness — descriptor field list must equal the variant exactly after every reload.
- Disposition: fix landed (WO-07 local commit `3e1c2be`): `ClassIn` starts from a fresh field list, and
  `ModuleRegistry::UnregisterModule` removes the module's component descriptors (KI-29).

### KI-28 — A module reload during Play bakes runtime state into the edit scene
- Status: Confirmed defect (contract §4 violation: "preserves the serialized edit scene, stops Play").
- Owner WO: WO-07 (L02).
- Anchor: `Projects/Starforge/src/StarforgeApp.cpp` `ReloadModule` at `fe3d807`: the snapshot
  `SceneSerializer::SaveToString(*m_Ctx.Scene)` is taken **before** `StopScene()`, and while playing
  `m_Ctx.Scene` is the runtime scene (`m_EditSceneBackup` holds the edit scene). `StopScene` restores
  the edit scene, `Scene.reset()` drops it, and the scene is rebuilt from the **runtime** snapshot.
  Reachable in the shipped editor: `BuildScripts` refuses to start while playing, but Play is not
  refused while a build is running, and the reload fires when the build completes.
- Repro: start a build, press Play while it compiles, let a script move an entity; when the build
  finishes the edit scene holds the moved position.
- Failing-before: same result file as KI-27 — `edit-scene Position [2732,8,9] != [7,8,9] (runtime
  state leaked into the edit scene)` (cycle 2; the L02Script drifts x by 1 per frame during the build).
- Regression: L02 harness cycles with `cycle % 5 == 2` (Play started during the build).
- Disposition: fix landed (WO-07 local commit `3e1c2be`): `ReloadModule` stops Play **before** snapshotting,
  so the snapshot is always the edit scene.

### KI-29 — Registry entries of an unloaded module stay live and are invoked after FreeLibrary
- Status: Confirmed defect (use-after-unload; ACCESS VIOLATION reproduced). Owner WO: WO-07 (L02/L04).
- Anchors:
  - `Cosmic/src/scripting/ModuleRegistry.cpp` `UnregisterModule` at `fe3d807` — erases the module's
    script/system descriptors and its component *notes*, but deliberately leaves the component
    `TypeDescriptor`s in `Reflect::GetRegistry()` ("overwritten on the next load"). Their
    `Add/Has/Get/Remove/Copy` + every field `Read/Write` are `std::function`s whose code lives in
    the module DLL that `GameModule::Unload` then `FreeLibrary`s.
  - `Cosmic/src/layers/PlayerLayer.cpp` — the runtime-plugin lifecycle never unregisters at all:
    `CS_MODULE_END`'s `CreatePluginLayer` registers the module, and nothing removes it before
    `Application::UnloadProjectDLL`'s `FreeLibrary` (relaunching the same project from the launcher
    re-registers over dangling `std::function`s).
- Repro: reload a module whose build succeeds but exports no `CosmicModule_Register` (or whose load
  fails for any reason) — `ReloadModule` rebuilds the scene from the snapshot,
  `SceneSerializer::LoadFromString` finds the stale descriptor by name and calls its `Add` →
  jump into unmapped memory. Every successful reload also leaves the previous module's stale field
  thunks in the list (KI-27) — they only "work" because the next `_hotN.dll` happened to map at the
  same base address.
- Failing-before: `evidence/WO-07/l02/failing-before-Debug-load-failure-access-violation.txt`
  (editor exit `-1073741819` = 0xC0000005 immediately after `GameModule: 'L02Reload_hot4.dll'
  exports no CosmicModule_Register`).
- Regression: L02 harness load-failure cycle (the custom blocks must survive **opaquely** per C05 and
  no stale descriptor may remain); L04 host case "module-registered types are gone before
  FreeLibrary" for the runtime-plugin path.
- Disposition: fix landed (WO-07 local commit `3e1c2be`): `TypeRegistry::Remove`, `UnregisterModule` removes
  the module's component descriptors (before `FreeLibrary`, so the thunks are destroyed while their
  code is mapped), and `PlayerLayer::OnDetach` unregisters its own module. A failed load now leaves
  the scene's custom blocks as opaque (forward-compat) blocks that re-resolve on the next good load.

### KI-30 — Script field overrides are dropped when a scene is loaded without its script class
- Status: Confirmed defect (silent data loss). Owner WO: WO-07 (L02, "preserve serialized scene /
  custom fields").
- Anchor: `Cosmic/src/scene/SceneSerializer.cpp` at `fe3d807` — `LoadEntityComponents` resolves a
  `NativeScript` (and `SystemScript`) `Fields` block only through the **registered** descriptor
  (`ModuleRegistry::FindScript`); when the class is not registered the overrides are discarded, and
  `SerializeEntity` writes a `Fields` block only for a registered class — so the next save (or the
  `ReloadModule` snapshot) no longer carries them. Reflected component blocks get the opaque C05
  passthrough; script overrides do not.
- Reachability (shipped editor): `StarforgeApp::OpenProject` opens the scene **before** any game
  module is loaded on a fresh editor start — every per-entity script override in the file (e.g. a
  tuned `HoverController.TargetAltitude`) is dropped at open; the first Ctrl+B snapshots the scene
  without them and rebuilds it with the C++ defaults. Same after any failed module load.
- Failing-before: `evidence/WO-07/l02/failing-before-Debug-script-overrides-lost.result.json`
  (`script field Rate override lost` / `Loops override lost` on every cycle after the failed-load
  cycle; the out-of-process oracle reports `NativeScript.Fields.Loops missing; …Rate missing`).
- Regression: L02 harness — the probe's `L02Script` overrides (Rate=3.5, Loops=5) must survive the
  failed-load cycle and every later reload; `test_scene_serializer.cpp` round-trip with an
  unregistered class (headless).
- Disposition: fix landed (WO-07 local commit `3e1c2be`): unresolved `Fields` are kept verbatim on the
  component (`PendingFields`) and re-emitted on save while the class is unregistered — the same
  forward-compat rule as opaque component blocks — and resolve on the next load that has the class.

### KI-31 — Inspector backfill of a newly added script field resets every existing override
- Status: Confirmed defect (silent data loss). Owner WO: WO-07 (L02).
- Anchor: `Projects/Starforge/src/panels/InspectorPanel.cpp` at `fe3d807` — `DrawScriptComponent`
  "Backfill any field the map is missing" calls `SeedScriptDefaults`, which does `nsc.Fields.clear()`
  and re-pulls **all** defaults from a throwaway instance. So the frame after a rebuild adds one
  field to a script, merely having an entity with that script selected wipes every tuned override
  on it (Rate 3.5 → 1, Loops 5 → 2 in the L02 probe).
- Repro: tune a script field, add a new public field to the script, Ctrl+B, select the entity: the
  tuned value shows its C++ default; the next save persists the loss.
- Failing-before: `evidence/WO-07/l02/failing-before-Debug-inspector-backfill-resets-overrides.result.json`
  (cycle 23, the first Inspector draw after variant V5 added `Bias`: `script Rate = 1, expected
  3.5`, `script Loops = 2, expected 5`; the out-of-process oracle confirms the saved scene).
- Regression: L02 harness cycles with `cycle % 5 == 3` (probe selected while a build runs, so the
  Inspector draws it across the reload) after a script-field addition (V5 at cycle 21).
- Disposition: fix landed (WO-07 local commit `3e1c2be`): the backfill seeds **only the missing** fields.

### KI-32 — EntitySelection invokes a listener that was unsubscribed during the dispatch
- Status: Confirmed defect (stale-callback class). Owner WO: WO-07 (L04, "callback disconnect
  during dispatch").
- Anchor: `Cosmic/src/telemetry/EntitySelection.cpp` `Notify` at `fe3d807`: it snapshots the
  whole subscription vector and then calls every snapshotted callback without re-checking that it
  is still subscribed. A listener that another listener removes earlier in the same dispatch —
  e.g. an owner torn down by the first callback — is still invoked (its captured `this` may be
  freed). The per-scene `EventBus::Emit` re-checks liveness before each call (`IsNamedLive`) and
  documents "a listener removed mid-dispatch does not fire"; EntitySelection did not.
- Repro: subscribe A then B; in A's callback `Unsubscribe(B)`; `SetByName("x")` → B fires.
- Failing-before: `evidence/WO-07/l04/failing-before-Debug-entityselection-removed-listener-fires.txt`
  (3 of 3 probe dispatches invoked the removed listener; EventBus 0 of 3).
- Regression: `test_wo07_l04.cpp` (`esRemovedMidDispatchFired == 0`, driven by
  `WO07TeardownFixture`), plus the headless `test_events.cpp` case
  `WO-07 L04: EntitySelection does not invoke a listener unsubscribed during dispatch`.
- Disposition: fix landed (WO-07 local commit `6d2e741`): `Notify` snapshots the handles and, for each,
  re-fetches the callback under the mutex only if it is still subscribed — the EventBus rule.

### KI-33 — UnloadProjectDLL frees the plugin while its job is still queued or in flight
- Status: Confirmed defect (worker runs into unmapped code; ACCESS VIOLATION reproduced).
  Owner WO: WO-07 (L04, "no invocation into unloaded DLL").
- Anchor: `Cosmic/src/core/Application.cpp` `UnloadProjectDLL` at `fe3d807` — OnDetach → delete
  layer → `FreeLibrary`, with no JobSystem drain in between. The JobSystem has no cancellation; a
  job the plugin submitted (its callable is code in the plugin DLL) that is still queued, or
  blocked inside the DLL when the plugin fails to join it in `OnDetach`, resumes after the image is
  unmapped. The full-shutdown path is safe by accident (`Shutdown` drains the pool first).
- Repro: `COSMIC_WO07_L04_CASE=2` — the fixture's OnDetach skips its join; the exe releases the
  job 300 ms after detach: the process dies right after "Project DLL safely unmounted and unloaded".
- Failing-before: `evidence/WO-07/l04/failing-before-Debug-job-runs-into-unmapped-plugin.txt`.
- Regression: `test_wo07_l04.cpp` mode 2 (`seqJobDone < seqAfterUnload`, `jobActiveAfterUnload == 0`),
  10 children per config through `wo07-l04.manifest.json`.
- Disposition: fix landed (WO-07 local commit `6d2e741`): `UnloadProjectDLL` waits for the JobSystem to go
  idle (`WaitIdle`, only while the pool is initialized) after `OnDetach`/delete and BEFORE
  `FreeLibrary`, so plugin-submitted work always completes against mapped code. A job that never
  completes still hangs the transition exactly as it already hangs `JobSystem::Shutdown` at exit —
  a plugin must not block its jobs forever (documented JobSystem rule).

### KI-34 — The Release editor hot-loads a Debug-CRT game module (ABI/heap mismatch, crash)
- Status: Confirmed defect (ACCESS VIOLATION reproduced). Owner WO: WO-07 (L02, release profile).
- Anchor: `Projects/Starforge/src/BuildRunner.h` `kHotConfig = "Debug"` at `fe3d807` — the
  hot-reload build configuration is a hard-coded constant, so a **Release** editor builds every
  game module `/MDd` (Debug CRT + `_ITERATOR_DEBUG_LEVEL=2` STL layouts) and `LoadLibrary`s it into
  its own `/MD` process. The inline `ModuleRegistry` templates then run with Debug `std::string` /
  `std::unordered_map` layouts against the engine's Release objects, and every `new`/`delete`
  crossing the boundary hits a different CRT heap — exactly the shared-allocator rule
  `docs/guide/project-anatomy.md` says must never be broken. The same constant chooses the
  module for a non-release **package** paired with the editor's own (Release) runtime dir.
- Repro: Release `Starforge.exe`, any scaffolded project, Ctrl+B — the editor dies at the first
  `GameModule::Load` (before "GameModule: loaded" is logged).
- Failing-before: `evidence/WO-07/l02/failing-before-Release-debug-module-in-release-editor.txt`
  (runner case L02 Release: exit `-1073741819` in cycle 0; `build/Debug/L02Reload_hot1.dll` built).
- Regression: L02 through `wo07-l02.manifest.json` in **Release** (the whole 52-build campaign).
- Disposition: fix landed (WO-07 local commit `3e1c2be`): `kHotConfig` now follows the editor's own build
  configuration (`NDEBUG` → "Release", else "Debug") — the only configuration whose CRT and STL
  layouts match the process a hot module is mapped into; the module search dir and the
  non-release package pairing follow automatically.

## WO-08 findings (2026-09-18) — renderer / camera / capture (R01–R07)

### KI-35 — DrawCircle `thickness` is inverted: a ring renders as a small disc
- Status: Confirmed defect (silent visual corruption against the documented contract).
  Owner WO: WO-08 (R01 "SDF discs/rings/ellipse … correct").
- Anchor: `Cosmic/assets/shaders/Circle.glsl` fragment stage and `CircleInstance.glsl` fragment
  stage at `2024748`: `alpha *= smoothstep(Thickness + Fade, Thickness, 1.0 - distance)` where
  `distance = 1.0 - length(LocalPosition)`, i.e. the second factor is evaluated on the normalised
  RADIUS `r`, so it keeps `r < Thickness` — a **disc of radius `Thickness`** — instead of the
  documented ring: `docs/guide/rendering-2d.md` §"Draw circles and rings" and
  `docs/reference/rendering-2d.md:764` both define `thickness` as "the ring wall as a fraction of
  the radius (`1.0` = filled)". Every shipping caller uses the documented meaning:
  `Projects/SF_Telem/src/DrivetrainLayer.cpp:1307-1312` ("faded ring" 0.03, "bold tyre" 0.11)
  and `Cosmic/src/layers/LauncherLayer.cpp:217-219` ("Mid ring" 0.02, "Inner ring" 0.015) — all
  of which currently draw a near-invisible dot at the centre instead of a ring.
- Repro: `CosmicRenderTests --test-suite="WO-08 R01" --test-case="R01 primitives*"`: the
  `thickness = 0.2` circle at world (-2, 0.5) has its CENTRE painted and its r = 0.9 ring empty
  (`ring centre is empty` / `ring at r=0.9` sentinels).
- Failing-before: `evidence/WO-08/ki35-circle-thickness/failing-before/` (Release; the captured
  frame `r01-primitives.png` shows the orange "ring" as a small filled dot).
- Regression: `tests/render/render_wo08_primitives.cpp` (ring sentinels + the `wo08_primitives`
  golden) and `render_wo08_instancing.cpp` (instanced thickness variants in the `instancing2d`
  golden, R03).
- Disposition: fix landed (this WO): both fragment stages evaluate the wall factor on `distance`
  (`1 - r`) — `smoothstep(Thickness + Fade, Thickness, distance)` — which keeps
  `1 - r < Thickness`, the outer wall of width `Thickness · R`; `thickness = 1.0` is unchanged (a
  full disc). No committed golden draws a circle (the five 2D scene goldens and the 3D goldens
  are circle-free), so no existing golden moves; `engine-3d` is untouched.

### KI-36 — After a Material quad, every following non-material quad is its own draw under the material's shader
- Status: Confirmed defect (silent visual corruption + per-quad batch break). Owner WO: WO-08
  (R02 "material/shader changes and mixed calls").
- Anchor: `Cosmic/src/renderer/Renderer2D.cpp` at `2024748` — the six non-material quad paths
  (`DrawQuad` colour/texture/sub-texture and their `DrawRotatedQuad` twins) open with
  `if (s_Data.CurrentMaterial != s_Data.DefaultMaterial) FlushAndReset();` but never set
  `CurrentMaterial` back to `DefaultMaterial`, and `FlushAndReset()` deliberately PRESERVES the
  active material across the reset. So once `DrawQuad(…, material)` has run in a pass, every
  later flat / textured / sub-textured quad (a) trips the `!= DefaultMaterial` flush on EVERY
  call — one draw call per quad — and (b) is flushed through `CurrentMaterial->Bind()`, i.e. the
  custom material's shader and uniforms, not the engine batch shader. Only the next
  `PushRenderPass`/`PopRenderPass` resets the bucket. Reachable from authored content:
  `Scene::OnRenderSprites` (`Scene.cpp:717-729`) draws a material sprite through
  `DrawRotatedQuad(…, s.ActiveMaterial)` and the next sprites in painter order through the
  colour / sub-texture overloads.
- Repro: `CosmicRenderTests --test-suite="WO-08 R02" --test-case="R02 material*"`: one quad on a
  magenta-tint material followed by five flat WHITE quads → `DrawCalls == 6`, `Flushes == 7`, and
  all five flat quads read (255,0,255) instead of white; a cyan material followed by textured /
  sub-textured / rotated-flat quads → 4 draws, all three cyan.
- Failing-before: `evidence/WO-08/ki36-material-restore/failing-before/` (Release; the capture
  `r02-material-then-flat.png` shows six magenta cells where one magenta + five white were
  submitted).
- Regression: `tests/render/render_wo08_batches.cpp` "R02 material / shader transitions" (five
  subcases: material→flat, material→textured/sub-textured, alternating, two materials on one
  shader, fresh-pass reset) plus the draw-count assertions of every other R02 case.
- Disposition: fix landed (this WO): each non-material quad path that flushes on
  `CurrentMaterial != DefaultMaterial` now also sets `CurrentMaterial = DefaultMaterial` so the
  quad joins the default bucket — the symmetric twin of the material path's
  `CurrentMaterial = material`. `FlushAndReset`'s material preservation (which the material
  path relies on across the 10,000-quad boundary) is unchanged.

### KI-37 — UiImage draws every texture vertically flipped (plain and 9-slice paths)
- Status: Confirmed defect (silent visual corruption of authored UI images and RTT feeds).
  Owner WO: WO-08 (R05 "RTT UiImage orientation").
- Anchor: `Cosmic/src/scene/ui/UiSystem.cpp` `DrawImageQuad` at `2024748`. The UI pass projects
  with `glm::ortho(0, w, h, 0)` (canvas +y DOWN), so `Renderer2D`'s quad corner with local
  (-0.5, +0.5) — the one that carries UV v = 1 — lands at the rect's screen BOTTOM. The plain path
  (`:364`, `DrawQuad(center, size, tex, 1, tint)`) therefore shows texture v = 0 at the top of the
  rect; the 9-slice path (`:330-345`) builds `vs[] = {1, 1-t/th, b/th, 0}` top-to-bottom (right)
  but hands `uvMin.v = vs[row+1]` to the corner that is the screen TOP under this projection
  (wrong), so each band is mirrored the same way. Files are loaded flip-on-load
  (`OpenGLTexture.cpp:77`: v = 1 is the file's top row) and an FBO attachment's row 0 is its
  bottom, so both an authored `TexturePath` image and a `RuntimeTexture` render-to-texture feed
  appear upside-down. Text is unaffected (it flips its geometry, `:416`). `docs/guide/game-ui.md`
  "Show a live render target in an image" documents the upright expectation (its option B says a
  TOP-left-origin buffer would arrive flipped, i.e. the quad is expected to sample bottom-left).
- Repro: `CosmicRenderTests --test-suite="WO-08 R05" --test-case="R05 two RTT*"`: a 64x48 target
  with red / green / blue / yellow corner markers and a red bar along its TOP edge, shown 1:1
  through `UiImageComponent::RuntimeTexture` (documented FboTexture adapter) — the rect's top-left
  reads blue (the target's bottom-left), its bottom edge carries the bar.
- Failing-before: `evidence/WO-08/ki37-uiimage-flipped/failing-before/` (Release; `r05-main.png`).
- Regression: `tests/render/render_wo08_rtt.cpp` "R05 two RTT targets through UiImage…" (corner +
  bar orientation on two targets of different aspect, plain path) and the added 9-slice / plain
  orientation checks on a GL-native two-row texture.
- Disposition: fix landed (this WO): the plain path draws through a `SubTexture2D` with
  `uvMin = (0, 1)`, `uvMax = (1, 0)` so v = 1 sits at the rect top; the 9-slice path hands
  `uvMin.v = vs[row]` / `uvMax.v = vs[row+1]`. Tint-only images (no texture) are untouched, so the
  committed `ui` and `scene2d` goldens (tints only) do not move.

### KI-38 — Camera2DController accepts NaN/inf zoom, focus, size and bounds and emits a non-finite projection
- Status: Confirmed defect (a NaN view-projection blanks the whole viewport and is sticky).
  Owner WO: WO-08 (R04 "zero/negative/nonfinite input policy … no NaN projection,
  divide-by-zero or resize crash").
- Anchor: `Cosmic/src/camera/Camera2DController.cpp` at `2024748`. `SetZoom` clamps with
  `std::clamp`, which returns NaN unchanged, so `SetZoom(NaN)` stores NaN and `Recalculate()`
  builds a NaN ortho; `SetFocus` and `FrameBounds` store whatever they are given (an infinite box
  averages to a NaN focus); `OnResize` / `SetViewportRect` guard only `<= 0`, so a NaN or
  infinite size passes and becomes the aspect; `OnMouseScrolled` computes
  `before * pow(1.15, -NaN)` = NaN, the `after == before` early-out is false for NaN, and the NaN
  is stored. The constructor guards `<= 0` but not `+inf`. The pure helpers (`ScreenToWorld`,
  `PanBy`, `ZoomAboutPoint`) guard zero-height / zero-zoom only. Once stored, every later frame
  projects through NaN (no vertex passes clipping — an empty viewport) until a finite `SetZoom`
  arrives; the zoom clamp cannot recover it. Reachable: framing/zooming is driven from scene data
  (entity bounds, saved view state), which the P2/P6 hardening notes already flag as able to
  carry NaN/inf.
- Repro: `CosmicTests --test-case="WO-08 R04: nonfinite input policy*"` (headless): after
  `SetZoom(NaN)` the projection is non-finite; `SetZoom(±inf)` moves the zoom to a clamp end
  instead of being rejected; `OnResize(NaN, 720)` / `OnResize(inf, 720)` change the aspect;
  `FrameBounds({-inf,-inf},{inf,inf})` moves the focus to NaN; a `MouseScrolledEvent(0, NaN)`
  through `OnEvent` poisons the zoom.
- Failing-before: `evidence/WO-08/ki38-camera-nonfinite/failing-before/` (Release, 107 failed
  assertions in the one case; every other R04/R06 headless case passed).
- Regression: `tests/test_wo08_camera.cpp` "nonfinite input policy" (+ the round-trip, anchor,
  zoom-range, FrameBounds and changing-viewport cases that must keep passing).
- Disposition: fix landed (this WO) — POLICY: any non-finite zoom, focus, size, viewport rect,
  bounds, aspect or scroll amount is REJECTED (the call is a no-op and the previous valid state
  stays); zero/negative zoom still clamps to the minimum; zero/negative sizes are still ignored;
  the pure helpers return `focus` unchanged for non-finite inputs, exactly as they already do for
  a zero viewport height / zero zoom. Documented in `Camera2DController.h`.

### KI-39 — Both trunk source audits fail at HEAD for reasons predating WO-08 (enforcement gap)
- Status: Enforcement gap (the WO-03 CI gates `tests/check_gl_conformance.ps1` and
  `tests/check_docs_coverage.ps1` are red on `main`; WO-02 recorded both clean at the baseline).
  Owner WO: WO-07 / WO-04 / WO-06 follow-up (not fixed by WO-08 — outside its scope, other work
  orders' files).
- Anchor (GL conformance, 8 violations): `tests/WO07PlotFixture.cpp:148-154` and
  `tests/WO07UiCyclesFixture.cpp:83` carry INLINE `/*GL_…*/` comments after code
  (`0x8CA8 /*GL_READ_FRAMEBUFFER*/` etc.); the scanner exempts full-line comments only, so the
  `GL_[A-Z0-9_]+` tokens count as violations. Anchor (docs coverage, 2 unlisted headers):
  `serial/ISerialTransport.h` (WO-04) and `utils/AtomicOutput.h` (WO-06) have no row in
  `docs/reference/README.md`. The WO-08 header `graphics/GpuObjectStats.h` was added WITH its row
  and chapter entry, and no WO-08 file adds a GL token.
- Repro: `powershell -File tests\check_gl_conformance.ps1` → exit 1 (8 violations);
  `powershell -File tests\check_docs_coverage.ps1` → exit 1 (2 unlisted headers). Evidence:
  `evidence/WO-08/audit-gl-conformance.txt`, `audit-docs-coverage.txt`.
- Regression: the audits themselves (CI step "GL conformance audit", docs coverage step).
- Disposition: open. Fix recipe: reword the eight inline comments so they do not spell `GL_…`
  (e.g. `/*READ_FRAMEBUFFER*/`), and add two manifest rows (`serial/ISerialTransport.h` →
  `serial.md`, `utils/AtomicOutput.h` → the utilities chapter) with a short entry each. Neither
  change touches behaviour.

## Register invariants

- No entry is closed without a landed regression (or an explicit reviewed won't-fix with reason).
- A defect found while executing a later WO is added here **before** it is fixed, with its
  failing-before evidence.
- Equipment/fixture unavailability is tracked as `ENVIRONMENT_BLOCKED` in the acceptance evidence,
  never as a closed KI and never as a pass.
