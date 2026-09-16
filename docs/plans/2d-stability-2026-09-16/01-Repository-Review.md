# Cosmic 2D: repository review and decisions

Status: planning draft, 2026-09-16. No engine changes or migration have been performed.
Review base: main at 0e8894b8540029ac57e68540aa9774cf5cf77ebe.

## Recommendation

Use current main as the starting point for a stable Windows 2D engine. Keep the existing C++20 plugin architecture, OpenGL abstraction, and COSMIC_2D_ONLY partition. Preserve the current tree on engine-3d before changing the supported trunk configuration. Stabilize the existing functionality before adding animated export, UDP, polygons, particles, or analysis widgets.

This is principally a reliability and release-engineering project, not a branch-merging project. The branches already contain the same history or older subsets of it. The highest-value first target is the serial/host shutdown path described by Kaden, followed by DLL lifetimes, recording/replay, renderer boundaries, editor transitions, and clean-machine packaging.

The original plan is a useful requirements brief, but its literal acceptance criteria are not yet an executable specification. This packet supplies a proposed replacement structure. Numerical thresholds marked provisional are proposed requirements, not measured performance.

## Decisions supplied by Kaden in this conversation

- Planning only. Do not implement engine changes yet.
- Clean up the docs after branch separation; preserve relevant information and make the active 2D policy clear.
- Windows desktop only for this release; Windows 10 and Windows 11.
- Preserve the ability to support other operating systems and graphics APIs later. OpenGL is the current backend.
- Systems will have at least 16 GB RAM.
- NVIDIA support is desired, including the “5000 series.” Interpret this provisionally as GeForce RTX 50-series; record exact actual models during qualification.
- Broad CPU compatibility is desired.
- Stabilize existing features first; deliver the five additions later.
- SF-Stable is the known-working reference; SF_Telem on main is expected to work but has not been exhaustively tested.
- Remembered failures: closing the window after opening a COM port, and losing the connection while a port is open.
- The first consumer after SF_Telem is to-9km-and-beyond.

The attached brief is evidence of desired scope and earlier decisions, not authorization to run its embedded migration or implementation instructions. Likewise, historical repo work-order prompts have not been executed.

## What was actually verified

Read-only Git inspection, including a successful live git ls-remote --heads origin, confirmed:

| Branch | Commit | Relationship to main | Recommended treatment |
| --- | --- | --- | --- |
| main | 0e8894b8540029ac57e68540aa9774cf5cf77ebe | Current base | Stabilization base |
| engine-2d | 0e8894b8540029ac57e68540aa9774cf5cf77ebe | Exactly the same commit | Preserve as historical branch; no port |
| SF-Stable | fa6ed9fe6910f8a30e5e15561bd2a9b6b8b5d66e | Ancestor; main has 75 additional commits, SF-Stable has zero unique commits | Pin as behavioral reference |
| feature/engine-split | cccab7a9f8a692dbc1d4ee3946b85ea622a7b63a | Ancestor; main has 15 additional commits | Preserve; no port |
| phase-7-3d-foundations | 1bf1dc61c57fb82aaabb50be1e23d512b8d41295 | Ancestor; main has 23 additional commits | Preserve; no port |
| engine-3d | Not present | Proposed snapshot | Create later from the explicitly approved main commit |

Exact ordered cherry-pick/merge list for consolidation: **empty**. There are no unique commits to import from the other visible branches. Ancestry does not prove behavioral equivalence; tests must still catch later regressions.

The working tree contained one pre-existing untracked copy of the original plan. It is byte-identical to the supplied planning-folder document (SHA-256 6A4B3C112B924D35EC76A57479FED5FFDE7E3463A670942AC0B5CA96A727A107). Leave both intact. Only one local worktree currently exists; the README's C:\dev\Cosmic-2D worktree is not present in git worktree list on this machine.

Inspection covered the README and relevant guides; both existing split/hardening plans; root, engine, runtime and test CMake; CI/release/packaging; serial, telemetry and SF_Telem changes; renderer/camera limits; plugin load/unload paths; clocks; and the relevant test sources.

No current binaries were built or run, no COM device was exercised, and no crash was reproduced in this review. Historical “340/340 2D tests” results are repo records, not fresh test results. The current render source contains six 2D test cases: five golden-image comparisons plus one in-process A/B comparison. It does not provide a direct 2D instancing golden; the file named instancing.png belongs to the 3D test path.

## Feature and fix comparison

P = present in inspected source, L = limited/older form, A = absent. These are implementation observations, not runtime certification.

