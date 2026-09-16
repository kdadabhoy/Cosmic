# Cosmic 2D stability: requirements and AI work orders

Status: planning draft; no work order is authorized for implementation by this document alone.
Base: 0e8894b8540029ac57e68540aa9774cf5cf77ebe. Read 01-Repository-Review.md first.
Acceptance IDs and exact fixtures are in 03-Acceptance-Test-Catalog.md.
Git operations, when later authorized, follow 04-Migration-Runbook.md.

## Product boundary

Cosmic remains a reusable Windows C++20 engine/backend for 2D visualizations, plots, telemetry, and authored 2D applications. SF_Telem is the stability reference; to-9km-and-beyond is the first subsequent consumer.

Retain the existing host/plugin model, RendererAPI boundary, OpenGL backend, editor, shared simulation and support services. Application-specific flight, orbit, sizing and SF telemetry decoding remain in consumer modules. Do not move each project's scientific model into the engine.

Shipping target: Windows 10/11 x64, OpenGL 4.5 core, >=16 GB RAM, qualified NVIDIA GPUs. Capture exact CPU instruction requirements, including the current Jolt SSE4.1/SSE4.2 configuration. Compatibility and 60-fps qualification are separate promises.

GPU automation may use a hidden desktop window. No Linux/macOS, second renderer, GPU-free rendering, 3D QA, physics-backend replacement, or public API redesign is required.

## Release gates

| Gate | Required result |
| --- | --- |
| G0 — decisions and provenance | Requirements, branch SHAs, retained features, exclusions and test requirements reviewed |
| G1 — preservation and baseline | Frozen 3D snapshot recorded; fresh isolated 2D baseline, environment and existing failures recorded |
| G2 — reproducible build/test entry | 2D enforced across supported entry points; automated unit, GPU and integration test execution/evidence |
| G3 — reliability | COM lifecycle, recording, shutdown, DLL lifetime, renderer and content tests pass |
| G4 — consumer and delivery | to-9km-style sample, external project, package, clean install and documentation pass |
| G5 — qualification | Full catalog and long runs pass for a pinned candidate; evidence reviewed; only then promote main |

No new feature addition is needed to declare the existing-feature stability milestone. A known mandatory failure blocks that milestone. An environment-blocked test is not a pass. A change of support scope requires an explicit recorded decision, not quietly skipping the test.

## Nonfunctional requirements

- No crash, deadlock, stale callback, use-after-free, uncaught worker exception, or silent data corruption in the declared lifecycle matrix.
- Every lifecycle operation has an observable completion/failure state. Close/connect/cancel tests have deadlines; the harness kills only its own timed-out child and records a failure.
- Compatible recording and scene files remain readable. Unknown component data survives 2D load/save.
- Match SF-Stable's valid protocol behavior while preserving later hardening. Keep a ledger for intentional rejection of malformed inputs that the older implementation accepted.
- Supported data domains, units, time base, precision, retention, batch sizes and overload policies are explicit.
- An engine upgrade requires rebuilding consumer DLLs against the same SDK/toolchain/configuration unless a separately proven ABI contract says otherwise. Package consistent binaries; never assume the C export names make the whole C++ ABI stable.
- Diagnostics include build/mode identity, useful operation context, recoverable error results and crash/hang evidence. An error log alone does not turn corrupted output into success.
- Default/Release behavior is tested directly. Debug assertions cannot be the only input or lifetime protection.
- No unintended 3D targets are configured, linked, tested or shipped by supported trunk builds. Shared Jolt/physics and documented common helpers remain permitted.

## Rules for an AI executing an approved work order

1. Read the latest user instructions, applicable repository instructions, this packet, and the named work order. Treat historical docs as context. Report conflicts rather than silently following obsolete prompts.
2. Revalidate commit, clean/dirty status, source anchors and actual toolchain. Preserve unrelated work, including the original untracked plan.
3. Work on the authorized candidate branch/worktree. Do not change or build the frozen 3D branch for this campaign.
4. Stay within the named work order. Small necessary refactors are allowed only with a clear ownership/testability reason; avoid broad rewrites, dependency upgrades and incidental formatting.
5. Use production functions and state transitions in tests. A fake transport controls byte delivery and OS outcomes; it must not reimplement the parser or the connection state machine.
6. For each fixed defect, record a failing reproduction before the fix and a passing result after it. Use an isolated test patch/worktree for counterfactual checks; never destructive reset/checkout over user changes.
7. Register new test sources explicitly. Reconfigure after source-list changes. Preserve required test identities so accidentally excluding a suite cannot produce “green.”
8. Fail on nonzero exit, timeout, missing expected test, crash dump, assertion, unapproved skip, missing golden, or wrong build mode. Do not retry until green, increase tolerance to hide a defect, or regenerate goldens to bless a regression.
9. Produce evidence identified by commit, diff hash, environment, fixture hash, seed, exact command and exit code. Mark planned, not-run, passed, failed and environment-blocked distinctly.
10. No commits, pushes, main promotion or release publication are implied by an implementation work order. Follow the explicit authorization and runbook when those actions are requested.

