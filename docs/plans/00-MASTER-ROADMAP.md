# Cosmic — Master Roadmap v4 (2026-07-04 · v4 update 2026-07-11)

> **Why this exists:** one place that says *what to do in what order*. Each workstream has its
> own plan document with PR-sized, acceptance-checked items; this file only sequences them into
> phases.
>
> **The v4 update (2026-07-11):** the user adopted the **editor vision** —
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md)
> (reference-editor screenshots → gap analysis) becomes **Phases 22–28** (docs 21–27), ending
> with the **Forge Isle** flagship showcase app. Fired unlocks: doc 18 **R8**, doc 19 **A2/A4**
> (A1 was already "now"), FEATURE-MATRIX's navmesh ✖ flips to Phase 26. Naming rule: adopted
> features take **Starforge** names — no borrowed branding from the reference editors. "The v3
> rule" (live docs = unimplemented work only) keeps its name and stays in force.
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
> | [`21-phase22-editor-shell-plan.md`](21-phase22-editor-shell-plan.md) | Phase 22: editor shell/viewport/branding — drop-a-PNG icon + top-bar logo, product toolbar, layout presets, per-op snapping, fly/possess camera, nav cube, infinite grid, universal gizmo, selection outline, viewport drag-drop (K1–K13) |
> | [`22-phase23-asset-workflows-plan.md`](22-phase23-asset-workflows-plan.md) | Phase 23: reflection metadata v2, asset accounting, Content Browser v2 (tree/search/rename/preview), Inspector v2 (search/tooltips/slots/QoL), per-entity Active, console/profiler/jobs (T1–T18) |
> | [`23-phase24-animation-editors-plan.md`](23-phase24-animation-editors-plan.md) | Phase 24: asset-editor host, timeline widget, **Starforge Animation Editor**, joint sockets, material slots, Animator crossfade tier (M1–M6; runtime = doc 19 A2) |
> | [`24-phase25-graphs-story-plan.md`](24-phase25-graphs-story-plan.md) | Phase 25: reusable node canvas (post-U6), flow variables, **Starforge Story Graph** (`.cstory`), vignette pass, post-chain graph view (Q1–Q6) |
> | [`25-phase26-navigation-ai-plan.md`](25-phase26-navigation-ai-plan.md) | Phase 26: Recast/Detour navmesh (Jolt vendoring pattern), scene bake + `.cnav`, agents/crowd + script `Nav()`, editor authoring/debug draw (N1–N5) |
> | [`26-phase27-world-2d-plan.md`](26-phase27-world-2d-plan.md) | Phase 27: physical sky, environment polish, particle curl noise + preview, 2D lights, world-anchored UI, render-to-texture verb (X1–X7) |
> | [`27-phase28-flagship-sample-plan.md`](27-phase28-flagship-sample-plan.md) | Phase 28: **Forge Isle** — the flagship showcase app + trailer/clean-machine acceptance (Z1–Z7) |
> | [`FEATURE-MATRIX.md`](FEATURE-MATRIX.md) | **Living index**: every missing/parked feature → phase home → unlock → size; plus the user acceptance ledger |
> | [`archive/`](archive/) | Completed plans (docs 01–11) kept as records, each with carried-forward pointers |
> | [`../design/modularity-audit.md`](../design/modularity-audit.md) | 2026-07-04 seam-by-seam swappability audit + the "how to swap X" cookbook |
> | [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) | The 2026-07-11 editor-vision **spec of record** — every Phase 22–28 work order cites its § |
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

## Phase order (v4)

**Phase 14 first** — it repairs what users touch every minute (camera, lights, environment,
chrome). Then 15 and 16 are independent of each other (interleave freely); 17 wants 14 done;
18 runs last of the *foundation* feature phases; 19–21 are unlock-driven menus, not marches.
Docs (12) run continuously; every phase ends with its D40 documentation row.

**The v4 march (2026-07-11):** with 14–18 code-complete, the order of work is now —
**finish Phase 17's remainder** (U1-click/U3/U4/U6/U7/U8 — three later phases and the flagship
sit on it) and run **Phase 22** in parallel (independent, all editor feel); then **doc 19
A1 → A4 → A2** (the fired Phase 20 anchors) feeding **Phase 23 → 24 → 25**; **Phase 26** is
parallel-safe any time after 15; **Phase 27** after 17; **Phase 28 (Forge Isle) last**, with
its Z1 design/greybox starting as soon as 17's remainder lands. Phases 19/21 stay menus (R8
fired and rides Phase 22).

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

