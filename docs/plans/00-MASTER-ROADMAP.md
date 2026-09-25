# Cosmic — Master Roadmap v5 (2026-09-20)

> **Why this exists:** one place that says *what to do in what order*. v5 replaces the v4 roadmap
> (archived as [`archive/00-MASTER-ROADMAP-v4.md`](archive/00-MASTER-ROADMAP-v4.md)) after two
> campaigns changed what the repository *is*: `main` is now the **2D-only trunk** (D-PURGE: the
> 3D source, its vendored deps, tests and goldens were deleted from `main` and live on the
> `engine-3d` branch at `0e8894b`, tag `cosmic-pre-2d-2026-09-16`), and the engine's purpose is
> the **2D App Platform** — logic in your own C++, screens authored in Starforge, packaged to an exe.
>
> **The v3 rule (user decision 2026-07-04) stays in force:** a live plan doc contains **only
> unimplemented work**. Completed/superseded docs live in [`archive/`](archive/) with a dated
> `ARCHIVED` banner and a replacement link; every future feature has exactly one home,
> cross-indexed in [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md). Parked 3D documentation lives in
> [`../parked-3d/`](../parked-3d/README.md) (parked 3D) and is not a plan.
>
> **How to execute work with an AI:** each campaign packet carries copy-paste prompts
> (`work-orders/*.md`) and global rules (`work-orders/README.md`). One work order per session;
> the prompt names the only files to read; the session commits locally as `kdadabhoy` with no AI
> trailer and never pushes (D-commit).

| Doc | Covers |
| --- | --- |
| [`ux-shipping-2026-09-24/`](ux-shipping-2026-09-24/00-Start-Here.md) | **The current campaign.** Kaden's 21 usability/shipping items: editor defects, guides-first docs, the SDK zip + Starforge installer, the app-in-its-own-repo path, the 2D world-mode contract; decisions D-SHIP/D-LIBRARY/D-SPECIMEN/D-GUIDES/D-SAMPLES/D-WORLD/D-LAUNCHER, work orders UX-00..UX-Q1, acceptance catalog, evidence |
| [`app-platform-2026-09-18/`](app-platform-2026-09-18/00-Start-Here.md) | The App Platform campaign (priority scope done 2026-09-20; its remainder — AP-D2, KI-63, tidy, soaks, hardening — is absorbed by the UX packet). Decisions D-UI/D-LANES/D-SAMPLE/D-DOCS/D-PURGE/D-LIVE/D-LINKS, contracts, work orders AP-00..AP-Q1, acceptance catalog, evidence |
| [`2d-stability-2026-09-16/`](2d-stability-2026-09-16/00-Start-Here.md) | The closed stability campaign (WO-00..WO-10 done; WO-11/12/13 absorbed by the App Platform packet); the running [known-issues register](2d-stability-2026-09-16/contracts/known-issues.md) |
| [`TESTING-PLAN.md`](TESTING-PLAN.md) | The testing not run inside a campaign: the multi-hour soaks (postponed by Kaden 2026-09-24), their commands and missing drivers, and the clean-machine / by-a-person checks |
| [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md) | Living index: every missing/parked capability → home → unlock; the 3D rows now sit under "Parked (engine-3d)" |
| [`archive/`](archive/) | Every completed/superseded plan (docs 01–29 and the v4 roadmap), each with an `ARCHIVED` banner and replacement link |
| [`archive/2d-trunk-consolidation-brief-2026-09.md`](archive/2d-trunk-consolidation-brief-2026-09.md) | The original 2026-09 consolidation brief that both packets grew from (superseded by them) |
| [`../installer-guide.md`](../installer-guide.md) | User-facing: build/ship/install a setup exe |

## The one design rule that spans everything

**The engine ships generic verbs; apps own domain logic.** DataBus channels, bound widgets, the
flow graph, `user://` paths, the serial link → engine. A telemetry app's wire protocol, a
simulation's equations, a game's rules → the project's own C++. When an app needs something the
engine does not have, the engine grows a *general* verb, never a domain-shaped one.

## Shipped foundation — Phases 1–29 (all archived)

Everything up to and including the Phase 29 engine split is complete and recorded in
[`archive/`](archive/README.md): the core runtime, 2D renderer and post chain, ECS + reflection +
serializer, Jolt physics behind a pluggable backend seam, the Starforge editor (shell, panels,
flow/story editors, packaging), in-game UI entities, tilemaps, 2D lights, serial/telemetry,
audio, jobs, the sim-math toolkit, and the docs tiers. The 3D half of that foundation (Phases
7–12, 18, 20, 24, 26, 28: `Renderer3D`, terrain, water, particles, voxels, navmesh, skeletal
animation, Forge Isle) shipped on the pre-split tree and is **preserved on `engine-3d`**, not on
`main`; its chapters are under [`../parked-3d/`](../parked-3d/README.md) (parked 3D).

## 2D stability campaign (2026-09-16 → 2026-09-18) — **done**

