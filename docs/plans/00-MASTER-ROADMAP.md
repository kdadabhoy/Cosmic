# Cosmic — Master Roadmap v2 (2026-07-01)

> **Why this exists:** one place that says *what to do in what order*. Each workstream has its own
> plan document with PR-sized, acceptance-checked items; this file only sequences them into phases.
>
> **How to execute a phase with an AI:** open a session on the repo and say
> *"Read `docs/plans/00-MASTER-ROADMAP.md` and the plan doc(s) for Phase N, then implement item
> \<X\>."* One item (or one small phase) per session. Every item's plan doc states its acceptance
> check — the item is not done until that check demonstrably passes. Before editing, the AI must
> re-verify quoted code still exists (line references were true on 2026-07-01 and will drift).
>
> | Doc | Covers |
> | --- | --- |
> | [`03-simulation-engine-plan.md`](03-simulation-engine-plan.md) | Generic sim toolkit: config, integrators, filters, tables, noise, RNG, UDP, gamepad (E-series) |
> | [`04-viper-sim-plan.md`](04-viper-sim-plan.md) | ViperSim app + portable `viper-fc` flight code (P0–P8, gates G1–G3) |
> | [`05-3d-engine-plan.md`](05-3d-engine-plan.md) | Full 3D roadmap: foundations → CAD nav/gizmos → PBR → terrain/water/particles → demos → perf → Vulkan gate (S-series) |
> | [`06-docs-plan.md`](06-docs-plan.md) | Remaining docs work: §40 refresh, README split, docs index (D-series) |
> | [`08-audio-plan.md`](08-audio-plan.md) | Audio subsystem (miniaudio): one-shots → loops/groups → positional (A1–A3) |
> | [`09-windowing-plan.md`](09-windowing-plan.md) | Fullscreen black-screen / snip-overlay / DPI hardening + responsive rendering (W-series) |
> | [`archive/`](archive/) | Completed plans kept as records: bug audit, work orders, readme pass, installer build-out |
> | [`../installer-guide.md`](../installer-guide.md) | User-facing: build/ship/install a setup exe (not a plan — a guide) |
> | [`../design/responsive-rendering-and-pause.md`](../design/responsive-rendering-and-pause.md) | Accepted design consumed by Phase 1 (W4) |

## The one design rule that spans everything

**The engine ships generic verbs; apps own domain logic.** UDP socket, RK4 template, `DrawMesh`,
terrain system, `user://` paths → engine. Tailsitter mixers, MAVLink, aero polars, the volcano
scene → apps. When an app needs something the engine doesn't have, the engine grows a *general*
verb, never a domain-shaped one. Every plan doc applies this rule.

## Foundation already in place *(former roadmap Phases 1–4, all ✅ 2026-07-01)*

Bug audit fixed + tests/CI/clang-tidy (`archive/01`, `archive/02`) · installer pipeline shipped:
`--project` flag, `Version.h`, DPI manifest, `user://` data root, `package_installer.bat` + Inno
(`archive/07`) · README honesty pass §20.5/§21.5/§26/§28.5 (`archive/06-readme-update-plan.md`) ·
sim foundations: configurable timestep, `Spatial.h`, `Framing.h`, 3D viewport S1+S2 with
acceptance app `Projects/Engine3DDemo`. Also done 2026-07-01: README §1.5 command reference,
`docs/installer-guide.md`, this plans-folder restructure.

---

## Phase order

Two long tracks run through the roadmap — **Sim/Viper** (phases 2–6) and **3D engine** (phases
7–12) — with windowing first because it hurts daily. The tracks are independent after Phase 2:
interleave them freely; the numbering is the default serialization, not a hard dependency, except
where a phase lists explicit prerequisites.

### Phase 1 — Windowing correctness *(doc 09, W1–W6)*
Fix the daily irritations: fullscreen black-flash, snip-overlay glitch, frozen render during
drag/resize, window-state edge cases.
- **Do:** W1 (instrument + repro matrix) → W2 (paint-through-transition) → W3 (DWM experiment,
  gated on W1 evidence) → W4 (responsive rendering + pause, per the accepted design doc) → W5
  (state hardening, parallel-safe) → W6 (docs).
- **AI tier:** W1/W5/W6 low; W2/W4 touch the frame loop — stronger model + your review.
- **Done when:** doc 09's acceptance checks pass at 100% and 125% scaling; snip over fullscreen
  captures cleanly; dragging the window keeps painting; README §24 + engineering note updated.

### Phase 2 — Sim math & config toolkit *(doc 03: E10–E15, E7)*
Small, header-heavy, unit-tested engine verbs that unblock everything sim-shaped.
- **Do (order in doc 03):** E10 TOML config → E11 integrators → E13 lookup tables → E12 filters →
  E15 RNG → E14 noise → E7 gamepad. All parallel-safe on separate branches; all land with doctest
  coverage.
- **AI tier:** low/medium — ideal small-model tasks.
- **Done when:** all seven merged with green tests; template project demonstrates config load +
  gamepad axes.

### Phase 3 — ViperSim skeleton + dynamics decision *(doc 04: P0–P1)*
- **Do:** P0 (project skeleton, SimHub, `viper.toml`, telemetry schema) → P1 (JSBSim spike behind
  `IDynamics`, **1-week timebox**, drop-test demo; record the JSBSim-vs-hand-rolled outcome in the
  Viper decision log and in doc 04).
- **Done when:** drop test replayable in ReplayScreen; dynamics decision CLOSED with rationale.

