# Cosmic — Master Roadmap v3 (2026-07-04)

> **Why this exists:** one place that says *what to do in what order*. Each workstream has its
> own plan document with PR-sized, acceptance-checked items; this file only sequences them into
> phases.
>
> **How to execute a phase with an AI:** open a session on the repo and say
> *"Read `docs/plans/00-MASTER-ROADMAP.md` and the plan doc for Phase N, then implement item
> \<X\>."* One item per session. Every item's plan doc states its acceptance check — the item
> is not done until that check demonstrably passes. Before editing, the AI must re-verify
> quoted code still exists (references were true on their doc's creation date and drift).
>
> **The v3 rule (user decision 2026-07-04):** a live plan doc contains **only unimplemented
> work orders**. Completed/superseded docs live in [`archive/`](archive/) with carried-forward
> pointers; every future feature has exactly one phase home, cross-indexed in
> [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md). App-specific work (ViperSim gates) is out of
> roadmap scope — engine leftovers were carried into phases; user-run items live in the
> **acceptance ledger** (§ below).
>
> | Doc | Covers |
> | --- | --- |
> | [`12-documentation-plan.md`](12-documentation-plan.md) | Docs: README expansion + diagrams, API reference, system explainers, **Starforge manual**, per-phase doc hooks (D5–D40) |
> | [`13-phase14-starforge-hardening-plan.md`](13-phase14-starforge-hardening-plan.md) | Phase 14: all reported editor bugs, SceneRenderer everywhere, lights that work, HDRI, chrome, file dialogs, logging, ForgePlayground v2, SystemScripts (H1–H10) |
> | [`14-phase15-physics-plan.md`](14-phase15-physics-plan.md) | Phase 15: Jolt physics — bodies, colliders, queries, events, character controller, terrain collision, editor authoring (J1–J9) |
> | [`15-phase16-app-platform-plan.md`](15-phase16-app-platform-plan.md) | Phase 16: external projects, dedicated Starforge launcher + library homescreen, packaging v2 (icon/installer/zip), per-app user data (S1–S8) |
> | [`16-phase17-ui-flow-2d-plan.md`](16-phase17-ui-flow-2d-plan.md) | Phase 17: in-game UI entities, `.cflow` screen-flow node graph, 2D/pixel authoring, tilemaps, game view (U1–U8) |
> | [`17-phase18-voxel-plan.md`](17-phase18-voxel-plan.md) | Phase 18: voxel worlds — chunks, meshing, editing, collision, generation (V1–V7) |
> | [`18-phase19-rendering-quality-plan.md`](18-phase19-rendering-quality-plan.md) | Phase 19 (menu, unlock-driven): CSM, SSAO prepass, progressive bloom, froxels, FFT ocean, tessellation, particle indirect, BCn, decals, view modes (R1–R12) |
> | [`19-phase20-asset-animation-plan.md`](19-phase20-asset-animation-plan.md) | Phase 20: assimp ON, skeletal animation, STEP tool, material/preview UX debt, brushes, prefab overrides v2, CSG, asset pak (A1–A9) |
> | [`20-phase21-scripting-connectivity-plan.md`](20-phase21-scripting-connectivity-plan.md) | Phase 21: UDP sockets, positional audio, Lua tier L1–L3 (parked w/ unlock), sequencer (C1–C6) |
> | [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md) | **Living index**: every missing/parked feature → phase home → unlock → size; plus the user acceptance ledger |
> | [`archive/`](archive/) | Completed plans (docs 01–11) kept as records, each with carried-forward pointers |
> | [`../design/modularity-audit.md`](../design/modularity-audit.md) | 2026-07-04 seam-by-seam swappability audit + the "how to swap X" cookbook |
> | [`../installer-guide.md`](../installer-guide.md) | User-facing: build/ship/install a setup exe |

## The one design rule that spans everything

**The engine ships generic verbs; apps own domain logic.** UDP socket, RK4 template,
`DrawMesh`, terrain system, `user://` paths, FlowMachine → engine. Tailsitter mixers, MAVLink,
aero polars, the volcano scene, a game's rules → apps/projects. When an app needs something
the engine doesn't have, the engine grows a *general* verb, never a domain-shaped one. Every
plan doc applies this rule; for Starforge specifically the corollary is doc 11's rule 2 (now
doc 13 §0.2): the engine never gains editor-branded names.