[`2d-stability-2026-09-16/`](2d-stability-2026-09-16/00-Start-Here.md). WO-00..WO-10 landed and
are pushed: provenance (`engine-3d` + tag), 2D-only configure gate, CI, the acceptance runner
(`tests/acceptance/`), SF/COM lifecycle, recording/replay integrity, plugin/UI lifetimes,
renderer/camera/capture limits, authored-2D systems, clocks/math + `AnalysisSample`. WO-11
(packages), WO-12 (docs) and WO-13 (qualification) were **absorbed** into AP-P1, AP-D1 + AP-D2
and AP-Q1 respectively. Locked there and still binding: SSE4.2 CPU floor, Win10/11 x64 + GL 4.5,
float telemetry, `COSMIC_2D_ONLY=OFF` rejected at configure, no COM hardware in CI (the
`FakeSerialTransport` seam), to-9km deferred.

## App Platform campaign (2026-09-18 → 2026-09-24) — priority scope done; remainder absorbed

[`app-platform-2026-09-18/`](app-platform-2026-09-18/00-Start-Here.md). Status as of 2026-09-20
(qualified SHA `fa1223a`, CosmicTests 519 passed / 14 skipped in both configs):

| WO | What | Status |
| --- | --- | --- |
| AP-00 | Packet, KI-39 CI fix, superseded banners on WO-11/12/13 | ✅ done |
| AP-05 A/B | 3D purge: deletions, fence removal, `COSMIC_2D_ONLY` as a compatibility no-op | ✅ done |
| AP-01 | Foundation: `DataBus`, app services, `Data()`, flow additions, host wiring, template move | ✅ done |
| AP-P1 | Packaging identity, writable user data, acceptance runner in CI | ✅ done |
| AP-02 | Bound widgets, hosted-panel collection, goldens | ✅ done |
| AP-04 | Template kinds, samples on disk, **PendulumLab**, headless tests | ✅ done |
| AP-03 | Editor UX: rect gizmo, Screens/DataBus panels, source links, live loop, self-test | ✅ done |
| AP-Q1 | Integrate, qualify, showcase kit ([`../showcase/`](../showcase/README.md)), release report | ✅ done (**lite scope**: the 2-hour soaks were not run) |
| GUIDE | PendulumLab from-scratch walkthrough with screenshots ([`../guide/pendulumlab-walkthrough.md`](../guide/pendulumlab-walkthrough.md)); KI-61/62 fixes | ✅ done |
| AP-D1 | Docs structure: archive tiers, `parked-3d/`, roadmap v5, link checker in CI, stale sweep, clone-and-run README | ✅ done (this session, 2026-09-20) |
| AP-D2 | Docs content: the app-authoring chapter, chapter updates for the new APIs, reference rows, DOC02 fresh-checkout walkthrough, roadmap final | ↪ absorbed by UX-D1 / UX-D2 / UX-D3 (DOC02 runs from the SDK zip in UX-Q1) |
| Soaks | Y03, S01, S02, N02-drift-2h, T05 (the 2-hour runs AP-Q1 lite skipped; commands in `evidence/AP-Q1/release-report.md` §9) | ↪ [`TESTING-PLAN.md`](TESTING-PLAN.md) (postponed out of UX-Q1, Kaden 2026-09-24) |
| KI-63 | Packager stages `.cscene.bak` (registered from the GUIDE lane) | ↪ UX-04 |
| Dead-3D tidy | The rest of the AP-Q1 tidy list (unreferenced shaders/textures kept on purpose in AP-05 [A3]) | ↪ UX-H1 |
| Hardening WO | A stability pass on the new surface (DataBus/services/live loop) once AP-D2 has documented it | ↪ UX-H1 (prompt written in the UX packet) |

Landing order into `main` was AP-00 → AP-05A → AP-05B → AP-01 → AP-P1 → AP-02 → AP-04 → AP-03 →
AP-Q1 → GUIDE → AP-D1 → the pointer lane (KI-64/65). Everything left over is scheduled in the UX packet below.

## UX & Shipping campaign (2026-09-24 → ) — the current work

[`ux-shipping-2026-09-24/`](ux-shipping-2026-09-24/00-Start-Here.md). Kaden's 21 items after using the App
Platform for real, turned into twelve sessions run by an orchestrator
([`work-orders/ORCHESTRATOR.md`](ux-shipping-2026-09-24/work-orders/ORCHESTRATOR.md)). Status as of
2026-09-24 (base `0c2edd8`):