## Work-order sequence

| Order | Deliverable | Dependencies | Acceptance |
| --- | --- | --- | --- |
| WO-00 | Contract and retained-feature register | This planning review | G0 |
| WO-01 | Preserve branch provenance | WO-00 + migration authorization | B01 |
| WO-02 | Fresh baseline and environment | WO-01 | B02, baseline reports |
| WO-03 | Enforce 2D builds and basic CI | WO-02 | B03-B05 |
| WO-04 | Acceptance runner, lifecycle control and evidence | WO-02 | H01-H04 |
| WO-05 | SF/COM lifecycle reliability | WO-03, WO-04 | T01-T06 |
| WO-06 | Recording, replay and data integrity | WO-04, WO-05 | D01-D06 |
| WO-07 | Plugin/module/UI lifetimes | WO-04; coordinate WO-05/06 | L01-L05, P01 |
| WO-08 | Renderer, camera and capture limits | WO-03, WO-04 | R01-R07 |
| WO-09 | Existing authored 2D/support systems | WO-03, WO-04 | C01-C06 |
| WO-10 | Clocks, math and analysis reference sample | WO-06, WO-08 | N01-N04, X01 |
| WO-11 | External consumers, packages and installation | WO-03, WO-05-WO-10 | K01-K04 |
| WO-12 | Documentation cleanup and support policy | WO-03-WO-11 | DOC01-DOC03 |
| WO-13 | Qualification, report and eventual promotion | All prior work | S01-S04, G5 |

The table describes dependencies, not permission to delegate work or run agents in parallel.

### WO-00 — Ratify contracts and pin the retained surface

Deliver a decision record for the support matrix, two-hour minimum session, serial close behavior, recording durability, precise CSV grammar, hot-reload state, and numeric precision.

Create a retained-feature register covering M1-M12 and the shared features omitted from the original acceptance table: sprites/animation, tilemaps, 2D lighting, UI layout/input, scenes/prefabs, VFS/assets, scripts, graphs, events, jobs, audio and shared physics. Map every entry to acceptance IDs and existing tests. Classify current limitations separately from required fixes and later additions.

Resolve the strict meaning of “main is 2D-only”: recommended policy is default ON plus configure-time rejection of OFF in the supported trunk. Keep the OFF path available on the preserved 3D branch. Record Jolt as an explicit retained dependency.

DoD: no required feature lacks an oracle, test owner/order and known execution environment. Hardware/fixture availability can remain pending, but its mandatory tests cannot disappear.

### WO-01 — Preserve 3D and reference provenance

Follow the migration runbook; re-query branch heads, pin approved full SHAs, create engine-3d from the approved original main without editing its tree, and verify its identity. Preserve every existing branch. Record an immutable snapshot identifier (an annotated tag is recommended if authorized).

Create only the authorized isolated candidate/baseline worktrees. Never assume C:\dev\Cosmic-2D already exists. No cherry-picks are needed for the reviewed branch state.

DoD: B01 evidence; a documented rollback/reference point exists before supported defaults change. This work order proves preservation, not 3D runtime stability.

### WO-02 — Establish a fresh 2D baseline

Inspect README, tests/CMakeLists.txt, render harness, existing Phase 29 results and Phase 30 P0. Use a fresh isolated source worktree and explicit COSMIC_2D_ONLY=ON. Build and run Debug and Release units, then the current GPU suite on an available qualified GPU.

Capture exact CMake/compiler/SDK/dependency versions, CMakeCache, test names and counts, warnings, GL renderer/version/driver, Windows build, CPU features, RAM, and reference machine identity. Run GL-conformance and documentation-coverage audits. Do not regenerate goldens.

Baseline SF_Telem's supported workflows using SF-Stable and current main in separate runtime directories. If the older build needs tooling unavailable here, keep source parity evidence, record the build blocker, and obtain a known-good binary/device capture later rather than claiming runtime parity.

DoD: clean baseline report with existing failures recorded. The historical 340-test claim is checked, not used as a success placeholder. Current GPU suite count is recorded accurately.

### WO-03 — Enforce 2D across supported entry points and CI

