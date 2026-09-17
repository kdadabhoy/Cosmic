# Cosmic 2D acceptance test catalog

Status: proposed test specification, 2026-09-16. No case in this catalog has been executed during this planning review.
Owner: each case is implemented by the matching work order in 02-Stability-Work-Orders.md.
A case's existence in source is not evidence that this candidate passed it.

## Execution model

Five execution tiers:
- U: CPU-only unit, deterministic state-machine, parser and numeric tests in/alongside CosmicTests.
- W: Windows integration: real host/DLL/process/file/serial behavior, without requiring a human for every transition.
- G: a real OpenGL context on a desktop GPU; a hidden window is sufficient for offscreen render tests.
- I: actual editor/application UI actions, including native dialogs, docking, DPI and window close.
- Q: qualified hardware, clean installation, performance and long-running tests.

CI/runner profiles proposed for WO-04:
- pr: 2D Debug and Release units, source/mode/docs audits, deterministic short integration; target <=15 minutes excluding initial dependency compilation.
- gpu: G plus host/module tests on a qualified GPU, fixed fixtures and controlled render settings.
- serial: fake/delayed transport campaign plus Windows virtual-COM and attached-device tests.
- nightly: extended fuzz, 30-minute telemetry, lifecycle stress and GPU boundaries.
- release: every mandatory case, both Windows versions, two-hour runs, package/install and reference-workload qualification.

These profile names and tests/acceptance/Run-Acceptance.ps1 are proposed interfaces, not existing commands. Current commands that already exist are in the migration runbook.

A missing GPU, COM fixture, Windows version or UI-control capability produces ENVIRONMENT_BLOCKED with the missing prerequisite. A filtered suite containing zero expected tests is a failure. A test driven through application commands cannot claim native-dialog/UI coverage unless it also exercises the actual UI.

## Evidence and isolation contract

Every run records:
- Test/requirement ID, status, start/end UTC, duration, timeout, seed and fixture hash.
- Commit SHA, dirty diff hash, build mode/configuration, SDK/plugin identity and tool versions.
- OS build, CPU/instruction features/RAM; GPU renderer/driver/OpenGL version for G/I/Q.
- Exact command/actions, exit codes, test names/counts, logs and metrics.
- Expected versus observed values, actual/expected/diff image paths, dump/hang evidence where relevant.
- Whether it exercised production code, a fake OS boundary, virtual COM, or physical device.
- For a regression fix: original failing reproduction and passing result.

Use a new temporary directory and user-data root per run. Never overwrite real recordings, settings, goldens or the original SF-Stable installation. Tests with file watchers/registries/process globals run isolated where necessary. Do not concurrently run suites that share a non-unique scratch path.

The parent runner enforces deadlines independently of the child. Timeout is FAILED, not a graceful pass. It terminates only processes it launched and preserves available diagnostics. Test harnesses must not invoke motor/flight-control actions; the SF qualification workload is receive/record/replay.

Proposed standard deadlines: U case 10 s unless cataloged otherwise; small W/G/I scenario 60 s; 50-rebuild campaign 30 min excluding initial toolchain setup; serial 35 min for a 30-min run; soak 140 min for a 120-min measured session plus warmup and evidence capture. These are limits, not sleeps. Extend a limit only through a reviewed contract change backed by measurement.

## Provisional numerical bars

Ratify/calibrate these once in WO-00/02 on named reference machines; never loosen them merely because a later change fails.