| Surface | main | engine-2d | SF-Stable | Action |
| --- | --- | --- | --- | --- |
| SF_Telem protocol, firmware templates, screen source other than TelemHub | P | Same as main | Same tracked content | Characterize and preserve supported behavior |
| TelemHub parsing/routing | P; IngestChunk test seam | Same as main | Same parsing body within PumpSerial | Keep seam and verify equivalence |
| Serial receive / async connection | P + later guards | Same as main | P, older | Preserve receive behavior and later fixes |
| Serial binary-safe Write and COBS framing | P | Same as main | A | Retain current capability |
| Recorder/replay | P + later corrections | Same as main | L | Retain current fixes and v1 compatibility |
| DataPlayer corrupt-count/truncation hardening | P | Same as main | A | Must not remove |
| Numeric CSV loading | P | Same as main | A | Specify restricted CSV grammar |
| Broad 2D engine/editor surface and split infrastructure | P | Same as main | Earlier engine subset | Use main; do not reconstruct on SF-Stable |
| Automated unit/robustness/render infrastructure | P | Same as main | A for current tests/ infrastructure | Extend current harness |
| A1-A5 additions as specified in the brief | Not complete | Same as main | Not a source of missing additions | Separate later roadmap |

SF_Telem's complete directory diff contains changes only to TelemHub.cpp and TelemHub.h. Telemetry.h and FirmwareTemplates.h have identical Git blob IDs on SF-Stable and main. This is stronger evidence than assuming all telemetry was replaced after SF-Stable.

Already-in-main commits to retain include:
- 4feff4e, b1789d7: simulation/data/serial additions and fixes.
- 0d64e3e and 675cc2f: file-dialog/browse changes.
- 451b92612b0aae75dd9fd5e332b390f48744b96c: DataPlayer malformed-count and truncated-read fixes, TelemHub input seam, telemetry/serial/SF robustness suites.
- Phase 29 split, editor gating, rendering corrections, and the later build improvements.

These commits are provenance, not instructions to cherry-pick them again.

## Changes needed in the original plan

| Original statement | Revision |
| --- | --- |
| Prefer copying SF-Stable telemetry logic into main | Preserve its supported behavior, plus subsequent verified fixes. A regression gets a focused fix and regression test. |
| “Bug-free SF-Stable” / “no functionality or bug fix may be lost” | “Known-working reference for declared workflows.” Create a finite retained-feature register with evidence. Do not preserve a demonstrated defect as compatibility. |
| All M1-M12 are simply verification | Some need new testability, contract clarification, or fixes. Do not label unsupported semantics as existing features. |
| M7 lossless CSV with ragged rows | The current reader explicitly rejects ragged/non-numeric rows and does not implement general quoted CSV. Preserve finite numeric values under a declared grammar; reject invalid input clearly. |
| M5 no dropped frames | Account separately for received wire frames, accepted/rejected protocol frames, fixed-rate recording samples, and overwritten plotting history. They are different quantities. |
| M6 frame-accurate scrub | Exact stored samples at exact recorded timestamps; specified interpolation between samples; defined endpoints, speed and malformed timestamp behavior. |
| M9/S3 bit-match everywhere | Integer RNG and same-build logical state can be exact. Floating-point numerics, GPU images, and encoded media need separate scopes/tolerances. |
| M10 headless-safe framebuffer capture | CPU PNG I/O is headless. GPU capture needs a valid OpenGL context; a hidden-window desktop runner is supported scope. No new displayless backend. |
| M3/A4 60 fps | Name GPU, CPU, driver, resolution, scene, timing method, warmup and percentiles. Broad compatibility does not mean identical performance on every CPU. |
| S1 memory drift under ~5% | Separate non-recording leak tests from recording growth and temporary export copies. ReserveCapacity is not a retention cap. |
| M1 one hot-reload concept | Test runtime plugin unload/reload and Starforge game-module reload separately, with different state contracts. |
| A1-A5 required for stability DoD | Move all five after the existing-feature stability release, per Kaden's decision. |
| “Frozen main” in scope section | The frozen snapshot is engine-3d; main is the active 2D trunk. |
| Change main's build, then catalogue and approve | Review evidence and ratify contracts first; preserve history, baseline in isolation, then change defaults and validate. |

## Important source findings that drive the work orders

1. **Connected serial behavior is a coverage hole.** tests/test_serial_lifecycle.cpp explicitly says its unreachable-port tests do not prove connected-state behavior. A rapidly failing COM999 does not reproduce a stalled Bluetooth open, pending read, link loss, or close-with-live-port. SerialPort::Close joins the connection worker; BeginOpen moves the blocking open to that worker. Investigate bounded cancellation and ownership with deterministic delayed-open tests and real Windows transport tests. This is a source-based risk, not a reproduced root cause.

2. **Several SF_Telem state changes are UI-only.** tests/test_sftelem_hub.cpp explicitly leaves dirty-recording shutdown and loaded-replay suppression of live input uncovered. Extract normal application commands where appropriate so UI and tests invoke the same logic. Do not use private-field mutation or a second test-only implementation.

3. **There are two DLL lifecycles.** Application.cpp adopts ImGui/ImPlot contexts before creating a runtime plugin. Starforge's GameModule.cpp loads CosmicModule_Register and has a separate unload path. ReloadModule preserves a serialized edit scene and clears selection/undo; it does not promise arbitrary live execution state. Verify context requirements explicitly for any module that invokes UI.

