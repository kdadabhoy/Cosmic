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

## Register invariants

- No entry is closed without a landed regression (or an explicit reviewed won't-fix with reason).
- A defect found while executing a later WO is added here **before** it is fixed, with its
  failing-before evidence.
- Equipment/fixture unavailability is tracked as `ENVIRONMENT_BLOCKED` in the acceptance evidence,
  never as a closed KI and never as a pass.