Inspect root/engine/Starforge CMake, CMakePresets.json, build/setup/package scripts, project templates, BuildRunner and both workflows.

Make new/default builds, CI, external scaffolds, editor packaging and release staging explicitly select the supported 2D mode. Prevent a stale OFF cache or script override from silently shipping 3D. Keep source files; exclude designated 3D targets and dependencies. Preserve Jolt and the shared backend interface.

Ensure mode/SDK/CRT/configuration consistency across plugin boundaries. Inspect the actual generated target/source graph, PUBLIC definitions and binary/package manifest. Do not use a blanket “no 3D-looking symbol” rule; shared math/camera/physics exceptions exist.

Basic PR CI: Debug/Release 2D units, audits, explicit mode, test discovery checks and report upload. Cache identity includes mode, architecture, toolchain and relevant CMake files. Establish a scheduled or dispatched GPU qualification job only on a suitable runner.

DoD: B03-B05 pass from clean and stale-cache scenarios. A release job cannot bypass the 2D contract.

### WO-04 — Make acceptance executable and failures observable

Proposed files/targets, not currently present:
- tests/acceptance/Run-Acceptance.ps1, manifest/fixture metadata, per-profile outputs.
- A small acceptance host/project and production-action adapters for lifecycle driving.
- Test fixtures for controlled transport outcomes, recordings, scenes and analysis data.

Build a minimal process runner with deadlines, per-run temporary directories, explicit mode/test checks and JSON/JUnit-compatible evidence. Capture exit code, logs, hang/crash evidence, metrics, actual/expected/diff images and environment. Commands are parameterized; no dependence on Kaden's absolute paths. Kill only children launched by the run.

Separate pure deterministic state-machine tests, Windows transport integration, hidden-window GPU tests, actual editor UI tests, packaging and soak profiles. At startup, detect missing capabilities and return environment-blocked rather than passing an empty suite.

Use narrow injectable transport/clock/file-failure seams where they improve ownership and reproducibility. UI buttons and test drivers invoke the same application commands. Do not expose raw handles, mutate private flags, or build a second simulated SF application. Test-only host control is excluded from distribution.

DoD: H01-H04 prove the runner catches intentional failure, hang, missing suite/golden and missing GPU; then a normal minimal case succeeds.

### WO-05 — Reproduce and fix COM close/disconnect failures first

Primary paths: serial/SerialPort.{h,cpp}, SerialLink, SF_Telem root OnDetach, TelemHub::Shutdown, and the host's deferred transition/shutdown.

Create deterministic scenarios for delayed open, abandoned successful open, pending read, silent stall, hard disconnect, in-flight write, reconnect and close. Cross them with app window close, return to launcher, connection-panel close, screen switching and recording/export states. Test the actual serial ownership chain, not just a local SerialPort object.

Investigate connection-worker join/cancellation, handle publication/cleanup, overlapping operations and reconnect intent. State the permitted caller/thread contract. Do not “fix” blocking shutdown by detaching a thread that still references a destroyed object.

Retain SF-Stable parser/firmware behavior and main's existing hardening. Get device/virtual-COM corroboration for tests using fakes. The firmware's raw KISS/CRC8 framing and PC-side tagged text protocol are separate test surfaces; COBS is not a substitute for either.

DoD: T01-T06 including the user's close/link-loss regression scenarios, no leaked thread/handle, clear cancellation policy and failing-before evidence. Hardware gaps remain explicit.

### WO-06 — Prove recording, replay, CSV and write-failure behavior

Extend current recorder/player robustness tests and add real UI-command state coverage. Pin successful SF-Stable v1 recordings as immutable compatibility fixtures. Verify stored samples, independent entity rates, timestamp interpolation, pause/seek/reverse playback, and isolation between loaded replay and live input.

Define limits for corrupt-file counts, timestamps, file size and allocations. Exercise disk-full/permission failures, interrupted autosave, invalid paths and shutdown during flush. Report failures truthfully and preserve the last complete recording. Do not claim a five-second crash-loss bound without measuring snapshot/write duration and successful completion.

For CSV, keep a deliberate restricted numeric format unless the user separately chooses general CSV import. Add parser and writer edge tests, including blank cells, range errors, locale, header restrictions and failed flush/close. Retention and export copying must have a supported session/memory budget.

DoD: D01-D06; recorded value/sample accounting agrees with the independent fixture; no invalid file is reported as a valid full recording. No format change without a version/compatibility decision.

### WO-07 — Prove runtime/plugin/module/UI teardown

