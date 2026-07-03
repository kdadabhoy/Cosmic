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

### Phase 1 — Windowing correctness *(doc 09, W1–W6)* — ✅ code complete 2026-07-02
Fix the daily irritations: fullscreen black-flash, snip-overlay glitch, frozen render during
drag/resize, window-state edge cases.
> **Status:** W1–W6 implemented and building. W2/W4/W5 log-verified (paint-through-transition,
> modal frame pump, maximize round-trip). W3 compat-mode mechanism ships (default unchanged); the
> W3 *decision* + the interactive repro matrix (snip overlay, 125% laptop, slow-mo capture) remain
> a **user manual pass** — template + findings table in doc 09 §3.5.
- **Do:** W1 (instrument + repro matrix) → W2 (paint-through-transition) → W3 (DWM experiment,
  gated on W1 evidence) → W4 (responsive rendering + pause, per the accepted design doc) → W5
  (state hardening, parallel-safe) → W6 (docs).
- **AI tier:** W1/W5/W6 low; W2/W4 touch the frame loop — stronger model + your review.
- **Done when:** doc 09's acceptance checks pass at 100% and 125% scaling; snip over fullscreen
  captures cleanly; dragging the window keeps painting; README §24 + engineering note updated.

### Phase 2 — Sim math & config toolkit *(doc 03: E10–E15, E7)* — ✅ 2026-07-02
Small, header-heavy, unit-tested engine verbs that unblock everything sim-shaped.
> **Status:** all seven shipped on `main` with green tests (`CosmicTests`: 55 cases / 103,870
> assertions). Template project demonstrates config load + gamepad axes (§ doc 03 status table).
- **Do (order in doc 03):** E10 TOML config → E11 integrators → E13 lookup tables → E12 filters →
  E15 RNG → E14 noise → E7 gamepad. All parallel-safe on separate branches; all land with doctest
  coverage.
- **AI tier:** low/medium — ideal small-model tasks.
- **Done when:** all seven merged with green tests; template project demonstrates config load +
  gamepad axes.

### Phase 3 — ViperSim skeleton + dynamics decision *(doc 04: P0–P1)* — ✅ 2026-07-02
- **Do:** P0 (project skeleton, SimHub, `viper.toml`, telemetry schema) → P1 (JSBSim spike behind
  `IDynamics`, **1-week timebox**, drop-test demo; record the JSBSim-vs-hand-rolled outcome in the
  Viper decision log and in doc 04).
- **Done when:** drop test replayable in ReplayScreen; dynamics decision CLOSED with rationale.
> **Status:** `Projects/ViperSim` ships. P0 skeleton (SimHub, `viper.toml` via E10, telemetry
> schema, homescreen → Flight/Replay + phase stubs) + P1 (`IDynamics` + `ComposableDynamics` drop
> test on E11 RK4, DataRecorder→replay, drop verified numerically). Dynamics decision
> **provisional-closed** → ship ComposableDynamics, keep JSBSim behind `IDynamics`
> (`Projects/ViperSim/docs/DYNAMICS_DECISION.md`). The formal JSBSim build spike itself is the one
> outstanding user item; the interface makes running it later cost-free.

### Phase 4 — Tuned hover + energy truth *(doc 04: P2–P3; doc 08: A1–A2)* — ✅ code complete 2026-07-02
> **Status:** A1/A2 shipped (miniaudio engine subsystem + template demo + headless-safe tests).
> `viper-fc` ships at `Projects/ViperSim/viper-fc/` (portable header-only lib + `ViperFcTests` doctest suite);
> P2 (SimHal/SITL backend, attitude loop, TuningScreen with live gains, step commands +
> replay-through-FC) and P3 (position hold, sensor noise models, complementary estimator,
> battery/energy accounting, E7 gamepad flying, EnergyScreen, alert tones) implemented. Gate
> **G1** is scripted in-app ("Run G1": noise + alternating 5 m/s gusts, auto PASS/FAIL incl. the
> 230 W ±15% check, auto-flushed recording) — the **user gate run + committed recording remain**.
- **Do:** P2 (`viper-fc` + SimHal + attitude loop + TuningScreen) → P3 (position hold, sensor
  noise, battery + energy accounting, gamepad flying, EnergyScreen) with audio alert plumbing
  (A1 core playback, A2 loop/alert groups) landing alongside P3's failsafe tones.
- **Milestone with real-world value:** the Energy screen answers the proposal's power questions
  (hover ≈230 W vs cruise ≈106 W, endurance splits) **before parts are purchased**.