| Quantity | Proposed bar |
| --- | --- |
| Normal close with idle/open/stalled port and no pending large export | App/process exits within 2 s; no deadlock, crash or post-unload callback |
| Delayed connect cancellation | UI remains responsive; no unsafe detached worker; bounded completion/cleanup policy must meet the approved close contract |
| Close with pending recording export | Explicit saving/cancel/error state remains responsive; successful completion within 30 s for the declared 2-hour fixture, or an explicit failure without claiming data was saved |
| UI service heartbeat during connection operations | No connection-induced gap >250 ms on reference hardware; record p95/p99 |
| Float comparison unless a domain-specific bound is supplied | abs(error) <= 1e-6 + 1e-5 * abs(expected); do not apply this to bit-exact storage assertions |
| Clock drift | <=one 60-Hz fixed step over the declared two-hour nominal run against an integer-tick/double reference; separately account for stored-float quantization. This is a proposed target, not a claim about the current float accumulator. |
| Camera projection round-trip | <=0.5 screen pixel within the declared local-coordinate envelope |
| Basic 10,000-instance performance | 1920x1080, Release, fixed camera/workload, vsync disabled, 10-s warmup + 60-s sample; p95 complete frame <=16.67 ms, p99 <=33.33 ms on a named qualified machine |
| Existing golden policy | Per-channel absolute tolerance 2/255; at most 0.1% pixels beyond it, matching current GoldenImage.h |
| Non-recording memory plateau | After 10-min warmup, final 5-min median private bytes minus initial 5-min median <=max(32 MiB, 5% of baseline); growth trend <=1 MiB per 10 min |
| Resource lifetimes | Zero net scenario-owned live threads, handles, callbacks and GPU objects after quiescence; documented bounded caches compared separately |
| SF two-hour recording peak | Provisional process private-bytes ceiling 2 GiB, with sample storage/vector capacity/snapshot/CSV temporary growth separately accounted for |
| Fuzz | Fixed seeds, bounded sizes/time; 2,000 cases/parser in PR and 50,000 in nightly; every failure minimized and committed as a regression fixture |

A whole-frame pixel tolerance can miss one small absent glyph/marker. Use exact interior-color/sentinel/ROI or geometric checks alongside goldens. Performance figures are machine qualifications, not guarantees for literally every supported CPU/GPU.

Private bytes are preferred to working set for the leak gate. Also record working set, process handles/threads, engine GPU object counts and GPU-memory trend. Driver caching and intentionally retained fonts/assets require a written finite allowance; they cannot excuse repeated per-cycle growth.

## Fixture register

| Fixture | Definition |
| --- | --- |
| F-SF-GOOD | Immutable valid $R/$L/$W tagged PC-side frames, heartbeat lines, known decoded values and checksums; include zero/min/max values and 1/2/3 active ESCs |
| F-SF-LONG | Deterministic 40 frames/s per active ESC, matching the simulator's 25-ms interval; all three ESCs produce 216,000 valid data frames in 30 min; heartbeat counts tracked separately |
| F-SF-FAULT | Same stream with byte fragmentation/coalescing, corrupt checksums, missing newline, >4-KiB noise, temporary silence and scripted disconnect/reconnect |
| F-KISS | Firmware-level 10-byte KISS packets with CRC8 and explicit corruption/resynchronization cases; separate from tagged desktop lines and separate from COBS/CRC16 |
| F-RECORD | Fixed v1 recordings: 3 entities at unequal rates, exact float values/times, zero/one/many samples, delayed entity start; include a real SF-Stable-produced file when available |
| F-CORRUPT | Truncations, bad magic/version/counts, nonfinite/decreasing times, oversized metadata and malformed scene/prefab/material/graph files |
| F-2D | Deterministic procedural textures/shapes, pinned font file and glyph text, overlapping colored sentinels, scenes for every batching/pass boundary |
| F-TRAJECTORY | Synthetic 10-s ballistic path: x=30*t, y=50*t-0.5*9.80665*t*t, vx=30, vy=50-9.80665*t, SI units, t=i/120 for i=0..1200; 1,201 samples with double reference values |
| F-SERIES-LARGE | 100,000 ordered samples/channel, 8 finite numeric channels plus time, known extrema and discontinuity markers; waveform equations/seed saved in metadata |
| F-LIFETIME | Plugin/module fixture owning a texture, framebuffer, script/component, listener, log sink, job, watcher and optional audio handle; lifetime counters and serial state observable |
| F-CONTENT | Small valid 2D authored project plus malformed variants, unknown component blocks and hierarchy/graph boundary cases |

Expected values must not be calculated by the same routine under test. Use checked-in reference values, simple independent equations or a reference decoder. Hash all immutable artifacts; changing a fixture requires a reviewed rationale.