## Shipped foundation (Phases 1–13 — all ✅ code-complete; records in `archive/`)

| Phases | What shipped | Record |
| --- | --- | --- |
| 1 | Windowing correctness (fullscreen/snip/DPI/responsive-render W1–W6) | `archive/09` |
| 2 | Sim math & config toolkit (TOML config, integrators, filters, LUTs, noise, RNG, gamepad) | `archive/03` |
| 3–6 | ViperSim + portable `viper-fc` (P0–P7 software; HIL/rig firmware) — app work now out of roadmap scope | `archive/04` |
| 7–9 | 3D foundations → CAD nav/gizmos/picking → PBR/IBL/shadows/SSAO/bloom/FXAA/sky/fog/ToD | `archive/05` §3–§6 |
| 10 | Terrain (quadtree LOD/splat/triplanar + exact CPU sampling), water v1 (Gerstner/reflection/refraction), GPU particles, god rays, heat haze | `archive/05` §7–§9 |
| 11 | Frontier world showcase + SceneRenderer, fly camera, GPU profiler, instancing/frustum, water v2, sky v2, snow, rain, audio ambience, 5 worlds | `archive/10` |
| 12 | Sorted render queue (cull/sort/auto-instance/LOD), sRGB/texture audit, GL-conformance CI, frame-lifecycle spec, **stay-on-OpenGL verdict** (reopen conditions in `archive/05` §12) | `archive/05` §10–§12 |
| 13 | **Starforge editor E1–E21**: reflection, JSON scenes+UUIDs, hierarchy, undo, panels, picking/gizmos, content browser, C++ script host + hot reload, play/pause/step, prefabs, primitives, OBJ import (+gated assimp), materials/environment, world-system authoring, packaging, telemetry panel, theme/polish/sample | `archive/11` |
| — | Audio A1/A2 (miniaudio one-shots, loops/groups) | `archive/08` |

Engine state at v3: `CosmicTests` 189/189, zero warnings across 5 projects, branch
`phase-7-3d-foundations` (user commits/merges).

---

## Phase order (v3)

**Phase 14 first** — it repairs what users touch every minute (camera, lights, environment,
chrome). Then 15 and 16 are independent of each other (interleave freely); 17 wants 14 done;
18 runs last of the feature phases; 19–21 are unlock-driven menus, not marches. Docs (12) run
continuously; every phase ends with its D40 documentation row.

### Phase 14 — Starforge hardening *(doc 13, H1–H10)* — ✅ code-complete 2026-07-04
Pose-based orbit (no MMB jump) → SceneRenderer as THE scene path (environment/sky/shadows/
post live in editor + shipped player) → lighting unification (scene lights affect default
materials; light billboards) → HDRI skies → chrome rebuild (toolbar visible, one menu bar,
panel ✕, viewport = scene name) → native file dialogs → logging (colors, `user://logs`,
Console sink) → ForgePlayground v2 + scene-camera adoption → SystemScript tier → consistency
sweep.
- **AI tier:** H2/H3 stronger model (render-path + shader semantics must be exact); the rest
  medium; H5/H7/H10 low.
- **Done when:** every §1 bug row in doc 13 has its repro-based acceptance demonstrated; the
  compat gate held throughout (shipped apps identical).
- **Status 2026-07-04:** all 10 work orders (H1–H10) landed; build green across 5 projects zero
  warnings; `CosmicTests` **195/195** (189→195: +3 pose-orbit, +3 SystemScript). Compat gate held
  (no SceneRenderer pass internals changed; `OnRender3D` byte-identical; shipped apps never opt
  into new WorkspaceLayer flags / HDRI). Remaining = the user's on-GPU acceptance pass (env-toggle/
  moving-shadow/lake-reflection visuals, HDRI orientation, chrome at 125% DPI, playground first
  frame) + a few small documented deferrals (light-billboard picking, Inspector per-system field
  section + FlockSystem sample project, content-browser fallback glyph, homescreen redesign→doc 15,
  Save-As/New-Project Browse buttons, `.exr`).