- **Done when:** Gate **G1** — hover stable against noise + 5 m/s gusts; recorded regression
  replay committed.

### Phase 5 — The hard flying: transition & orbit *(doc 04: P4–P5; doc 05: S3)* — ✅ code complete 2026-07-02
> **Status:** P4 (full-envelope aero with prop-wash elevon authority, TECS-lite cruise, transition
> state machine both directions + TransitionScreen visualizer) and P5 (orbit-on-ROI with
> ground-course wind rejection, full failsafe supervisor + fault-injection buttons + session
> checklist, FPV inset = S3.1, trail ribbon = S3.2) implemented. Gates **G2/G3** are scripted
> in-app with PASS/FAIL reports + auto-flushed recordings; test cards in
> `Projects/ViperSim/docs/test-cards/`. The **user gate runs (incl. CG/airspeed envelope sweeps
> for G2) + committed recordings remain.**
- **Do:** P4 (full-envelope aero + transition state machine, both directions) and P5 (orbit-on-ROI
  + full failsafe set + fault injection), pulling S3 viewport items (FPV inset, ribbon, horizon,
  labels) as P4/P5 need them.
- This is the project's core research risk — budget for iteration; every attempt recorded/replayable.
- **Done when:** Gates **G2** and **G3** pass (doc 04 §4) — the sim-side contract that clears real
  flight testing to proceed per the Viper playbook.

### Phase 6 — Hardware in the loop + rig *(doc 04: P6–P7; optional E4+P8)* — ✅ software complete 2026-07-02
> **Status:** P6 software shipped — `HilBackend` (E5 COBS+CRC over the new `SerialLink::Write`,
> sim-clock-stamped SensorPackets, actuator echo → latency on screen, SITL↔HIL dropdown) +
> `viper-fc/firmware/` PlatformIO project for the Teensy 4.1 running the identical FC. P7
> software shipped — `RigOutput` (rate-clamped `RIG,r,p,y` ASCII @ 50 Hz over its own link).
> **Physical acceptance needs hardware:** flash the Teensy, fly the P4 transition over HIL;
> wire the gimbal rig and verify servo directions. E4 UDP + P8 MAVLink remain optional/unstarted.
- **Do:** P6 (Teensy HIL over E5 framing; latency on screen) → P7 (gimbal rig). Optionally E4 UDP
  + P8 (MAVLink → QGroundControl).
- **Done when:** the P4 transition flies on the physical Teensy; rig mirrors sim attitude.

### Phase 7 — 3D engine foundations *(doc 05: S4.0–S4.7)* — ✅ code complete 2026-07-02
Unified camera hierarchy → material-driven meshes → 3D scene components → asset cache + glTF →
lighting v1 → MRT framebuffers → compute/SSBO. Strictly ordered inside; each a PR.
> **Status:** S4.0–S4.7 all implemented on branch `phase-7-3d-foundations`. Full VS-cmake build
> green (engine + every project DLL + tests), `CosmicTests` 66/66 (103,899 assertions). New engine
> surface: `camera/Camera` base, material `DrawMesh`, `MeshRendererComponent`/light components +
> `Scene::OnRender3D`, `AssetLibrary` cache, cgltf `Model`/`DrawModel` (+ committed `Duck.glb`),
> `UniformBuffer` lights UBO + `MeshLit.glsl`, MRT `FrameBuffer` + entity-ID `ReadPixel`,
> `StorageBuffer` + compute verbs. Every item has an Engine3DDemo toggle. **Remaining — user
> visual/perf pass:** run Engine3DDemo, exercise each S4.x toggle (material pad, ECS scene, glTF
> Duck, lighting, picking, 1M-point compute ≥ 60 fps), confirm the 2D overlay still renders.
> **2026-07-02 (later, same branch):** post-review hardening pass applied — standards/Vulkan
> portability review passed; behavior-neutral fixes landed (`GpuMemoryBarrier` rename,
> `SetCullMode` verb, `BindingPoints.h` registry, `Texture::SetSampling` + Font.cpp de-GL,
> glTF winding/scene fixes, light-cap warning, depth-attachment sampling params). Details:
> doc 05 §3 "S4 post-review hardening" note.
> **2026-07-02:** doc 05 §3 rewritten into explicit, code-verified work orders (exact files,
> signatures, GL facts, step lists, gotchas, per-item acceptance procedures) so each item can be
> handed to a lower-tier model in one session. Two structural changes: new **S4.0** — the vendored
> GLAD loader is GL 3.3-era and must be regenerated for 4.5 core (hard-gates only S4.7; S4.5 UBOs
> and S4.6 MRT work on the current loader) — and **S4.4 split into a/b** (asset cache, then
> cgltf/Model import).
> **S4.0 ✅ 2026-07-02:** loader regenerated (glad 0.1.36, GL 4.5 core, no extensions), all 59
> engine-used GL functions audited present, GL-version startup log added; full build + `CosmicTests`
> 58/58 green. User visual pass (apps render identically, log shows ≥ 4.5) pending.
- **Do:** ~~S4.0 (GLAD regen, lowest-risk PR — do first)~~ ✅ → S4.1 camera base → S4.2 material
  `DrawMesh` → S4.3 scene 3D components (ABI break, `build_all`) → S4.4a asset cache → S4.4b
  glTF/`Model` → S4.5 lights + UBO → S4.6 MRT + entity-ID readback → S4.7 compute + SSBO.