## Build, preservation and harness

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| B01 | W/release | Verify every pre-existing branch remains; engine-3d equals the approved original main commit/tree; the `engine-3d` snapshot (`0e8894b`) is a distinct commit from the working `main`. Main-only campaign (no candidate branch). No 3D build required. |
| B02 | U,G/baseline | Fresh 2D Debug/Release builds; list and run existing units and render cases; capture warnings, configured targets and exact results. Existing failures remain visible. |
| B03 | W/pr | Fresh default configure, explicit 2D preset and stale OFF cache through each supported entry point. Result must be ON or reject the unsupported request clearly. No implicit full-engine rebuild. |
| B04 | W/pr | Inspect generated target/source graph and link/package manifests: designated terrain/voxel/water/nav/old 3D-particle/Renderer3D/model/assimp/Recast paths absent. Shared Jolt/physics/cameras explicitly allowed. Consumer compile definitions match. |
| B05 | U,W/pr | Run source GL and docs coverage audits; build external minimal DLL with correct mode/SDK/config. CI requires actual test names, not only zero process exit. Wrong-mode/missing export/mismatched declared SDK fixture is rejected or prevented by the build/package contract. |
| H01 | W/pr | Runner self-test: intentional nonzero exit and crashing fixture produce failure plus logs/dump status; a passing fixture produces pass. |
| H02 | W/pr | Hanging child exceeds a 2-s self-test deadline; runner marks timeout, captures diagnostics and terminates only its child. Parent remains usable. |
| H03 | U,G/pr,gpu | Missing mandatory test, fixture or golden fails; update-goldens environment/flag cannot be enabled in acceptance. Hash/compare source goldens before/after run. Note current comparator writes a missing golden before failing; harness must preserve failure and flag that mutation. |
| H04 | W,G/pr,gpu | Missing GPU/COM/Windows environment returns environment-blocked, with no phantom pass; fake transport cases still run and identify themselves as fake. Reports are parseable and include all required IDs. |

## Telemetry and the user's COM regressions

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| T01 | U/pr | Feed F-SF-GOOD through actual ParseFrame/IngestChunk at every byte split and then one-byte/random/coalesced chunks. Assert exact good/bad/per-ESC counts, routing, known decoded values and no unintended entity updates. Verify protocol/firmware parity with pinned SF-Stable. |
| T02 | U/nightly | Seed F-SF-FAULT. Assert counters for accepted/rejected records, bounded accumulator and recovery after a defined delimiter + subsequent valid frame. Test >4-KiB purge with a sentinel proving the limit was reached. Check staleness at 1.5 s and just above it under the selected clock. |
| T03 | U,W/pr,serial | The lifecycle matrix below, using barriers rather than timing guesses. At least 100 deterministic iterations per transition schedule; worker/handle/callback ownership balances, no deadlock/race/crash and approved close deadline. |
| T04 | W,I,Q/serial,release | Repeat critical matrix scenarios on actual Windows virtual COM plus representative USB/Bluetooth SPP hardware, both Windows versions. At least 20 iterations per physical disconnect/close scenario; cancellation/loss/reconnect is observable; no manual process kill counted as success. |
| T05 | U,W,I/nightly | 30 min F-SF-LONG at 40 Hz per ESC: exactly 216,000 accepted data frames, 0 unexpected rejects/duplicates, all decoded values match. Record separately at 60 Hz: exactly 108,000 ticks/entity under the controlled clock, not 216,000 samples/entity. Rings retain only their declared capacity. Exercise screen switches and pause/minimize/restore with a documented acquisition policy. |
| T06 | U,W/nightly | Burst feed at 10x nominal rate for 60 s in the in-memory fixture; real COM rates stay within configured wire capacity. Inject 1/10-s silence, 20 disconnect/reconnect events and intentional corrupt frames. Assert loss accounting, bounded queues, no stale partial bytes across reconnection, and clean recovery. Test F-KISS resync independently and preserve existing COBS tests. |

T03/T04 transition matrix (use real application close/return-to-launcher as well as service-level close):

| Initial operation/state | Disruption | Required result |
| --- | --- | --- |
| Never connected / failed connect | Close twice, destroy, reopen | Idempotent cleanup; no stale reconnect intent |
| BeginOpen pending, delayed OS result | Close window before completion; late success and late failure variants | No access to destroyed object; late handle cleaned once; bounded shutdown |
| Successful open, no incoming bytes | Close app / close panel / return to launcher | Panel visibility has documented ownership; app/project close releases connection; no hang |
| Pending read | Disconnect device then close immediately | Read completion/cancellation cannot race freed storage; handle closed once |
| Streaming | Power/link loss; then auto-reconnect; then close | UI remains responsive, correct state transition, exactly one connection worker |
| Reconnect pending | Close app or leave SF_Telem | Reconnect intent cleared before teardown; no delayed resurrection |
| Write pending, if Write is in supported use | Device loss/close/cancel | Bounded result; OS-overlapped storage outlives completion; no use-after-free |
| Recording active | Close app / return to launcher | Defined save/cancel policy invoked; preserved data or explicit error |
| Autosave/export active | Link loss plus close | No recorder/serial worker outliving its owner; valid complete saved file or explicit failure |
| Replay loaded | Incoming live traffic, switch back to live, then close | Replay state not overwritten; live reentry is defined; no stale listeners |