### Phase 4 — Tuned hover + energy truth *(doc 04: P2–P3; doc 08: A1–A2)*
- **Do:** P2 (`viper-fc` + SimHal + attitude loop + TuningScreen) → P3 (position hold, sensor
  noise, battery + energy accounting, gamepad flying, EnergyScreen) with audio alert plumbing
  (A1 core playback, A2 loop/alert groups) landing alongside P3's failsafe tones.
- **Milestone with real-world value:** the Energy screen answers the proposal's power questions
  (hover ≈230 W vs cruise ≈106 W, endurance splits) **before parts are purchased**.
- **Done when:** Gate **G1** — hover stable against noise + 5 m/s gusts; recorded regression
  replay committed.

### Phase 5 — The hard flying: transition & orbit *(doc 04: P4–P5; doc 05: S3)*
- **Do:** P4 (full-envelope aero + transition state machine, both directions) and P5 (orbit-on-ROI
  + full failsafe set + fault injection), pulling S3 viewport items (FPV inset, ribbon, horizon,
  labels) as P4/P5 need them.
- This is the project's core research risk — budget for iteration; every attempt recorded/replayable.
- **Done when:** Gates **G2** and **G3** pass (doc 04 §4) — the sim-side contract that clears real
  flight testing to proceed per the Viper playbook.

### Phase 6 — Hardware in the loop + rig *(doc 04: P6–P7; optional E4+P8)*
- **Do:** P6 (Teensy HIL over E5 framing; latency on screen) → P7 (gimbal rig). Optionally E4 UDP
  + P8 (MAVLink → QGroundControl).
- **Done when:** the P4 transition flies on the physical Teensy; rig mirrors sim attitude.

### Phase 7 — 3D engine foundations *(doc 05: S4.1–S4.7)*
Unified camera hierarchy → material-driven meshes → 3D scene components → asset cache + glTF →
lighting v1 → MRT framebuffers → compute/SSBO. Strictly ordered inside; each a PR.
- **Done when:** ECS scene renders lit glTF meshes; entity-ID readback works; compute demo hits 60 fps.

### Phase 8 — CAD navigation, gizmos, picking *(doc 05: S5)*
SolidWorks-style navigation (S5.1 — **[filler]: only needs S1, safe to pull into any earlier
phase**), frame/snap views, ViewCube, ImGuizmo transforms, ID-buffer picking + selection outline.
- **Done when:** Engine3DDemo manipulates entities with gizmos; MMB-orbit-about-cursor-point feels
  like SolidWorks at both DPIs.

### Phase 9 — Visual realism core *(doc 05: S6–S7)*
HDR pipeline → PBR + IBL → CSM shadows → SSAO → bloom → AA, then sky/atmosphere/fog/time-of-day.
- **Done when:** glTF reference scene matches a reference viewer; day-night scrub looks plausible;
  profiler-free frame still ≥60 fps on the dev GPU.

### Phase 10 — World systems *(doc 05: S8–S10)*
Terrain (quadtree LOD, splat/triplanar materials, `SampleHeight`) → water Tier 1 (+FFT Tier 2
later) → GPU particles, froxel volumetrics, heat haze. Internally reorderable.
- **Done when:** each system's stage acceptance in doc 05 passes.

### Phase 11 — Flagship demos + performance *(doc 05: S11–S12)*
Snow/lava systems as generic engine features; `Projects/VolcanoDemo`, `WinterDemo`, ocean/lake
demo as acceptance scenes; then culling, sort keys, instancing, LODs, GPU profiler, texture
pipeline.
- **Done when:** the volcano/snow/water demos run ≥60 fps at 1080p with profiler evidence — the
  "realistic volcanoes, water, snow" goal made concrete.

### Phase 12 — RHI hardening + Vulkan gate *(doc 05: S13; backlog S14)*
Conformance audit (no GL outside the platform layer), frame-lifecycle spec, then the explicit
stay-GL / go-Vulkan / adopt-RHI decision **made on S12 profiler data**, not vibes. Groom the S14
game-engine backlog (animation, Jolt gate, editor app, serialization) against real needs.

### Continuous — docs & release polish *(doc 06; archived 07 leftovers)*
D-series (docs index, §40 refresh, README split last) any time; parked release items (CI release
job, code signing, `--replay` file association) unlock when distribution matters.

---

## Dependency snapshot

```
Phase 1 (windowing) ──────────── independent, do first
Phase 2 (E-toolkit) ─► Phase 3 ─► Phase 4 ─► Phase 5 ─► Phase 6        (Sim/Viper track)
Phase 7 (S4) ─► Phase 8 (S5) ─► Phase 9 (S6–S7) ─► Phase 10 (S8–S10) ─► Phase 11 ─► Phase 12
   ▲ 3D track: independent of the sim track after Phase 2; interleave at will
S5.1 CAD nav, S3 items, doc 06 D-items, audio A1: [filler] — pull into any phase
```

## Working agreement (how these plans get executed)

- **Branch per item/phase, PR into `main`** (SF-Improvements pattern). You compile and run —
  the AI writes code (standing preference: don't run `build.bat` unless asked).
- **One work order per AI prompt**; fresh session if the model drifts. Doc 03 and W1/W5/W6-class
  items suit a lower tier; frame-loop, shader, and dynamics work deserve the stronger model or
  your review.
- **Re-verify before edit** — quoted code moves; find it by content, not line number.
- **Definition of done** lives in each doc's acceptance lines; a phase isn't done until its
  acceptance demo runs and (from Phase 3 on) is saved as a replayable recording or committed
  screenshot/demo app.
- **Plans stay honest:** when an item ships, mark it ✅ with the date in its plan doc and update
  this file's phase status; when a decision changes, strike it through and date it (Viper
  decision-record style) rather than silently rewriting history.