- **AI tier:** low/medium with doc 05 §3's work orders — S4.0/S4.1/S4.3/S4.4a are mechanical;
  S4.6/S4.7 touch raw GL state (follow their gotcha lists; worth your review on those two PRs).
- **Done when:** ECS scene renders lit glTF meshes; entity-ID readback works; compute demo hits
  60 fps at 1M points — each item's acceptance demo shown in Engine3DDemo with the 2D overlay
  intact and `CosmicTests` green.

### Phase 8 — CAD navigation, gizmos, picking *(doc 05: S5)* — ✅ code complete 2026-07-02
SolidWorks-style navigation (S5.1 — **[filler]: only needs S1, safe to pull into any earlier
phase**), frame/snap views, ViewCube, ImGuizmo transforms, ID-buffer picking + selection outline.
> **Status:** S5.1–S5.5 all implemented on branch `phase-7-3d-foundations`. Full VS-cmake build
> green (engine + new ImGuizmo lib + every project DLL + tests); `CosmicTests` **73/73** (103,934
> assertions) incl. new `tests/test_s5_navigation.cpp`. New engine surface: `NavStyle`/`ViewPreset`
> CAD bindings + zoom-to-cursor + orbit-about-cursor + snap/frame on `OrbitCameraController`,
> `camera/NavigationCube`, `scene/ScenePicker` + `FrameBuffer::ReadDepth` + `Mesh` local AABB,
> `graphics/Gizmo` (vendored ImGuizmo). All exercised in Engine3DDemo's "CAD Tools (S5)" panel +
> F/Home/W-E-R hotkeys. Selection outline ships as an oriented wire-AABB (documented deviation from
> the S5.4 ID-edge-detect post pass — that needs the S6.1 post stack; picking is ID-buffer based).
> **Remaining — user visual/DPI pass** (doc 05 §4 note).
- **Done when:** Engine3DDemo manipulates entities with gizmos; MMB-orbit-about-cursor-point feels
  like SolidWorks at both DPIs.