| WO | What | Status |
| --- | --- | --- |
| UX-00 | Packet, decisions, prompts | ✅ done (planning session) |
| UX-01 | Flow editor: size/dock/focus, backward-link routing, trigger-kind picker, Editors ✕ | ☐ |
| UX-02 | One gizmo per selection, UI Active semantics, what-a-button-does, Scenes list, Preferences + unsaved prompt | ☐ |
| UX-03 | Test fixtures hidden from the Launcher, samples from disk incl. PendulumLab, welcome, App default | ☐ |
| UX-04 | `Cosmic-SDK-<ver>-win64.zip`, `Starforge-Setup-<ver>.exe`, release workflow, KI-63 | ☐ |
| UX-G0 | 2D world-mode design doc + the five reserve-now contract items | ☐ |
| UX-D2 | API matrix by feature, `reference/app-services.md`, matrix checker in CI | ☐ |
| UX-D1 | Guides tier (`docs/guides/`, `docs/guide/` → `docs/developer/`), eight guides with captured pictures | ☐ |
| UX-05 | Cosmic as an SDK: `sdk` preset, app-repo kit, CI consumer job, SF_Telem dry run | ☐ |
| UX-H1 | Crash dumps, ground-control dry run, scale profile, dead-3D tidy remainder | ☐ |
| UX-D3 | Developer-tier chapter updates, `systems/app-platform.md` | ☐ |
| UX-Q1 | Qualify, DOC02 from the zip, quick legs (soaks → TESTING-PLAN), showcase refresh, release report, staged push/tag/release | ☐ |

Landing order: UX-00 → UX-01 → UX-02 → UX-03 → UX-04 → UX-G0 → UX-D2 → UX-D1 → UX-05 → UX-H1 → UX-D3 → UX-Q1.
The 2D world-mode *features* (parallax, camera follow/zoom, `Particle2D`, sorting layers, the to-9km sample)
are deliberately a later packet; UX-G0's design doc is its brief.

## Deferred — not in any work order (each names the contract it would extend)

From [`app-platform-2026-09-18/00-Start-Here.md`](app-platform-2026-09-18/00-Start-Here.md)
"What is deliberately deferred", plus the stability packet's consumer-driven A-backlog:

| Item | Would extend | Unlock |
| --- | --- | --- |
| Link/transport abstraction for CAN / I2C / UDP with a COBS command codec (`serial/Framing.h` and `SerialLink::Write` exist; no production consumer) | contracts §1 (DataBus sources) + `serial/` | a real non-serial link consumer (stability backlog **A2 UDP**) |
| `Config::Save` | `utils/Config.h` | an app that needs to write settings from the UI |
| "Run services while editing" | contracts §2 (`ServiceHost`) + §6 (live loop) | editor-time data preview demand |
| Flow `Fade` transitions | contracts §5 (screens/flow) | a shipped app that wants them |
| Additive under-scene rendering for overlays | `SceneRenderer` | overlay screens over a live screen |
| `UiText.Wrap` | contracts §3 (bound widgets) | long readout labels |
| GIF/MP4 export (stability **A1** animated export: deterministic frame sequence first) | capture path (WO-08 R-series) | a showcase/trailer need |
| Polygon fills (stability **A3**), analysis widgets (**A5**), 2D-native particles (**A4**) | `Renderer2D` / UI widgets | the analysis sample or a 2D game demonstrates repeated need |
| Everything 3D | — | 3D resumes on `engine-3d`; see [`../parked-3d/README.md`](../parked-3d/README.md) (parked 3D) for how |

## Acceptance ledger (user-run items — never silently dropped)

| Item | Origin |
| --- | --- |
| The 2-hour soaks Y03 / S01 / S02 / N02-drift / T05 — commands, drivers, order in [`TESTING-PLAN.md`](TESTING-PLAN.md) | AP-Q1 lite scope, 2026-09-19; postponed again out of UX-Q1, 2026-09-24 |
| Clean-machine install run of the packaged PendulumLab and SF_Telem (K03/K04 on a disposable VM) | stability catalog K03/K04; AP-P1 ran them on the dev machine |
| Clean-machine run of `Starforge-Setup-<ver>.exe` (SD04 on a disposable VM without Visual Studio) | UX catalog SD04 |
| Fresh-clone walkthrough of the root README Quickstart by a person, not a script (DOC02) | AP-D1 proved it by script (`evidence/AP-D1/clone-check.md`); AP-D2 owns the prose |

## Decision log (pointer)

All decisions since 2026-09-16 are recorded, dated, in the two packets:
[`2d-stability-2026-09-16/01-Repository-Review.md`](2d-stability-2026-09-16/01-Repository-Review.md)
and [`app-platform-2026-09-18/00-Start-Here.md`](app-platform-2026-09-18/00-Start-Here.md)
("Locked decisions"). The 2026-07 decision logs are in the archived v4 roadmap.

## Working agreement (how these plans get executed)

1. One work order per session; the packet prompt is the whole brief.
2. Explicit 2D mode: configure with `-DCOSMIC_2D_ONLY=ON` (an always-ON compatibility no-op since AP-05).
3. Production path in tests; failing-before/passing-after for every fix; honest gates (no golden regeneration, no tolerance loosening).
4. Evidence under the packet's `evidence/<WO>/`, keyed by SHA, config, command and exit code; `*.log` stays gitignored.
5. Both audits (`tests/check_gl_conformance.ps1`, `tests/check_docs_coverage.ps1`) and the link checker (`tests/check_docs_links.ps1`) exit 0 before a commit lands.
6. Commit locally as `kdadabhoy <kdadabhoy28@gmail.com>`, no AI trailer; the AI never pushes.
