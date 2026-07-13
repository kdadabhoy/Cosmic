# Docs Plan v2 — README Overhaul, API Reference, System Explainers (D5–D40)

> **Extended 2026-07-04 (roadmap v3):** added §13 (the **Starforge user manual**, D37–D39 —
> the user's "document explaining all Starforge features") and §14 (**per-phase documentation
> hooks**, D40 — the standing rule + per-phase checklist that keeps docs current as Phases
> 14–21 ship). Diagram inventory §4 gained DG-15…DG-18. Doc 06 now lives in
> [`archive/06-docs-plan.md`](archive/06-docs-plan.md).
>
> **Created 2026-07-03.** Supersedes [`archive/06-docs-plan.md`](archive/06-docs-plan.md):
> - **D1** (§1.5 command-reference contract) — *carried forward unchanged*, and extended by
>   this plan's API-reference contract (§11).
> - **D2** (§40 packaging refresh) — *absorbed into D23*.
> - ~~**D3** (split the monolith)~~ — **struck 2026-07-03 by user decision:** the README stays
>   one document "in a similar format as it is right now", growing new sections. Deep
>   internals migrate to `docs/systems/` explainers instead of `docs/engine-internals.md`
>   (§1 decision log).
> - **D4** (docs index) — ✅ *shipped by this planning session* (`docs/README.md`).
>
> **Division of labor:** this planning session (2026-07-03) shipped the entire scaffolding —
> `docs/README.md`, `docs/reference/` (index + 15 chapter skeletons with per-header coverage
> checklists), `docs/systems/` (index + 19 explainer skeletons with section plans + truth
> sources). Each numbered work order below is **one implementation session** that fills
> skeletons / edits the README. No engine code is touched by any item except D5 (one script +
> one CI line).

---

## 0. Execution notes — READ ONCE, THEY APPLY TO EVERY ITEM

1. **Never run git write commands** (no add/commit/branch). Leave working-tree edits; the
   user reviews and commits. Don't build the engine — docs items don't need it (D5's script
   runs standalone: `powershell -File tests\check_docs_coverage.ps1`).
2. **Headers and source are the only truth.** The skeleton checklists, this plan, and the
   root README were accurate on 2026-07-03 and WILL drift. Before writing any entry: open the
   header, copy the signature verbatim, and verify behavior claims in the `.cpp` (or the
   cited test). If a checklist row names an API you cannot find, the header wins — fix the
   checklist row, don't invent the API. **Never document unshipped/parked API** (e.g. audio
   A3 positional, FFT ocean, CSM).