Test delayed open with controlled completion barriers. Immediate COM999 failure alone does not satisfy it. Measure normal deadline violations as failures; do not extend the timeout to the known 10–20-s Bluetooth stall and call shutdown bounded.

## Recording, replay and CSV

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| D01 | U/pr | Record/flush/load F-RECORD. Exact stored float values, channel names, sample counts and timestamp order; compare all entities and original v1 fixtures, including scene.bin precedence over per-entity fallback. |
| D02 | U/pr | Seek exact sample times, between samples, endpoints and outside bounds; play/pause; speeds -2,-1,0,0.25,1,4. Exact sample values at stored times; independent linear interpolation within float tolerance; endpoint clamp/stop and per-entity delayed start/unequal rates defined. Reject or explicitly normalize invalid times; never silently binary-search unsorted data. |
| D03 | U/nightly | Existing truncation/count guards plus bounded F-CORRUPT fuzz. Reject invalid inputs without unbounded allocation/hang; failed load has documented state (previous data preserved or cleared consistently). Accepted trailing bytes, if retained for compatibility, are explicitly tested. |
| D04 | U,W/pr,nightly | 4 writers x10,000 Record calls, concurrent flush, double flush, clear/re-register policy and destructor during flush. Snapshot contains a valid, individually consistent prefix for each entity; final post-join flush contains all intended records. Do not assert one global cut across independent entity locks unless implemented. |
| D05 | W,I/serial,release | Start/stop recording through production commands; dirty shutdown, loaded replay, autosave rollover, permission-denied/disk-full/partial-write injection and interrupted flush. No false success; last committed snapshot remains loadable; live stream and save state obey policy. Measure actual data-loss window rather than assuming autosave interval bounds it. |
| D06 | U,W/pr,nightly | CSV matrix below. Independent numeric oracle and valid grammar; ragged/invalid input rejected with a useful result, not coerced into a successful dataset. Record malformed-row behavior and output-state contract. Verify actual write/flush/close results. |

D06 cases:
- Header and headerless numeric tables; LF/CRLF; blank lines; exponential notation; +0/-0; small/large finite doubles; max_digits10-sensitive values. For finite supported values require numeric bit round-trip where signed-zero preservation is declared; no claim of preserving textual formatting.
- Exactly rectangular rows; too few/many columns; empty/whitespace-only cells; trailing delimiter; tabs/spaces; numeric prefix with trailing junk. Blank numeric cells must not silently become zero.
- Empty/header-only file; duplicate/numeric-looking headers; BOM; Unicode paths; quoted commas/newlines. Each has an explicit supported/rejected contract. Current simple parser is not a general CSV parser.
- NaN/Inf/overflow/underflow and locale changes. Choose the finite-data policy and locale-invariant on-disk representation; assert it consistently.
- Header count vs column count, unequal column lengths, invalid circular-buffer offset/count/capacity, directory creation error and write error.
- A generic CSV reader is not automatically a MATLAB/JPL/Horizons importer. Real consumer schemas require their own fixtures/adapters.

## DLL, host and UI lifetimes

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| L01 | W,G/gpu | Runtime load/unload 100 cycles with F-LIFETIME; 10 fresh launches/closes. Assert OnDetach/destructors before FreeLibrary and GL-context loss, no callbacks after unload, no leaked owned resources; reset-state contract matches. |
| L02 | W,G,I/gpu,release | 50 real successful Starforge rebuild/reload cycles, with at least 10 reflected-field changes; preserve serialized scene/custom fields, clear selection/undo as documented, stop Play according to supported rule. Old module code remains loaded until its objects die; registry entries neither accumulate nor disappear incorrectly. |
| L03 | W,G,I/gpu | Missing DLL/export/dependency, null CreatePluginLayer, failed compile and failed module load. Assert recoverable host/UI and preserved edit-scene data; no stale module pointers; logs identify failure. Do not assume transactional rollback exists without proving it. |
| L04 | U,W,G/nightly | Cancel/finish jobs, watcher notifications, selection/events/log callbacks/audio while owners unload. Test callback disconnect during dispatch and deferred transitions. No invocation into unloaded DLL; same resource baseline after quiescence. |
| L05 | I,Q/release | 200 scripted open/close/screen-switch cycles over Main/Testing/Analysis/Replay as present in source; cancel native file dialogs, open/close replay, toggle viewport, minimize/restore, resize, dock/undock, fullscreen; run with closed/open/lost COM port. No crash; no input/plot context regression. Save exact action log and screenshots. |
| P01 | G,I/gpu | Plot time series, XY, scatter, shaded band, multi-series legends, log/linear axes, empty and nonfinite data under the chosen policy. Exercise themed UI, multiple contexts/hosts where actually supported, and reload/reopen 50 times. Plots show known samples/ranges and valid adopted ImGui/ImPlot contexts. |