Test Application runtime plugins and Starforge script modules as separate lifecycles. Cover OnAttach/OnDetach/destruction order, module-owned components/scripts, Log sinks, EntitySelection/EventBus listeners, jobs, file watchers, audio, hotkey callbacks, project fonts, textures and ImGui/ImPlot contexts.

For runtime reload, define reset/reopen behavior. For editor reload, preserve serialized edit-scene content and document cleared selection/undo and stopped Play. Do not promise preservation of arbitrary running C++ state.

Exercise repeated open/close, modal dialogs, cancelled browsing, failed plugin/module loads, failed builds, close during background activity and return-to-launcher. Failure must leave recoverable UI and saved data. Any module UI usage must have a valid adopted context.

DoD: L01-L05 and P01; 50 real editor rebuild/reloads plus repeated host loads; balanced owned resources after warmup; stale callbacks cannot execute after unload.

### WO-08 — Finish the renderer/camera/capture safety net

Reuse Phase 30 P1-P3/P7 as case-design input. Extend tests/render rather than replacing its image comparator. Test every primitive, text/font path, batch limit +/-1, texture slot rollover, 20,000-instance chunking and mixed draw/state transitions.

Preserve documented primitive grouping within a flush; do not accidentally turn this into a renderer ordering redesign. Ensure test counters are enabled/reset and sentinels make missing/duplicated items visible. Test actual 2D instancing, not the 3D instancing golden.

Cover camera bounds and invalid inputs, cursor anchored zoom, DPI/viewport offsets, nested RenderPass restoration, odd/zero-size targets, RTT UiImage orientation, PNG alpha/origin and missing assets/shaders. Measure performance on a named machine; keep correctness separate.

DoD: R01-R07. Reviewed images and numerical/counter assertions agree; original goldens change only for a separately justified intentional correction.

### WO-09 — Harden existing content and shared services

Reuse Phase 30 P2-P6: sprite ordering/animation, tilemap bounds/culling, nested UI and cycles, Flow/Story/EventBus mutations and reentrancy, malformed scene/prefab/material JSON.

Test create/edit/save/reload/play/stop/undo/delete transitions; maintain unknown component blocks. Bound traversal/allocation under malformed data. Use seeded fuzz plus fixed regression fixtures and a process deadline for parser hangs.

Keep shared physics regressions, asset/VFS/cache lifecycle, audio teardown, scripts and jobs under the retained-feature register. Test scale across the EnTT storage-page boundary, asynchronous work completion and no callbacks into unloaded owners. Apply targeted fixes with regressions, not a new ECS architecture.

DoD: C01-C06 and existing 2D tests; each retained service has meaningful boundary/lifetime coverage.

### WO-10 — Verify clocks, numerics and the first analysis workload

Pin actual Application fixed/variable dispatch semantics as well as TimelineState. Cover clock injection or an equivalent isolated production seam, pause/resume, positive speed, negative replay, long uptime and render-rate variation. Define safe handling of unsupported negative global physics scale; do not treat it as a correct reverse solver.

Reuse analytic integrator/filter and canonical PCG tests. Scope exact repeatability to known builds/seeds/input schedules. Keep scientific source values as double in the consumer sample; explicitly convert local relative coordinates for float rendering. Do not migrate the entire engine to double during this order.

Create a small analysis reference project using existing primitives/ImPlot/CSV/replay/PNG. Use the catalog's synthetic ballistic trajectory plus a later real to-9km fixture. It is a compatibility specimen, not A5's reusable widget library.

DoD: N01-N04 and X01. The sample builds outside the SDK checkout, plots and animates correctly, scrubs by the defined time base, and exports a correct still. Actual to-9km qualification remains pending until its real schema/workflow is exercised.

### WO-11 — Package and install the same qualified 2D app

Unify or verify equivalent behavior across cmake install, package scripts, editor packaging and release staging. Preserve only the selected app's payload plus required runtime/assets/licenses. Enforce consistent 2D build identity and explicit writable user-data paths.

Build an external project from a clean SDK consumption path. Package SF_Telem and the analysis sample. Test portable and installed layouts, different working directories, spaces/non-ASCII paths, missing assets, no SDK/compiler on target, and VC++ runtime availability.

On Windows 10 and 11, install as appropriate, run as a normal user from a read-only installation location, connect/replay/record/export, update and uninstall according to a documented user-data preservation policy. A VM without suitable graphics cannot certify OpenGL rendering.

DoD: K01-K04. Staged artifacts and installer results match the candidate; no source-tree or developer PATH dependency.

### WO-12 — Clean up the docs after separation

Kaden explicitly requested this cleanup. Inventory README, docs/guide, docs/reference, docs/systems, docs/design, docs/engineering-notes, docs/plans and SF_Telem docs.