### Phase 17 — UI, screen flow, 2D *(doc 16, U1–U8)* — ✅ code-complete 2026-07-11
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
  `CosmicTests` **241/241** (+22).
- **Status 2026-07-11 (UNcommitted):** the remainder landed — **U1** editor click-consumption
  (+ `UiSystem::HitTest` UI select), **U3** full 2D mode (engine `Camera2DController`, pixel
  grid, sprite picking/framing, generic `Scene::OnRenderSprites` sprite+tilemap pass in editor
  AND player, `pixel_art` point-sampling preset + template entry), **U4** Tilemap (int-array
  Cells serialization, camera-rect-culled draw, Tile Palette painter, stroke-coalesced undo),
  **U6** Flow Graph panel (vendored `imgui-node-editor`, reusable flow-free `widgets/NodeCanvas`
  for Phase 25, validation badges, layout persisted in `.cflow`), **U7** game view
  (primary-camera Play + eject + letterboxed aspect presets + `Window::SetCursorCaptured`
  cursor capture + flow-driven editor Play closing U5's remainder), **U8** staged samples
  (zero-code **FlowDemo**, **ForgePong**; homescreen-built; template scripts compile-smoked).
  Build green Debug+Release zero warnings, `CosmicTests` **272/272**, GL-conformance clean,
  compat gate held. Remaining = the user's recorded acceptance
  ([`../design/ui-flow-2d-acceptance.md`](../design/ui-flow-2d-acceptance.md), ledger row).

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
- **2026-07-11:** **R8's unlock fired** (editor vision) — run R8 with/before Phase 22 K6,
  which places the view-mode selector on the viewport strip. R11 pairs naturally with
  Phase 27 X1 if scheduled together.

### Phase 20 — Asset pipeline & animation *(doc 19, A1–A9)* — A1/A2/A4 now, rest unlock-driven
**A1 (assimp ON) is the anchor — schedule it with Phase 14–16 era work.** Then: skeletal
animation · STEP tool · material-undo/preview-rig/thumbnails · in-place reload · terrain
brushes · prefab overrides v2 · CSG · asset pak.
- **2026-07-11:** **A2 and A4 unlocks fired** (editor vision + the Forge Isle character
  project). A4 expands to the shared **PreviewRig** service (gap analysis §14.3 — interactive
  mode consumed by Phases 23/24). Recommended order: A1 → A4 → A2, feeding Phases 23–24.

### Phase 21 — Scripting & connectivity *(doc 20, C1–C6)* — unlock-driven
UDP sockets · positional audio · Lua L1–L3 (unlock: reload latency hurts a real tuning loop
or modding need — user decision 2026-07-04 keeps C++-first) · sequencer/cinematics.
- **2026-07-11 note:** C2 (positional audio) is a natural Forge Isle polish item and C6
  (sequencer) reuses Phase 24's Timeline widget — both stay unlock-driven, but expect the
  flagship to fire them.

### Phase 22 — Editor shell, viewport & branding *(doc 21, K1–K13)* — ✅ code-complete 2026-07-11
Drop-a-PNG branding (window/taskbar icon + top-bar logo, hot-swap, no code — user request) →
product toolbar w/ centered transport → layout presets → undo UI → status bar → viewport
header strip + per-op snapping → fly/possess camera rig → axis navigator → stats chips →
infinite grid → universal gizmo → selection outline → viewport drag-drop.
- **AI tier:** K12 stronger model (post-pass + state-restore); K1/K7 medium; the rest low/medium.
- **Done when:** every K-item's acceptance demonstrated; shipped apps byte-identical (compat
  gate); the K1 icon swap works live in a recorded clip.