Differentiate a legacy runtime project DLL from a Starforge game module. Running Starforge.exe with --project merely exercises its runtime-host override; it is not proof of the editor's game-module lifecycle.

## Rendering, cameras and capture

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| R01 | G/gpu | F-2D includes colored/rotated/textured/atlas quads, SDF discs/rings/ellipse, lines, rects and SDF text. Compare reviewed goldens + sentinel pixels/ROIs; alpha, UVs, documented per-flush primitive grouping and font fallback are correct. |
| R02 | G/nightly | For quads/lines/circles/glyphs run 0,1,limit-1,limit,limit+1,2*limit+1 with limit 10,000 as currently declared. Test 30/31/32/33 distinct non-white textures (slot 0 is reserved), material/shader changes and mixed calls. No dropped/duplicated sentinels; assert actual flush/draw counts for isolated homogeneous cases. |
| R03 | G/nightly | Instanced circles and quads at 0,1,19,999,20,000,20,001,40,001; default/custom shader paths and normal draws before/after. For an isolated nonempty homogeneous submission expect ceil(N/20,000) instance draws; 10,000 gives 1. Verify rendered sentinels and state restoration. |
| R04 | U,G/pr,gpu | Camera pan/zoom anchors, viewport offset and 100/125/150/200% DPI; default zoom half-height 0.01..10,000; zero/negative/nonfinite input policy; 641x359 and changing viewport. Round-trip <=0.5 px in the declared local range; no NaN projection, divide-by-zero or resize crash. |
| R05 | G/gpu | Nested/multiple RenderPass cameras and two different RTT targets. Render distinct corners/orientation markers then UiImage; verify camera/viewport/FBO/blend/depth restoration and no source-target feedback. Repeated resize/create/destroy 200 times; no retained owned GPU object. |
| R06 | U,G/pr,gpu | CPU PNG encode/decode exact 4-corner RGBA fixture; invalid args/file path fail. GPU readback captures 641x359 top-left RGBA8 and defined HDR-to-byte behavior; verify dimensions, alpha and orientation independently. A GPU-context-free FBO capture is not required. |
| R07 | G,Q/release | Qualified 10,000-instance scene: 10-s warmup +60-s run at 1080p, separately circles/quads; timing method accounts for GPU completion without measuring readback as the workload. Record CPU/GPU/p95/p99, draw counts and hardware. Performance bar applies only to named configurations. |

Existing comparison tolerance is retained initially. Font readability needs selected glyph/zoom/DPI specimens and explicit reviewer signoff in addition to numeric image comparison. “Crisp at all zooms” is not an unbounded measurable contract.

## Authored 2D and shared support surface

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| C01 | U,G/nightly | Sprite list at 0/1/10,000+ items, equal sort keys, flips, active/disabled hierarchy, zero/negative/nonfinite transforms; sprite animation at loop/end/large delta. Deterministic ordering and documented transform behavior; no invalid draw data. |
| C02 | U,G/nightly | Tilemaps at 0/1/1024 cells per dimension and 1025 invalid boundary; max valid map is 1,048,576 cells. Test culling at edges/extreme supported zoom, atlas indices and batch rollover. No overflow/out-of-range access; rendered/cull counts match independent visible-cell calculation. |
| C03 | U,G/nightly | UI layout/hit-test, invalid/inverted rectangles, deep hierarchy and cycles, 1,000+ controls, canvas scale extremes; multiple lights with zero/negative radius/offscreen position and odd buffer dimensions. Bounded traversal, defined rejection/fallback, correct hit-test/light composite. Ratify supported depth/light ceilings in WO-00/02. |
| C04 | U/nightly | Flow/Story/EventBus: cycles, dangling targets, missing entry, malformed JSON, listener removal/addition during emit, nested emit and clear. Bounded evaluation, deterministic results, correct listener counts/lifetimes; source limits are asserted. |
| C05 | U,W,I/pr,release | Scene/prefab/material/config valid round-trip plus seeded malformed input; unknown/3D blocks preserved on 2D load/save; create/import/save/reopen/play/stop/undo/redo/delete using a real project. No user-data loss, stale entity reference or unbounded parsing. |
| C06 | U,W,G/nightly | Existing 2D physics/Jolt tests; asset/VFS/cache failure/unload, audio lifecycle, scripts, jobs and component storage below/at/above its page size. Shared service owners shut down cleanly; no out-of-page contiguous access, callback leak or incorrect entity update. |