### Phase 9 — Visual realism core *(doc 05: S6–S7)* — ✅ code complete 2026-07-03
HDR pipeline → PBR + IBL → shadows → SSAO → bloom → AA, then sky/atmosphere/fog/time-of-day.
> **Status (2026-07-03):** **S6.1–S6.7 + S7.1–S7.3 all code-complete** on branch
> `phase-7-3d-foundations`. Full VS-cmake Debug build green (engine + every project DLL + CosmicApp) and
> `CosmicTests` **73/73** (103,934 assertions); a smoke-run of `CosmicApp --project Engine3DDemo` booted
> on OpenGL 4.5 with **zero** shader-compile / framebuffer-incomplete / error logs (all 13 new shaders +
> the IBL bake + all post/shadow FBOs healthy; Duck.glb now imports through the PBR-material path). New
> engine surface (details in doc 05 §5 banner): S6.2 tangents + PBR texture maps + glTF material import +
> `Texture2D` decode-from-memory; S6.3 `TextureCube` + `EnvironmentMap` (procedural-sky IBL bake +
> skybox) + `Renderer3D::SetIBL`; S6.4 `ShadowMap` (2k directional map + PCF) + `Renderer3D::SetShadow`;
> S6.5/6.6/6.7 SSAO + bloom + FXAA in `PostProcessStack`; S7.2 height fog (tonemap) + S7.1/S7.3 the
> analytic sky is the env source, driven by a time-of-day sun scrub. Every item has an Engine3DDemo
> toggle. **Documented tier deviations:** single shadow map (CSM next), whole-image SSAO composite
> (ambient-only needs a depth prepass), Gaussian bloom (CoD-progressive is a quality follow-up), skybox
> drawn background-first (depth-func LEQUAL verb pending). **Remaining — user visual pass** of the toggles
> + a committed screenshot, and a DamagedHelmet-class glb to close S6.2's "matches a reference viewer".
> --- history ---
> Scoped **foundation-first**. **S6.1 (HDR pipeline) ✅ code-complete 2026-07-02** on
> branch `phase-7-3d-foundations`: `renderer/PostProcessStack` (HDR `{RGBA16F, DEPTH24STENCIL8}`
> scene target + fullscreen-triangle passes), `assets/shaders/Tonemap.glsl` (ACES + exposure +
> gamma), new `RenderCommand::BindTextureSlot` verb (sample FBO attachments, §0 rule 1), wired into
> Engine3DDemo (whole 3D world → HDR target → tonemap → viewport, 2D overlay composites after) with
> an HDR toggle + exposure slider. Build + `CosmicTests` 73/73 green. Camera-UBO migration
> **deferred to S6.2** (rides its shader rewrite — rationale in doc 05 §5). doc 05 §5 was expanded
> into explicit per-item work orders (S6.2–S6.7) so each is one session; §6 (S7) stays at summary
> altitude until S6 lands.
> **S6.2 🚧 core code-complete 2026-07-03** (same branch): the **camera UBO** (binding 1) fully
> migrated — `renderer/CameraUniforms.h` + `Renderer3D::BeginScene` upload; all 5 engine 3D shaders
> read an instance-named `CameraBlock` (`u_Camera.ViewProjection`) and every loose
> `u_ViewProjection`/`u_CameraPos` setter is gone. **PBR core** — `assets/shaders/PBR.glsl`
> (Cook-Torrance GGX/Smith/Schlick, metallic-roughness factors, lights UBO + camera UBO, HDR-linear
> output) + an Engine3DDemo "PBR sphere grid" toggle (roughness × metallic), verified visually.
> **Deferred to an S6.2 follow-up:** tangents + PBR textures (normal/albedo/MR/AO/emissive maps) +
> glTF factor/texture import + a DamagedHelmet sample (the full "matches a reference viewer" line).
> Build + `CosmicTests` 73/73 green. **Remaining:** user visual pass (S6.1 HDR toggle + S6.2 PBR grid).
- **Do (order):** ~~S6.1 HDR + post stack~~ ✅ → ~~S6.2 PBR + camera UBO~~ ✅ → ~~S6.2 texture
  follow-up~~ ✅ → ~~S6.3 IBL + skybox~~ ✅ → ~~S6.4 shadows~~ ✅ → ~~S6.5 SSAO~~ ✅ → ~~S6.6 bloom~~ ✅
  → ~~S6.7 AA~~ ✅ → ~~S7.1–S7.3 sky/fog/time-of-day~~ ✅ *(all code-complete 2026-07-03; CSM,
  ambient-only SSAO, progressive bloom are documented tier follow-ups)*.
- **AI tier:** S6.1/S6.6/S6.7 medium; S6.2/S6.3/S6.4 touch raw GL state + math (stronger model or
  your review). Every item has a doc 05 §5 work order + an Engine3DDemo acceptance toggle.
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
- **Non-interactive build recipe (when an AI *is* asked to build):** do **not** invoke
  `build_all.bat`/`build.bat` — they end in `pause` and hang, and `cmd /c "...bat < NUL"` fails
  with "not recognized". Instead drive the VS-bundled `cmake.exe` directly (PowerShell):
  ```powershell
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  $cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
  & $cmake -S C:\dev\Cosmic -B C:\dev\Cosmic\build -A x64 -DCOSMIC_BUILD_ENGINE_ONLY=OFF   # configure (re-globs new files)
  & $cmake --build C:\dev\Cosmic\build --config Debug --parallel                          # engine + all projects + tests
  ```
  Outputs in `build\Runtime\Debug\`; tests: `build\Runtime\Debug\CosmicTests.exe`. Configure
  after adding/removing source files (GLOB has no CONFIGURE_DEPENDS). Don't pipe native-exe
  stderr through `2>&1`/`*>&1` in PowerShell 5.1 (wraps errors, flips the exit code); use
  `Tee-Object` + `$LASTEXITCODE`.
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