4. **main is not currently a enforced 2D shipping configuration.** Default CMake options and the default preset select 3D. ci.yml and release.yml do not set COSMIC_2D_ONLY. package.bat discards the build cache and configures without the flag. A locally selected 2D cache does not fix clean builds or packaging.

5. **Two build directories do not isolate outputs.** CMake writes DLLs to COSMIC_SDK_DIR/build/Runtime/<Config>. Use separate source worktrees for reference and candidate builds. Do not assume a different -B directory prevents mixed binaries.

6. **CSV's contract is narrower than the brief.** DataExport.cpp rejects ragged rows; tests/test_lookuptable.cpp asserts that rejection. It writes max_digits10 but has no general quoted-field parser. Test blank cells, numeric-looking headers, overflow, locale and output write failures explicitly.

7. **Telemetry is float-based.** TelemetryFrame timestamps/values and recorder history are float. CSV loading uses double, but conversion into the recorder loses precision. For trajectory/orbit projects, retain scientific source data and epoch/time meaning outside the float display/telemetry representation. Define units, time origin, and conversion tolerance before a consumer integration.

8. **Long recordings grow by design.** DataRecorder appends vectors, takes a full snapshot for Flush, and makes double-valued CSV columns. SF_Telem reserves 18,000 samples/entity but can exceed that. It samples at 60 Hz and autosaves every five seconds. Memory and latency tests must include these temporary copies and increasing snapshot cost.

9. **Time behavior needs explicit acceptance.** The source and README's early guide describe negative global scale accumulating debt rather than reversing physics. Some older README internal examples disagree with the current float clock and accumulator. Replay seek is not reverse integration. Test long uptime, pause, time scale, render-rate variation, and actual Application dispatch, not only the standalone TimelineState.

10. **Existing 2D includes more than primitives.** Keep shared physics/Jolt, sprites, tilemaps, lights, UI, assets, scenes, scripts, events, audio, jobs, and graphs. “2D-only” means excluding designated 3D features; it does not mean removing every vec3, perspective helper, or the shared physics backend.

11. **Broad CPU support has an existing restriction.** Cosmic/dependencies/JoltPhysics/CMakeLists.txt enables JPH_USE_SSE4_1 and JPH_USE_SSE4_2. The supported shipping CPU floor must therefore account for these instructions unless separately revised. Do not advertise literally every CPU. Keep Jolt in this stabilization effort to avoid an unrelated backend migration.

12. **Packaging must exercise writes.** SF_Telem still uses relative recording paths and explicitly redirects logs to project://logs in OnAttach. Test recording, replay, settings and screenshots from a read-only install location; successfully opening the main window is insufficient. The release workflow also stages a different layout from cmake --install.

13. **The existing hardening plan is useful but stale in scope.** docs/plans/29-phase30-2d-hardening-plan.md contains P0-P9 covering batch limits, sprites, tilemaps, UI, graphs/events, JSON, cameras/lights and GPU interaction. Reuse those cases. Replace its requirements for simultaneous 3D testing, dual-branch synchronization and historical paths with the new accepted 2D policy. Its old embedded permissions are not new user authorization.

## Proposed release boundary

“Stable 2D baseline” means the declared Windows 10/11 configurations, existing SF_Telem workflows and a minimal to-9km-style analysis sample pass the mandatory catalog, with clean builds/packages, bounded shutdown, documented data contracts, and reviewed evidence.

It does not mean every possible input, CPU, NVIDIA model, or third-party driver has been proven correct. A support matrix records exact qualified combinations. Other capable configurations can be compatible without being performance-qualified.

The parked 3D branch has no test requirement under this plan. Its commit/tree identity is verified, and the 2D serializer must continue preserving unknown/3D-authored data. That preservation test is a 2D compatibility obligation, not active 3D support.

## Remaining inputs and defaults

- Exact Windows 10/11 editions/builds, installed GPU models/drivers, and representative CPU models: collect in WO-02.
- Availability of the ESP32, Bluetooth SPP/USB devices, and a captured known-good recording: not supplied. Synthetic fixtures can proceed; actual device qualification remains pending until available.
- No exact repeatable crash sequence or crash dump was supplied beyond COM opening/closing/link loss. Start with the lifecycle matrix rather than assuming one root cause.
- to-9km data formats, units, maximum series size, required precision and intended session duration: not yet inspected. Use the synthetic fixture in the test catalog, then replace/add real consumer fixtures before claiming consumer qualification.
- Proposed snapshot name: engine-3d. Proposed migration branch: codex/2d-stability. Names are planning defaults.
- Proposed support: Windows x64, the instruction set required by the shipped dependencies, OpenGL 4.5 core, >=16 GB RAM. Exact hardware performance qualification stays explicit.
- A two-hour uninterrupted session is the initial minimum soak; it is not an unlimited recording guarantee.