Phase 30 P1-P7 supplies many detailed cases for R/C groups. Reuse the substance; its old dual-branch verification policy does not apply to this accepted 2D-only campaign.

## Clocks, numerics and to-9km reference

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| N01 | U,W/pr | Drive the production fixed-step scheduler with equal total simulated time split across 30/60/144-Hz and irregular render schedules; at speeds 0,0.25,1,4, with pause/resume and a >0.25-s stall. Assert the declared tick count and stall/clamp policy, no double tick and no pause debt. Distinguish global and plugin-local scaling. |
| N02 | U,W/nightly | Inject clock origins at 0,2 h,24 h with sub-frame deltas; finite state, monotonic time and approved tick/time error budget. Test zero/nonfinite rate/speed. Negative replay/Timeline playback works; negative global physics scale is explicitly rejected or otherwise has a safe documented policy without accumulating hidden restart debt. |
| N03 | U/pr | Keep RK4 projectile dt=1/480 for 1 s and current 1e-4 relative bound; oscillator analytic case 2,000 steps at 1/1000 with current 2e-3 bound; RK4 halving-step error ratio 10..24; semi-implicit energy <1.10*initial over existing 10-s case. Add independent double reference/units tests where the consumer needs them. |
| N04 | U/pr | Canonical PCG integer vector plus 1,000,000 outputs from same seed/stream; exact same-build reproducibility. Retain filter/lookup tests and deterministic scene state tests; seeded Gaussian/transcendentals use the declared same-toolchain scope. Never require cross-GPU image or MP4 bit equality. |
| X01 | U,G,W,I/release | Analysis sample loads F-TRAJECTORY and F-SERIES-LARGE, animates marker/trail, plots position/speed, play/pause/scrub at exact/midpoint times, exports PNG, then packages/runs. Verify 1,201 trajectory samples, equations, units and error; numeric source remains double. Selected/scrubbed sample agrees across plot and marker; test a 1e11 origin offset subtracted in double before float display. |

N01 must test Application/host dispatch, not solely TimelineState.Advance or FixedSubstepper. A deterministic integrator unit test cannot prove the wall-clock scheduler is correct.

For X01, scientific truth remains the independent equation/reference data. A visually plausible trajectory is insufficient. The 1e11 offset case tests the sample's display conversion, not a new promise of native large-coordinate precision throughout Cosmic.

A real to-9km data file, schema and integration workflow are still required to replace “representative analysis sample” with “qualified downstream consumer.”

## Distribution and documentation

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| K01 | W/release | Configure/build the minimal external consumer using a clean SDK and declared supported toolchain. No source-relative hidden dependency; PUBLIC 2D mode and DLL/CRT identity agree. |
| K02 | W,G/release | Package SF_Telem and analysis sample through the supported CLI and editor paths. Inspect payload/2D identity, selected DLL/assets/fonts/shaders/runtime/licenses and boot.cfg semantics; no excluded 3D app or developer-only test target. |
| K03 | I,Q/release | Clean Windows 10 and Windows 11 targets without compiler/SDK/dev PATH. Portable and installed launch; arbitrary CWD, spaces/Unicode paths; record/export/replay/config/screenshot under normal-user permissions in read-only install location. No asset/path/CRT failure and no false save success. |
| K04 | W,I,Q/release | Update/reinstall/uninstall on disposable test installation; documented user data survives or is removed only by the chosen policy. Saved older v1/scene fixtures remain readable. Package hashes and runtime build identity match the qualified candidate. |
| DOC01 | U/pr | Documentation coverage + local Markdown link/anchor checks; 2D public APIs still represented; archived paths have replacement links. No skeleton labeled complete. |
| DOC02 | W/release | Fresh-checkout walkthrough solely from current docs: configure 2D, build/run tests, create external sample, package/run. Commands work without undocumented mode cache or Kaden-specific paths. |
| DOC03 | Review/release | Active documentation has one consistent main/engine-3d policy; obsolete dual-test/byte-identical carry instructions clearly historical. 3D docs retained/parked, acceptance results dated, scientific/data/lifecycle limits published. |