### Phase 15 — Physics & collision *(doc 14, J1–J9)* — ✅ code-complete 2026-07-04
Vendor Jolt → PhysicsWorld service → reflected components → Play-session integration →
script API + collision events → character controller → terrain collider → editor authoring/
debug draw → samples + determinism proof.
- **AI tier:** J1/J2/J4 stronger model (vendoring flags + tick-order contract); J3/J5–J8
  medium.
- **Done when:** doc 14 J9's recorded acceptance: authored ground/boxes/character scene
  simulates in editor + packaged exe; two headless runs bit-match.
- **Status 2026-07-04:** all 9 work orders (J1–J9) landed (UNcommitted). **Jolt v5.5.0**
  vendored PRIVATE-static into `Cosmic.dll`; `PhysicsWorld` pimpl (no JPH in public
  headers); reflected rigid-body/collider/character components; Scene play-session
  integration with the documented tick-order contract; script `Physics()`/`Character()`
  proxies + collision/trigger events; `CharacterVirtual` walker + `WalkController` sample;
  terrain `HeightFieldShape` (≤2 cm parity); editor collider gizmos + live debug draw;
  ForgePlayground physics ball + determinism proof. Build green 5 projects, **zero engine
  warnings**, `CosmicTests` **213/213**, GL-conformance clean, compat gate held (shipped
  apps create no `PhysicsWorld`). Remaining = the user's on-GPU acceptance demo + commit
  (see doc 14's STATUS banner).

### Phase 16 — App platform & shipping *(doc 15, S1–S8)* — ✅ code-complete 2026-07-05
External project folders (VFS mounts + registry) → `Starforge.exe` boot → product homescreen
library → Launcher back to dev tool → packaging v2 (Release orchestration, icon embed,
title/size, zip, Inno, signing hook, CI release job, `--replay` assoc) → per-app `user://`
isolation → run-standalone/thumbnails/About → clean-machine acceptance.
- **AI tier:** S1/S6 stronger model (VFS + data-location compat); S2–S5/S7 medium.
- **Done when:** doc 15 S8's clean-machine run passes for both a packaged app and packaged
  Starforge itself.
- **Status 2026-07-05:** all 8 work orders (S1–S8) landed (UNcommitted). Engine gained
  `FileSystem` PATH-mode mounts + per-app `SetAppIdentity`, `ExeResources` (icon embed),
  `ImageIO::WritePNG`, `FrameBuffer::ReadPixels`, `Window::SetSize/SetTitle`; PlayerLayer reads
  `[window]` keys. Editor gained the external-project model (registry v2 + migration,
  project-local builds + DLL search), the product homescreen library, `Packager` (Release
  orchestration + icon + zip + installer + sign hook), Project Settings, Run Standalone,
  thumbnails, About; Starforge self-packages. Launcher reframed as the dev/demo browser.
  Build green Debug+Release 5 projects zero warnings, `CosmicTests` **219/219**, GL-conformance
  clean, compat gate held. Remaining = the user's clean-machine acceptance (S8 ledger row) + commit.
  **2026-07-10:** S3 homescreen defect found + fixed + verified on-GPU — the project-library window
  rendered permanently behind the dockspace host (`NoBringToFrontOnFocus` back-of-stack semantics;
  the 07-08 "collapsed viewport" fix was secondary). Full root-cause + verification:
  [`../engineering-notes/starforge-homescreen-hidden.md`](../engineering-notes/starforge-homescreen-hidden.md).
  The scene Viewport panel now hides while no project is open (engine verb `SetViewportVisible`,
  already present, now used); Voxels panel got a first-open size + min-size floor. Same day,
  **desktop app identity** landed: dev boots now set the real Win32 window title (Starforge =
  "Starforge — Project / Scene *", Launcher = "Cosmic Launcher"; `Window::SetTitle` made
  idempotent), every boot sets a per-app `AppUserModelID` ("Cosmic.<App>") for taskbar
  grouping/pinning, and the dev tree builds a dedicated **`Starforge.exe`** (target
  `StarforgeEditor`, compiled-in default project — CLI/boot.cfg still override) with its own
  VERSIONINFO, completing decision #1's "dedicated Starforge.exe" for the dev loop, not just dist.

### Phase 17 — UI, screen flow, 2D *(doc 16, U1–U8)* — ◑ engine foundation code-complete 2026-07-08
UI entities (canvas/rect/button/text/image + hit-testing) → scene event bus → 2D authoring
mode (ortho, pixel-perfect, sorting) → sprite animation + tilemaps → FlowMachine + `.cflow` →
node-graph editor panel → game-view correctness (primary camera, aspect presets, eject) →
zero-code two-screen app + "ForgePong" sample.
- **AI tier:** U1/U5 stronger model (layout math + state-machine semantics headless-tested);
  rest medium.
- **Done when:** doc 16 U8: a two-screen app built with zero C++ ships and runs; the 2D
  sample plays packaged.
- **Status 2026-07-08 (UNcommitted):** engine foundation landed — **U1** UI components +
  UiSystem (pure layout/hit-test/button + Renderer2D draw, wired into PlayerLayer + Starforge
  viewport + Entity▸UI menu), **U2** ✅ EventBus + script signals, **U3/U4 engine slice**
  (SpriteRenderer SourceRect/PPU/ZOrder + SpriteAnimation flipbook + Entity▸2D menu), **U5** ✅
  FlowMachine + `.cflow` wired into PlayerLayer. Build green 5 projects zero warnings,
  `CosmicTests` **241/241** (+22). Remaining = editor panels + on-GPU/samples: U1 editor click
  consumption, U3 full 2D mode, U4 Tilemap+painter, **U6** node-graph panel (vendor
  imgui-node-editor), **U7** game-view, **U8** zero-code app + ForgePong + recorded acceptance.

### Phase 18 — Voxel worlds *(doc 17, V1–V7)* — ◑ engine + editor foundation code-complete 2026-07-08
Chunk store/palette/serialization → mesher (culled→greedy) → render integration → edit
tools + script API → Jolt collision → generation/streaming → "ForgeBlocks" sample.
- **AI tier:** V2/V3/V6 stronger model; most deferrable phase — reorder freely if priorities
  shift.
- **Done when:** doc 17 V7's recorded demo on a clean path.
- **Status 2026-07-08 (UNcommitted):** all 7 work orders landed. New `Cosmic/src/voxel/`
  (`VoxelVolume`/`BlockPalette`/`VoxelMesher`/`VoxelGenerator`/`VoxelRender`), reflected
  `VoxelVolumeComponent` + `Scene::SyncVoxelVolumes` (JobSystem meshing, streaming gen,
  S12-queue chunk draws), script `Voxels()` proxy, per-chunk static Jolt `MeshShape`
  collision in `ScenePhysics`, Starforge Voxels panel + undoable place/break viewport brush +
  World▸Voxel Volume, template `VoxelDigger` + `BuildForgeBlocks` sample (homescreen "Voxel
  Sample"). Build green Debug+Release 5 projects **zero warnings**, `CosmicTests` **262/262**
  (241→262), compat gate held (no shipped app attaches a VoxelVolume). Remaining = the user's
  on-GPU acceptance + recorded ForgeBlocks demo (V7 DoD) + documented follow-ups (per-vertex AO
  bake, streaming unload/disk paging = parked "infinite worlds", `.cflow` sample menu).

### Phase 19 — Rendering quality tier 2 *(doc 18, R1–R12)* — menu, unlock-driven
CSM · SSAO prepass · progressive bloom · froxels · FFT ocean · terrain tessellation/holes ·
particle indirect+sort · wireframe/ID view modes · BCn/KTX2 · decals · sky depth verb ·
world-system builder registry. Run an item when its unlock fires; each lands with an
Engine3DDemo toggle.

### Phase 20 — Asset pipeline & animation *(doc 19, A1–A9)* — A1 now, rest unlock-driven
**A1 (assimp ON) is the anchor — schedule it with Phase 14–16 era work.** Then: skeletal
animation · STEP tool · material-undo/preview-rig/thumbnails · in-place reload · terrain
brushes · prefab overrides v2 · CSG · asset pak.

### Phase 21 — Scripting & connectivity *(doc 20, C1–C6)* — unlock-driven
UDP sockets · positional audio · Lua L1–L3 (unlock: reload latency hurts a real tuning loop
or modding need — user decision 2026-07-04 keeps C++-first) · sequencer/cinematics.

### Continuous — documentation *(doc 12, D5–D40)*
Coverage checker → reference chapters (parallel) → README expansion → system explainers →
**Starforge manual (D37–D39)** → link sweep → per-phase hooks (D40 standing rule). Docs-only
sessions; run any time.

---

## Acceptance ledger (user-run items — not phases, never silently dropped)

| Item | Origin |
| --- | --- |
| Phase 16 clean-machine acceptance (script: `docs/design/app-platform-acceptance.md`): author an external project → package with icon/zip/installer → on a clean VM the app runs with its icon/title, isolates user data to LOCALAPPDATA, `--replay` works; `dist/Starforge` passes the same test | doc 15 S8 |
| Phase 15 on-GPU physics acceptance: author ground+boxes+character in-editor → Play (stack settles, character walks, telemetry records), package (E19) → shipped exe simulates identically; collider-gizmo + Physics-Debug visual pass | doc 14 J9 |
| Phase 13 recorded acceptance demo (script: `docs/design/starforge-acceptance-demo.md`) | archive/11 E21 |
| Phase 12 on-GPU perf pass (cull/instance/LOD evidence; 5 Frontier worlds ≥60 fps, CPU≪GPU) + screenshots | archive/05 §10 |
| Phases 7–10 visual passes wherever still unticked (Engine3DDemo toggles) | archive/05 §3–§9 |
| W3 DWM decision + interactive repro matrix (snip overlay, 125 % laptop) | archive/09 §3.5 |
| Viper gates G1–G3 + recordings; Teensy HIL flight; gimbal rig (app-side by decision) | archive/04 |
| Water look tuning (from-below surface, caustics/shafts) | `docs/design/water-rendering-notes.md` |

## Decision log — 2026-07-04 (all user-approved)

1. **Starforge separates**: dedicated `Starforge.exe` boot + product homescreen; projects are
   self-contained folders anywhere on disk; Cosmic Launcher = dev tool. *(doc 15)*
2. **Standalone-first shipping**: packaged apps are real products (Release, icon, installer,
   isolated user data); the Starforge↔Cosmic gap closes by design. *(doc 15)*
3. **Jolt now** — full physics phase rather than a minimal built-in layer. *(doc 14)*
4. **Game styles**: 2D/pixel AND voxel both in scope; 2D rides Phase 17, voxel is its own
   Phase 18.
5. **Class-of-entities logic = C++ SystemScript tier** (doc 13 H9); **Lua stays parked** with
   its unlock (doc 20 C3–C5).
6. **Screen flow = node-graph `.cflow` asset** over an engine FlowMachine + UI-entity events
   (doc 16), not a transition table and not code-only.
7. **Plan shape**: split per-phase docs (13–20) + living FEATURE-MATRIX; roadmap sequences.
8. **Archive discipline (the v3 rule)**: all completed/superseded docs archived (01–11);
   live docs carry only unimplemented work; ViperSim app work out of scope, its engine
   leftovers carried (UDP → doc 20).

## Dependency snapshot

```
Phase 14 (hardening) ──► do FIRST (everything visible rides it)
Phase 15 (physics)  ──┐  independent of each other; interleave freely
Phase 16 (platform) ──┘
Phase 14 ──► Phase 17 (UI/flow/2D)     Phase 15 ──► Phase 18 (voxel collision)
Phase 17 ──► Phase 18 (samples)        doc 19 A1 (assimp): anytime, early
Phases 19/20/21: unlock-driven menus   docs (12): continuous, parallel-safe
```

## Working agreement (how these plans get executed)

- **Branch per item/phase, PR into `main`.** You compile and run — the AI writes code
  (standing preference: don't run `build.bat` unless asked; a full build+test pass at the end
  of a work batch is expected).
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
- **One work order per AI prompt**; fresh session if the model drifts. Frame-loop, shader,
  physics-tick, and VFS/data-location work deserve the stronger model or your review.
- **Re-verify before edit** — quoted code moves; find it by content, not line number.
- **Definition of done** lives in each doc's acceptance lines; a phase isn't done until its
  acceptance demo runs and is saved as a recording or committed screenshot/demo app, plus its
  doc 12 D40 row.
- **Plans stay honest:** when an item ships, mark it ✅ with the date in its plan doc and
  update this file's phase status; when a decision changes, strike it through and date it
  rather than silently rewriting history; when a doc completes, archive it with a
  carried-forward note (the v3 rule).
- **Never run git write commands — the user commits.**
