# Feature Matrix — every missing/parked capability, its phase home, and its unlock

> **Created 2026-07-04. This is a LIVING document** — the single cross-reference the roadmap
> promises: *every* feature that is not shipped appears here exactly once, with where it will
> be implemented (or why it is deliberately unplanned). When a feature ships, flip its Status
> to ✅ + date and leave the row (history is cheap; hunting is not). When a new need appears,
> add a row BEFORE writing code, and give it a phase home or an explicit "unplanned" verdict.
>
> Sibling documents: [`00-MASTER-ROADMAP.md`](00-MASTER-ROADMAP.md) sequences the phases;
> each `docs/plans/1x-*.md` holds the work orders; `docs/design/modularity-audit.md` covers
> *architectural* swappability (how to replace a system rather than add one).
>
> Legend — Size: S (≤1 session) · M (1–2) · L (3–6) · XL (a phase). Status: ☐ planned ·
> ⏸ parked (has a phase home, waits for its unlock) · ✖ unplanned (needs a planning session
> before any code) · ✅ shipped · ◑ partially shipped (dated; the remainder is listed in the
> phase doc's status banner).

## Editor & tooling

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| Orbit camera without MMB jump (pose-based pivot) | look-at rig re-aims on press | 14 · doc 13 H1 | now (daily irritation) | M | ✅ 2026-07-04 |
| Environment/sky/shadows/post live in editor + player | `Scene::OnRender3D` ignores `EnvironmentComponent` | 14 · doc 13 H2 | now | L | ✅ 2026-07-04 |
| Scene lights affect default materials + light billboards | color path ignores lights UBO; lights invisible | 14 · doc 13 H3 | now | M | ✅ 2026-07-04 |
| HDRI skies | enum exists, no loader | 14 · doc 13 H4 | now | M | ✅ 2026-07-04 |
| Editor chrome: toolbar visible, one menu bar, panel ✕, viewport title | top dock clips toolbar; hard-coded names | 14 · doc 13 H5 | now | M | ✅ 2026-07-04 |
| Native file dialogs everywhere | hand-typed paths; 2 ad-hoc usages | 14 · doc 13 H6 | now | M | ✅ 2026-07-04 |
| Colored terminal logs; logs in `user://`; engine log in Console panel | pattern uncolored; logs land in content dirs | 14 · doc 13 H7 | now | S | ✅ 2026-07-04 |
| ForgePlayground that demos well + scene-camera adoption | content buried in terrain; camera spawns underground | 14 · doc 13 H8 | now | M | ✅ 2026-07-04 |
| SystemScript tier (logic over a *class* of entities) | per-entity scripts only | 14 · doc 13 H9 | now (user need #4) | M | ✅ 2026-07-04 |
| Editor consistency sweep (labels, glyphs, Add-Component filtering) | assorted rough edges | 14 · doc 13 H10 | now | S | ✅ 2026-07-04 |
| Material-edit undo + preview rig + browser thumbnails | live-but-not-undoable; no thumbnails | 20 · doc 19 A4 | **fired 2026-07-11** (editor vision; expands to the shared PreviewRig service, gap §14.3) | M/L | ✅ 2026-07-12 (PreviewRig interactive+batch; self-test proved byte-identical scene render) |
| In-place texture/asset hot reload into held Refs | cache-slot swap only | 20 · doc 19 A5 | live-tuning workflow | S | ⏸ |
| Terrain sculpt/splat brushes | recipe params only | 20 · doc 19 A6 | param terrain stops being enough | L | ⏸ |
| Prefab overrides v2 (field-level diff/propagation) | whole-instance apply/revert | 20 · doc 19 A7 | content-heavy project | M | ⏸ |
| Wireframe / entity-ID view modes | no fill-mode verb | 19 · doc 18 R8 | **fired 2026-07-11** (rides Phase 22 K6's viewport strip) | S | ✅ 2026-07-11 |
| Sequencer / cinematics (keyframes on reflected fields) | none | 21 · doc 20 C6 | trailer/cutscene need (reuses Phase 24 M2's Timeline widget; Forge Isle may fire it) | L | ⏸ |
| Drop-a-file branding: window/taskbar icon + top-bar logo, hot-swap | GLFW default icon in dev; exe-embed only at package time | 22 · doc 21 K1 | now (user request 2026-07-11) | M | ✅ 2026-07-11 |
| Editor chrome v2 (icon toolbar/centered transport, layout presets, undo UI, status bar) | text-button strip, one layout | 22 · doc 21 K2–K5 | now (editor vision) | M | ✅ 2026-07-11 |
| Viewport instrument (header strip, per-op snap, fly/possess camera, axis navigator, stats chips, infinite grid) | top-bar toolbar, one snap value, orbit-only | 22 · doc 21 K6–K10 | now | L | ✅ 2026-07-11 |
| Universal gizmo + selection outline + viewport drag-drop | single-op gizmo, wire-box highlight, Inspector-only drops | 22 · doc 21 K11–K13 | now | M | ✅ 2026-07-11 |
| Reflection metadata v2 (per-field docs/ranges/units → tooltips + bounded widgets) | name/kind/flags only | 23 · doc 22 T1/T10 | now | M | ☐ |
| Asset accounting/enumeration + JobSystem introspection panels | none | 23 · doc 22 T2/T18 | now | M | ☐ |
| Content Browser v2 (tree pane, history, search, rename+retarget, preview/metadata/audio, import, OS drops) | single-pane grid, texture thumbs only | 23 · doc 22 T3–T8 | now | L | ☐ |
| Inspector v2 (property search, asset-slot widget, component copy/paste/reset/enable, live-Play behavior) | reflected auto-UI w/ undo | 23 · doc 22 T9–T12/T15 | now | L | ☐ |
| Per-entity Active semantics + Hierarchy icons/toggles | no visibility/active concept | 23 · doc 22 T13/T14 | now | M | ☐ |
| Console v2 + GPU profiler panel (Starforge port) | basic console; profiler only in Frontier | 23 · doc 22 T16/T17 | now | S | ☐ |
| Asset-editor document host + reusable Timeline widget | one shared Inspector; no document tabs | 24 · doc 23 M1/M2 | now (anim/story editors sit on it) | L | ☐ |
| Starforge Animation Editor (skeleton tree, bone-overlay preview, clip scrub, sockets UI) | none | 24 · doc 23 M3 | with doc 19 A2 | L | ☐ |
| Reusable node canvas + Starforge Story Graph editor + post-chain graph view | doc 16 U6 ships the flow panel | 25 · doc 24 Q1/Q4/Q6 | after U6 + M1 | L | ☐ |
| Undoable content-browser rename/delete | confirm-dialog only (by design, E10; rename itself ships in 23 · T6) | — | revisit only if it bites | S | ✖ |

## Platform, shipping, projects

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| Projects anywhere on disk (external folders, relocatable) | locked to `assets/projects/<name>` | 16 · doc 15 S1 | now (decision 2026-07-04) | M | ✅ 2026-07-05 |
| Dedicated `Starforge.exe` + product homescreen/library | plugin tile + floating Home panel | 16 · doc 15 S2/S3 | now | M | ✅ 2026-07-05 |
| Launcher as dev tool (grouping, copy) | flat DLL list | 16 · doc 15 S4 | now | S | ✅ 2026-07-05 |
| Packaging v2: Release orchestration, exe icon, title/size, zip, installer, signing hook | current-config staging, no icon | 16 · doc 15 S5 | now | L | ✅ 2026-07-05 |
| Per-app `user://` isolation (+ portable mode) | shared root | 16 · doc 15 S6 | now | M | ✅ 2026-07-05 |
| Run-standalone button, save-thumbnails, About | — | 16 · doc 15 S7 | now | S | ✅ 2026-07-05 |
| Desktop app identity: live OS window titles, per-app AppUserModelID, dev-tree `Starforge.exe` (own VERSIONINFO) | static "Cosmic Engine" title; one anonymous host exe | 16 · doc 15 S2/S5 follow-through | now (desktop tools can't identify the window) | S | ✅ 2026-07-10 |
| Binary asset pak | loose files | 20 · doc 19 A9 | shipped-app size/IO measured to matter | M | ⏸ |
| Project templates gallery | one template + picker seam | 17 ships the 2D one | a third real template | S | ⏸ |
| Cloud/team project sync, DB service | registry file over folders | — (doc 15 §3) | multi-machine/team | XL | ✖ |
| Auto-update channel | — | — (doc 15 §3) | real users | L | ✖ |
| macOS/Linux | Win32+GL only | — (doc 05 §12 reopen conditions, archived) | a second platform request | XL | ✖ |

## Gameplay systems

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| Rigid-body physics, colliders, queries, triggers (Jolt) | **shipped (Jolt v5.5.0, 2026-07-04)** | 15 · doc 14 J1–J5 | now (decision 2026-07-04) | XL | ✅ |
| Character controller (walk/step/slope) | **shipped (CharacterVirtual, 2026-07-04)** | 15 · doc 14 J6 | with physics | M | ✅ |
| Terrain heightfield collision | **shipped (HeightFieldShape, ≤2 cm parity, 2026-07-04)** | 15 · doc 14 J7 | with physics | M | ✅ |
| Physics constraints/joints/ragdolls | — | 15 · doc 14 §3 | articulated-body project | L | ⏸ |
| Rigid-body water buoyancy | script-applied forces via S9 queries | 15 · doc 14 §3 | floating-dynamics need | M | ⏸ |
| In-game UI as entities (canvas/button/text/image) | ImGui only (editor chrome) | 17 · doc 16 U1/U2 | now | L | ✅ 2026-07-11 (editor click-consume + `UiSystem::HitTest` select; engine/tests 2026-07-08) |
| Screen-flow node graph (`.cflow` + FlowMachine + panel) | code-only SceneManager | 17 · doc 16 U5/U6 | now (user need #5) | L | ✅ 2026-07-11 (U6 Flow Graph panel + vendored imgui-node-editor + flow-driven editor Play; U5 runtime 2026-07-08) |
| 2D/pixel authoring (ortho mode, crisp sampling, sorting) | engine 2D exists, editor can't author it | 17 · doc 16 U3 | now (user need #2) | M | ✅ 2026-07-11 (2D mode + Camera2DController + pixel grid + `Scene::OnRenderSprites` + pixel-art sampling preset) |
| Sprite animation + tilemaps | none | 17 · doc 16 U4 | now | M | ✅ 2026-07-11 (TilemapComponent + int-array Cells + culled draw + Tile Palette painter w/ stroke undo; flipbook 2026-07-08) |
| Game-view correctness (primary camera, aspect presets, eject) | editor camera always | 17 · doc 16 U7 | now | S | ✅ 2026-07-11 (primary-camera Play + eject + letterboxed aspect presets + cursor capture) |
| Voxel worlds (chunks, meshing, edit, collision, gen) | none | 18 · doc 17 V1–V7 | after 14–17 (user-approved scope) | XL | ✅ 2026-07-08 (code + editor; recorded V7 demo stays on the user ledger) |
| Visual *logic* scripting (blueprints) | — | — (doc 16 §3) | explicit demand post-flow; doctrine is C++ logic | XL | ✖ |
| Navmesh / AI pathfinding (Recast/Detour, bake + `.cnav`, crowd agents, script `Nav()`) | none | 26 · doc 25 N1–N5 | **now — verdict flipped 2026-07-11** (editor vision + Forge Isle AI) | XL | ☐ |
| Flow variables (typed blackboard on `.cflow`: groups, defaults, guards/actions/`Flow().GetVar`) | guards read reflected entity fields only | 25 · doc 24 Q2 | now (editor vision) | M | ☐ |
| Story graphs (`.cstory` dialogue runtime: speaker/portrait/audio/options w/ guards + once; zero-code UI binding) | none | 25 · doc 24 Q3/Q4 | now (editor vision) | L | ☐ |
| Joint sockets (attach entities to animated joints) | none | 24 · doc 23 M4 | with doc 19 A2 | M | ☐ |
| 2D lighting (radial lights + ambient darkness over Renderer2D) | 2D is unlit | 27 · doc 26 X5 | now (2D game parity) | M/L | ☐ |
| World-anchored UI (nameplates/prompts/health bars via UiWorldAnchor) | screen-space canvas only | 27 · doc 26 X6 | now | S/M | ☐ |
| Render-to-texture verb (+ UiImage runtime texture; minimap building block) | offscreen passes are internal-only | 27 · doc 26 X7 | now | M | ☐ |
| Flagship showcase app (**Forge Isle**: character/AI/story/2D vignette/branding, packaged + trailer) | samples only (Playground/Pong/Blocks) | 28 · doc 27 Z1–Z7 | last — the capstone (decision #12, 2026-07-11) | XL | ☐ |
| Networking / multiplayer | none (C1 gives UDP transport) | — | a networked project (plan a phase then) | XL | ✖ |
| Save-game system | serializer exists; no slot/versioning layer | — | first game needing saves (likely doc 16-adjacent) | M | ✖ |
| Input rebinding UI | codes + gamepad polling exist | — | first shipped game with options menu | M | ✖ |

## Scripting

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| Lua tier L1 (embed, reflection-generated bindings) | mapped in archived doc 11 §4 | 21 · doc 20 C3 | C++ reload latency hurts a real tuning loop, or modding need | L | ⏸ |
| Lua L2 (live reload in Play, editor integration) | — | 21 · doc 20 C4 | with L1 | M | ⏸ |
| Lua L3 (interop, budget, doc page) | — | 21 · doc 20 C5 | with L1/L2 | M | ⏸ |
| Hot reload DURING Play (C++) | edit-mode only (by design, E12) | — | revisit with Lua decision | L | ✖ |

## Rendering & audio quality (tier 2 — the doc 18 menu)

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| Cascaded shadow maps | single 2k map + camera-follow workaround | 19 · doc 18 R1 | shadow range complaints | L | ⏸ |
| Depth prepass + ambient-only SSAO | whole-image composite | 19 · doc 18 R2 | SSAO dirtying lit surfaces | M | ⏸ |
| Progressive bloom | Gaussian pyramid | 19 · doc 18 R3 | shimmer complaints / cinematic pass | M | ⏸ |
| Froxel volumetrics | shadow-map raymarch god rays | 19 · doc 18 R4 | local fog volumes / multi-light shafts | L | ⏸ |
| FFT ocean (water tier 2) | 8-wave Gerstner | 19 · doc 18 R5 | open-ocean scale app | L | ⏸ |
| Terrain tessellation + holes | quadtree LOD, no holes | 19 · doc 18 R6 | silhouette quality / cave entrances | L | ⏸ |
| Particle indirect draw + sorting | fixed-count quads, unsorted | 19 · doc 18 R7 | effects-heavy overdraw measured | M | ⏸ |
| BCn/KTX2 compressed textures | none (audited: not needed yet) | 19 · doc 18 R9 | VRAM/load-time pressure | M | ⏸ |
| Projected decals | none | 19 · doc 18 R10 | content polish need | M | ⏸ |
| Skybox LEQUAL depth verb | background-first draw | 19 · doc 18 R11 | pair with any sky work | S | ⏸ |
| World-system builder registry (swap water/terrain impls) | concrete factories behind data recipes | 19 · doc 18 R12 | a second implementation must coexist | S | ⏸ |
| Positional/3D audio (panning, doppler, streaming) | distance-gain loops app-side | 21 · doc 20 C2 | true 3D audio need (Forge Isle polish may fire it) | M | ⏸ |
| Physical-atmosphere sky (turbidity/Rayleigh/Mie, IBL-matched) | artistic gradient/detailed/HDRI modes | 27 · doc 26 X1 | now (editor vision) | L | ☐ |
| Environment polish (sun elevation/azimuth widget, ambient intensity, exposed gamma, sun angular size) | raw vec3 sun; gamma fixed 2.2 | 27 · doc 26 X2 | now | S | ☐ |
| Vignette post pass (tonemap-folded, default off) | none | 25 · doc 24 Q5 | now | S | ☐ |
| Particle curl-noise turbulence + live noise preview + bounds clamp | recipe forces only (gravity/drag/wind) | 27 · doc 26 X3/X4 | now (CPU/GPU twins stay in lockstep) | M | ☐ |
| Arbitrary post-FX pass-graph executor | fixed verified chain (Q6 ships a graph *view* of it) | — | ✖ decision #13 2026-07-11 — revisit only with a real compositing need | XL | ✖ |
| Vulkan / second RHI backend | **stay on OpenGL** (S13.3 provisional-closed) | — | doc 05 §12 reopen conditions (archived): GL perf wall, platform need, or driver pain | XL | ✖ |
| MSAA (vs FXAA) | FXAA ships | — | perceived AA quality issue on thin geometry | M | ✖ |

## Assets & animation

| Feature | Today | Phase home | Unlock / trigger | Size | Status |
| --- | --- | --- | --- | --- | --- |
| assimp backend ON (FBX/STL/DAE/PLY live) | written, gated off (`COSMIC_WITH_ASSIMP`) | 20 · doc 19 A1 | now (anchor of Phase 20; feeds Phases 23–24) | M | ✅ 2026-07-12 (assimp v5.4.3 vendored/trimmed, default ON; + glTF via cgltf, multi-mesh `#i` children, materials→`.cmat`) |
| Skeletal animation (skins/clips/skinning) | none | 20 · doc 19 A2 | **fired 2026-07-11** — Forge Isle is the character project; editor superstructure = Phase 24 | XL | ✅ 2026-07-12 (runtime: Skeleton/Clip sampling, glTF+FBX skins, SSBO-10 GPU skinning + shadow twin, Animator + editor scrub; Fox verified on-GPU) |
| Animator crossfade tier (script-driven clip switching w/ timed blend) | — | 24 · doc 23 M6 | with A2 (the minimal tier a playable character needs) | S | ☐ |
| Animation blend trees / state machines (full controller graph + editor) | — | — (doc 23 M6 restates the park) | after Forge Isle ships; editor would ride Phase 25's canvas | L | ✖ |
| Material slots (multi-material meshes on ONE entity, per-slot override) | one slot; multi-material sources import as child entities | 24 · doc 23 M5 | with A1 (engine-architectural — schedule deliberately) | L | ☐ |
| STEP/CAD B-rep import (`step2gltf` tool) | STL path only | 20 · doc 19 A3 | STEP-only workflow appears | L | ⏸ |
| CSG booleans (manifold) | primitives only | 20 · doc 19 A8 | modeling outgrows primitives | M | ⏸ |
| Connectivity: UDP sockets | serial only | 21 · doc 20 C1 | UDP telemetry/sim link need | S | ⏸ |

## Standing user-acceptance ledger (not features — recorded here so nothing silently drops)

| Item | Origin |
| --- | --- |
| Viper gates G1 (hover w/ gusts — estimator suspect), G2, G3 + committed recordings; Teensy HIL flight; gimbal rig | archived doc 04 (app-side by decision 2026-07-04) |
| Phase 12 on-GPU perf pass (cull %, auto-instance, LOD swaps; 5 Frontier worlds ≥60 fps, CPU≪GPU) + screenshots | archived doc 05 §"S12" |
| Phase 13 recorded acceptance demo (new project → import → scene → script → Play/telemetry → package → clean run) | archived doc 11 E21 / `docs/design/starforge-acceptance-demo.md` |
| W3 DWM compat-mode decision + interactive repro matrix (snip overlay, 125 % laptop) | archived doc 09 §3.5 |
| Water look tuning (from-below surface, caustics/shafts) | `docs/design/water-rendering-notes.md` |
| Phase 17 recorded acceptance (zero-code FlowDemo authored/played/packaged; ForgePong match + package; U1/U3/U4/U7 on-GPU spot checks) | doc 16 U8 / `docs/design/ui-flow-2d-acceptance.md` |
| Phase 20 remainder: Blender FBX + glTF of one object at identical world size; Fox playback vs a reference viewer; 50 animated instances ≥60 fps; skinned-shadow visual (a lit scene w/ shadows); packaged-app animation run. (Self-run 2026-07-12: Fox import/playback/scrub/clip-switch, `.cmeta` rescale re-import, preview self-test PASSED, thumbnails, material undo.) | doc 19 A1/A2/A4 |