The same installed SF_Telem recording workload must pass as the dev-tree workload. In particular, project://logs and relative recording paths cannot rely on a writable program installation directory.

## Soak, resources and release signoff

| ID | Tier/profile | Procedure and oracle |
| --- | --- | --- |
| S01 | W,G,I,Q/release | SF_Telem runs 2 h after warmup under nominal stream; include record/autosave/export, periodic screen switches, defined disconnect/reconnect sequence and final close. Separate a non-recording segment/run for the memory plateau oracle. No crash/hang, correct samples/counters, approved memory/export latency bounds, final files validate. |
| S02 | G,W,Q/release | Analysis sample runs 2 h after warmup, looping animation/scrub/resize/capture and repeated project reopen. No unbounded memory/resource growth, clock drift beyond declared tolerance, state corruption or increasing frame time. |
| S03 | U,W,G/nightly,release | Repeat deterministic logical fixtures with the same seed/input sequence 5 times. Exact integer/state/storage checks where specified; numeric/golden tolerances elsewhere. Record checksums and any run-to-run variation. |
| S04 | Review/release | Full requirement-to-case-to-evidence matrix, exact candidate hash and qualified environments, failure dispositions and reviewed goldens. No mandatory missing/blocked test disguised as success; promotion only after G5 and separate authorization. |

Memory interpretation for S01:
- 60-Hz recording for 2 h creates 432,000 stored samples per entity under a controlled clock.
- Expected raw float history is sum over entities of 4 * sample_count * (1 + channel_count) bytes.
- Vector capacity, metadata, simultaneous snapshots and CSV double columns add overhead. Capture capacity/copy counts, not just logical bytes.
- ReserveCapacity(18,000) is preallocation, not a five-minute cap.
- A supported maximum session/data volume, explicit stop/rotate policy and recoverable limit behavior must be documented. No infinite recording promise.
- Finish export, unload the project, wait for owned workers, and compare against a warmed baseline to distinguish retained data from leaked ownership.
- Finite allocator/driver caches may keep process memory elevated; require owned resource balance and no repeat-cycle growth, not an artificial “working set returns to launch” assertion.

## Traceability to the original brief

| Requirement | Catalog coverage |
| --- | --- |
| M1 plugin load/hot reload | B05, L01-L05, P01, resource gates |
| M2 primitives/text | R01-R02, C01 |
| M3 instanced 2D | R03, R07 |
| M4 ImPlot | P01, L02-L05, X01 |
| M5 serial/telemetry | T01-T06, D05, S01 |
| M6 replay | D01-D05, X01, S03 |
| M7 CSV | D06, X01 |
| M8 camera/multi-pass/RTT | R04-R06 |
| M9 animation/timeline | C01, N01-N02, X01 |
| M10 still capture | R06, K03 |
| M11 sim/math | N03-N04, X01 |
| M12 builds/boots/authors | B01-B05, C05, K01-K04, DOC01-DOC03 |
| S1 soak | S01-S02 |
| S2 reload storm | L01-L05 |
| S3 determinism | N04, S03 |
| S4 packaging/install | K01-K04 |
| S5 CI | B02-B05, H01-H04, profile policy |
| Additional stability obligations | C02-C06, data durability, actual OS/COM behavior, module ownership and documentation |
| A1-A5 | Deferred; acceptance outlines in 02-Stability-Work-Orders.md |

## Definition of a passing campaign

Mandatory evidence includes CPU tests, real GPU rendering, real application/module lifecycles, Windows serial integration, two-hour runs, clean installed workloads and current documentation. Fakes and unit tests provide fast regression coverage; they do not certify drivers, dialogs or physical reconnects.

Track test coverage by retained feature, lifecycle transition, invalid-input class and failure oracle. A high line-coverage percentage, a historical pass count or “no crash while watched” is insufficient.

Every newly discovered crash/data-loss/hang gets a minimal regression and a recorded disposition. Broader optional platforms, 3D, new feature additions and unqualified hardware remain outside this release claim.