Make one support/build/release policy authoritative. Update all main/engine-2d/engine-3d descriptions, fresh checkout commands, presets, packaging, test profiles, branch carry instructions, known limits and consumer contracts. Remove the active requirement to synchronize byte-identical branches or test parked 3D.

Preserve 3D technical/reference history. Mark it as parked and outside the supported trunk; archive superseded plans with origin/date/commit and replacement links. Do not mass-delete useful explanations or break the public-header coverage manifest. Keep old command prompts clearly historical so a future AI will not execute them as current work orders.

Reconcile README internal clock examples with source; fix stale “every test passed” language, 6-golden versus 6-case wording, SF screen documentation, and any new lifecycle/data contracts. Label skeletons honestly rather than claiming they are complete. Finish the 2D-facing documentation needed for the qualified workflows; a complete rewrite of all 3D documentation is not part of this order.

DoD: DOC01-DOC03; links/coverage checks pass; a new AI can build/test/package a 2D app using only the current documented path.

### WO-13 — Qualify and promote only the proven candidate

Run the complete acceptance catalog on the pinned candidate after all code, packaging and documentation changes. Include both Windows versions, Debug/Release units, qualified GPU tests, actual host/editor workflows, real Windows serial integration, 30-minute telemetry and two-hour soak sessions.

Collect a release report: exact SHA/diff status, feature/test traceability, qualified machines, command logs, skipped/blocked tests, crash/hang evidence, memory/resource/performance charts, package hashes, durability results, known limits and regression fixes. Required bugs remain blocking; optional deferred additions are listed separately.

After explicit authorization, use the runbook to promote the validated candidate without rewriting existing branches. Verify the required CI on that exact commit. Keep packaged evidence and the previous stable release available. Do not automatically publish an installer or reset main.

DoD: every mandatory case passed or the user explicitly changed the release scope; G5 satisfied; no claim that an unexecuted test passed.

## Copy-paste execution prompt

Use only after implementation is separately authorized. Replace the work-order ID and paths.

~~~text
Execute only WO-XX from the approved Cosmic 2D stability planning packet.
Read 01-Repository-Review.md, 02-Stability-Work-Orders.md, the relevant acceptance
IDs in 03-Acceptance-Test-Catalog.md, current repository instructions, and the
latest user decisions. Revalidate the current SHA and source anchors.

Report prerequisite failures before editing. Work only in the authorized candidate
worktree. Preserve SF-Stable behavior and later fixes, keep the 3D snapshot parked,
and follow the work order's allowed scope. Use the production path in tests.

For every fixed bug, record a failing reproduction and the passing regression.
Run the named checks with explicit 2D mode, deadlines and durable evidence.
Do not regenerate goldens, suppress failures, broaden scope, commit, push, or
promote main without applicable authorization.

Return: changes and rationale; exact commands and results; evidence paths;
remaining risks/blocked tests; and whether this work order's DoD is met.
~~~ 

## Later additions: a separate, consumer-driven backlog

Order these after the stable baseline based on to-9km's actual needs, not their original A-number.

| Addition | Bounded first work order | Required proof |
| --- | --- | --- |
| A1 animated export | Deterministic frame-sequence export, then separately chosen MP4/GIF encoders; hidden GL context is acceptable | 300 source frames for 10 s at 30 fps, frame/time metadata, independent decoding, orientation/colors, cancellation and encoder failure; define GIF's representable timing separately |
| A3 polygon fills | Simple concave/convex polygons with documented winding/degeneracy rules; holes/self-intersections deferred unless needed | Independent area/triangulation checks, clockwise/counterclockwise, collinear/duplicate input, invalid rejection, overlap/order and batch boundaries |
| A5 analysis widgets | Extract reusable presentation components only after the analysis sample demonstrates repeated need | Each widget consumes domain-independent data, has unit/time semantics, empty/error states, bounded updates, documented examples |
| A2 UDP | Reuse a proven transport boundary; define sequence/timestamps, duplicate/loss/reorder policy and backlog limits | Seeded loss/reorder/duplicate injection with exact accounting, disconnect/reconnect/close, bounded memory; UDP cannot promise serial-style delivery |
| A4 2D particles | Renderer2D-native, seeded, bounded emitter with declared count/overdraw target | State determinism, lifetime/resource bounds, limits, alpha/order, measured CPU/GPU cost and a reviewed golden |

Do not make all four A5 widgets, MP4/GIF, or particles prerequisites for using the stabilized engine. An ellipse already has a SDF-circle drawing path; arbitrary polygon tessellation is the new part of A3.