3. **Formats are fixed, not suggestions.** API entries: the template in
   [`../reference/README.md`](../reference/README.md#entry-format-mandatory--copy-this-shape).
   Explainers: the section shape + writing bar in
   [`../systems/README.md`](../systems/README.md#document-format-mandatory--every-explainer-uses-this-shape).
   README additions: match the existing README's voice (second person, tables for facts,
   `>` blockquotes for warnings, code fences with real namespaces).
4. **Code samples must compile against the current API.** Model them on Engine3DDemo /
   Frontier / SF_Telem usage (those projects build green — they are living examples). No
   `...` placeholders inside statements; full `Cosmic::` qualification like the README does.
5. **Mermaid rules** (GitHub renders ```mermaid fences natively):
   - Allowed types: `flowchart TD/LR`, `sequenceDiagram`, `classDiagram`, `stateDiagram-v2`.
   - No `%%{init}%%` theme overrides (must read in GitHub light AND dark). No HTML labels.
   - Quote any label containing `(){}[]:;` — bare parens break the parser.
   - Keep ≤ ~40 nodes; split rather than cram. Verify syntax at mermaid.live before landing.
   - Diagram IDs (DG-n, §4) are assigned — build the assigned ones, don't freelance new
     diagrams into other documents (add to §4 first if genuinely needed).
   - **Existing ASCII diagrams stay** unless your mermaid version preserves *every*
     annotation; when it does, the mermaid replaces the ASCII in place.
6. **Section numbers in the root README are frozen.** `README §26`-style references exist in
   code comments, plan docs, and engineering notes. New sections use **decimal insertion**
   (house precedent: §1.5, §20.5, §21.5, §28.5). Never renumber, never delete a heading —
   a superseded section becomes a summary + link (§8 Phase D rules).
7. **Don't fork facts.** Each fact lives in exactly one home (§1 tier table) and is *linked*
   from everywhere else. Where a design doc exists (`frame-lifecycle.md`,
   `water-rendering-notes.md`, `responsive-rendering-and-pause.md`) the explainer summarizes
   and links — it never restates the spec in full.
8. **Skeleton bookkeeping:** when you fill a skeleton, delete its `STATUS: SKELETON` banner,
   flip its row in the tier index (`reference/README.md` or `systems/README.md`) from
   `SKELETON — D#` to `✅ YYYY-MM-DD`, and update the item's status banner in THIS file
   (✅ + date + one-line result). Tick checklist boxes as you cover them; a box you
   deliberately don't cover gets a one-line reason instead of a tick.
9. **Plain-language bar for explainers:** §1–§3 of every systems doc must survive the
   "smart friend test" (a reader who has never written a shader follows it). Define every
   term at first use. This is the requirement rushed sessions miss — it's the point of the
   tier.

---

## 1. The documentation model (decisions — the "why" behind the structure)

Three tiers, each answering one question, mirroring Diátaxis:

| Tier | Question | Home | Shape |
| --- | --- | --- | --- |
| Developer Guide | "How do I do X?" | root `README.md` (stays ONE file) | Task-oriented guide, Parts I & II, §-numbered |
| API Reference | "What exactly does this call do?" | `docs/reference/` (index + 15 chapters) | OpenGL-man-page-style entries |
| System Explainers | "How does it actually work?" | `docs/systems/` (index + 19 docs) | Plain-English overview → technical implementation |

**Decision log (2026-07-03, user-directed unless noted):**
1. **README stays a monolith** in its current format and grows new sections (user).
   Supersedes doc 06 D3's split.
2. **The API Reference is one logical document, physically chaptered** (planning decision).
   The user asked for "one of the most complete documents in the project… split into
   sections by command type". A single file would exceed ~15k lines and be unmaintainable in
   one AI session; 15 domain chapters behind one index (`docs/reference/README.md`) give the
   same reading experience, and the index's **manifest table** makes completeness checkable
   by script. The README links the reference **at the top** (D19).
3. **Diagrams are Mermaid, in-repo, text-based** (planning decision). GitHub renders them
   natively, they diff in PRs, and any AI session can maintain them — unlike the orphaned
   root `CosmicUML.png` (referenced by nothing; retired in D35).
4. **README Part II sections become summaries + links as `docs/systems/` supersedes them**
   (planning decision — the only way "explain every subsystem" docs don't fork facts).
   Every Part II heading survives (frozen numbering, note 6); its full content *moves* into
   the corresponding explainer in the same work order, leaving a faithful 2–3 paragraph
   summary + link. Nothing is deleted without being carried somewhere.
5. **Client-facing 3D/audio/config/etc. get full new README Part I sections** (user: "a lot
   of additional sections") at decimal insertion points (§7 Phase C table).
6. **Enforcement is scripted, not aspirational** (user: reference "should always be updated
   when implementation occurs"): D5 ships `tests/check_docs_coverage.ps1` + CI step; §11 is
   the human contract.

## 2. Gap inventory — why this is worth ~30 sessions

Verified 2026-07-03: root README = 4,795 lines, 43 sections, all 2D-era. A grep across the
README for `Renderer3D|AudioEngine|Config::|Terrain|Water|ParticleEmitter|SceneRenderer|`
`EnvironmentMap|ShadowMap|PostProcess|Noise::|Integrat|LookupTable|Gamepad|FlyCamera|OrbitCamera`
returns **5 hits total**. Meaning: everything below is engine surface with **zero client
documentation** outside plan docs:

- **Phase 2 sim toolkit (E-series):** Config/TOML, integrators, filters, lookup tables,
  noise, PCG32 RNG, gamepad input.
- **Audio (A1/A2):** AudioEngine, Sound, groups/loops.
- **Phases 7–9 3D:** Camera base + perspective/orbit/fly controllers, CAD nav + ViewCube +
  gizmos + picking, Renderer3D, Mesh/Model/glTF, AssetLibrary, lights, MRT framebuffers,
  compute/SSBO, HDR pipeline, PBR, IBL, shadows, SSAO, bloom, FXAA, sky/fog/time-of-day.
- **Phases 10–11:** Terrain, Water, particles, SceneRenderer, InstanceSet/Frustum,
  CoverageCapture/snow, GPU profiler verbs, presets.
- **Phase 12:** sorted render queue semantics (cull/sort/auto-instance), `SetTransparent`,
  **the material-read-at-flush breaking change + `Material::Clone`**, LODGroupComponent.
- **Also underdocumented:** SerialLink (engine-side connect UI), ThemeManager/port-mode
  docking (§24/§28 partially cover), `BindingPoints` reserved registry.

Existing strengths to preserve: §1–§29 client guide voice, §1.5 command reference (+
contract), the honesty-pass sections, §30–§43 internals (source material for explainers).

## 3. Target structure

```
README.md                          ← Developer Guide (grows ~10 sections + ~8 diagrams; top link block)
docs/
├── README.md                      ← docs index                        ✅ shipped 2026-07-03
├── reference/                     ← API REFERENCE (the "always updated" document)
│   ├── README.md                  ← index + entry format + manifest + contract   ✅ shipped
│   └── {core, events-input, graphics-resources, rendering-2d, rendering-3d,
│        rendering-pipeline, world-systems, ecs, cameras, math, assets-io,
│        audio, serial-telemetry, jobs, ui}.md      ← 15 skeletons     ✅ shipped (fill: D6–D18)
├── systems/                       ← SYSTEM EXPLAINERS
│   ├── README.md                  ← index + format + writing bar      ✅ shipped
│   └── {architecture-overview, core-runtime, windowing, events-input,
│        cameras-navigation, rendering-2d, rendering-3d, rendering-pipeline,
│        terrain, water, particles, ecs-scene, assets-vfs, audio,
│        math-sim-toolkit, jobs-parallelism, serial-telemetry, ui-theming,
│        build-plugin-packaging}.md                 ← 19 skeletons     ✅ shipped (fill: D25–D34)
├── design/ · engineering-notes/ · plans/ · archive/ · installer-guide.md   ← unchanged
tests/check_docs_coverage.ps1      ← NEW (D5) + a ci.yml step
```

## 4. Diagram inventory (build exactly these; IDs are referenced by skeletons)

| ID | Type | Content | Home(s) | Truth source |
| --- | --- | --- | --- | --- |
| DG-1 | flowchart TD | Module block diagram: host exe → engine DLL (subsystem boxes) → platform/OpenGL; project DLLs plugging in | README §1, systems/architecture-overview | `Cosmic/src/` tree, Cosmic.h |
| DG-2 | classDiagram | Core object model: Application–Window–LayerStack–Layer–ImGuiLayer–WorkspaceLayer–LauncherLayer (+ownership) | README §30, architecture-overview | core/*.h, layers/*.h |
| DG-3 | sequenceDiagram | One frame: PollEvents → fixed pass ×N → variable pass → ImGui → swap → Safe Zone | README §3, core-runtime | Application.cpp Run loop |
| DG-4 | flowchart TD | Event propagation: OS → Application handlers → overlays→layers with Handled short-circuit + viewport-hover pass-through | README §5, events-input | Application::OnEvent, ImGuiLayer |
| DG-5 | sequenceDiagram | Plugin DLL lifecycle: scan → LoadLibrary → InitializePluginContexts → CreatePluginLayer → hooks → delete-before-FreeLibrary | README §31, build-plugin-packaging | Application.cpp DLL code, Cosmic.h |
| DG-6 | classDiagram | Renderer stack: Renderer2D/Renderer3D/SceneRenderer → RenderCommand → RendererAPI → OpenGL*; resources (Shader/Material/Mesh/Texture) | README §35, rendering-3d (systems) | renderer/*, platform/OpenGL/* |
| DG-7 | flowchart LR | 3D submission: DrawMesh → frustum cull → sort key → auto-instance detect → flush (opaque F2B, transparent B2F) | README §8.5, reference/rendering-3d, systems/rendering-3d | Renderer3D.cpp, RenderQueue.h |
| DG-8 | flowchart TD | SceneRenderer pass graph incl. read/write targets: shadow → coverage → reflection → refraction → main HDR → water → particles → post chain → present | README §8.6, reference+systems rendering-pipeline | SceneRenderer.cpp, frame-lifecycle.md |
| DG-9 | classDiagram | ECS: Scene ⇄ entt registry ⇄ Entity handle ⇄ component types (+ which pass consumes which component) | README §15, ecs-scene | scene/*.h |
| DG-10 | flowchart TD | Time waterfall: rawDelta → global scale → (fixed accumulator \| variable) → layer scale → GetLocalTime; Pause() tap | README §7 or §32, core-runtime | Application.cpp, Layer.h |
| DG-11 | stateDiagram-v2 | App states: Launcher ⇄ Workspace(project) with queued Safe-Zone transitions | README §3, core-runtime | Application transition code |
| DG-12 | flowchart LR | Job system: main-thread frame lanes vs worker pool, submit/wait sync points, GL-stays-on-main rule | README §22, jobs-parallelism | JobSystem.cpp |
| DG-13 | flowchart LR | Telemetry: device/sim → SerialPort/SerialLink → framing/decode → channels (columnar) → recorder file ⇄ player → panel/plots | README §26, serial-telemetry | telemetry/*, serial/* |
| DG-14 | flowchart TD | Packaging: source → Release build → cmake --install staging → dist/<App> prune → zip / Inno installer | README §40, build-plugin-packaging | package.bat, CosmicSetup.iss |
| DG-15 | flowchart TD | Starforge architecture: editor DLL ⇄ engine seams (reflect/serializer/CommandStack/ScriptHost/SceneManager) ⇄ project folder ⇄ game DLL (two exports) | starforge manual §1, systems/build-plugin-packaging | archived doc 11 §2.1 (verify vs code) |
| DG-16 | sequenceDiagram | Project lifecycle: New → scaffold → edit/save (.cscene) → Build Scripts (hot reload steps 1–5) → Play (snapshot→runtime scene) → Package → standalone boot (boot.cfg → PlayerLayer) | starforge manual §2/§7 | StarforgeApp.cpp, GameModule.cpp, PlayerLayer.cpp |
| DG-17 | stateDiagram-v2 | FlowMachine: states/transitions/guards/overlay push-pop (once doc 16 U5 ships) | starforge manual flow chapter, reference/ecs or new flow chapter | scene/FlowMachine.h |
| DG-18 | flowchart LR | Physics tick order: scripts OnFixedUpdate → PhysicsWorld::Step → transform write-back → collision events (once doc 14 J4 ships) | README physics §, systems explainer | scene/Scene.cpp physics hooks |

---

## 5. Phase A — enforcement tooling

### D5 — `tests/check_docs_coverage.ps1` + CI step — S
```
Files: NEW tests/check_docs_coverage.ps1; EDIT .github/workflows/ci.yml (one step, mirror the
existing check_gl_conformance.ps1 step's placement/style — read that script first for repo
PowerShell conventions; PS 5.1-safe, no && chains).
Behavior:
 1. Parse Cosmic/src/Cosmic.h for lines matching ^\s*#include\s+"(.+)" → the public header set
    (ignore <...> system/vendor includes like imgui/implot).
 2. Parse the manifest table in docs/reference/README.md (rows: | `header` | [chapter](...) |).
 3. FAIL (exit 1) listing any public header with no manifest row, and any manifest row whose
    header no longer exists on disk (stale row).
 4. For each mapped chapter file WITHOUT a "STATUS: SKELETON" banner (strict mode is per-file,
    automatic): scrape `class COSMIC_API (\w+)` / `struct COSMIC_API (\w+)` names from each of
    its headers and FAIL if a name never appears in the chapter text. Skeleton-bannered files
    → WARN only (prints, exit 0 contribution).
 5. Output format mirrors check_gl_conformance.ps1 (file: reason per line, summary count).
Gotchas: PowerShell 5.1 (no ternary, no -AsHashtable); read files with -Raw + -Encoding UTF8;
the manifest's last row ("Cosmic.h (plugin exports…)") and the "Not in Cosmic.h but
client-reachable" footnote are special-cased allowlist entries — encode them in the script,
don't force-fit the table format.
Acceptance: script runs clean on the current tree (all chapters are skeletons → warnings
only, exit 0); deleting a manifest row makes it exit 1; ci.yml step added and YAML-valid.
```
**Status:** ☐

---

## 6. Phase B — API Reference chapters (D6–D18)

**Shared procedure for every item** (the skeleton lists scope + checklist + chapter-specific
traps; this is the loop):
1. Read the chapter skeleton, then read **every scope header end-to-end**. Enumerate public
   symbols (`COSMIC_API` classes/structs/free functions/macros/enums) — the checklist is a
   starting point, the headers are truth (note 2).
2. Write entries per the mandatory template — signature verbatim, what/why/example/pitfalls.
   Examples modeled on real usage (Engine3DDemo / Frontier / SF_Telem / template project).
   Every failure mode pinned (nullptr vs degraded object vs log-and-continue).
3. Verify each behavior claim in the .cpp or cited doctest before asserting it.
4. Build any diagram assigned to the chapter (§4). Bookkeeping per note 8. Run D5's script —
   your chapter must pass strict mode (banner removed → class-name check applies).

| Item | Chapter (skeleton) | Size / notes |
| --- | --- | --- |
| **D6** | `reference/core.md` | M. Application/Layer/Window/Log/plugin boundary. Include the Pause-vs-TimeScale table (README §7) condensed per-entry. |
| **D7** | `reference/events-input.md` | M. Full code tables (KeyCodes/MouseButtonCodes/GamepadCodes headers → tables). |
| **D8** | `reference/graphics-resources.md` | L. The BindingPoints registry table is load-bearing — other chapters link it. Material Clone/flush semantics stated here once, linked elsewhere. |
| **D9** | `reference/rendering-2d.md` | M. Every DrawQuad overload individually (that's the OpenGL-doc style the user asked for). |
| **D10** | `reference/rendering-3d.md` | **XL — may split into two sessions** (submission+queue semantics, then Mesh/Model/InstanceSet/Frustum). Stronger model recommended: the deferred-flush semantics must be *exactly* right. DG-7. |
| **D11** | `reference/rendering-pipeline.md` | L. Enumerate SceneRenderer's real header surface — the skeleton deliberately doesn't guess it. DG-8 (shared with systems doc — build once here, reuse). |
| **D12** | `reference/world-systems.md` | L. Pin the Terrain `32·2^k+1` resolution rule and the SampleHeight ≤1 cm guarantee with test citations. |
| **D13** | `reference/ecs.md` | M. The Components.h field-by-field table is the centerpiece. |
| **D14** | `reference/cameras.md` | M. Hotkey tables from OrbitCameraController + Engine3DDemo bindings. |
| **D15** | `reference/math.md` | M. Cite doctests per header; determinism box. |
| **D16** | `reference/assets-io.md` + `reference/audio.md` | M (two small chapters, one session). VFS dev-vs-packaged path examples verified against Runtime/Main.cpp. |
| **D17** | `reference/serial-telemetry.md` + `reference/jobs.md` | L (two chapters). Threading contracts are the hard part — verify against .cpps, not comments ("v3" docstrings are stale; code writes v1). DG-13 optional here or in D33. |
| **D18** | `reference/ui.md` | M. DockPort table + the never-persist-dock-node-ids rule. |

All of Phase B is **parallel-safe across items** (distinct files); the only shared file is
`reference/README.md` — touch ONLY your chapter's Status cell.

---

## 7. Phase C — README overhaul (D19–D24, **serial** — shared ToC and §-numbering)

New Part I sections at decimal insertion points (frozen numbering, note 6). Implementer:
before creating a decimal number, grep the README — §20.5/§21.5/§28.5 already exist.

| New § | Title (working) | Content summary | Order |
| --- | --- | --- | --- |
| §6.5 | Gamepad Input | Polling API, codes, deadzones, template-project example | D22 |
| §8.5 | 3D Rendering API | BeginScene/DrawMesh/DrawModel, Mesh/Model/glTF, materials in 3D, lights, **queue semantics + Material::Clone breaking-change guidance**, stats | D20 |
| §8.6 | Lighting, Environment & Post-Processing | SceneRenderer quickstart, IBL/sky/time-of-day, shadows, HDR/tonemap/bloom/SSAO/FXAA/god-rays/flare toggles | D21 |
| §8.7 | Terrain, Water & Particles | Client-facing setup for each + component routes + presets | D21 |
| §8.8 | 3D Performance Toolkit | Frustum culling, auto-instancing conditions, InstanceSet, LODGroupComponent, GPU profiler verbs + F3-style HUD pattern | D20 |
| §16.5 | CAD Navigation, Fly Camera & Gizmos | Orbit NavStyle/ViewPreset/ViewCube/frame-snap, fly camera, picking, ImGuizmo + hotkeys | D20 |
| §17.5 | Configuration Files (TOML) | Config load, typed getters, viper.toml-style example | D22 |
| §19.5 | Audio | One-shots, loops/groups, headless behavior | D22 |
| §22.7 | Simulation Math Toolkit | Integrators/filters/LUT/noise/RNG tour + when-to-use table (full detail → reference/math.md) | D22 |
| §28.6 | Theming, Fonts & Widgets | ThemeManager/SetImGuiTheme, Theme Studio, Fonts/Lucide, Widgets/PlotStyle, DockPort recap | D22 |
| §42.5 | 3D & World-System Internals (directory) | Part II: table of links into docs/systems/ for everything with no legacy Part II section | D24 |

### D19 — README front door — M
```
EDIT README.md only. (1) Directly under the H1: a prominent link block —
**📖 [API Reference](docs/reference/README.md)** · [System Explainers](docs/systems/README.md)
· [Docs Index](docs/README.md) · [Command Reference §1.5](#15-command-reference--every-command)
— the user requirement is the reference linked AT THE TOP. (2) Rewrite the §1 "What is
Cosmic?" opener honestly: C++20 / OpenGL 4.5 core / Windows x64, 2D + 3D (PBR, IBL, shadows,
terrain/water/particles), plugin-DLL model, simulation+telemetry tooling — verify each claim
against the tree, no marketing. (3) Insert DG-1 (§1), DG-3 + DG-11 (§3), DG-10 (§7). (4) Add
ToC entries ONLY for sections that exist (new-section ToC rows land with their sections).
Acceptance: all four diagrams render on a GitHub preview; every ToC link resolves; §1.5
untouched.
```
**Status:** ☐

### D20 — README 3D core sections (§8.5, §8.8, §16.5) — XL, stronger model
```
Source material: Engine3DDemo panels (each S-item has a toggle — mine its code), doc 05
banners, reference chapters D10/D14 if already landed (else headers directly). Every snippet
compile-shaped against current headers. §8.5 MUST carry the S12 breaking-change box:
material values read at flush; per-draw variation = Material::Clone; mid-scene state islands
= Renderer3D::Flush(). Insert DG-7 (§8.5) and DG-6 (§35 while you're there, if trivial —
else leave for D24). Update ToC.
Acceptance: sections read like the existing §8 (same voice/table style); a reader can draw a
lit glTF model + a 5k-instance field from these sections alone; ToC links resolve.
```
**Status:** ☐

### D21 — README environment + world sections (§8.6, §8.7) — L
```
Source: Frontier worlds (IslandWorld = the everything-example), Engine3DDemo "World systems"
panel, doc 05 §5–§9, doc 10 F-series. §8.6 leads with the SceneRenderer quickstart (the
8-pass orchestrator most apps should use) and DG-8; manual-pass driving is the advanced
footnote. §8.7 gives minimal working setups: terrain (incl. 32·2^k+1 resolution rule +
async-build/loading-screen pattern), water (reflection handoff + underwater), particles
(presets first, custom emitters second). Update ToC.
Acceptance: as D20; the three worked setups name their exemplar file in Projects/.
```
**Status:** ☐

### D22 — README toolkit sections (§6.5, §17.5, §19.5, §22.7, §28.6) — L
```
Five smaller sections, one session. Sources: template project (config+gamepad demos), doc 03
/ doc 08 acceptance notes, ThemeManager/Fonts/Widgets headers, ViperSim for audio-alert +
sim-math usage patterns. Each section ends with a "Full reference: docs/reference/<x>.md"
line. Update ToC.
Acceptance: as D20.
```
**Status:** ☐

### D23 — §40 build/packaging refresh + §1.5 sweep *(absorbs doc 06 D2)* — M
```
EDIT README §40 (+ §1.5 only if the sweep finds drift). Doc 06 D2's source list verbatim:
package.bat dist/<Name> layout + single-app prune; LauncherLayer scan order (exeDir/projects
then exeDir); Release-implies-distribution (verify no COSMIC_DIST flag in
Cosmic/CMakeLists.txt); package_installer.bat + Inno flow; --project boot flag; link
docs/installer-guide.md; COSMIC_SDK = build-time only (Runtime/Main.cpp sets CWD
exe-relative). Insert DG-14. Then re-run the §1.5 verification sweep from doc 06 D1 (ls
*.bat; grep -- flags in Runtime/Main.cpp; grep option( in the three CMakeLists; grep CS_KEY_
in Window.cpp) and fix any drift.
Acceptance: every D1-sweep item appears in §1.5; §40 claims verified against the scripts.
```
**Status:** ☐

### D24 — README Part II integration pass — M
```
EDIT README Part II. (1) Add §42.5 internals-directory table (all 19 systems docs, one line
each). (2) Insert remaining assigned diagrams not yet placed (DG-2 §30, DG-4 §5, DG-5 §31,
DG-6 §35, DG-9 §15, DG-12 §22, DG-13 §26). (3) Stale-claims sweep of Part II: §34 (GL version
facts — loader is 4.5 core since S4.0), §43 known-limitations/roadmap → rewrite as a short
pointer to docs/plans/00-MASTER-ROADMAP.md instead of a stale list. Do NOT convert Part II
sections to summaries here — that happens with each systems doc (Phase D) so content moves
exactly once.
Acceptance: diagrams render; no Part II sentence contradicts current code (spot-check §34
claims against platform/OpenGL/).
```
**Status:** ☐

---

## 8. Phase D — System explainers (D25–D34)

**Shared procedure:** each skeleton carries its section plan, assigned diagrams, and truth
sources — follow it. Additionally, when a systems doc supersedes a README Part II section
(mapping below), the SAME work order converts that README section to a faithful 2–3 paragraph
summary + "Full internals:" link, carrying every fact into the explainer (anything dropped
must be verified stale, with a note in the work-order status). Explainer §1–§3 must pass the
plain-language bar (note 9).

| Item | Explainers (skeletons in docs/systems/) | Absorbs README Part II | Size |
| --- | --- | --- | --- |
| **D25** | architecture-overview.md | §30 (source map) | L — write LAST §5 directory table against the shipped docs, but do this item FIRST for orientation; revisit its cross-links in D36 |
| **D26** | core-runtime.md + windowing.md | §32, §33 | L |
| **D27** | events-input.md + cameras-navigation.md | §41 | M |
| **D28** | rendering-2d.md + rendering-3d.md | §36, §38 (RenderPass impl → whichever fits; likely rendering-2d) | L — stronger model for rendering-3d |
| **D29** | rendering-pipeline.md | §34, §35 (keep §35's class diagram DG-6 in README; move prose) | **XL, may split (passes/post, then PBR-IBL-shadow theory)** — stronger model; the PBR page must EXPLAIN, not name-drop |
| **D30** | terrain.md + water.md | — | L |
| **D31** | particles.md + ecs-scene.md | — | M |
| **D32** | assets-vfs.md + audio.md + math-sim-toolkit.md | §37 | M (three small) |
| **D33** | jobs-parallelism.md + serial-telemetry.md | §39, §42 | L |
| **D34** | ui-theming.md + build-plugin-packaging.md | §31, §40 (keep §40's client-facing command tables; move architecture prose) | L |

Pairs within an item share a session; different items are parallel-safe EXCEPT their README
Part II conversions — if run in parallel, coordinate on README (or defer the README
conversion of the later item).

---

## 9. Phase E — integration & enforcement (D35–D36)

### D35 — link/anchor sweep + relic cleanup — M
```
(1) Script-or-grep every relative link and #anchor across README.md, docs/**/*.md — GitHub
anchor rule: lowercase, spaces→-, strip punctuation. Fix all dead links (grep repo-wide for
"README.md#" too — code comments and plan docs reference §-anchors). (2) Root CosmicUML.png
is referenced nowhere (verified 2026-07-03): move to docs/archive/CosmicUML.png with a
one-line note in docs/archive/ (superseded by DG-2) — flag the move in your summary for user
approval at commit time. (3) Confirm every DG-1..14 landed where §4 assigns it; fix strays.
Acceptance: zero dead relative links/anchors (paste the check output in the status banner).
```
**Status:** ☐

### D36 — enforcement finale + plans bookkeeping — S
```
(1) All SKELETON banners gone → run D5's checker in (now fully strict) mode; fix to green.
(2) Verify both tier indexes show all-✅ status columns; docs/README.md "Status" paragraph
rewritten to steady-state. (3) Update 00-MASTER-ROADMAP.md "Continuous — docs" row: doc 06 →
this doc, phase-complete note. (4) Add the §11 PR checklist to the repo's contribution docs
home (README §1.5 preamble is the precedent — extend its contract sentence to name the API
reference + manifest). (5) Mark this plan's header ✅ complete with date.
Acceptance: check_docs_coverage.ps1 exit 0 strict; roadmap + indexes consistent.
```
**Status:** ☐

---

## 10. Order, sizing, parallelism

| Step | Items | Depends on | Parallel? |
| --- | --- | --- | --- |
| 1 | D5 | — | — |
| 2 | D6–D18 (reference) | D5 (checker exists) | ✅ fully parallel |
| 3 | D19 → D20 → D21 → D22 → D23 → D24 (README) | reference chapters help but don't block (link targets exist as skeletons) | ❌ serial (shared ToC) |
| 4 | D25–D34 (explainers) | D24 (Part II stable) | ✅ parallel except README conversions |
| 5 | D35 → D36 | everything | ❌ |

**AI tier:** D5 low (scripting). Reference chapters: medium, except **D10 strong**. README:
D19/D22/D23 medium; **D20/D21 strong** (3D semantics must be exact). Explainers: medium,
except **D28(3D)/D29 strong** (correct-and-accessible graphics theory is the hard combo).
Every item: one session; XL items note their split point.

## 11. The upkeep contract (lives beyond this plan)

Three standing rules — copy into any PR checklist:
1. **CLI surface** (scripts/flags/CMake options/hotkeys) changed → update README §1.5
   *(doc 06 D1, unchanged)*.
2. **Public C++ API** changed (anything reachable via `Cosmic.h` or objects it hands out) →
   update the mapped `docs/reference/` chapter **in the same PR**: signature blocks verbatim,
   new symbols get full entries, removed symbols get deleted + a changelog line. New public
   header → manifest row + chapter assignment. CI's `check_docs_coverage.ps1` (D5) enforces
   the mechanical half.
3. **Architecture/behavior** changed (pass order, threading, formats, lifecycle) → update the
   affected `docs/systems/` explainer + any diagram (§4 table maps diagram → truth source;
   if your PR touches a truth source, re-verify its diagrams) + the README section if
   client-visible.

## 13. Phase F — Starforge user manual (D37–D39) *(added 2026-07-04)*

A fourth tier: the **product manual** for Starforge users (people *using* the editor, not
engine clients — a different reader than all three existing tiers). Home: `docs/starforge/`
(index + chapters). Format: task-oriented, screenshot-friendly (screenshots are user-captured
— the text must stand alone without them), every claim verified against the running editor.
`docs/design/starforge-ui.md` (the Stage-D quick guide) is **absorbed** by D39 and becomes a
pointer. Chapters marked *(P14+)* document behavior only after the named phase ships — write
them in the same order the phases land.

### D37 — Manual skeleton + core chapters — M
```
NEW docs/starforge/README.md (index + reading order) and chapters:
 01-getting-started.md   launch (Launcher today / Starforge.exe after doc 15 S2), homescreen,
                         open the ForgePlayground sample, first scene tour
 02-projects.md          project anatomy (project.cproj, scenes/, src/, assets/), where
                         projects live (external folders after doc 15 S1), registry, autosave/.bak
 03-scenes-entities.md   hierarchy, Inspector (reflection-driven), undo model, prefabs,
                         camera navigation (post-H1 behavior), viewport tools/gizmos/bookmarks
Acceptance: a reader who has never seen Starforge reaches an edited+saved scene; every
hotkey/menu named exists in the build; index links resolve.
```
**Status:** ☐

### D38 — Authoring chapters — L
```
NEW docs/starforge/: 04-primitives-import.md (parametric shapes, .cmeta units, assimp status),
05-materials-environment.md (.cmat workflow, environment panel incl. HDRI after H4),
06-world-systems.md (terrain/water/particle recipes, presets, .cemitter), 07-2d-ui-flow.md
(P17: sprites, tilemaps, UI entities, flow graph). Each chapter ends "Under the hood" linking
the matching docs/systems/ explainer (no fact forking, note 7).
Acceptance: each workflow reproduced start-to-finish from the text alone.
```
**Status:** ☐

### D39 — Logic, play, ship chapters + absorb the quick guide — L
```
NEW docs/starforge/: 08-scripting.md (C++ scripts, SystemScripts after H9, hot reload,
telemetry marks; Lua pointer to its matrix row), 09-play-telemetry.md (play/pause/step,
recording, takes, CSV), 10-packaging.md (Package dialog, icon [P16], installer/zip [P16],
clean-machine checklist), 11-shortcuts.md (generated from the Help modal's table — keep in
sync rule), plus DG-15/DG-16 built here. REWRITE docs/design/starforge-ui.md → 5-line pointer
to the manual. Acceptance: E21's acceptance-demo script can be executed by a new user from
the manual alone; check_docs_coverage untouched (manual is not API-reference scope).
```
**Status:** ☐

## 14. Phase G — per-phase documentation hooks (D40, standing) *(added 2026-07-04)*

**D40 is not one session — it is a standing checklist** executed as the LAST work order of
every implementation phase (14–21), plus one bookkeeping pass now:

| When phase ships | README (decimal §, note 6) | reference/ chapter rows | systems/ explainer | Manual chapter | Diagrams |
| --- | --- | --- | --- | --- | --- |
| 14 (hardening) | update §8.6 (env live in editor), §16.5 (camera feel) | cameras.md (new orbit semantics), core.md (Log sinks), ui.md (chrome verbs), assets-io (FileDialog) | rendering-pipeline (editor path note) | D37/D38 updates | — |
| 15 (physics) | NEW §9.5 Physics | NEW physics.md chapter + manifest row | NEW physics.md explainer | 08-scripting physics API | DG-18 |
| 16 (platform) | §40 refresh (packaging v2), §1.5 sweep (new flags/keys) | assets-io (mounts, user:// policy) | build-plugin-packaging update | 01/02/10 updates | DG-14 refresh |
| 17 (UI/flow/2D) | NEW §10.5 In-game UI & flow; §8 2D notes | NEW ui-runtime rows (or ui.md §) | ecs-scene update | 07 chapter | DG-17 |
| ↳ **DUE 2026-07-11** (Phase 17 U1–U8 ✅). New surface to document: `scene/ui/` UiComponents+UiSystem, EventBus signals, FlowMachine+`.cflow`+Flow Graph panel, 2D mode + `camera/Camera2DController`, SpriteRenderer TexturePath/YSort + `Scene::OnRenderSprites`, Tilemap + Tile Palette painter, game view (primary cam/aspect/eject), manifest keys `startup_flow`/`pixel_art`/`capture_cursor`, `Window::SetCursorCaptured`, `AssetLibrary::SetDefaultTextureSampling`, vendored `imgui-node-editor` (its VENDOR-NOTES.md), FlowDemo/ForgePong samples, `docs/design/ui-flow-2d-acceptance.md`. | | | | |
| 18 (voxel) | NEW §8.9 Voxels | NEW voxel.md chapter | NEW voxel explainer | 06 update | — |
| 19 (rendering menu) | per-item §8.6/§8.8 notes | rendering-pipeline/world-systems rows | terrain/water/particles updates | 05/06 updates | DG-8 refresh |
| 20 (assets/anim) | §8.5 animation note; import § update | NEW animation rows; assets-io | NEW animation explainer §or doc | 04 update | — |
| 21 (scripting/conn) | §22.7/§26 updates; Lua § if built | serial-telemetry (UDP), audio (positional) | audio/serial updates | 08 update | — |
| 22 (editor shell/branding) | §16.5 camera-rig update (fly/possess); §43 Starforge notes (chrome v2) | core.md (`Window::SetIcon/ClearIcon`, `CommandStack::UndoNameAt/RedoNameAt`), rendering rows (`SetPolygonMode`, `SceneRendererSettings::Wireframe/Outline*`, `Renderer3D::DrawInfiniteGrid`), assets-io (`ImageIO::ReadPixels/ResizeRgba`, `utils/Branding` resolution order), ui.md (`WorkspaceLayer::SetBottomInsetPixels`), scene rows (`ScenePicker` filtered `RenderIdPass` + `GetIdTextureID`) | rendering-pipeline (outline pass 8b note) | D37/D38: branding how-to (drop a PNG at `branding/icon.png`), layout presets, viewport strip/per-op snap, camera modes, view modes, viewport drag-drop | — |
| ↳ **DUE 2026-07-11** (Phase 22 R8 + K1–K13 ✅ code-complete; row queued for the next docs session). | | | | |
| 23 (asset workflows/inspector/hierarchy) | §43 Starforge notes (Content Browser v2, Inspector v2, Hierarchy v2, utility dock) | reflect.md (metadata v2: `FieldUnits`/`.Doc()`/`Field_OmitIfTrue`), assets-io (`AssetLibrary::Enumerate` + `Texture/Mesh::GetGpuBytes` + `Sound::CopyPcm`), core.md (`WindowFileDropEvent`), scene rows (`TagComponent::Active` + `Scene::IsActiveInHierarchy`, per-component `Enabled` gates), jobs (`JobSystem` stats) | ecs-scene (effective-active gate) | D37/D38: Content Browser v2, Inspector v2 (asset slots/copy-paste-reset/enable), Hierarchy v2, console/profiler/jobs dock | — |
| ↳ **DUE 2026-07-12** (Phase 23 T1–T18 ✅ code-complete; row queued for the next docs session). New editor surface: `AssetTypes.h` type table, `ProjectAssets::RetargetPath`, interactive `PreviewRig` consumers, `PropertyRows::DrawAssetSlot`/`SlotContext`, `EditorContext::{PendingImportModel,PendingDroppedFiles,PendingRevealAsset,Playing}`, `LogSource`, `ProfilerPanel`/`SystemPanel`. | | | | |
| 24 (animation editors/material slots) | §8.5 animation note (Animation Editor + crossfade); §5.5-style Materials-array note | scene rows (`SocketComponent` + `Scene::GetWorldTransform` joint composition, `AnimatorComponent::{JointModelMatrices,CrossfadeTo}`, `Mesh` submesh table + `MeshRendererComponent::MaterialPaths`), rendering rows (`RendererAPI::DrawIndexed` offset, `Renderer3D::DrawMesh` index range, `SceneDrawContext::DrawMeshRange`), animation rows (`AnimationClip::BlendLocals`), scripting (`ScriptableEntity::AnimatorProxy`/`Animator()`) | animation explainer (crossfade + sockets); rendering-pipeline (multi-material submesh submit) | D37/D38: **Starforge Animation Editor** (open a rig, skeleton tree, scrub, select joints, socket a prop), material slots on a mesh, script crossfade | — |
| ↳ **DUE 2026-07-12** (Phase 24 M1–M6 ✅ code-complete; row queued for the next docs session). New editor surface: `editors/IAssetEditor`+`AssetEditorHost` (tabbed documents), `widgets/Timeline` (`TimelineState`/`Timeline::Draw`), `editors/AnimationEditor`, `PreviewRig::{RenderSkeletal,ProjectPoint}`, Inspector Materials list + `Commands::SetMaterialSlot`, `AssetTypes` open-in-editor column + "Animation" layout preset, `EditorContext::PendingOpenAnimEditor`. | | | | |
| 25 (graphs/flow variables/story) | NEW §10.6 Flow variables + Story Graph note; §8.6 vignette note | scene rows (`FlowAsset::Variables`/`FlowVariable`/`FlowValue::Enum`, `FlowGuard::Var` + `FlowAction::SetVar`, `FlowMachine::{GetVar,SetVar}` + `EvaluateFlowGuard`, `Scene::ActiveFlow`; NEW `scene/StoryGraph` = `StoryGraph`/`StoryNode`/`StoryOption`/`StoryRunner`), rendering rows (`PostProcessStack::SetVignette*` + `EnvironmentComponent` vignette fields), scripting (`ScriptableEntity::Flow()`) | flow/story explainer (blackboard + dialogue runner) | D37/D38: **Starforge Story Graph editor** (author a branching guarded dialogue, Play preview), flow variables panel, Post Chain view, the `StoryUiBinding` stock script | — |
| ↳ **DUE 2026-07-12** (Phase 25 Q1–Q6 ✅ code-complete; row queued for the next docs session). New editor surface: `editors/FlowEditor` (rehosted M1 doc), `editors/StoryEditor`, `editors/PostChainEditor`, `widgets/VariablesPanel` (`DrawFlowVariablesPanel`/`DrawFlowGuardFields`), `AssetTypes` `.cflow`/`.cstory` editor actions + New ▸ Story, `EditorContext::PendingOpenDocument`, template `StoryUiBinding.h`. | | | | |

Bookkeeping pass now (part of any next docs session): add the table above as tracked rows,
and extend the §11 upkeep contract with rule 4: **"a phase's final work order runs its D40
row"** — the per-phase plan docs' kickoff prompts already end with acceptance/banner
discipline; reviewers enforce the doc row the same way.

**Status (D40 bookkeeping):** ☐

## 15. Kickoff prompt (paste for each implementation session)

> Read `docs/plans/12-documentation-plan.md` §0 fully, then work order **D\<n\>** only. Open
> the skeleton file(s) named in the item — the skeleton's scope list, checklist, and truth
> sources are binding. Verify every signature and behavior claim against the current headers
> and source before writing (checklists are starting points; headers are truth; never
> document parked/unshipped API). Follow the mandatory entry/explainer format from the tier
> index README. Finish with the item's Acceptance, do the note-8 bookkeeping (banners,
> index status cells, this doc's status banner), run `tests\check_docs_coverage.ps1` if it
> exists, and do NOT run any git write command — leave changes in the working tree with a
> one-paragraph summary.