- **Status 2026-07-11:** doc 18 **R8** (prerequisite: `SetPolygonMode` verb +
  Lit/Unlit/Wireframe/Entity-ID view modes) plus **all 13 work orders (K1–K13) landed**
  (UNcommitted). Engine gained only generic default-off/call-only verbs (`Window::SetIcon`
  16/32/48/256 via ImageIO + `utils/Branding` resolution order, `SceneRendererSettings::
  Wireframe`/`Outline*` + `SceneRenderDesc::SelectedEntities` + the filtered `ScenePicker`
  id pass, `Renderer3D::DrawInfiniteGrid`, `Gizmo::Operation::Universal`,
  `WorkspaceLayer::SetBottomInsetPixels`, `CommandStack::UndoNameAt/RedoNameAt`,
  `Bindings::TexUnitOutlineMask=13`); Starforge gained the product toolbar (measured centered
  icon transport + undo badges/history), layout presets (`LayoutPresets`, per-project
  persistence), the status bar, the viewport instrument (header strip w/ per-op snapping,
  `EditorCameraRig` orbit/fly/possess, NavigationCube adoption, stats chips, infinite grid),
  the post-composite selection outline, viewport drag-drop (single-undo spawns/assigns), and
  the drop-a-file branding pipeline with FileWatcher hot-swap + a default molten-orange
  `branding/icon.png` staged next to the exe. Build green Debug+Release zero warnings,
  `CosmicTests` **276/276**, GL-conformance clean, compat gate held. Remaining = the user's
  on-GPU acceptance (incl. the recorded K1 live icon-swap clip) + commit.

### Phase 23 — Asset workflows, Inspector & Hierarchy v2 *(doc 22, T1–T18)* — after/with 22
Reflection metadata v2 → asset accounting → file-drop events → Content Browser v2 (two-pane/
search/rename+retarget/preview+audio/import) → Inspector v2 (search/tooltips/asset slots/
component QoL/live-Play) → per-entity Active semantics + Hierarchy icons → Console v2 +
profiler port + jobs/resources. **Schedule doc 19 A4 (PreviewRig) before/with T7/T11.**
- **AI tier:** T1/T12/T13 stronger model (reflection ABI + engine gate semantics); rest medium.
- **Done when:** T-acceptances pass; old scenes/projects load byte-identical; a full authoring
  session (import → browse → assign → tune → Play) never needs a hand-typed path.

### Phase 24 — Animation editors & multi-material meshes *(doc 23, M1–M6)* — after A1/A2/A4
Asset-editor host (document tabs) → reusable Timeline widget → **Starforge Animation Editor**
(skeleton tree, bone-overlay preview, clip scrub) → joint sockets → material slots →
Animator crossfade tier. Runtime stays doc 19 A2; full controller graphs stay parked.
- **AI tier:** M4/M5 stronger model (transform composition; submesh render/serialize compat);
  M1/M3 medium.
- **Done when:** a rigged character imports, previews, sockets a prop, and crossfades in a
  recorded editor session; non-skinned scenes byte-identical throughout.

### Phase 25 — Node graphs, flow variables & story *(doc 24, Q1–Q6)* — after U6 + M1
Reusable node canvas → flow variables (typed blackboard) → **Starforge Story Graph**
(`.cstory` runtime + editor w/ Play preview) → vignette pass → post-chain graph view (view of
the fixed chain — an arbitrary pass-graph executor is explicitly out of scope).
- **AI tier:** Q2/Q3 stronger model (runtime semantics, versioned serialization); rest medium.
- **Done when:** a branching, variable-gated dialogue authors entirely in-editor and runs
  zero-code via the stock binding; post edits through the graph view match panel edits.

### Phase 26 — Navigation & AI *(doc 25, N1–N5)* — any time after 15; before Z4
Vendor Recast/Detour (Jolt pattern) → `NavMeshComponent` + collision-sourced bake + `.cnav` →
editor authoring/debug draw + Regenerate → `NavAgentComponent` + crowd + script `Nav()` →
patrol/chase sample. Flips FEATURE-MATRIX's former ✖ navmesh verdict.
- **AI tier:** N1/N2/N4 stronger model (vendoring, bake correctness, tick-order/determinism);
  N3/N5 medium.
- **Done when:** N5's recorded bake→Play→package demo; headless path/determinism tests green.

### Phase 27 — World rendering & 2D game parity *(doc 26, X1–X7)* — after 17's U3/U4 for 2D
Physical-atmosphere sky → environment polish (sun-angle widget, ambient, gamma, sun size,
settings nav) → particle curl noise + live preview + bounds → 2D lights → world-anchored UI →
render-to-texture verb.
- **AI tier:** X1/X5 stronger model (scattering + IBL parity; 2D composite path); rest medium.
- **Done when:** each item's toggle demo recorded; default-off items leave every existing
  scene byte-identical (conformance + screenshot compare).

### Phase 28 — Flagship showcase: **Forge Isle** *(doc 27, Z1–Z7)* — LAST; Z1 may start early
Design/greybox → playable character → living world (day/night, water, particles, voxel dig
site) → creatures & navmesh AI → story graphs + flow/UI/HUD/minimap → the 2D tent-game
vignette (absorbs doc 16 U8's 2D-sample duty) → branding/package/trailer + clean-machine
acceptance. **App-side only — engine gaps found here get filed in their owning phase doc.**
- **AI tier:** Z1/Z5 medium; content items medium; Z7 low.
- **Done when:** doc 27 Z7's DoD — clean-machine install runs start-to-finale ≥60 fps/1080p
  with its own branding, and the Z1 feature→moment matrix is demonstrated on a recording.

### Continuous — documentation *(doc 12, D5–D40)*
Coverage checker → reference chapters (parallel) → README expansion → system explainers →
**Starforge manual (D37–D39)** → link sweep → per-phase hooks (D40 standing rule). Docs-only
sessions; run any time.

---

## Acceptance ledger (user-run items — not phases, never silently dropped)

| Item | Origin |
| --- | --- |
| Phase 17 recorded acceptance (script: `docs/design/ui-flow-2d-acceptance.md`): zero-code FlowDemo authored in the Flow Graph panel → Play navigates → packaged exe navigates; ForgePong match + package; U1/U3/U4/U7 on-GPU spot checks | doc 16 U8 |
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

## Decision log — 2026-07-11 (all user-approved; the v4 update)

9. **Editor vision adopted**: `docs/design/example-images-gap-analysis.md` (the ExampleImages
   reference editors) becomes Phases 22–28 (docs 21–27); the gap analysis is the spec of
   record each work order cites. Fired unlocks: doc 18 R8, doc 19 A2/A4; FEATURE-MATRIX's
   navmesh ✖ verdict flips to Phase 26.
10. **Starforge naming**: features adapted from the reference screenshots take Starforge names
    ("Starforge Animation Editor", "Starforge Story Graph") — no borrowed branding ("Ignite",
    "StoryFlow") anywhere in code, UI, or docs.
11. **Drop-a-file branding** (Phase 22 K1): window/taskbar icon + editor top-bar logo resolve
    from a conventional `branding/icon.png` (+ `user://` and project-manifest overrides) and
    hot-swap at runtime — replacing the image file updates everything with zero code edits.
    Packager keeps embedding the exe icon for Explorer/pinned shortcuts.
12. **Flagship capstone**: the roadmap ends with **Forge Isle** (doc 27) — a packaged,
    clean-machine-accepted showcase whose Z1 feature→moment matrix must demonstrate every
    Phase 14–27 capability; it absorbs doc 16 U8's 2D-sample duty (the tent-game vignette).
13. **Post-FX stays a fixed chain**: Phase 25 Q6 ships a graph *view* of the verified
    pipeline; an arbitrary pass-graph executor is explicitly out of scope (revisit only with
    a real compositing need).

## Dependency snapshot

```
Phases 14–18: ✅ code-complete foundation (14 hardening, 15 physics, 16 platform,
              17 UI/flow/2D ✅ 2026-07-11, 18 voxel)

v4 march (2026-07-11):
Phase 17 remainder ✅ (U1c/U3/U4/U6/U7/U8 landed 2026-07-11) ──► Phases 25, 27(2D), 28 unblocked
Phase 22 ✅ code-complete 2026-07-11 (R8 + K1–K13; user acceptance + commit pending)
doc 19: A1 (assimp) ──► A4 (PreviewRig) ──► A2 (skeletal runtime)
Phase 23 (workflows; wants A4) ──► Phase 24 (anim editors; wants A1/A2/A4)
Phase 24 M1 (doc host) + doc 16 U6 ──► Phase 25 (graphs/story)
Phase 26 (nav; wants 15) ──► parallel-safe any time; before Z4
Phase 27 (world/2D; wants U3/U4 for 2D items) ──► before Z3/Z6
Everything above ──► Phase 28 (Forge Isle; Z1 greybox may start after 17)
Phases 19/21: unlock-driven menus      docs (12): continuous, parallel-safe
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
