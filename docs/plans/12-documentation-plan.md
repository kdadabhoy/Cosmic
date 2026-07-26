# Docs Plan v2 — Guide Tier, API Reference, System Explainers (D5–D45)

> **RESTRUCTURED 2026-07-25 (user decisions 1b + 1c).** The README monolith is **split**: a fourth
> tier, **[`docs/guide/`](../guide/README.md)**, takes the per-topic detail, and the root README
> stays a **substantial overview** with a documentation map at the top (1b, reversing decision 1 of
> 2026-07-03). The guide tier is then **written from scratch against the source rather than
> extracted** (1c) — the old README is a quarry, not a source, because a grep of all 4,875 lines
> returns **zero** hits for two dozen major subsystems. Both decisions are recorded with their
> rationale in §1 rather than silently rewritten.
>
> **Phase C is now D46–D61: 29 chapters, 16 serial sessions — ✅ COMPLETE 2026-07-26 (D61).**
> The old D19–D24 are **retired**
> (they assumed the monolith, then assumed extraction); their text is preserved in `<details>`
> blocks in §7's history for provenance. Deriving chapters from the engine rather than from the
> README's table of contents also absorbed the ten subsystems that had no chapter at all.
> Nothing outside §1, §3, §7 and §10 changed.
>
> **Extended 2026-07-25 (Phase 29):** added §15 (**the engine split**, D41–D45) — two new system
> explainers, the previously-missing physics reference chapter, the Phase 29 plan doc's own status
> and deviation record, and the index/pointer updates. **All five are ✅ written**, and D41–D43 are
> complete documents rather than skeletons (see §15). Numbering runs from **D41** because the
> pre-existing work orders end at D40. The kickoff prompt is now §16.
>
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

**Four** tiers since 2026-07-25, each answering one question, mirroring Diátaxis:

| Tier | Question | Home | Shape |
| --- | --- | --- | --- |
| Overview | "What is this, and where do I look?" | root `README.md` | Subsystem tour + doc map + §1.5 commands + §1.6 configurations |
| Developer Guide | "How do I do X?" | `docs/guide/` (index + 16 chapters) | Task-oriented chapters, worked examples |
| API Reference | "What exactly does this call do?" | `docs/reference/` (index + 16 chapters) | OpenGL-man-page-style entries |
| System Explainers | "How does it actually work?" | `docs/systems/` (index + 21 docs) | Plain-English overview → technical implementation |

**Decision log (2026-07-03, user-directed unless noted):**
1. ~~**README stays a monolith** in its current format and grows new sections (user).
   Supersedes doc 06 D3's split.~~ — **REVERSED 2026-07-25 by the user**: *"I would also like a
   better format for documentation than one monolith file if possible… main readme is a large
   overview but at the top has a file tree link to other documentation."*

   **Decision 1b (2026-07-25, user):** a **fourth tier, `docs/guide/`**, takes the per-topic
   detail; the root README stays a **substantial overview** — a tour of every subsystem with
   enough detail to orient and choose, each section linking to its full chapter — and gains a
   **documentation map with a file tree at the top**. `§1.5 Command Reference` and
   `§1.6 The Two Engine Configurations` stay in the README in full: §1.5 carries its own upkeep
   contract (doc 06 D1) and both are linked as `README §1.5` / `§1.6` from across `docs/`.

   This partially reinstates doc 06's struck **D3**, but not as D3 proposed it: D3 split the
   README into `docs/engine-internals.md`, and Part II's internals still go to `docs/systems/`
   under decision 4, not to a second monolith. What changed is **Part I**, which becomes the
   guide tier. Rationale for the reversal, recorded honestly: the README reached **4,875 lines**,
   and one section — §29 Viewport Visibility & Center Docking — was ~~**1,128 lines**, 23 % of the
   file~~. The 2026-07-03 decision was made when the README was 4,795 lines of *2D-era* content;
   the ~10 planned new sections would have pushed it past 7,000.

   > **Correction (D60, 2026-07-26): the §29 figure was wrong and is struck.** At the time this was
   > written §29 ran README lines 3312–3338 — **27 lines**, not 1,128. No section in the file was
   > anywhere near that size; the largest was §26 Telemetry at **365**. (`1128 / 4875 = 23.1 %`, so
   > the two numbers are consistent with each other and with nothing in the file.) **The decision
   > itself stands on the 4,875-line total, which is real and verified** — but do not re-quote the
   > §29 figure, and treat other unverified line-count claims in this section the same way.

   **Decision 1c (2026-07-25, user):** the guide tier is **written from scratch against the
   source**, not extracted from the README. *"It is probably best to just write it all from
   scratch."* The old README is a **quarry** — mine it for anything still good, verify every
   borrowed line against source, rewrite freely around what you keep.

   **Why this replaced the extraction plan, measured rather than asserted.** A grep of all 4,875
   README lines returns **zero** occurrences of `PhysicsWorld`, `CharacterController`,
   `SceneRenderer`, `Terrain`, `ParticleEmitter`, `VoxelVolume`, `NavMesh`, `Animator`,
   `AudioEngine`, `Config::`, `Gamepad`, `FlyCamera`, `OrbitCamera`, `Light2D`, `Tilemap`,
   `UiSystem`, `FlowMachine`, `StoryGraph`, `AssetLibrary`, `ScriptableEntity`, `ScriptHost`,
   `SceneSerializer`, `Prefab` or `CommandStack`. The only post-Phase-13 symbols with any hits at
   all are `Renderer3D` (2), `Water` (3), `RigidBody` (4) and `ThemeManager` (4). Auditing text
   that stale line-by-line costs more than writing correct text, and yields a worse chapter —
   it inherits the old document's shape instead of the engine's.

   **The consequence that made this clearly right:** with chapters derived from the *engine*
   rather than from the README's table of contents, the **ten subsystems with no chapter at all**
   (physics, scripting, UI entities, flow/story, 2D authoring, assets/prefabs, animation,
   navigation, voxels, serialization) stop being an unplanned addendum and simply become chapters.
   The tier goes from 22 planned chapters to **29**, and Phase C from 9 work orders to 16.
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

**Updated 2026-07-25** for decision 1b (the guide tier).

```
README.md                          ← OVERVIEW: doc map + subsystem tour + §1.5 + §1.6
docs/
├── README.md                      ← docs index                        ✅ shipped 2026-07-03
├── guide/                         ← DEVELOPER GUIDE (new tier, 2026-07-25)
│   ├── README.md                  ← index + format + authoring contract  ✅ shipped 2026-07-25
│   └── 29 chapters, written from scratch (D46–D61)  ✅ ALL WRITTEN 2026-07-26:
│        getting-started, project-anatomy, logging-and-diagnostics,
│        events-and-input, time-and-ticks, entities-and-components,
│        scenes-and-serialization, scripting, flow-and-story, rendering-2d,
│        sprites-and-tilemaps, game-ui, materials-and-shaders, cameras,
│        rendering-3d, lighting-and-environment, world-systems, voxels,
│        animation, physics, navigation-and-ai, sim-math-toolkit,
│        assets-and-vfs, audio, serial-and-telemetry, jobs-and-parallelism,
│        windowing-and-viewport, editor-ui-and-theming, building-and-shipping
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
│       + {build-2d-3d-split, physics-backends}.md  ← 2 WRITTEN (D41/D42, Phase 29 W10) = 21 total
├── design/ · engineering-notes/ · plans/ · archive/ · installer-guide.md   ← unchanged
tests/check_docs_coverage.ps1      ← NEW (D5) + a ci.yml step
```

## 4. Diagram inventory (build exactly these; IDs are referenced by skeletons)

| ID | Type | Content | Home(s) | Truth source |
| --- | --- | --- | --- | --- |
| DG-1 | flowchart TD | Module block diagram: host exe → engine DLL (subsystem boxes) → platform/OpenGL; project DLLs plugging in | ✅ **built D46** in [`../guide/getting-started.md`](../guide/getting-started.md#dg-1--how-the-pieces-fit); systems/architecture-overview reuses it | `Cosmic/src/` tree, Cosmic.h |
| DG-2 | classDiagram | Core object model: Application–Window–LayerStack–Layer–ImGuiLayer–WorkspaceLayer–LauncherLayer (+ownership) | ✅ **built D61** in README §30; systems/architecture-overview reuses it | core/*.h, layers/*.h |
| DG-3 | sequenceDiagram | One frame: PollEvents → fixed pass ×N → variable pass → ImGui → swap → Safe Zone | ✅ **built D47** in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-3--the-frame-sequence); systems/core-runtime reuses it | Application.cpp Run loop |
| DG-4 | flowchart TD | Event propagation: OS → Application handlers → overlays→layers with Handled short-circuit + viewport-hover pass-through | ✅ **built D48** in [`../guide/events-and-input.md`](../guide/events-and-input.md#dg-4--event-propagation); systems/events-input reuses it | Application::OnEvent, ImGuiLayer, Window.cpp callbacks |
| DG-5 | sequenceDiagram | Plugin DLL lifecycle: scan → LoadLibrary → InitializePluginContexts → CreatePluginLayer → hooks → delete-before-FreeLibrary | ✅ **built D60** in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-5--the-plugin-dll-lifecycle); README §31 points at it and systems/build-plugin-packaging reuses it | Application.cpp DLL code, Cosmic.h |
| DG-6 | classDiagram | Renderer stack: Renderer2D/Renderer3D/SceneRenderer → RenderCommand → RendererAPI → OpenGL*; resources (Shader/Material/Mesh/Texture) | ✅ **built D61** in README §35; systems/rendering-3d + rendering-2d reuse it | renderer/*, platform/OpenGL/* |
| DG-7 | flowchart LR | 3D submission: DrawMesh → frustum cull → sort key → auto-instance detect → flush (opaque F2B, transparent B2F) | ✅ **built D54** in [`../guide/rendering-3d.md`](../guide/rendering-3d.md#dg-7--what-one-submission-actually-does); reference/rendering-3d + systems/rendering-3d reuse it | Renderer3D.cpp, RenderQueue.h |
| DG-8 | flowchart TD | SceneRenderer pass graph incl. read/write targets: shadow → coverage → reflection → refraction → main HDR → water → particles → post chain → present | ✅ **built D55** in [`../guide/lighting-and-environment.md`](../guide/lighting-and-environment.md#dg-8--the-pass-graph); reference+systems rendering-pipeline reuse it | SceneRenderer.cpp, frame-lifecycle.md |
| DG-9 | classDiagram | ECS: Scene ⇄ entt registry ⇄ Entity handle ⇄ component types (+ which pass consumes which component) | ✅ **built D49** in [`../guide/entities-and-components.md`](../guide/entities-and-components.md#dg-9--scene-registry-entity-components); systems/ecs-scene reuses it | scene/*.h |
| DG-10 | flowchart TD | Time waterfall: rawDelta → global scale → (fixed accumulator \| variable) → layer scale → GetLocalTime; Pause() tap | ✅ **built D48** in [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md#dg-10--the-time-waterfall); systems/core-runtime reuses it | Application.cpp, Layer.h, WorkspaceLayer.cpp |
| DG-11 | stateDiagram-v2 | App states: Launcher ⇄ Workspace(project) with queued Safe-Zone transitions | ✅ **built D47** in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-11--application-states); systems/core-runtime reuses it | Application transition code |
| DG-12 | flowchart LR | Job system: main-thread frame lanes vs worker pool, submit/wait sync points, GL-stays-on-main rule | ✅ **built D59** in [`../guide/jobs-and-parallelism.md`](../guide/jobs-and-parallelism.md#dg-12--where-the-work-actually-runs); systems/jobs-parallelism reuses it | JobSystem.cpp, Scene.cpp |
| DG-13 | flowchart LR | Telemetry: device/sim → SerialPort/SerialLink → framing/decode → channels (columnar) → recorder file ⇄ player → panel/plots | ✅ **built D59** in [`../guide/serial-and-telemetry.md`](../guide/serial-and-telemetry.md#dg-13--the-telemetry-data-path); systems/serial-telemetry reuses it | telemetry/*, serial/* |
| DG-14 | flowchart TD | Packaging: source → Release build → cmake --install staging → dist/<App> prune → zip / Inno installer | ✅ **built D61** in [`../guide/building-and-shipping.md`](../guide/building-and-shipping.md#dg-14--from-source-tree-to-installed-app) (the Phase C precedent: the diagram lives in the guide chapter that owns the topic); systems/build-plugin-packaging reuses it | package.bat, CosmicSetup.iss |
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

**📋 PROMPT — D5**

```
Execute work order D5 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §0 (execution notes) and §5 in full first. This is the enforcement tooling every other
documentation work order depends on — do it before D6-D18 and D25-D34.

WHY THIS MATTERS (read before writing code): Phase C proved the manifest cannot be maintained by
hand. EVERY work order from D52 to D60 found at least one public header with no manifest row, in
FIVE distinct flavours. Your script has to catch all five:
  1. Unlisted outright (e.g. the whole scripting/ and reflect/ tiers).
  2. Reachable only TRANSITIVELY through another header — graphics/Skeleton.h and
     graphics/AnimationClip.h arrive via scene/Components3D.h; voxel/VoxelVolume.h and
     voxel/BlockPalette.h plus nav/NavWorld.h, nav/NavTypes.h and scene/SceneNav.h arrive via
     scripting/ScriptableEntity.h. A one-level scan of Cosmic.h will MISS these.
  3. Reachable only by EXPLICIT include, never from Cosmic.h at all — utils/Branding.h is
     COSMIC_API-exported, unit-tested, and called from a project DLL at
     Projects/Starforge/src/StarforgeApp.cpp:2617. Also voxel/VoxelMesher.h, VoxelGenerator.h,
     VoxelRender.h.
  4. MIS-ROUTED — scene/ScenePicker.h has a row but points at reference/ecs.md, while the chapter
     that actually covers it is guide/cameras.md. Also physics/ScenePhysics.h: no row at all, yet
     reference/physics.md's scope block already names it and its five siblings all have rows.
  5. Included by Cosmic.h DIRECTLY AND UNFENCED with simply no row — utils/FileWatcher.h,
     utils/FileDialog.h, utils/ImageIO.h, utils/ExeResources.h (Cosmic.h:167-170). These four are
     exactly what a Cosmic.h-include table is FOR, which is the strongest argument for the script.
The running list lives in docs/guide/README.md's "Not covered by any reference chapter yet" note
and in each Dnn findings block in §7. Read them; do not re-derive the list by hand.

BUILD IT:
1. NEW tests/check_docs_coverage.ps1, per §5's behaviour spec (steps 1-5). Read
   tests/check_gl_conformance.ps1 FIRST for the repo's PowerShell conventions and copy its output
   format. PowerShell 5.1 only: no ternary, no ??, no -AsHashtable, no && chains; read with
   -Raw -Encoding UTF8.
2. CRITICAL: parse Cosmic.h WITH its #ifndef COSMIC_2D_ONLY fences. A naive parse reports every 3D
   header as missing when run in a 2D tree. And "inside a fence" is NOT the test for 3D-only —
   camera/NavigationCube.h is UNFENCED but its .cpp is filtered out of the 2D build, so it compiles
   and fails at LINK time. Classify by the CMake list(FILTER) block in Cosmic/CMakeLists.txt
   (lines ~178-210), not by the fence.
3. Decide and document how you handle flavours 2 and 3. Following transitive includes is the
   honest fix; if you scope this pass to direct includes only, then flavour-3 headers need an
   explicit allowlist in the script and you must say so in the acceptance note. Do not silently
   ignore them.
4. EDIT .github/workflows/ci.yml — one step, mirroring the existing check_gl_conformance.ps1
   step's placement and style. YAML must stay valid.

ACCEPTANCE: runs clean on the current tree (every chapter still carries STATUS: SKELETON except
reference/physics.md, so skeletons WARN and exit stays 0); deleting a manifest row makes it exit 1;
it reports all five flavours above against today's tree. PROVE IT IS NON-VACUOUS the way Phase 29 W9
did — delete a row, run it, record the observed failure, restore.

Then update: §5's Status to ✅ + date, docs/reference/README.md's manifest with every row the script
finds missing (that is the point of running it), and doc 12 §7's guide-index note if it closes gaps.
Report what it found. No git write commands — leave edits in the working tree.
```

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

### 📋 Copy-paste prompts (D6 → D18)

**Standing preamble — every D6–D18 prompt inherits it.** Restated in the shared block below rather
than in all thirteen.

```
STANDING RULES for any D6-D18 reference chapter (read once, they apply to every item):

1. Read docs/plans/12-documentation-plan.md §0 and §6, then your chapter's skeleton file. The
   skeleton's scope list, coverage checklist and named traps are binding — but HEADERS ARE TRUTH.
   A checklist row naming an API you cannot find means the checklist is wrong; fix the row.
2. Read every scope header END TO END and enumerate the real public surface: COSMIC_API classes and
   structs, free functions, macros, enums. Then read the .cpp, then the tests. Never write from a
   comment — DataRecorder.cpp:257 still says "v3 format" three lines above `version = 1u`.
3. Use the mandatory entry template from
   docs/reference/README.md#entry-format-mandatory--copy-this-shape. Signatures copied VERBATIM
   from the header, never paraphrased.
4. **THE GUIDE TIER IS WRITTEN AND IS NOT YOURS TO REPEAT.** All 29 chapters in docs/guide/ landed
   in Phase C (D46-D61), written from source. Your chapter is the formal per-call lookup BEHIND the
   guide chapter: signature, exact behaviour, failure mode, pitfalls. The guide owns usage, idiom
   and worked walkthroughs. Most skeletons already carry a "Read first / don't re-derive" note
   naming their guide chapter — follow it, link it, and do not fork it.
5. State the failure mode for every call that can fail. Cosmic's conventions vary ON PURPOSE:
   Shader::Create returns nullptr; Texture2D::Create returns a DEGRADED non-null 0x0 object that is
   then CACHED by AssetLibrary until Reload; some calls log and continue. Say which, every time.
6. State the build configuration where it matters (README §1.6). "3D only" is PER HEADER, not per
   chapter — the manifest marks fenced headers with ³ᴰ.
7. Diagrams: §4 assigns them, and THIRTEEN ARE ALREADY BUILT (DG-1 through DG-14 except DG-15/16).
   Reuse by link; never redraw. Only build one if §4 assigns it to YOUR chapter and it is unbuilt.
8. Bookkeeping (execution note 8): delete the STATUS: SKELETON banner, flip your row in
   docs/reference/README.md from "SKELETON — D#" to "✅ YYYY-MM-DD" (touch ONLY your row — the file
   is shared), and set your item's status in doc 12 §6 with a one-line result.
9. Run tests/check_docs_coverage.ps1 if D5 has landed — removing your banner turns on strict mode,
   so every COSMIC_API class name in your scope headers must appear in your chapter text.
10. Report: what the chapter covers · what you found that contradicts the skeleton, a comment or a
    header docstring · what you deliberately left out and why · anything unverifiable. Add any
    engine defect you find to §7's findings log as a Phase 30 candidate, with its file:line.
11. No git write commands. Leave edits in the working tree.
```

Then one line per item:

```
Execute work order D6 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/core.md. Scope: core/Application.h, core/Layer.h, core/LayerStack.h,
core/Window.h, core/Log.h, core/Core.h, core/Timestep.h, core/CommandStack.h, core/UUID.h and the
plugin-export boundary in Cosmic.h. Condense README §7's Pause-vs-TimeScale table into per-entry
form. TWO HEADER DOC-COMMENTS ARE WRONG AND D60 VERIFIED BOTH: Application.h:93 documents
GetViewportPos/GetViewportSize as "GLFW window-space pixels" but the value is
ImGui::GetCursorScreenPos() recorded in WorkspaceLayer.cpp:214-215, i.e. ImGui SCREEN (desktop)
pixels — the space Input::GetMouseScreenPosition() lives in; and SetPauseOnMinimize defaults to
FALSE. Also: CS_ASSERT/CS_CORE_ASSERT are compiled out in EVERY configuration (gated on
GLCORE_DEBUG||CS_DEBUG, neither defined anywhere), so document no guard as enforced. Guide
chapters: project-anatomy.md, time-and-ticks.md, windowing-and-viewport.md, logging-and-diagnostics.md.
```

```
Execute work order D7 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/events-input.md. Scope: events/*.h (Event, ApplicationEvent, KeyEvent,
MouseEvent), core/Input.h, codes/KeyCodes.h, codes/MouseButtonCodes.h, codes/GamepadCodes.h.
Render the three code headers as full tables — that is the chapter's centerpiece. Pin the mouse-space
contract exactly: GetMousePosition() is WINDOW-CLIENT relative, GetMouseScreenPosition() is desktop
space, and Input.h:57-63 says the two "only match by luck when the window sits at the desktop
origin". There are NO CS_MOD_*/CS_ACTION_* constants — the fullscreen hotkey override is the one
place raw GLFW action/mods reach client code, and GLFW_MOD_CONTROL is 0x0002 while GLFW_MOD_ALT is
0x0004. WindowCloseEvent is marked Handled before the layer walk, so there is no client veto.
Guide chapter: events-and-input.md (DG-4 built there — reuse).
```

```
Execute work order D8 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/graphics-resources.md. Scope: graphics/Shader.h, Texture.h, TextureCube.h,
Material.h, MaterialAsset.h, Buffer.h, VertexArray.h, UniformBuffer.h, StorageBuffer.h,
FrameBuffer.h, SubTexture2D.h, Font.h, Gizmo.h, renderer/BindingPoints.h. THE BINDINGPOINTS TABLE IS
LOAD-BEARING — other chapters link it, so it lives here once, in full. State Material::Clone and the
material-read-at-flush semantics HERE once and link them from rendering-2d/3d. Failure modes differ
per factory and that difference is the chapter's most useful content: Shader::Create returns nullptr,
Texture2D::Create returns a degraded non-null 0x0 object. Note that Shader::Create does NOT resolve
VFS paths. FramebufferSpecification::Samples and SwapChainTarget are RESERVED and have no effect.
Guide chapter: materials-and-shaders.md.
```

```
Execute work order D9 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/rendering-2d.md. Scope: renderer/Renderer2D.h, RenderPass.h,
renderer/Light2DRenderer.h. EVERY DrawQuad/DrawRotatedQuad/DrawCircle OVERLOAD GETS ITS OWN ENTRY —
that is the OpenGL-man-page style the user asked for; do not collapse them into one entry with a
parameter table. Document every batch limit with its flush behaviour (MaxQuads/MaxLines/MaxCircles/
MaxTextQuads = 10000, MaxTextureSlots = 32, MaxInstancedQuads/MaxInstancedCircles = 20000). Two traps
to state plainly: Renderer2D::StatsEnabled defaults FALSE so stats read zero unless SetStatsStatus(true)
was called, and Flush() is public but does not reset counters, so a client call draws twice.
DrawInstancedQuads never populates the texture-slot table. Guide chapter: rendering-2d.md.
```

```
Execute work order D10 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/rendering-3d.md. XL — SPLIT INTO TWO SESSIONS if needed (submission + queue
semantics first, then Mesh/Model/InstanceSet/Frustum). Use a stronger model: the deferred-flush
semantics must be EXACTLY right. Scope: renderer/Renderer3D.h, RenderQueue.h, renderer/InstanceSet.h,
graphics/Mesh.h, graphics/Model.h, math/Frustum.h. THE CENTRAL FACT is material-read-at-flush:
material state is read when the queue flushes, not when you submit, so mutating a shared Material
between submit and flush retroactively changes earlier draws — Material::Clone is the fix. Note the
configuration split is PER HEADER: Renderer3D, InstanceSet and Model are filtered out of the 2D build
AND fenced in Cosmic.h; graphics/Mesh.h and header-only math/Frustum.h are unfenced and compile in a
2D tree. Nothing in the engine calls Renderer3D::ResetStats, so unreset counters read as lifetime
totals. DG-7 is ALREADY BUILT in guide/rendering-3d.md — reuse it, do not redraw.
```

```
Execute work order D11 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/rendering-pipeline.md. Scope: renderer/SceneRenderer.h,
renderer/PostProcessStack.h, renderer/EnvironmentMap.h, renderer/ShadowMap.h,
renderer/CoverageCapture.h, renderer/CameraUniforms.h. Enumerate SceneRenderer's REAL header surface;
the skeleton deliberately does not guess it. THE CONFIGURATION STORY IS SUBTLE AND D55 GOT IT RIGHT:
SceneRenderer and PostProcessStack ship in BOTH configurations — a 2D frame runs the same
BeginHDR -> DrawTransparent -> tonemap/FXAA/bloom/vignette -> DrawOverlay2D spine, which is why
docs/design/frame-lifecycle.md §5 holds verbatim on both engines. What fences out is EnvironmentMap,
ShadowMap, CoverageCapture, desc.Lights, the routed DrawOpaque and the world-content half of
SceneRenderDesc. BuildRenderDesc ignores WaterComponent::Enabled and ParticleEmitterComponent::Enabled
— a real defect, log it. Summarize and LINK frame-lifecycle.md; never restate the spec (note 7).
DG-8 is ALREADY BUILT in guide/lighting-and-environment.md — reuse.
```

```
Execute work order D12 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/world-systems.md. Scope: terrain/Terrain.h, water/Water.h,
water/GerstnerWave.h, water/Presets.h, particles/ParticleSystem.h, particles/Presets.h,
scene/WorldSystemRecipes.h. Pin the Terrain resolution rule (32·2^k+1) and the SampleHeight
tolerance with their TEST citations, not prose assertions. NOTE TWO MANIFEST GAPS D55 FOUND that
this chapter should close: scene/WorldSystemRecipes.h (the E18 recipe->spec layer every
scene-authored terrain, water body and emitter goes through) and water/Presets.h have NO manifest
row — particles/Presets.h does. Terrain::Create and Water::Create are pure CPU; GL is allocated
lazily in EnsureGpuResources on first render, which is what makes async world building possible.
All 3D-only. Guide chapter: world-systems.md (the only other documentation of these subsystems).
```

```
Execute work order D13 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/ecs.md. Scope: scene/Scene.h, Entity.h, Components.h, Components3D.h,
System.h, ComponentRegistry.h, SelectableComponent.h, scene/ScenePicker.h. THE COMPONENTS.H
FIELD-BY-FIELD TABLE IS THE CENTERPIECE. Two things to get right: there is NO Entity::GetUUID() —
use GetComponent<IDComponent>().ID.Value(); and Scene::OnUpdate/OnFixedUpdate HAVE NO ENGINE CALLER
(the only in-tree callers are TemplateTelemetryLayer.cpp:305,313), so the entire four-pass pipeline
inside them only runs if a project calls it — state that as a boxed warning, not a footnote.
ScenePicker.h IS in the manifest but MIS-ROUTED here; the chapter that covers it is guide/cameras.md
— re-point the row. Guide chapter: entities-and-components.md (DG-9 built there — reuse).
```

```
Execute work order D14 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/cameras.md. Scope: camera/Camera.h, PerspectiveCamera.h,
OrthographicCamera.h, OrthographicCameraController.h, OrbitCameraController.h,
FlyCameraController.h, Camera2DController.h, NavigationCube.h. Hotkey tables from
OrbitCameraController plus the Engine3DDemo bindings. Camera2DController.h has NO MANIFEST ROW (D52)
— it is the only controller of the six missing; add it. CONFIGURATION: every camera and controller
ships in BOTH builds, but NavigationCube and ScenePicker are filtered out of the 2D build and they
fail DIFFERENTLY — ScenePicker's include is fenced in Cosmic.h, NavigationCube's is not, so the
latter compiles and fails at LINK time. Say so. Guide chapter: cameras.md.
```

```
Execute work order D15 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/math.md. Scope: math/Spatial.h, Integrators.h, Filters.h, LookupTable.h,
Noise.h, Random.h, Frustum.h — all header-only, all unfenced, both configurations. Cite the doctest
per header rather than asserting behaviour. Include a DETERMINISM box: which helpers are
reproducible across runs and platforms and which are not, and that fuzz/sim work must seed
explicitly and never use random_device. Note the doctest gotcha for anyone verifying: doctest's
Approx is RELATIVE, not absolute. Guide chapter: sim-math-toolkit.md (currently the only written
documentation of this tier — systems/math-sim-toolkit.md is still a skeleton).
```

```
Execute work order D16 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapters: docs/reference/assets-io.md AND docs/reference/audio.md — two small chapters, one session.
Scope: assets/AssetLibrary.h, assets/MeshImport.h, utils/FileSystem.h, utils/Config.h,
utils/DataExport.h, utils/FileWatcher.h, utils/FileDialog.h, utils/ImageIO.h, utils/ExeResources.h,
utils/Branding.h; audio/AudioEngine.h, audio/Sound.h. THE LAST FIVE UTILS HEADERS HAVE NO MANIFEST
ROW — FileWatcher/FileDialog/ImageIO/ExeResources are included by Cosmic.h DIRECTLY AND UNFENCED
(lines 167-170) and Branding.h is COSMIC_API-exported but not in Cosmic.h at all. Add all five.
Verify the VFS dev-vs-packaged path examples against Runtime/Main.cpp AND FileSystem.cpp: the
user:// root depends on BOTH whether SetAppIdentity ran (only boot.cfg sets it — never --project)
and a live writability probe of the exe dir. THE OLD "DLL-side resolution rule" IS OBSOLETE — the
mount moved into the engine DLL in Phase 20/A1, so there is one active project per PROCESS; four
in-tree comments still teach the old rule, do not carry it forward. A failed texture load is CACHED
as a degraded object; failed shaders/meshes/materials are not. AssetLibrary has NO LOCKING of any
kind and constructs GPU resources in its getters, so it is strictly main-thread. Config's getters
log NOTHING on a type mismatch despite Config.h:64-65 promising they do. .ogg is listed in
Starforge's audio row but miniaudio has no Vorbis decoder compiled in. Guide chapters:
assets-and-vfs.md, audio.md, building-and-shipping.md (owns ExeResources).
```

```
Execute work order D17 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapters: docs/reference/serial-telemetry.md AND docs/reference/jobs.md — L, two chapters. Scope:
serial/SerialPort.h, SerialLink.h, Framing.h; telemetry/TelemetryChannel.h, DataRecorder.h,
DataPlayer.h, TelemetryPanel.h, EntitySelection.h, EntityPicker.h; jobs/JobSystem.h,
ParallelSystem.h, SystemQuery.h, ParallelFor.h, DoubleBuffer.h, ComponentArray.h. ALL FIFTEEN HAVE
CORRECT MANIFEST ROWS (D59 verified) — no gap work here. THREADING CONTRACTS ARE THE HARD PART;
verify against the .cpp, never the comment. Facts D59 established: JobSystem::WaitIdle() is a GLOBAL
barrier, not per-caller, which is why ParallelSystem::OnParallelExecute forbids it and why
ParallelFor (which calls it internally) inherits the hazard. ParallelForAsync runs SYNCHRONOUSLY
below minChunkSize (default 64) or on a single-worker machine, so a by-reference capture is safe
there and dangles above it — the static_assert only catches move-only functors. ComponentArray<T>::From
returns an EMPTY view once the pool spans more than one EnTT page; FlatComponentArray<T> is the fix.
ReadWriteQuery::Commit's structural-change guard does not exist in any build. DataRecorder::Flush and
DataPlayer::Load do NOT resolve VFS paths — Flush("user://takes") silently creates a literal "user:"
directory. SerialPort::Write IS implemented (SerialPort.cpp:231), contrary to the old README.
SerialLink's 3-second auto-reconnect can silently switch you to a DIFFERENT COM port. Framing::
EncodeFrame drops an oversized frame in total silence. DG-12 and DG-13 are ALREADY BUILT in the guide
— reuse. Guide chapters: jobs-and-parallelism.md, serial-and-telemetry.md.
```

```
Execute work order D18 from docs/plans/12-documentation-plan.md. Apply the D6-D18 standing rules.
Chapter: docs/reference/ui.md. Scope: ui/Fonts.h, ThemeManager.h, Theme.h, Widgets.h, PlotStyle.h,
Overlay.h, IconsLucide.h, layers/ImGuiLayer.h, layers/ImGuiThemes.h, layers/WorkspaceLayer.h. The
DockPort table plus the NEVER-PERSIST-DOCK-NODE-IDS rule. layers/ImGuiThemes.h HAS NO MANIFEST ROW
(D60) — it is the home of enum class ImGuiTheme, the parameter type of the exported
ImGuiLayer::SetTheme / Cosmic::SetImGuiTheme overloads, plus GetBuiltInThemes() and NameForTheme();
add it. WorkspaceLayer is NOT COSMIC_API-exported — only its INLINE members are reachable from a
project DLL (DockWindow, SetViewportVisible, SetBottomInsetPixels, BeginViewportOverlay);
SetViewportLayer/ClearViewportLayer and the hook overrides are engine-internal and will not link.
Say so per entry. SetEdgeRatios(left,right,top,bottom) and SetEdgeMinPixels(top,bottom,left,right)
take their edges in DIFFERENT ORDERS. ShowThemeSelector's DockPort parameter is DEAD. Fonts::Get
IGNORES its sizePx argument entirely (selection is by name; size applies at draw time) and falls
back to the default face for an unknown name. Fonts::Init assigns io.FontDefault = Roboto-Regular,
so custom faces are the DEFAULT, not opt-in. ThemeManager::SaveToFile/LoadFromFile/LoadFolder all
take RESOLVED DISK PATHS, not VFS paths. Guide chapter: editor-ui-and-theming.md.
```

---

## 7. Phase C — the guide tier, written from scratch (D46–D61, **serial**)

> **REWRITTEN TWICE, 2026-07-25.** Originally this phase grew the README monolith with ~10 new
> Part I sections (decision 1). Decision 1b split the README into a `docs/guide/` tier and made
> Phase C an *extraction*. **Decision 1c — the current plan — makes it an authoring phase: every
> chapter is written from scratch against the source, and the old README is a quarry rather than a
> source.** Rationale in §1. The original D19–D24 work orders are **retired**; the new numbers are
> **D46–D61**, continuing past D45 so nothing collides.

**Read [`../guide/README.md`](../guide/README.md) first.** Its *authoring contract* (five rules +
verification bar), *document format*, and *chapter table* are binding on every item below.

### Why from scratch, in one paragraph

The README's client guide describes the Phase 1–13 engine. Sixteen phases landed after it, and a
grep across all 4,875 lines returns **zero** hits for `PhysicsWorld`, `CharacterController`,
`SceneRenderer`, `Terrain`, `ParticleEmitter`, `VoxelVolume`, `NavMesh`, `Animator`, `AudioEngine`,
`Config::`, `Gamepad`, `FlyCamera`, `OrbitCamera`, `Light2D`, `Tilemap`, `UiSystem`, `FlowMachine`,
`StoryGraph`, `AssetLibrary`, `ScriptableEntity`, `ScriptHost`, `SceneSerializer`, `Prefab` and
`CommandStack`. Auditing text that stale, line by line, costs more than writing correct text — and
produces a worse chapter, because it inherits the old document's shape instead of the engine's.
Deriving the chapter list from the engine also closes the gap the extraction plan could not: ten
subsystems that had no chapter at all now have one.

### Standing rules every D46–D61 prompt inherits

Restated in each prompt so they stand alone, but they are all the same:

1. **Read `docs/guide/README.md` in full first** — authoring contract, verification bar, format.
2. **Write from the headers**, then the `.cpp`, then the tests. Never from comments (the telemetry
   docstrings say "v3"; the code writes v1). A claim you cannot point at in source does not ship.
3. **Mine the old README** for anything still good — explanations, examples, pitfalls, tables —
   but **verify every borrowed line against source** before reusing it. Rewrite freely around what
   you keep.
4. **Model examples on real usage** in `Projects/` (template, Engine3DDemo, Frontier, ForgePong,
   ForgeIsle, ViperSim, SF_Telem), not on imagination. Every example must compile against the
   current API.
5. **State failure behaviour** for every call that can fail, and **state the build configuration**
   where it matters (root README §1.6).
6. **Retire the README sections your chapter replaces** in the same work order: body becomes a
   *newly written* 2–4 paragraph overview + link; heading and number stay; update the ToC, both
   index tables, and every inbound `README §<n>` link in `docs/`.
7. **Report:** what the chapter covers · **what the old README got wrong or omitted** · what you
   deliberately left out and why · anything you could not verify.
8. **No git write commands.** Leave edits in the working tree.

**Serial, not parallel.** Every item touches the README ToC and overview.

### The item map

| WO | Prompt | Chapters | Retires | Size | Status |
| --- | --- | --- | --- | --- | --- |
| **D46** | 1 | README overview rewrite + `getting-started.md` | §1 | M | ✅ 2026-07-25 |
| **D47** | 2 | `project-anatomy.md`, `logging-and-diagnostics.md` | §2, §3, §4, §19 | M | ✅ 2026-07-25 |
| **D48** | 3 | `events-and-input.md`, `time-and-ticks.md` | §5, §6, §7 | M | ✅ 2026-07-25 |
| **D49** | 4 | `entities-and-components.md` | §15 | **XL — the component catalogue** | ✅ 2026-07-25 |
| **D50** | 5 | `scenes-and-serialization.md`, `scripting.md` | §23, §21 | L | ✅ 2026-07-25 |
| **D51** | 6 | `rendering-2d.md`, `materials-and-shaders.md` | §8–§14, §9, §10, §18 | L | ✅ 2026-07-26 |
| **D52** | 7 | `sprites-and-tilemaps.md`, `game-ui.md` | — | M | ✅ 2026-07-26 |
| **D53** | 8 | `flow-and-story.md`, `cameras.md` | §16 | M | ✅ 2026-07-26 |
| **D54** | 9 | `rendering-3d.md` | — | **XL, stronger model** | ✅ 2026-07-26 |
| **D55** | 10 | `lighting-and-environment.md`, `world-systems.md` | — | **L, stronger model** | ✅ 2026-07-26 |
| **D56** | 11 | `voxels.md`, `animation.md` | — | L | ✅ 2026-07-26 |
| **D57** | 12 | `physics.md`, `navigation-and-ai.md` | — | L | ✅ 2026-07-26 |
| **D58** | 13 | `assets-and-vfs.md`, `audio.md`, `sim-math-toolkit.md` | §17 | L | ✅ 2026-07-26 |
| **D59** | 14 | `serial-and-telemetry.md`, `jobs-and-parallelism.md` | §20, §26, §22 | L | ✅ 2026-07-26 |
| **D60** | 15 | `windowing-and-viewport.md`, `editor-ui-and-theming.md` | §24, §29, §27, §28 | L | ✅ 2026-07-26 |
| **D61** | 16 | `building-and-shipping.md` + README Part II pass + §1.5 sweep + final link sweep | §40, §25, §30–§43 pass | L | ✅ 2026-07-26 |

**PHASE C IS CLOSED (2026-07-26).** All 29 chapters written, all 16 work orders done.

**29 chapters, 16 sessions.** D49 and D54 are the two that should get a stronger model or your
review; D55 is close behind.

**D46 ✅ 2026-07-25.** `docs/guide/getting-started.md` written from source (DG-1 built); README §1's
body replaced with a four-paragraph overview + chapter link, heading and number kept; §1.5/§1.6
untouched; ToC, "Most-asked pages", the guide index (which gained the **Status** column it was
missing) and the three inbound `README §1` pointers in `docs/` updated. **Findings that matter to
D47–D61**, because they are all in text those work orders will inherit: `setup.bat` sets
`COSMIC_SDK`, **not** `COSMIC_SDK_DIR` (the old §1 named the wrong variable); the Launcher's
generator button is *"New C++ plugin (advanced)"* and is compiled out of Release; the three
`"Project Inspector …"` magic dock names are **dead** — port-mode `DockWindow(name, DockPort::…)`
replaced them and §1's whole naming-convention subsection was wrong; `Layer::OnRender()` is declared
but **never called** by `Application::RenderSingleFrame`; and per-app `user://` isolation arms only
on a `boot.cfg` identity, so `package.bat <AppName>` dists (which boot via `--project`) do **not**
get it. Two stale source comments found on the way: `Application::LoadProjectDLL` step 5 still
claims `FileSystem` is header-only with per-DLL state (the A1 fix moved it into the engine DLL), and
`ModuleMacros.h` says "two exports" where the macros expand to three.

**D47 ✅ 2026-07-25.** `docs/guide/project-anatomy.md` (**DG-3** + **DG-11** built) and
`docs/guide/logging-and-diagnostics.md` written from source; README §2/§3/§4/§19 bodies replaced with
newly-written overviews + chapter links, headings and numbers kept; ToC, "Most-asked pages", the
guide index and six inbound pointers in `docs/` updated (`reference/core.md` ×3, `reference/ecs.md`,
`reference/graphics-resources.md`, `systems/build-plugin-packaging.md`, `systems/core-runtime.md` ×4).
**Findings that matter to D48–D61:**

- **`CS_ASSERT` / `CS_CORE_ASSERT` / `GLCORE_ASSERT` are compiled out in EVERY configuration.**
  `Core.h` gates them on `GLCORE_DEBUG || CS_DEBUG`, and **neither symbol is defined anywhere** in the
  tree — no CMake target, no source file. So `LayerStack`'s `!m_Iterating` guards and `Clear()`'s
  "all detached first" precondition never fire; mid-iteration mutation is silent UB. Any chapter that
  was going to say "the engine asserts if you…" must not.
- **`SetPauseOnMinimize` defaults to `false`, not `true`** — the old §3 said the opposite. The engine
  keeps ticking while minimized (suits sims/telemetry); the Safe Zone runs regardless.
- **The "Project Inspector" magic dock names are LEGACY, not dead** — refining D46's finding.
  `BuildDockspace` honours them **only when a project registers zero `DockWindow` bindings**, and the
  middle slot is `"Project Inspector Mid"` — bare `"Project Inspector"` is just the shell's
  no-project placeholder window. The template project still relies on the legacy path. Matters to
  D60.
- **`WorkspaceLayer` is NOT `COSMIC_API`-exported.** Only its *inline* members are reachable from a
  project DLL (`DockWindow`, `SetViewportVisible`, `SetBottomInsetPixels`, `BeginViewportOverlay`, …);
  `SetViewportLayer`/`ClearViewportLayer`/the hook overrides are engine-internal and won't link.
  Matters to D60.
- **A plugin layer is never on the engine `LayerStack`.** `WorkspaceLayer::SetViewportLayer` calls its
  `OnAttach` and forwards every hook; only the shell itself is pushed. Any claim about "your layer on
  the stack" is wrong.
- **Release has no console.** `Runtime/CMakeLists.txt` links both hosts `/SUBSYSTEM:WINDOWS` +
  `/ENTRY:mainCRTStartup` in Release, so spdlog's console sink writes nowhere and the log file is the
  only output. Matters to every "check the console" instruction.
- **Renderer stats are asymmetric traps.** `Renderer2D::StatsEnabled` defaults **false** and only
  `StarforgeApp` (inside `#ifdef COSMIC_2D_ONLY`) ever calls `SetStatsStatus`; `Renderer3D` has no
  flag but **nothing in the engine calls either `ResetStats`**, so unreset 3D counters read as
  lifetime totals. Matters to D51/D54.
- **The shipped samples redirect logs to unwritable roots.** `TemplateProject`, `Frontier` and
  `SF_Telem` all call `Log::SetLogDirectory(Resolve("project://logs"))` on attach and `"logs"`
  (bare, CWD-relative) on detach — both fail once installed. Only `StarforgeApp` uses `user://logs`.
  Don't copy the samples' pattern into D58's VFS chapter.
- **`Cosmic::CreateApplication()` is a dead declaration** in `Application.h` — never defined, never
  called. `Main.cpp` constructs `Application` directly.
- **GPU zone results lag 1–3 frames by design** and only exist if something calls
  `SceneRenderer::Render` (which owns the only `GpuFrameMark`). Matters to D55.
- Two more stale source comments, on top of D46's: `SceneManager.h` is right that it is "owned +
  ticked by whoever runs the frame" — that owner-ticked pattern (SceneManager, SerialLink, ScriptHost,
  PhysicsWorld, SceneRenderer, FlowMachine) had **no client documentation at all** and is now written
  up; and `docs/README.md` still described Phase C as an extraction tracked as "D19–D24" (fixed here).

**D48 ✅ 2026-07-25.** `docs/guide/events-and-input.md` (**DG-4** built) and
`docs/guide/time-and-ticks.md` (**DG-10** built) written from source; README §5/§6/§7 bodies replaced
with newly-written overviews + chapter links, headings and numbers kept (the three sections were 397
lines; the README is now 4,345). ToC, the guide index (5 of 29 written) and six inbound pointers
updated: `reference/core.md`, `reference/events-input.md` ×2, `systems/core-runtime.md` ×4,
`systems/events-input.md` ×3, `design/responsive-rendering-and-pause.md`. All three code tables were
regenerated from `codes/` — the old §6 tables were a 14-row *sample* of 120 key codes and had no
gamepad section at all. **Findings that matter to D49–D61:**

- **The fixed `dt`'s magnitude is NOT scaled by `TimeScale`** — the old §7 table said
  `(1/60) × globalScale` twice. `Application.cpp` scales the *accumulator*, so the scale changes how
  **often** the fixed pass fires while `dt` stays exactly `±1/FixedHz`. That is the correct design
  (fixed-step solvers need a constant step) and the reference/physics contract depends on it.
- **A negative `TimeScale` does not rewind the fixed pass — it stalls it, and leaves a debt.** The
  drain loop is `while (accumulator >= fixedDt)` while a negative scale drives the accumulator
  *downward*, so `OnFixedUpdate` never fires and the `signedFixedDelta` negative branch is
  **unreachable**. The accumulator is never clamped or reset (three references tree-wide), so N
  seconds of rewind costs N seconds of forward time before fixed updates resume. The old §7's
  "optional rewind behavior" example in `OnFixedUpdate` is dead code, and
  `design/responsive-rendering-and-pause.md` line 172's "never collides with rewind" is optimistic.
  Left the design doc alone — it is a decision record.
- **`if (dt == 0.0f) return;` in `OnFixedUpdate` is dead for a stack layer** but *live* for a plugin
  layer: `WorkspaceLayer::OnFixedUpdate` multiplies by `m_ClientViewportLayer->GetTimeScale()`. Same
  asymmetry as `ts` (README §32 has this right, and it is the one Part II section that does).
- **Nothing feeds `u_Time`.** The old §7 claimed shaders reading `u_Time` receive `GetAbsoluteTime()`.
  The preprocessor *declares* it for Shadertoy-style sources and four engine subsystems set it on
  their own shaders; there is no global upload for client materials. Matters to D51.
- **`F11` never becomes an `Event`.** `Window::HandleFullscreenHotkey` runs inside the GLFW key
  callback, before any `Event` object is constructed, and consumes F11 — and offers every keystroke
  to `SetFullscreenHotkeyOverride` first. Matters to D60.
- **`WindowCloseEvent` never reaches a layer.** `Application::OnWindowClose` returns `true`, so it is
  `Handled` before the stack walk. There is no client-side veto on window close.
- **Three `EventType` enum values have no class**: `WindowFocus`, `WindowLostFocus`, `WindowMoved`.
  Nothing constructs or dispatches them. The D7 reference checklist's "app tick/update/render events
  if public" row should be answered *no*.
- **`KeyTypedEvent::GetKeyCode()` returns a Unicode codepoint**, not a `CS_KEY_*` value, and
  `GetRepeatCount()` is a 0/1 flag rather than a counter (`Window.cpp` passes literals). The old §5
  table listed both without the distinction.
- **Events carry no modifier bits.** There are no `CS_MOD_*` or `CS_ACTION_*` constants anywhere;
  chords are polled, and the only place raw GLFW `action`/`mods` reach client code is a
  fullscreen-hotkey override. Matters to D60's hotkey material and any §1.5 hotkey sweep.
- **`MouseMovedEvent` coordinates are window-client pixels**, not "screen-space" as the old §5 table
  said — the `GetMousePosition()` space, not the `GetMouseScreenPosition()` / `GetViewportPos()` one.
- **The spiral clamp is on the frame time, not the accumulator** — README §32's code sketch has this
  backwards, states a hard `1/60` where the rate is configurable, and shows `OnUpdate` before
  `UpdateLayerTime` where the code calls them the other way round. Noted in `systems/core-runtime.md`
  for D26; §32 itself is Part II and was left untouched.
- Gamepad support is Phase 2-era and had **zero** README mentions. Worked examples exist in the tree
  and were mined: `WalkController.h` (threshold deadzone), `SimHub.cpp` (rescaled deadband, flies the
  whole simulator off four axes), `TemplateProject.cpp` (the live axis readout for unmapped devices).

**D49 ✅ 2026-07-25.** `docs/guide/entities-and-components.md` written from source (**DG-9** built) —
the entity handle, **the full 34-component catalogue** (fields · units · defaults · *who consumes it*,
read exhaustively out of `Components.h` + `Components3D.h` + both `TypeRegistry*.cpp`), what a 2D
build sees, hierarchy, the `Active`/`Enabled` gates, queries, the `System`/`ParallelSystem`/
`SystemQuery` tier, the automatic-draw contract, and custom components. README §15's body replaced
with a newly-written four-paragraph overview + chapter link, heading and number kept (§15 was 103
lines). ToC, "Most-asked pages", the guide index (6 of 29) and four inbound pointers updated:
`reference/ecs.md`, `reference/physics.md`, `systems/ecs-scene.md` (which also gained a
*don't re-derive the catalogue* note), `systems/physics-backends.md`. The Phase 29 split table was
**not** re-derived — `systems/ecs-scene.md` is linked. **Findings that matter to D50–D61:**

- **`Scene::OnUpdate` and `Scene::OnFixedUpdate` have no callers** in `Cosmic/src`, `Projects/` or
  `tests/`. `PlayerLayer` and Starforge tick `ScriptHost`, `UpdateSpriteAnimations` and
  `UpdateAnimators` **directly**. The only in-tree caller is
  `Cosmic/templates/ExampleProject/src/TemplateTelemetryLayer.cpp:313`. So a registered `System`
  never runs unless the scene's owner ticks it — and `Scene::AddSystem` has exactly one call site
  tree-wide. Matters to D59 (`jobs-and-parallelism.md`), whose §"parallel systems" must not imply
  the engine drives them.
- **`Scene::OnRender(const OrthographicCamera&)` — the legacy material-bucketed 2D path — is
  dead**: zero callers anywhere. `OnRenderSprites` superseded it and is the only place 2D draw order
  is decided (`BuildSpriteDrawList`). Matters to D51/D52.
- **`WaterComponent::Enabled` and `ParticleEmitterComponent::Enabled` are ignored by
  `Scene::BuildRenderDesc`** (`Scene3D.cpp:819`, `:840` gate on `!wc.WaterAsset` / `!pc.Emitter`
  only). `SyncWorldSystems` and `OnRenderWorldFX` both honour them, but `BuildRenderDesc` is the
  path Starforge's viewport and `PlayerLayer` use — so unticking an already-built water body leaves
  it rendering. Documented as a warning, not silently. **Looks like a real bug; a candidate for a
  Phase 30 fix.** Matters to D55.
- **`LODGroupComponent::Levels` is neither reflected nor special-cased by the serializer** (only
  `Color` + `CastShadows` are registered). LOD levels are code-only and **do not survive a scene
  save/load**. Matters to D54.
- **T13 (`Active`) is not universal.** It is honoured by the sprite/2D-light/legacy passes,
  `SubmitOpaqueMeshes`, `GatherSceneLights`, `SyncWorldSystems`, `OnRenderWorldFX`,
  `ScenePhysics::BuildBodies` and `ScriptHost::Tick`/`FixedTick` — but **not** by
  `ScriptHost::Bind` (an inactive entity's script still gets `OnCreate`/`OnStart`), not by the
  terrain draw in either render path, and not by `BuildRenderDesc`'s water/emitter gather. Matters
  to D50 and D57.
- **The 2D passes deliberately ignore the hierarchy.** `OnRenderSprites`, `OnRender2DLights`,
  tilemaps and the legacy `OnRender` all use the **raw** `TransformComponent`; only the 3D submit,
  physics and voxel paths compose through `WorldOf`. Parenting a sprite does not move it. Matters
  to D52.
- **Two engine paths silently set `TransformComponent::UseQuatRotation = true`** —
  `ScenePhysics::WriteBackWorldPose` (every dynamic body and character, every fixed step) and
  `Scene::SetParent(..., keepWorldPose = true)`. The Euler `Rotation` field is stale afterwards.
  Matters to D53's camera material and D57.
- **The old §15 was wrong on both entity-API failure modes.** `AddComponent` on an existing
  component does **not** assert — it warns and returns the existing instance (the guard exists
  because a second EnTT `emplace` aborts the process). `GetComponent` on a missing component does
  **not** assert either: `CS_ASSERT` is compiled out in every configuration (D47's finding), so what
  fires is EnTT's own `assert` — live in Debug, `NDEBUG`-compiled-out in Release, i.e. UB. §15 also
  still typed `Scale` as `glm::vec2` (it is `vec3` since S4.3), showed `TagComponent` without
  `Active`, and documented 3 of 34 components.
- **`CS_ENABLE_ASSERTS` is never defined**, so `ReadWriteQuery::Commit`'s structural-change guard
  (the staged-entity-count comparison) does not exist in any build — the member is `#ifdef`'d out
  entirely. The rule still holds; nothing enforces it. Matters to D59.
- **`Renderer3D` takes the first enabled+active `DirectionalLightComponent` as the sun and stops**;
  extra directional lights are silently ignored, and point lights truncate at
  `kMaxPointLights = 16` inside `SetLights`. Matters to D55.
- Smaller, all verified: `TilemapComponent`, `LODGroupComponent`, `TerrainComponent`,
  `VoxelVolumeComponent`, `CameraComponent`, `CharacterControllerComponent`, `AnimatorComponent`,
  `NavMeshComponent`, `NavAgentComponent` and `RigidBodyComponent` have **no `Enabled` field** at
  all; `Light2DComponent::Enabled` is the only one that is a visible Inspector row; the
  `TerrainCollider` heightfield build **drops the far +X/+Z edge row** (Jolt rounds its sample count
  up to a multiple of 2 and terrain resolutions are odd); a `MeshCollider` on an imported mesh falls
  back to the mesh's local **AABB box** and warns; and there is no `Projects/TemplateProject` — the
  template lives at `Cosmic/templates/ExampleProject`, which is where `AgentSystem.h` (the only live
  `ParallelSystem`) and `BallPhysicsSystem.h` (present but never registered) sit.

**D50 ✅ 2026-07-25.** `docs/guide/scenes-and-serialization.md` and `docs/guide/scripting.md` written
from source. README §23's body replaced with a newly-written five-paragraph overview + chapter link
(§23 was 79 lines); **README §21 retired without a successor** — the heading keeps a pointer to
`getting-started.md` + `project-anatomy.md` plus a short, corrected paragraph about the template,
and **§21.5 was deliberately left live** (the multi-screen app shape has no chapter on the list, and
`docs/plans/archive/04-viper-sim-plan.md` cites it by number). ToC, "Most-asked pages" (+2 entries),
the guide index (**8 of 29**) and five inbound `README §23` pointers updated: `reference/ecs.md`,
`reference/physics.md`, `systems/ecs-scene.md` ×2, `systems/physics-backends.md`. **Findings that
matter to D51–D61:**

- **Neither chapter has a reference tier to link, and that is a manifest gap, not an oversight.**
  `scene/SceneSerializer.h`, `scene/SceneManager.h`, `core/CommandStack.h`, `core/UUID.h`,
  `scene/EventBus.h`, `scene/FlowMachine.h`, `scene/StoryGraph.h`, all of `scripting/` and all of
  `reflect/` have **zero rows** in `reference/README.md`'s manifest — a grep for any of those names
  returns nothing. Both chapters say so in their header block rather than linking a chapter that
  does not exist, and the guide index now carries the list. **D53 (`flow-and-story.md`) inherits the
  same hole** for `FlowMachine`/`StoryGraph`/`EventBus`. This is exactly what D5's checker exists to
  catch.
- **`ModuleMacros.h` expands to THREE exports, not two** — confirming D46's finding from the other
  side. `CS_MODULE_END()` emits `CosmicModule_Register`, `CreatePluginLayer` **and**
  `InitializePluginContexts`; the header's own comment still says "the two exports" in two places.
- **A hot reload whose *load* step fails leaves the reflection registry holding dangling
  descriptors.** `GameModule::Unload` calls `ModuleRegistry::UnregisterModule` + `FreeLibrary`, but
  `UnregisterModule` deliberately does **not** touch `Reflect::GetRegistry()` — those descriptors
  are only overwritten by the *next successful* load. Starforge then rebuilds the scene from its
  JSON snapshot, which will call `descriptor->Add(...)` for any module-owned component through a
  thunk that now points into unloaded code. The user-visible symptom is only the log line
  `"[Module] Load failed — scripts unavailable this session."` Documented as "restart the editor",
  **but it looks like a real latent crash and is a candidate for a Phase 30 fix.**
- **Enum fields serialize as integers but deserialize from *either* an integer or an option name.**
  `SerializeValue` writes `std::get<int32_t>`; `DeserializeValue` accepts a string and looks it up in
  `Hints.EnumEntries`. Hand-written scene/asset files may therefore use readable enum names. Matters
  to every chapter that shows a `.cscene`/`.cmat` snippet.
- **`Quat` is serialized `[w, x, y, z]`**, not `[x, y, z, w]`. Easy to get backwards when
  hand-authoring; `Main.cscene` shows `"RotationQuat": [1.0, 0.0, 0.0, 0.0]` (identity).
- **`EntityRef` field values are NOT remapped by `InstantiatePrefab`.** Parent/child links are
  remapped through an old→new UUID table; reflected `EntityRef` fields are loaded verbatim, so every
  instance of a prefab whose members reference each other points at the *first* instance (or at
  nothing). Documented as a pitfall; a v2 prefab feature, not a doc problem.
- **`InstantiatePrefab` stamps the RESOLVED DISK path** into `PrefabComponent::SourcePath`.
  Starforge immediately overwrites it with the VFS path (`Prefabs.h`), which is what makes a project
  relocatable — code that instantiates prefabs directly must do the same or the scene bakes in an
  absolute path. Matters to D58 (`assets-and-vfs.md`).
- **Prefab apply/revert are not undoable** and revert destroys the subtree outright; the only
  undoable prefab path is `Commands::RecordSpawn` (viewport drop). Consistent with the v1 note in
  `Prefabs.h`, but it is not stated anywhere a user would look.
- **The undo stack is cleared on FOUR boundaries**, not one: entering Play, leaving Play, a script
  hot reload, and project/scene close. Matters to D60's editor chapter.
- **`SceneManager`'s "async" is a fade, not a thread.** The loader runs on the main thread in one
  `OnUpdate` during the `Loading` frame; `Progress()` reports *transition* progress (0→0.5 fade-out,
  0.5 load, 0.5→1 fade-in), never bytes. The JobSystem CPU-prepass split is a recorded follow-up,
  not shipped. Matters to D55's loading-screen material.
- **`SceneManager` owns the swap and nothing else** — it does not tick the scene, re-instantiate
  scripts, or rebind physics. `PlayerLayer::RebindScripts` is the reference implementation of the
  rebind, and every host duplicates it.
- **`SceneManager` does NOT resolve VFS paths**, unlike most of the engine's path-taking API. Both
  `Load(path)` and `Request(path, …)` hand the string straight to `SceneSerializer::Load`, which
  opens it as a filesystem path — a bare `"project://scenes/X.cscene"` silently fails to open and
  reads as "the fade played but nothing changed". `PlayerLayer` wraps every path in
  `FileSystem::Resolve`. Matters to D58.
- **`ScriptHost::Instantiate` is not gated by `Active`, and neither are five other callbacks.** Only
  `Tick`/`FixedTick` check `IsActiveInHierarchy`; `OnCreate`, `OnStart`, `OnEvent`, `OnSignal`, the
  four contact callbacks and `OnDestroy` all fire on inactive entities. **`SystemScript` is not
  gated at all** — neither its tick nor its membership query filters inactive entities. This
  sharpens D49's finding into the full list.
- **`SystemScript` has no `GetEntity()` and none of the eight proxies** — it is scene-bound, not
  entity-bound. Reach services through `GetScene().GetNav()` / `GetPhysics()` / `Events()` /
  `ActiveFlow()`, which is what `NavCritter.h` does.
- **`Physics()`/`Character()` are NOT fenced; `Nav()`/`Animator()`/`Voxels()` are.**
  `ScriptableEntity.h` includes `physics/ScenePhysics.h` outside the `COSMIC_2D_ONLY` fence
  deliberately, so a 2D game gets the full body + character-controller proxy surface. Naming a
  fenced proxy in a 2D build is a compile error, by design.
- **`Character().Move` takes a velocity whose Y component is IGNORED** (`CharacterController.h:40`
  — "gravity + Jump own the vertical axis"), and the controller's own gravity defaults to
  −9.81 m/s². Matters to D57.
- **Field pull-back on Stop is deliberately not done**, so values tuned during Play are discarded —
  but the editor *does* call `ScriptHost::PullFields` once, on a throwaway instance, to seed the
  Inspector with a script's C++ defaults when a class is first chosen.
- Smaller, all verified: `SaveToString` skips any entity without an `IDComponent`, so a
  registry-created entity is silently absent from the file; `Load` does **not** clear the target
  scene (every in-tree caller loads into a fresh one); the `Relationship` block serializes
  **`Children` only**, and `Parent` is rebuilt on load via `SetParent(..., keepWorldPose=false)`;
  `Field_OmitIfTrue` is why `"Enabled": true` / `"Active": true` never appear in a scene file;
  `SaveReflectedToString` accepts **either** `{ "cosmic_type", "fields" }` **or** a bare field object
  on read; and the template project now has **five** child mode layers (Render, Sprite,
  RenderBenchmark, Telemetry, ThemeShowcase) plus `AgentSystem.h`, where the old §21 file tree
  listed three and omitted both systems.

**D51 ✅ 2026-07-26.** `docs/guide/rendering-2d.md` and `docs/guide/materials-and-shaders.md` written
from source. **Eight README sections retired in one work order** — §8, §9, §10, §11, §12, §13, §14
and §18 — each body replaced with a newly-written 3–4 paragraph overview + chapter link, headings and
numbers kept; the README drops from 4,163 to 3,793 lines. ToC (eight rows gained the
*overview; full chapter* form), "Most-asked pages" (the 2D entry now points at the chapter, and a
*Materials & Shaders* entry was added), the guide index (**10 of 29**) and five inbound pointers
updated: `reference/rendering-2d.md` ×2, `reference/graphics-resources.md`, `systems/rendering-2d.md`,
`design/frame-lifecycle.md`. **Findings that matter to D52–D61 — and to Phase 30:**

- **The batch limits are all 10 000 except the instanced pair, which are 20 000**, and *nothing* about
  hitting one is observable: no log, no warning, no dropped geometry. `MaxQuads`/`MaxLines`/
  `MaxCircles`/`MaxTextQuads` = 10 000 (`Renderer2D.cpp:63,68,71,119`), `MaxTextureSlots` = 32
  (`:66`, so **31 distinct textures** — slot 0 is permanently the white texture),
  `MaxInstancedQuads`/`MaxInstancedCircles` = 20 000 (`:150,:76`). Every check is a `FlushAndReset()`
  fence *before* the vertices are written, so no limit can be overrun and the only symptom is an
  extra draw call. **Phase 30's 2D stress tests should assert on `DrawCalls`, not on output.**
- **Draw order between primitive types is FIXED, not submission order.** `Flush()` always emits
  quads → lines → circles → text (`:647,:675,:689,:711`). Inside one flush a line always draws over
  a quad and text always draws last. `Renderer2D.h:89-92`'s "rasterized in submission order" is true
  *per type* and misleading across types — and the same comment cites `Scene::OnRender` sorting by
  `Position.z`, a path D49 already found has **zero callers**. Two stale claims in one header
  comment. This is also the mechanism behind the Phase 29 on-GPU finding that the 2D collider
  overlay drew under the sprites.
- **`Renderer2D::Flush()` is public and does NOT reset the counters** (`:644`). Only the private
  `FlushAndReset()` and the pass push/pop reset them, so a client call to `Flush()` draws the pending
  geometry and then `EndScene` draws it **again**. Nothing in-tree calls it from outside the class.
  Either make it private or reset in it — a candidate Phase 30 fix.
- **The line-batch check is `LineVertexCount >= MaxLineVertices - 1`** (`:1238`), not
  `>= MaxLineVertices`. The counter always moves in steps of 2 from 0, so it lands on exactly 20 000
  and the `- 1` never changes the outcome. Harmless, but it reads like an off-by-one and cost time to
  verify.
- **The instanced path never populates the texture-slot table.** `DrawInstancedQuads` binds only the
  white texture to slot 0 (`:1408`), yet `InstanceQuadData::TexIndex` indexes `u_Textures[]`. Any
  non-zero `TexIndex` samples whatever a previous batch left bound. The old §13 documented `TexIndex`
  as if it worked. **Looks like a real gap; a Phase 30 candidate.**
- **Null-handling is inconsistent across the `DrawQuad` family.** The `Ref<Texture>` overload
  null-checks, warns and falls back to white (`:845-850`); the `Ref<Material>` overload returns
  silently (`:885`); the `Ref<SubTexture2D>` overloads **dereference immediately** (`:930`, `:1072`),
  as does `SubTexture2D::CreateFromCoords` on a null atlas. Three conventions in one class.
- **`Renderer2D::BeginScene` overwrites the GL viewport** with `Renderer2D`'s own tracked size, which
  starts at 1280×720 (`:170`) and is only maintained by `Renderer::OnWindowResize` (`Renderer.cpp:53`)
  and `WorkspaceLayer` (`WorkspaceLayer.cpp:92`). Any host outside those two must call
  `SetViewportSize` or `BeginScene` silently resets the viewport. `PopRenderPass` also restores **no**
  viewport when the stack empties.
- **`PopRenderPass`'s empty-stack guard is a `CS_CORE_ASSERT`** (`:566`) — compiled out in every
  configuration (D47) — so an unmatched pop is `pop_back()` on an empty vector: UB with no
  diagnostic. Same class of hole as `OpenGLFrameBuffer`'s **max-8-colour-attachments assert**
  (`OpenGLFrameBuffer.cpp:161`), where a ninth attachment overruns a fixed `std::array<GLenum, 8>`.
  Both are Phase 30 candidates; the D47 finding keeps paying out.
- **`StatsEnabled` defaults false, and text glyphs are counted as quads.** Confirms D47 from the
  other side and adds the detail that matters when reading a counter: each visible glyph increments
  `QuadCount` (`:1217`), and instanced draws add `batchSize` per chunk (`:1339`, `:1439`).
  `GetTotalIndexCount()` excludes lines on purpose (they are `glDrawArrays`).
- **There is NO `#include` in the shader preprocessor** — no include, import or snippet system, and a
  tree-wide grep for `#include` across `Cosmic/assets/shaders/*.glsl` returns **zero** hits. Shared
  GLSL is duplicated today. The D51 prompt asked for "includes"; there are none to document, and that
  absence is itself the answer. Matters to D54/D55.
- **`Shader::Create` does not resolve VFS paths** (`OpenGLShader.cpp:84` opens the string directly),
  unlike most path-taking engine API — the same trap D50 found in `SceneManager`. `AssetLibrary::
  GetShader` resolves *and* caches, and does not cache failures. Every guide chapter showing a shader
  load must route through one or the other.
- **`OpenGLShader::m_RendererID` has no initialiser** (`OpenGLShader.h`), and `Compile` assigns it
  only on success — so after a compile/link failure `IsValid()` reads indeterminate memory, and
  `Shader::Create`'s documented "returns nullptr on failure" rests on that read. In practice a fresh
  heap block reads 0; it is not guaranteed. **A one-line Phase 30 fix (`= 0`).**
- **`Material::Set` validates nothing and undeclared uniforms are dropped at location -1** — a typo'd
  uniform name fails with no log line anywhere. Deliberate (it is what lets one material feed the
  instancing/skinned twins) but worth stating everywhere materials appear.
- **`Renderer2D`'s material path has its own uniform contract**, undocumented before now: `u_Texture`
  and `u_Color` are read out of the material **at submit** (`:890-893`) and baked into vertex data,
  while the rest of the cache uploads once at flush via `Material::Bind()`. So `u_Color` varies
  per-quad and nothing else does — a partial exception to the S12.2 read-at-flush rule that D54 must
  state precisely. The engine's own `Texture.glsl` does not even declare `u_Color`.
- **Interleaving material and non-material quads costs one draw call per quad.** Every non-material
  overload flushes when `CurrentMaterial != DefaultMaterial`; every material overload flushes on
  pointer inequality (`:815,:852,:887,:927,:961,:992,:1033,:1069`). Two materials with identical
  contents are still two batches. `Scene::BuildSpriteDrawList` sorts for this; hand-written loops do
  not. Matters to D52.
- **There are no `.cmat` files in the tree.** `MaterialAsset` round-trips through the reflection
  layer as `{ "cosmic_type", "fields" }` pretty-printed with `dump(2)`, and the editor writes them,
  but the format had to be derived rather than read off an example. Matters to D58.
- Smaller, all verified: `RenderPass`'s constructor takes `const Camera&` while its own header prose
  still says `OrthographicCamera`; `FramebufferSpecification::Samples`/`SwapChainTarget` are
  **reserved and unimplemented** (MSAA does nothing), which the old §18 said correctly and is worth
  keeping; `ReadPixel`/`ReadDepth` take GL bottom-left coordinates while `ReadPixels` returns a
  **top-left-origin** buffer — two conventions in one class, both deliberate; `DrawCircle`'s
  `thickness`/`fade` have defaults **only** on the `vec2` overload; `SubTexture2D` has no tiling
  factor by design; `Font` metrics are in em units with the first baseline at the transform origin;
  and `u_Textures[]` sampler indices are uploaded automatically at link time
  (`OpenGLShader.cpp:473-496`), so no client ever calls `SetIntArray` for a batch shader.

**D52 ✅ 2026-07-26.** `docs/guide/sprites-and-tilemaps.md` and `docs/guide/game-ui.md` written from
source — **two topics that had never had any client documentation**, and the first work order in
Phase C that retires **no** README section (neither subject appears in the README's Part I at all).
ForgePong is the worked example in both; it is not a folder in `Projects/` but a project **generated
in code** by `StarforgeApp::BuildForgePong` (`StarforgeApp.cpp:3027`), which is where every quoted
snippet comes from. The guide index (**12 of 29**) and five inbound pointers updated: root README §12
(its "gets its own chapter … in work order D52" placeholder became a real link) and **§28** (a new
scope-boundary box: ImGui chrome vs entity UI — the *both ways* half of the D52 cross-link brief,
since `editor-ui-and-theming.md` does not exist until D60, whose prompt already carries the other
half), "Most-asked pages" (+2), `reference/ecs.md`, `reference/cameras.md`, `reference/ui.md` (a
"do not document it here" scope box) and `systems/rendering-2d.md`. **Findings that matter to
D53–D61:**

- **The whole in-game-UI tier is missing from the reference manifest**, plus two more headers:
  `scene/ui/UiComponents.h`, `scene/ui/UiSystem.h`, `camera/Camera2DController.h` and
  `renderer/Light2DRenderer.h` have **zero rows** in `reference/README.md`. `Camera2DController` is
  the *only* camera controller absent — `cameras.md`'s manifest lists the other five plus
  `NavigationCube`. Both chapters say so in their header blocks and the guide index's gap list now
  carries all four. More D5 fuel, from a different corner than D50's.
- **World-anchored UI draws projected but hit-tests un-projected — in BOTH shipped hosts.** The
  camera view-projection is an optional trailing parameter defaulting to `nullptr`;
  `PlayerLayer.cpp:369` and `StarforgeApp.cpp:1418` pass it to `UiSystem::Render`, while
  `PlayerLayer.cpp:291`, `ViewportController.cpp:399` and `:409` pass **nothing** to
  `UiSystem::Update`/`HitTest`. So a `UiWorldAnchorComponent` button renders over the world entity
  and is clickable at its parent-relative rect instead. Documented as "keep world-anchored UI
  non-interactive". **Looks like a real bug; a Phase 30 candidate** (the fix is threading the same
  `camVP` into the two `Update` call sites).
- **`RuntimeTexture` and `RenderToTexture` do not connect.** `SceneRenderer::RenderToTexture` takes a
  `Ref<FrameBuffer>`; `UiImageComponent::RuntimeTexture` is a `Ref<Texture2D>`; **no engine call
  bridges them**, `FrameBuffer` exposes only a raw `GetColorAttachmentRendererID`, and every
  `Texture2D` factory allocates its own storage. A tree-wide grep confirms **nothing ever sets
  `RuntimeTexture`** — the "minimap pattern" is a design-doc aspiration (`design/forge-isle.md:291`),
  not a shipped path. The chapter states the gap and gives a client-side `Texture2D` adapter over the
  attachment handle (verified against what `Renderer2D::ResolveTextureSlot` actually reads:
  `GetRendererID()` + `Bind(slot)`), plus the `ReadPixels`→`SetData` fallback and its top-left/
  bottom-left flip. **A one-class Phase 30 fix would close it.**
- **`RectTransformComponent::ZOrder` is FLAT within a canvas, not nested.** `CollectElements` sorts
  the whole scene's elements by `(CanvasOrder, ZOrder, DFS seq)` — a child does not inherit or sit
  above its parent's `ZOrder`, so a panel with `ZOrder = 3` draws **over its own children**. The
  `ui` render golden encodes the workaround by hand (panel 0, titlebar 1, buttons 2, badge 3, dot 4
  — `tests/render/render_2d.cpp:437-458`). Nothing in the header says this.
- **Only buttons occlude buttons.** `UiSystem::Update`'s topmost search skips anything without an
  *interactable* `UiButtonComponent`, so an image or label drawn over a button does **not** block the
  click. A modal shield has to be a transparent `UiButtonComponent`, not a `UiImageComponent`.
- **A missing UI image draws BLACK, not the tint.** `AssetLibrary::GetTexture` → `Texture2D::Create`
  returns a degraded **non-null** 0×0 texture (D51) and `GetOrLoad` caches it, so `Resolved` is set
  and `DrawImageQuad`'s flat-tint fallback — which tests the `Ref`, not the size — is never taken.
  The quad samples handle 0. The empty-`TexturePath` path is the only correct way to ask for a solid
  colour.
- **`UiTextComponent::Wrap` is reflected and read by nothing.** Word wrap is unimplemented;
  `DrawTextInRect` splits on `\n` only and long lines overflow the rect silently. `Pivot` is the same
  shape — stored, reflected, and never read during layout (no rotation in v1). Two fields that look
  like features.
- **The two 2D compat gates are not symmetric, and the second one traps authors.**
  `OnRenderSprites` skips everything when the scene has no sprites *or* tilemaps; `OnRender2DLights`
  skips only when there are **no lights AND `Ambient2D` is white**. So adding a `Light2DComponent`
  to a scene whose ambient is still the default white produces a bright spot on an already-fully-lit
  scene — **darkening the ambient is what actually arms 2D lighting.** Matters to D55's environment
  material.
- **`Scene::UpdateSpriteAnimations` is owner-ticked and Starforge only calls it in Play**
  (`StarforgeApp.cpp:807`), so flipbooks are frozen in the editor viewport by design. Confirms D49's
  "`Scene::OnUpdate` has no callers" from the other side, and there is no completion callback for a
  one-shot clip — `PongBall` computes `Frames / FPS` as its own cooldown.
- **Sprites are never frustum-culled; only the tilemap cell walk is.** `OnRenderSprites` computes
  the camera's world XY bounds from `inverse(viewProjection)` **only when the scene has a tilemap**
  (`Scene.cpp:600-616`), and uses it solely to clamp the per-map cell range. Every enabled+active
  sprite is submitted every frame. Matters to Phase 30's 2D stress plan.
- **The tilemap's two row orders are deliberately opposite:** `Cells` row 0 is the map's **bottom**
  row (cells grow +Y from the entity's bottom-left corner, one cell = one world unit), while atlas
  index `v-1` counts row-major from the image's **top**. Cell value 0 is empty, `v > 0` is tile
  `v-1`. `EnsureCells` resizes `Cells` **linearly**, so changing `GridW` on a painted map shifts
  every row rather than re-flowing it, and `kMaxGrid = 1024` clamps with no log line.
- **Untextured vs textured sprites use opposite sizing rules**, and adding a `TexturePath` silently
  switches which applies: `WorldSize` returns `Transform.Scale.xy` when there is no texture (the
  legacy quad behaviour every ForgePong sprite relies on) and `SourceRect texels / PixelsPerUnit ×
  Scale.xy` when there is.
- **An orthographic `CameraComponent` at `z = 0` clips the entire scene.** `GetProjection` feeds
  `Near = 0.1` / `Far = 1000` straight into `glm::ortho`, so world Z within 0.1 of the camera is
  clipped; ForgePong parks its camera at `z = 10`. `Camera2DController` has no such trap — it pins
  the camera at `(Focus, 0)` with a ±1000 clip range.
- **`Cosmic.h` does NOT include `scene/ui/UiComponents.h`** (it *does* include
  `camera/Camera2DController.h`), so every client file touching UI components needs the explicit
  include — and the stock **`StoryUiBinding.h` template script is missing it**. It gets away with
  using `UiTextComponent`/`UiImageComponent`/`UiButtonComponent` only because the template
  `Module.cpp` neither includes nor `CS_SCRIPT`-registers it: it ships in every generated project
  and is never compiled. **Matters to D53**, whose `flow-and-story.md` was going to use it as the
  worked example.
- Smaller, all verified: the 2D passes read the **raw** `TransformComponent`, so parenting a sprite,
  tilemap or 2D light does not move it (D49's finding, now written up client-side); the canvas root
  itself is drawable if it carries UI content, and an element with no `RectTransformComponent`
  inherits its parent's rect exactly; `UiWorldAnchorComponent::TargetEntity` is a `uint64_t`, which
  `TypeRegistry.h:46` auto-deduces to `FieldKind::EntityRef` — so D50's *"`EntityRef` fields are not
  remapped by `InstantiatePrefab`"* applies directly to nameplate prefabs; `TilemapComponent::Cells`
  is a hand-written serializer block (a plain int array), not a reflected field;
  `SpriteRendererComponent::Enabled` is `HideInInspector` + `OmitIfTrue` so the editor exposes only
  the entity-level Active checkbox; `Light2DComponent` has **no cap on light count** (unlike the 3D
  `kMaxPointLights = 16`) and its `Radius` is a hard cut in `Light2D.glsl`, not an asymptote; and the
  2D light buffer is half-res `RGBA16F`, a deliberate deviation from the plan's `R11G11B10F`
  (`Light2DRenderer.cpp:37-39`).

**D53 ✅ 2026-07-26.** `docs/guide/flow-and-story.md` and `docs/guide/cameras.md` written from source.
`flow-and-story.md` covers **three headers that had never had any client documentation** and still
have no reference chapter (`scene/FlowMachine.h`, `scene/StoryGraph.h`, `scene/EventBus.h`); the
worked example is **FlowDemo**, which — like ForgePong — is not a folder in `Projects/` but a project
generated in code by `StarforgeApp::BuildFlowDemo` (`StarforgeApp.cpp:2852`), supplemented by
`Projects/ForgeIsle/flows/Main.cflow`, the **only** `.cflow` that ships as a real file (there is no
`.cstory` anywhere in the tree). `cameras.md` retires **README §16** — 88 lines that covered
`OrthographicCameraController` and `OrthographicCamera` and nothing else; the body is a newly-written
four-paragraph overview + chapter link, heading and number kept. ToC, "Most-asked pages" (+2), the
guide index (**14 of 29**, plus a new mixed-configuration note) and five inbound pointers updated:
`reference/cameras.md` (its *Read first* pointed at §16; now carries a D14 configuration note),
`reference/ecs.md` (the `ScenePicker` mis-routing, below), `reference/README.md` (the new ³ᴰ⁺ marker),
`systems/cameras-navigation.md` (its *Guide* pointed at §16), `systems/build-2d-3d-split.md` (§6).
**Findings that matter to D54–D61:**

- **`camera/NavigationCube.h` is the one 3D-only header `Cosmic.h` includes UNFENCED** (`Cosmic.h:91`).
  Every other one — `graphics/Model.h`, `assets/MeshImport.h`, `scene/Components3D.h`,
  `scene/ScenePicker.h` — sits inside `#ifndef COSMIC_2D_ONLY`. Its dependencies (`FrameBuffer.h`,
  `Mesh.h`, `OrbitCameraController.h` for `ViewPreset`) all survive the 2D filter, so the header
  compiles in a 2D tree and `NavigationCube::Create(...)` fails at **link** time with an unresolved
  external instead of at compile time with a clear message. The reference manifest listed it
  **unmarked**, i.e. as present in both configurations, which is wrong; it now carries a new ³ᴰ⁺
  marker with the distinction spelled out, because **D5's checker must not treat "inside a fence" as
  the sole test for 3D-only**. One-line fix (fence the include); **a Phase 30 candidate.**
- **`OrthographicCameraController::OnWindowResized` has no zero-height guard.** It calls
  `OnResize(w, h)` unconditionally and `OnResize` does `m_AspectRatio = width / height`
  (`OrthographicCameraController.cpp:81,120`). `Application::OnWindowResize` returns `false` on a
  0×0 minimise event, so it reaches every layer and every controller — leaving the aspect `inf`
  (or `NaN`) until the next real resize. `OrbitCameraController` and `FlyCameraController` both
  guard with `if (e.GetHeight() > 0)`, and `PerspectiveCamera::SetViewportSize` and
  `Camera2DController::OnResize` guard too. **The odd one out; a one-line Phase 30 fix.**
- **`OrthographicCameraController` is the only rig with LINEAR zoom.** `target -= yOffset ×
  zoomSpeed` (`:113`), where the 2D, orbit and fly rigs all multiply by `1.15^-notch`. The old §16
  documented `SetZoomSpeed` as "world units per scroll tick", which is true and is exactly the
  problem — the feel is scale-dependent. Worth stating anywhere the old rig is recommended.
- **`Gizmo::Manipulate(camera, TransformComponent&, …)` edits the LOCAL transform.** It reads
  `TransformComponent::GetTransform()`, which composes only that entity's own TRS —
  `Scene::GetWorldTransform` is the world one. Starforge passes the component directly
  (`ViewportController.cpp:1165`), so a parented entity's gizmo sits at its local pose. Not
  documented anywhere; matters to D60's editor chapter.
- **The flow's `key:<Name>` transitions are a general format with exactly ONE bridge.**
  `PlayerLayer.cpp:238` and `StarforgeApp.cpp:770` each feed `"key:Escape"` on the rising edge and
  nothing else — `key:Space`, `key:Tab` etc. parse, serialize, validate and never fire. Also note
  the flow is advanced **outside** `PlayerLayer`'s `if (!app.IsPaused())` gate, deliberately, so a
  pause overlay still responds while the app is paused.
- **A non-`push` transition clears the WHOLE overlay stack.** `FlowMachine::Enter(state, push=false)`
  does `m_Stack.clear()` before pushing (`FlowMachine.cpp:587`), so leaving a pause overlay by
  transitioning anywhere other than `@pop` discards the game frame under it. `@pop` on the base
  frame warns and does nothing. Matters to any chapter showing a menu graph.
- **Story option guards are completely silent; flow guards warn once.** `StoryRunner::RebuildValid`
  calls `EvaluateFlowGuard` with **no `warn` callback** (`StoryGraph.cpp:382`), while
  `FlowMachine::EvalGuard` passes a dedup'd warner. A typo'd variable or component name in a
  `.cstory` produces no log line at all — the option just never appears. The dedup set
  (`m_GuardWarned`) is never cleared, so a flow warns exactly once per distinct reason per machine.
- **`StoryRunner::ValidOptions()` is rebuilt only on node ENTRY.** Changing a variable while sitting
  on a node does not re-evaluate its guards (`tests/test_story.cpp:80` is built on this). And
  `Choose(i)` indexes **`ValidOptions()`, not `Options`** — an out-of-range index is ignored
  silently and an in-range wrong one picks the wrong branch, which makes it hard to spot.
- **The field-guard comparator has no `Kind::Enum` case.** `CompareValue` (`FlowMachine.cpp:393`)
  handles Bool/Number/String and falls through to `false` for `Enum`. Harmless for file-authored
  guards (the JSON parser only ever produces Bool/Number/String) but a code-built
  `FlowValue::MakeEnum` against a *reflected field* silently never matches. Variable guards are
  fine — `CompareFlowValues` treats Enum as String.
- **`ScriptHost::DispatchSignal` routes to `NativeScriptComponent` instances ONLY** — a
  `SystemScript` has no `OnSignal` callback at all and must `Connect` explicitly through
  `GetScene().Events()`. This sharpens D50's "`SystemScript` has none of the eight proxies" from the
  signal side. Matters to D59.
- **The stock `StoryUiBinding.h` header comment says "Each frame it maps the runner's CURRENT
  node…" — there is no per-frame call.** No `OnUpdate` override exists; `Refresh()` runs on
  `OnStart` and on each `story_choose_*` signal. Re-confirms D52's finding that the file also lacks
  `#include "scene/ui/UiComponents.h"` and is never registered in the template `Module.cpp`, i.e.
  **ships in every generated project and does not compile as written.** Its option labels must live
  on the button entity itself — `SetText` writes `UiTextComponent` on the entity it is handed, so a
  label parented *under* the button is never written.
- **`FlowAsset::Load` and `StoryGraph::Load` DO resolve VFS paths** (`FlowMachine.cpp:312`,
  `StoryGraph.cpp:230`) — unlike `SceneManager` (D50) and `Shader::Create` (D51). Worth stating,
  because the pattern is now three-for-five and readers assume the trap.
- **`.cflow` bumps its version conditionally; `.cstory` does not.** `FlowAsset::SaveToString` writes
  `2` only when a `variables` block, a `setVar` action or a variable guard is present, so
  variable-free flows stay byte-stable at v1; `StoryGraph::SaveToString` writes `Version` verbatim.
  Both pretty-print with `dump(2)`, matching `.cmat` and `SaveToString` on scenes.
- **`ScenePicker`'s manifest row points at `ecs.md`**, while the guide chapter that covers it is
  `cameras.md`. Not a gap — a mis-routing. Noted in `reference/ecs.md` and the guide index for D5/D13.
- Smaller, all verified: `Cosmic.h` aggregates **every** camera header but **not** `FlowMachine.h`
  or `StoryGraph.h` (it does pull `EventBus.h` in via `Scene.h`); `Camera2DController::OnEvent`
  dispatches **scroll only** and never `WindowResizeEvent`, unlike the other three controllers;
  `FlyCameraController` moves on WASD **whether or not RMB is held** (the look and movement blocks
  are independent in `OnUpdate`); the `ScenePicker` ID pass sees only what `Scene::OnRender3D` draws,
  so sprites, tilemaps, UI, water and particles all read as a miss (which is why Starforge rect-picks
  sprites first); `FlowState::Overlay` and `FlowTransition::Transition` are both **runtime-inert** —
  `push` is what stacks and the transition hint is never read by the engine; `setField` supports only
  scalar/string/enum field kinds and logs "unsupported kind" for vectors and quaternions, and unlike
  guard warnings these are **not** deduped; `FlowMachine::SetVar` creates on write, so a typo makes a
  new variable rather than an error; the blackboard is per-run (`Stop()` clears it, and `@quit` calls
  `Stop()`); `EventBus::Emit` snapshots both listener lists and liveness-checks each entry, so
  connecting inside a handler does not receive the in-flight signal while disconnecting inside one
  reliably suppresses it; and the Flow/Story editors keep **document-local JSON undo stacks** that
  deliberately do not share the scene `CommandStack`.

**D54 ✅ 2026-07-26.** `docs/guide/rendering-3d.md` written from source (**DG-7** built) — the largest
never-documented subsystem in the tree. The README's only `Renderer3D` mentions are the §1.6
configuration prose (×3), one sentence in §9 about read-at-flush and one in §16 about `BeginScene`
taking `const Camera&`; there has never been a 3D rendering section, so **D54 retires nothing**. The
chapter covers the submit → cull → sort → auto-instance → flush pipeline, the material-read-at-flush
rule with its migration examples, scene boundaries, meshes/`MeshData`/primitives/OBJ, glTF `Model` +
the assimp `MeshImport` split, lit drawing with the full convention-uniform table, per-slot submesh
materials, transparency, frustum culling and the `Frustum` utility, **both** instancing paths with a
5,000-instance worked example, `LODGroupComponent`, the statistics counters, the debug-line verbs and
`DrawMeshSkinned`. Worked examples come from Engine3DDemo (the S12 panel — each toggle has code),
Frontier's `Scatter.h` + `IslandWorld.cpp` (5,000 pines, transparent water sheets), Starforge's
`PreviewRig` (the only in-tree mid-scene `Flush()`), `tests/test_render_queue.cpp` and the
`instancing` golden in `tests/render/render_3d.cpp`. The guide index (**15 of 29**, plus a new
per-header configuration note), the reference manifest and both `rendering-3d` skeletons were
updated. **Findings that matter to D55–D61 — and to Phase 30:**

- **Auto-instancing is unreachable from any authored asset, and unreachable from the ECS at all.**
  Two independent gates close it. (a) The material needs an instancing twin, and **nothing registers
  one automatically**: `AssetLibrary::BuildMaterial` registers the *skinned* twin on every `.cmat`
  material (`AssetLibrary.cpp:168`) but never the instancing twin, `MaterialAsset` has a
  `Transparent` field and no instancing field, and `Model::CreateFromGLTF` registers neither
  (`Model.cpp:286-307`). (b) The precondition is `entityID == -1`, but `Scene::SubmitOpaqueMeshes`
  passes the real handle (`Scene3D.cpp:683`), so **every component-driven mesh is disqualified by
  construction**. S12.3 therefore only ever fires for hand-written `Renderer3D::DrawMesh` loops with
  a code-registered twin — which in the whole tree is Engine3DDemo's 48-sphere ring and the render
  golden. Gate (b) is deliberate (per-instance IDs are not in the SSBO, so batching would break
  picking); gate (a) looks like an oversight. **A Phase 30 candidate: a `MaterialAsset` twin field,
  or an opt-out `Pickable` flag on `MeshRendererComponent`.**
- **`LODGroupComponent` measures distance from the entity's LOCAL `Position` but draws at its WORLD
  transform.** All four selection sites read `TransformComponent::Position` and then draw at
  `WorldOf(entity)` / `GetWorldTransform(e)`: `Scene3D.cpp:748`, `SceneRenderer.cpp:452` (shadow),
  `:511` (coverage), `ScenePicker.cpp:85`, `ViewportController.cpp:563`. Flat entities are fine; a
  **parented** LOD entity picks its level from the wrong point and can distance-cull while on screen.
  Consistent across all five sites, so it is a shared assumption rather than a typo. **Phase 30
  candidate** (the fix is `GetWorldTransform(e)[3]` at each site). Documented as "keep LOD entities
  unparented".
- **An entity carrying both a `MeshRendererComponent` and a `LODGroupComponent` draws twice.** The
  two walks in `SubmitOpaqueMeshes` are independent views (`Scene3D.cpp:671`, `:741`) with no
  mutual exclusion. Harmless-looking in the editor (z-fighting on identical geometry), and there is
  no warning anywhere.
- **`Renderer3D::Flush()` and `Renderer2D::Flush()` have opposite contracts under the same name.**
  The 3D one executes *and clears* both queues (`Renderer3D.cpp:922`, `:953`), so mid-scene calls are
  safe and idempotent — it is the documented state-island escape hatch. The 2D one draws without
  resetting its counters, so `EndScene` draws the same geometry again (D51). Any doc that mentions
  "Flush" must say which renderer. Matters to D55/D60.
- **`Renderer3D` has no `StatsEnabled` flag at all**, so its counters always accumulate — and
  **Starforge never calls `ResetStats`** (a tree-wide grep finds only `Renderer2D::ResetStats` at
  `StarforgeApp.cpp:1224`). Both editor readouts — `ProfilerPanel.cpp:87` and the viewport chip at
  `StarforgeApp.cpp:2505` / `ViewportController.cpp:1476` — therefore display **lifetime totals**.
  Engine3DDemo (`:835`) and Frontier (`FrontierApp.cpp:103`) do reset. This is the exact shape of
  D47's asymmetry finding, now with call sites. **A one-line Phase 30 fix.**
- **A 5,000-instance auto-instanced run is 5 draw calls, not 1.** Runs are drawn in
  `kScratchCapacity = 1024` chunks (`Renderer3D.cpp:178`, `:867`), each chunk one
  `AutoInstanceBatches`. Also: the scratch pool's index resets in **`BeginScene`, not `Flush`**
  (`:314`), so repeated mid-scene flushes in one scene keep consuming fresh scratch SSBOs — that is
  deliberate (a run must never rewrite an SSBO a just-issued draw is still reading) but it means the
  pool sizes to the *scene's* peak, not the flush's.
- **Auto-instanced runs upload a null tint array** (`:878`), so every instance is opaque white. That
  is correct — a run shares one material by definition — but it means `InstanceSet`'s per-instance
  `Tint` is reachable **only** through the explicit `DrawMeshInstanced` path.
- **Null assets are dropped in total silence; lifecycle errors are loud.** `DrawMesh(nullptr, …)`, a
  null material, a material with no shader and `DrawModel(nullptr, …)` all return with no log line
  (`:609`, `:626`, `:633`, `:1109`), while "called outside BeginScene/EndScene" warns on every verb.
  Same class of inconsistency D51 found across the `DrawQuad` family, and the reason "nothing draws
  and the log is clean" means "check your `Ref`s".
- **`Model` is glTF-only; FBX/STL/DAE/PLY never touch it.** The D54 prompt says "glTF/FBX import",
  but `Model::CreateFromGLTF` is the cgltf path and the assimp formats go through
  `assets/MeshImport.h` → `AssetLibrary::GetMesh`, producing a `Ref<Mesh>` with a submesh table
  rather than a `Ref<Model>` with parts. Two different products from two different code paths; the
  chapter says so. Matters to D10 and D58.
- **`Model::CreateFromGLTF` imports only the file's DEFAULT SCENE** (roots plus all descendants);
  files with no declared scene fall back to every node (`Model.cpp:205-228`). Nodes outside the
  default scene are treated as authoring data and silently skipped — a plausible "half my model is
  missing" report.
- **Engine convention uniforms are uploaded AFTER `Material::BindFull`, so they always win.** This is
  load-bearing in a place that looks like a bug: the glTF importer writes `u_HasIBL = 0` into every
  material it builds (`Model.cpp:294`), and imported models still receive image-based lighting
  because `ApplySceneBindings` overwrites it at flush (`Renderer3D.cpp:691-695`, `:1005-1016`). The
  general rule — a material may cache a value the engine silently overrides — is worth stating
  wherever materials meet the 3D path.
- **The reference manifest listed TEN 3D-fenced headers unmarked**, i.e. as present in both
  configurations: `Renderer3D.h`, `Model.h`, `InstanceSet.h`, `EnvironmentMap.h`, `ShadowMap.h`,
  `CoverageCapture.h`, `Terrain.h`, `Water.h`, `ParticleSystem.h`, `Presets.h`. The prose note at
  `reference/README.md:90` already enumerated them as fenced, so the table contradicted its own
  preamble. **Fixed here** — all ten now carry ³ᴰ, continuing D53's ³ᴰ⁺ work, and D5's checker can
  now trust the column. `graphics/Mesh.h` and `math/Frustum.h` are correctly left unmarked: both are
  **unfenced** in `Cosmic.h` and compile in a 2D tree.
- **`renderer/RenderQueue.h`'s absence from the manifest is NOT a gap.** The manifest is a
  `Cosmic.h`-include table (D5 diffs it against that header), and `RenderQueue.h` is not included by
  `Cosmic.h` — clients never include it directly. Noted so a future sweep does not "fix" a non-gap;
  its semantics are documented in the guide chapter and belong in D10's entries.
- Smaller, all verified: `DrawMeshInstanced` and `DrawInfiniteGrid` are **immediate**, not queued
  (`:1091-1100`, `:450`), so neither participates in opaque sorting; `Renderer3D::BeginScene` does
  **not** touch the GL viewport, unlike `Renderer2D::BeginScene` (D51's 1280×720 trap has no 3D
  twin); the line-batch guard is `>= MaxLineVertices - 1` (`:396`) — the same harmless off-by-one
  D51 found in `Renderer2D`, and the cap is 20 000 lines; `SetLights` truncates past
  `kMaxPointLights = 16` with a `static bool` **once-per-run** warning, so a scene permanently over
  the cap logs exactly one line for the whole process; `indexOffset` is in **elements** and
  `OpenGLRendererAPI::DrawIndexed` multiplies by 4 for the byte offset (`:141-146`); `Mesh::Create`
  returns `nullptr` on empty input but only *warns* on an index count that is not a multiple of 3;
  the transparent queue has **no state grouping at all** by design (`RenderQueue.h:67-71`), so it
  changes state more often than the opaque pass; `Model` part geometry has the glTF node's world
  transform baked into the vertices, so `DrawModel`'s `transform` places the whole model; and
  `Scene::BuildRenderDesc` deliberately leaves `SceneRenderDesc::EcsScene` **null**
  (`Scene3D.cpp:851-853`) so the routed `DrawOpaque` is the single submit path — the `EcsScene` walks
  in `SceneRenderer::PassShadow`/`PassCoverage` are the *other* host's path and would double-draw if
  both were set. Matters to D55.

**D55 ✅ 2026-07-26.** `docs/guide/lighting-and-environment.md` (**DG-8** built) and
`docs/guide/world-systems.md` written from source. Neither topic has ever had a README section, so
**D55 retires nothing**; the two chapters were added to "Most-asked pages" and the guide index
(**17 of 29**, plus a new configuration note — see below). `lighting-and-environment.md` leads with
the `SceneRenderDesc` + `Render()` quickstart and demotes hand-driven passes to a footnote, then
covers the pass graph, lights + the UBO, PBR/IBL, all four sky modes including the Phase 27 physical
atmosphere, time-of-day, shadows + coverage capture, and every post toggle with its preconditions.
`world-systems.md` leads with the E18 recipe route, then terrain (the `32·2^k+1` rule, the height
sources, the CPU queries, the async-build/loading-screen pattern), water (presets, the reflection
handoff, shore awareness, buoyancy) and particles (presets → custom → curl noise → bounds →
ribbons → `.cemitter`). Worked examples come from Frontier's `IslandWorld` + `DayNightCycle` +
`LoadingScreen`, Engine3DDemo's *World systems (Phase 10 / S8-S10)* panel, `ForgeIsle`'s
`Island.cscene`, Starforge's `WorldSystemsPanel` and the six 3D render goldens.
`docs/design/frame-lifecycle.md` was **summarised and linked, not forked**, per standing rule 7.
**Findings that matter to D56–D61 — and to Phase 30:**

- **`EnvironmentComponent::TimeOfDay` is reflected, Inspector-editable, written by three shipped
  sample scenes — and read by NOTHING.** A tree-wide search returns only writes
  (`StarforgeApp.cpp:2735`, `:2875`, `:2914`, `:3308`) plus the reflection registration
  (`TypeRegistry.cpp:106`, whose `.Doc()` claims it "drives the procedural sun when scrubbed").
  `ApplyEnvironment` does not map it and no panel derives a sun direction from it; Engine3DDemo's
  `m_TimeOfDay` is an unrelated local bool. Scrubbing the slider changes nothing. Same shape as D52's
  `UiTextComponent::Wrap` and `Pivot` — a field that looks like a feature. **A Phase 30 candidate**
  (either wire it to `SunDirection` or mark it `HideInInspector`).
- **`SkyMode::Detailed` is unreachable through `ApplyEnvironment`.** The detailed per-pixel sky draws
  only when `desc.DetailedSky` is non-null (`SceneRenderer.cpp:548`, `:595`), and `ApplyEnvironment`
  never sets that pointer — `Detailed` falls into the same `else` branch as `Procedural`
  (`ClearHdri()` + physical sky disabled, `:299-313`). Every in-tree user sets the pointer by hand,
  and all five are Frontier worlds. So an ECS-authored scene with `Sky = Detailed` renders the plain
  procedural cube, silently. `EnvironmentComponent::SunAngularSize` has the same half-wired shape:
  it reaches `PhysicalSkyDesc` (so `Physical` mode honours it) but the `SkyDetailDesc::
  SunAngularRadius` its own comment promises for `Detailed` is never written. **Phase 30 candidate.**
- **A scene with no `Environment` entity renders with sky, IBL and shadows FORCED OFF under
  `PlayerLayer`** (`PlayerLayer.cpp:385-390`) — the exact opposite of the `SceneRendererSettings`
  defaults, which have all three on. `StarforgeApp` has the same `if (env)` shape at `:1330`. The
  symptom is "the packaged build is flat and washed out but the editor looked fine". Documented as a
  pitfall; worth deciding whether the fallback should instead be the engine defaults.
- **`ApplyEnvironment` does not map `Lights.Ambient`.** `GatherSceneLights` seeds it from the
  process-wide `Renderer3D::GetAmbient()` (`Scene3D.cpp:98`) and nothing on the
  `EnvironmentComponent` touches it — `AmbientIntensity` scales the term, it does not set it. So
  "ambient" is authored in two unrelated places depending on which host you are in. Matters to D60.
- **`EnvironmentComponent::IBLIntensity` maps onto `EnvironmentMap::SetSkyIntensity`**
  (`SceneRenderer.cpp:294`), which scales the whole baked cube — the **visible skybox background as
  well as** the lighting. The name promises less than it does.
- **The sun-direction convention is inverted in exactly one place, and it is the one most likely to
  be called by hand.** `SceneLightsDesc::SunDirection`, `DirectionalLightComponent::Direction`,
  `ShadowMap::SetLight` and `SetLensFlareSun` all take the direction light **travels**;
  `EnvironmentMap::SetSunDirection` takes the direction **to** the sun (`EnvironmentMap.h:113`).
  `ApplyEnvironment` negates (`:291-293`); anyone driving `GetEnvironment()` directly must too.
- **God rays are silently ANDed with shadows, twice.** `SceneRenderer.cpp:728` computes
  `godRays = s.GodRays && s.Shadows`, and `PostProcessStack::RenderEffects` adds
  `m_GodRaysEnabled && m_ShaftShadowMapID != 0` (`:196`). Neither logs. Same shape for heat haze:
  `s.HeatHaze && !desc.DistortionEmitters.empty()` (`:747`) then `m_HeatHazeEnabled &&
  m_DistortionWritten` (`PostProcessStack.cpp:454`). Two "the toggle does nothing" reports built in.
- **`SceneRenderer::Init` is one-shot, so `shadowMapSize` can only be chosen on the first call**
  (`:203-204`). `SetViewportSize` resizes the post stack but never the shadow map. Not a bug; not
  stated anywhere either, and both shipped hosts call `Init` with the default 2048.
- **`RenderToTexture` resizes the renderer's post stack to the target** (`:395-396`), so sharing one
  `SceneRenderer` between the main viewport and an RTT target reallocates the whole post chain twice
  per frame. The header says "use a dedicated SceneRenderer"; nothing enforces it. Matters to D60.
- **Every `EnvironmentMap` setter is change-guarded, but a moving sun still rebakes everything.**
  `SetSunDirection`/`SetSkyIntensity`/`SetNightSky`/`SetMoon`/`SetPhysicalSky`/`SetHdri` all compare
  before assigning (`EnvironmentMap.cpp:123-199`), so per-frame calls with unchanged values are free
  — but any change costs 6 cube faces + mips + 6 irradiance faces + 6 × 5 prefilter mips. A
  continuously animated time-of-day pays that every frame. That is the real reason Frontier's cycle
  has a play/pause control, and it is worth stating in D29.
- **Recipe-authored terrain cannot be moved, and the recipe is a strict subset of the spec.**
  `BuildTerrainSpec` never writes `Origin` (stays `{0,0}` — the world origin), `LodDistanceFactor`,
  `SkirtDepth`, `HeightFunction`, per-layer `Tiling`, or any of `TerrainMaterialParams` except
  `HighHeight`/`HighBlend`. Combined with `TerrainComponent`'s documented "the entity's
  TransformComponent is not applied", a scene can hold exactly one terrain and it is always centred
  on the origin. Matters to D57 (terrain colliders) and to any future multi-terrain work.
- **`ClampTerrainResolution` snaps silently and its range excludes 2049+.** Valid values are
  `{65, 129, 257, 513, 1025}` (`WorldSystemRecipes.cpp:40`); type 400 in the Inspector and you get
  513 with no message. `Terrain::Create` itself accepts any `32·2^k + 1` — Frontier uses 2049 — so
  the editor range is a deliberate authoring cap, not the engine rule. Two different limits with the
  same name; the chapter states both. Note also **`Terrain.h:89`'s spec comment says
  `(64 * 2^k) + 1`** while `Terrain.cpp:44` errors with `32*2^k + 1` and validates against
  `kPatchQuads = 32`. The two forms coincide for every value anyone uses (65, 129, 257, 513, 1025,
  2049 satisfy both); they diverge at exactly one point — **`Resolution = 33`, which `Create`
  accepts and the header comment excludes**. Cosmetic, but the header is the narrower of the two and
  the error message is the correct one. One-line comment fix.
- **`BuildEmitterSpec` never writes `GpuSimulation`**, so every recipe-authored emitter is on the
  GPU compute path (the spec default). The CPU twin is reachable only from code — which is exactly
  what the `particles` render golden does, because `StepCpu` is bit-exact run to run and the compute
  path is not.
- **`BuildWaterSpec` exposes ~12 of the ~25 `WaterSpecification` fields.** `DepthFadeDistance`,
  `FoamDepth`, `RefractionStrength`, `ReflectionStrength`, `Detail{Tiling,Speed,Strength}`,
  `SpecularPower`, `ShoreDepthRange`, `CausticScale` and `ReflectionResolution` come from the
  `WaterPreset` alone and cannot be authored in a scene. Not a bug — but it means "tune the water in
  the editor" has a hard ceiling, and `water/Presets.h` is the real authoring surface.
- **Confirms D49's `BuildRenderDesc` gap from the other side.** `Scene3D.cpp:819`/`:840` gate only on
  `!wc.WaterAsset` / `!pc.Emitter`, so `Enabled` and `IsActiveInHierarchy` are honoured by
  `SyncWorldSystems` and `OnRenderWorldFX` but **not** by the path the editor viewport and
  `PlayerLayer` actually use. Written up as a pitfall with the workaround (clear the asset). Still a
  **Phase 30 candidate.**
- **Two more manifest gaps**, both in `reference/README.md`'s own prose already but with no table
  row: `scene/WorldSystemRecipes.h` (the whole E18 recipe→spec layer) and `water/Presets.h` — while
  `particles/Presets.h` *is* listed, so the omission is asymmetric rather than categorical. Added to
  the guide index's gap list. More D5 fuel, from the D50/D52 pattern.
- **`SceneRenderer`'s configuration story is per-class, not per-chapter** — the sharpest case yet of
  D54's finding. `SceneRenderer` and `PostProcessStack` ship in **both** builds and a 2D frame runs
  the same compositor spine; `EnvironmentMap`, `ShadowMap`, `CoverageCapture`, `desc.Lights`,
  `desc.DrawOpaque` and the world-content half of `SceneRenderDesc` are the fenced parts. The guide
  index's *Configuration coverage* section now records this. D5's checker must not treat
  `reference/rendering-pipeline.md`'s scope as uniformly ³ᴰ: `SceneRenderer.h` and
  `PostProcessStack.h` are correctly **unmarked**, the other three correctly marked.
- Smaller, all verified: the lights UBO is uploaded **once up front**, before any pass, so reflection
  / opaque / transparent all read identical lighting (`SceneRenderer.cpp:347-350`) — but
  `Scene::OnRender3D` re-uploads from the ECS, which is why `PassOpaqueHDR` re-asserts `desc.Lights`
  right after it (`:619-624`); `PassReflection` **never walks `desc.EcsScene`**, so ECS geometry
  reflects only via the `DrawOpaque` route `BuildRenderDesc` uses; the point-light overflow warning
  is a `static bool` and therefore fires **once per process**, not once per frame or per scene;
  bloom is a soft-knee prefilter plus **ten** separable Gaussian passes (`PostProcessStack.cpp:273`);
  `Water::Create` keeps at most **8** waves and returns `nullptr` on a degenerate extent;
  `Terrain::BuildHeights` uses `stbi_load_16`, so 8-bit heightmaps are widened and 16-bit precision
  survives; `EnvironmentMap::Bake` only runs when `IBL || Skybox` is on, so turning both off freezes
  the cube at whatever was last baked; a failed `SetHdri` **or** a missing `EquirectToCube.glsl`
  reverts to the procedural sky and logs rather than going black (`EnvironmentMap.cpp:191`, `:253`);
  `ShadowMap::SetLight` clamps radius to ≥ 1 and puts the far plane at `4 × radius`, and
  `BeginDepthPass` front-face-culls (restored to `None`, not to the previous mode); and
  `Renderer3D::SetSnow` is sticky process-wide state that `IslandWorld::OnDetach` clears explicitly
  (`IslandWorld.cpp:312`) so the next world does not inherit the coverage.

**D56 ✅ 2026-07-26.** `docs/guide/voxels.md` and `docs/guide/animation.md` written from source.
Neither topic has ever had a README section, so **D56 retires nothing**; both are also the **only
documentation of their subsystem anywhere in the tree** — unlike every previous Phase C item, they
have no reference chapter *and* no `docs/systems/` explainer to link (see the manifest finding
below). The guide index (**19 of 29**) and "Most-asked pages" (+2) updated. `voxels.md` covers the
palette, the sparse 32³ chunk store and its seam-dirty rule, `.cvox`/`.cpal`, culled-vs-greedy
meshing, `VoxelVolumeComponent` + the ordered `SyncVoxelVolumes` contract, the `Voxels()` proxy and
the editor brush, generation + streaming, and per-chunk static collision. `animation.md` covers
import (glTF via cgltf, FBX/DAE via assimp), `Skeleton`/`AnimationClip`, `AnimatorComponent`,
scrubbing, `CrossfadeTo`/`BlendLocals`, joint sockets, and GPU skinning through SSBO 10 + the
shadow twin. Worked examples come from `StarforgeApp::BuildForgeBlocks` (`:2690` — ForgeBlocks is
generated in code, not a `Projects/` folder, like ForgePong and FlowDemo), the stock `VoxelDigger.h`
/ `WalkController.h` template scripts, the Starforge Voxels panel + Animation Editor, and
`tests/test_voxel.cpp`, `test_voxel_collision.cpp`, `test_animation.cpp`, `test_crossfade.cpp`,
`test_sockets.cpp`. **Findings that matter to D57–D61 — and to Phase 30:**

- **Seven more manifest gaps, of a THIRD kind: transitively reachable, never directly included.**
  D50/D52/D55 found headers with no row; D54 established that the manifest is a `Cosmic.h`-include
  table and that `RenderQueue.h`'s absence is therefore *not* a gap. These seven sit between the two.
  `graphics/Skeleton.h` and `graphics/AnimationClip.h` reach every 3D client through
  `scene/Components3D.h` (`Cosmic.h:72`'s own comment says so: *"Skeleton/AnimationClip ride
  Components3D.h"*), and `voxel/VoxelVolume.h` + `voxel/BlockPalette.h` reach it through
  `scripting/ScriptableEntity.h` — all four are named in public component members and proxy return
  types, so they are unambiguously client surface. `voxel/VoxelMesher.h`, `voxel/VoxelGenerator.h`
  and `voxel/VoxelRender.h` need an explicit include and appear in shipped sample code.
  **D5's checker cannot use "directly included by `Cosmic.h`" as its sole membership test** — that
  rule silently exempts two whole subsystems.
- **Neither subsystem has a `docs/systems/` explainer, and neither is in the systems index.** Voxels
  and skeletal animation are the only two shipped subsystems with no row in *either* tier index. Both
  guide chapters now say so in their header block. Worth a decision in Phase D: add rows, or record
  that the guide chapter is the terminal document for these two.
- **`rd.Generated` is dead weight, and the recipe-change reset is a no-op.**
  `Scene::SyncVoxelVolumes` clears `Render->Generated` when the generator signature changes, with the
  comment *"recipe changed -> re-terrain untouched chunks"* (`Scene3D.cpp:317`). It cannot:
  the streaming loop skips a chunk when `rd.Generated.count(cc) || vc.Volume->HasChunk(cc)`
  (`:338`), and `VoxelGenerator::GenerateChunk` → `EmplaceChunk` makes every generated chunk
  resident — including all-air ones. So `Generated` never gates anything `HasChunk` does not already
  gate, and a recipe change only ever affects chunks that have never existed. Only the Voxels panel's
  **Regenerate** (which calls `Volume->Clear()`) actually re-terrains. **Phase 30 candidate** (erase
  the generated chunks on a signature change, or delete the set).
- **The streaming shell walk costs ~`2·(ViewRadius+1)⁴` map lookups per frame in the steady state.**
  `Scene3D.cpp:329-347` iterates the full `[-r, r]³` cube per radius and `continue`s on non-shell
  cells, so one call is `Σ(2r+1)³ = (R+1)²(2(R+1)²−1)` iterations. The `generated < kGenBudget`
  early-out only fires while chunks are still being made — once the neighbourhood is full, the loop
  runs to completion **every frame**. At the default `ViewRadius = 8` that is 13 041 iterations
  (fine); at the **reflected maximum of 64** it is 35 697 025 (not fine). Documented as "keep
  ViewRadius modest"; **a Phase 30 candidate** (iterate the shell directly, or skip the scan when
  the previous pass found nothing).
- **Voxel undo coalescing is unreachable, and two comments describe behaviour that does not exist.**
  `VoxelEditCommand` implements `MergeKey`/`TryMerge` keyed on `ctx.VoxelBrush.Stroke`, but
  `ViewportController.cpp:260` increments `Stroke` on **every** successful hit, so no two commands
  ever share a key. There is also no drag painting — only rising-edge LMB/RMB clicks are handled.
  `VoxelPanel.h:14-15`'s *"coalesced per brush stroke"* and the panel tooltip's *"Each drag is one
  undo step"* are both wrong. The tile painter (U4) is the one that really coalesces — it increments
  `TileBrush.Stroke` only on the mouse-**down** edge (`ViewportController.cpp:312`) and then edits
  every frame while held, which is exactly what the voxel brush was meant to do. Same shape as D52's
  `UiTextComponent::Wrap` — machinery that reads like a feature.
- **The editor brush and the shipped script use OPPOSITE mouse buttons.** `ViewportController`:
  LMB places, RMB breaks. `VoxelDigger.h` (and the ForgeBlocks HUD hint): LMB digs, RMB places. Both
  are deliberate in isolation — the editor's CAD nav puts the camera on MMB — but nothing anywhere
  says they differ. Documented in both chapters.
- **A `.cvox`'s stored placement is discarded inside a scene.** `VoxelVolume::Load` restores
  `Origin` and `VoxelSize` from the file, and `SyncVoxelVolumes` then overwrites both from the
  component (`Scene3D.cpp:293-294`) — `SetVoxelSize(vc.VoxelSize)` and the entity's world
  translation. Only a hand-loaded volume keeps its saved placement.
- **Voxel volumes read only the world TRANSLATION; rotation and scale are ignored** — and the chunk
  collision bodies bake that translation into their vertices at build time, with nothing marking
  chunks dirty on a transform change. Moving a volume during Play therefore moves the visuals and
  leaves the collision behind. Same family as D55's "recipe-authored terrain cannot be moved".
- **`LoadFromBuffer` clears the volume BEFORE it validates.** `VoxelVolume.cpp:284` wipes
  `m_Chunks`/`m_Dirty` after the header check but before decoding any chunk, and returns `false` on
  the first malformed record — so a failed load leaves a partially emptied volume, not the original.
  Load into a scratch volume and swap on success. (`SyncVoxelVolumes` only ever loads into a
  freshly-created one, so the engine's own path is safe.)
- **A skinned mesh with no `MaterialAsset` silently never animates.** `SubmitOpaqueMeshes` gates the
  skinned route on `mr.MeshAsset->IsSkinned() && mr.MaterialAsset` (`Scene3D.cpp:693`), so the
  Lambert `Color` path draws the **bind pose** with no log line. The skinned twin only exists on
  materials `AssetLibrary::BuildMaterial` produced (every `.cmat`), never on a hand-built
  `Material::Create`. This is the single most likely cause of "my model renders but never moves" and
  is the fourth instance of D54's *null assets are dropped in total silence* pattern.
- **The two model-spawn paths in Starforge are not equivalent, and the easier one produces exactly
  that broken state.** **File ▸ Import Model…** (`ImportModelFile`) copies the source, writes
  `.cmat`s, assigns them, and attaches an `AnimatorComponent` pointed at the file's first clip
  (`StarforgeApp.cpp:4262`, `:4279`, `:4288`). Dragging the **same file** from the Content Browser
  into the viewport spawns `MeshRendererComponent` with **only** `MeshPath`
  (`ViewportController.cpp:1601-1607`) — no material, no animator — so a rigged model dropped that
  way is silently frozen in its bind pose. Nothing in the UI hints at the difference. Documented in
  the chapter's quick start; **worth considering a Phase 30 fix** (have the drop path reuse the
  import spawn, or at least attach the default material).
- **Skinned rigs animate in the EDITOR viewport; sprite flipbooks do not.** `StarforgeApp.cpp:1321`
  calls `UpdateAnimators` every frame when not playing, while `UpdateSpriteAnimations` is only
  called in Play (`:807`, D52's finding). Two animation systems with opposite edit-mode behaviour,
  stated nowhere. Also confirms D49 from a third side: `Scene::OnUpdate` — the only in-engine caller
  of `UpdateAnimators` — still has no callers, so every host ticks it directly.
- **A clip with no joint-targeting channels is discarded at import, in BOTH backends.**
  `MeshImport.cpp:445` and `:945` both push only when `!clip.Channels.empty()`. A file whose
  animations move non-joint nodes therefore yields *no clips at all* and surfaces as
  `"'X' contains no animation clips."` — not as an empty clip. Matters to D58's import material.
- **glTF clip `Duration` is DERIVED (max key time), assimp's is `mDuration / mTicksPerSecond` with a
  25 tick/s default** when the file omits the rate. Two different provenances for the same field;
  the 25 default is a plausible "the FBX plays at the wrong speed" report.
- **`ShadowMap::DrawCasterSkinned` loads its shader with a bare CWD-relative path** —
  `Shader::Create("assets/shaders/ShadowDepthSkinned.glsl")` (`ShadowMap.cpp:119`), not
  `engine://`. Consistent with D51's finding that `Shader::Create` does not resolve VFS paths, but
  it means the deforming-shadow shader is found only when the working directory is the runtime root.
  It also keeps its **own** binding-10 buffer uploading per caster at base 0, deliberately, because
  `Renderer3D`'s scene-wide upload happens later in the frame and re-binds the slot.
- **`Skeleton::ComputeGlobals` has no cycle guard.** The `while (j >= 0 && !done[j])` parent walk
  (`Skeleton.cpp:48`) pushes forever on a looping parent chain. Both importers produce trees, so this
  only bites code-built rigs — but it is an unbounded loop plus unbounded `stack` growth, not a
  warning. Worth a `Phase 30` line if hand-built skeletons ever become a supported path.
- **`Scene.h:25`'s forward-declaration comment is stale**: it says `AnimatorComponent` is in
  `scene/Components.h`; Phase 29 W4 moved it to `Components3D.h`. One-line fix. (Third stale
  source comment class after D46's and D51's.)
- Smaller, all verified: voxel chunk meshes are submitted **inside** `SubmitOpaqueMeshes`, so they
  route through `SceneDrawContext` and therefore **cast shadows and feed coverage capture** as well
  as drawing lit — and `ScenePicker` draws them too, so voxel volumes are click-pickable, unlike
  sprites/water/particles (D53); `VoxelMesher::VertexAO` is unit-tested and called by **nothing**
  (the shared `MeshVertex` has no colour channel to hold it); the mesh budget is 24 chunks per
  `SyncVoxelVolumes` call and the collision budget is 8 chunk bodies per fixed step, with leftovers
  re-queued in both; `BlockPalette` has **no editor** — `.cpal` is `AssetOpen::None`, Create ▸ Palette
  writes `CreateDefault()` verbatim, and the Voxels panel only *picks* a block; `.cvox` has an
  AssetTypes row but is **not** in `CreatableTypes`, so the only way to make one is the panel's
  *Save .cvox* button, which appears only when `VolumePath` is non-empty; every chunk body is tagged
  with the **volume entity's** UUID, so contacts and raycasts report one entity for the whole world;
  glTF `CUBICSPLINE` imports the middle value of each in/value/out triple and drops the tangents, and
  `STEP` is approximated as linear; skin weights cap at the **4 strongest** influences per vertex and
  renormalize **in the shader**, not at import; a merged multi-part import keeps its skin only when
  **every** sub-mesh is skinned; skinned draws form their own state group, never auto-instance, and
  have their bind-pose cull AABB **padded by 50 % per side** (`Renderer3D.cpp:556-564`) — the one
  place the engine compensates for a pose leaving bind bounds; and `AnimationClip::BlendLocals` grows
  `out` but never shrinks it, so a reused buffer keeps stale entries past `min(a.size(), b.size())`.

**D57 ✅ 2026-07-26.** `docs/guide/physics.md` and `docs/guide/navigation-and-ai.md` written from
source. **No README sections retired** — neither subsystem has ever had a section in it (Part I goes
straight from §16 Camera System to §17 Virtual File System), which is why the item map's *Retires*
cell is `—`. `physics.md` covers the session lifecycle, the fixed-step tick order, the three motion
types as three code paths, the five colliders and their sizing rules, triggers, the two-sided
category/mask filter, the character-controller walk model, the `Physics()` query surface, contact
callbacks, 2D physics, the debug overlays and backend swapping; it opens with a boxed
**"physics ships in *both* builds"** statement, per the work order. `navigation-and-ai.md` covers the
collision-view bake, the recipe, the signature gate, the `.cnav` sidecar, sync vs async baking, the
crowd, the `Nav()` proxy, the `SystemScript` route, `nav.arrived` and agent-free navmesh queries.
Updated: the guide index (**21 of 29**, both Status cells + the manifest-gap paragraph), the root
README's "Most-asked pages" (its `[Physics]` link pointed at the *reference* chapter; now the guide
chapter, and Navigation & AI is new), `reference/physics.md` (Read-first + See-also),
`systems/physics-backends.md` (Guide + See-also) and `reference/README.md`'s manifest-gap prose.
**Findings that matter to D58–D61 — and to Phase 30:**

- **A character controller's entity rotation is overwritten with IDENTITY every fixed step.**
  `ScenePhysics::Step` stage 4 calls `WriteBackWorldPose(e, ctrl.GetPosition(), glm::quat(1,0,0,0))`
  (`ScenePhysics.cpp:440`), which sets `RotationQuat` **and** flips `UseQuatRotation = true`. So any
  yaw a script authors on a character entity — Euler *or* quaternion — is discarded before it is
  drawn, and every child composed through `WorldOf` inherits the identity. `Projects/ForgeIsle`'s
  `PlayerController` is written against the opposite assumption: `OnStart` sets
  `UseQuatRotation = false` and `OnUpdate` writes `Rotation.y = m_Yaw`, expecting its child
  `PlayerCamera` to follow. The shipped `WalkController` sample dodges it only because its movement
  is deliberately world-relative and it never authors a yaw. **A real bug and a strong Phase 30
  candidate** (write back position only, like `SceneNavRuntime::WriteBackWorldPos` already does, or
  preserve the authored rotation). Documented in the chapter with the parent-a-Yaw-child workaround.
- **A character controller generates NO contact events at all.** `CharacterVirtual` is a query
  volume, not a body, and `JoltBackend::CreateCharacter` installs **no** `CharacterContactListener`
  — grep the backend for one and there is nothing. So walking a `CharacterControllerComponent`
  through a trigger raises neither `OnTriggerEnter` nor `OnCollisionEnter`, which is the natural way
  to build a pickup and fails silently. Documented as a pitfall with two workarounds (poll from the
  sensor's side, or carry a kinematic body alongside). Worth a Phase 30 decision: wire a character
  contact listener, or state the limit in the header.
- **A scene character's gravity is a hard-coded `-9.81f`, not `PhysicsSettings::Gravity`.**
  `ScenePhysics::BuildBodies` calls `ctrl.SetGravity(-9.81f)` (`:261`) with no reference to the
  world settings, and neither `CharacterControllerComponent` nor the `Character()` proxy exposes it.
  A low-gravity world therefore floats its crates and drops its characters at Earth rate. It *is*
  reachable — `Character().Ctrl()->SetGravity(...)` — and the chapter says so, but nothing in the
  Inspector hints at it. Phase 30 candidate: seed from `settings.Gravity.y`, or add the field.
- **`IsTrigger` is a per-BODY flag ORed across every collider on the entity.** `BuildColliderDesc`
  accumulates `anyTrigger |= c->IsTrigger` and writes one `BodyDesc::IsTrigger`, so a compound body
  with one sensor collider becomes entirely a sensor — the "solid thing with a sensor bubble" shape
  is impossible on one entity. Deliberate (Jolt sets `mIsSensor` per body) but stated nowhere.
- **A collider's `Offset` is NOT scaled by the entity's world scale, while its dimensions are** —
  and the editor's collider gizmo disagrees. `BuildColliderDesc` sets `d.Scale = s` and passes
  `c->Offset` through untouched; `BuildPrimitive` bakes `s` into the extents and
  `RotatedTranslatedShapeSettings` applies the raw offset. The overlay
  (`ViewportController.cpp:1065`) uses `world * translate(Offset) * scale(...)`, so the offset
  *is* scaled there. Identical on an unscaled entity (the common case); divergent the moment you
  scale an entity that has an offset collider. **Phase 30 candidate** — pick one and make both agree.
- **Trigger volumes are baked into the navmesh as solid geometry.** `SceneNav::GatherGeometry`
  filters on collider *presence* and `TessellateBody` never looks at `BodyDesc::IsTrigger`, so a
  sensor slab becomes walkable floor or an obstacle. Combined with the sphere/capsule/hull → AABB
  approximation, this is the most likely cause of "agents route around nothing". Phase 30 candidate:
  skip sensors in the gather.
- **`NavAgentComponent::AutoRepath` is reflected, tooltipped, serialized — and read by nothing.**
  `SceneNavRuntime::BuildAgents` copies only `Radius`/`Height`/`MaxSpeed`/`MaxAccel` into
  `NavAgentParams`, and `NavWorld::AddAgent` hard-codes `ap.updateFlags` (`NavWorld.cpp:537`). Same
  family as D52's `UiTextComponent::Wrap` and D56's voxel undo coalescing — machinery that reads
  like a feature. (`NavAgentParams::SeparationWeight` is also never set from the component and stays
  at its 2.0 default; that one is at least invisible.) Documented as a pitfall.
- **`SceneNavRuntime::SetTarget` marks `HasTarget = true` even when the request was dropped.**
  `NavWorld::SetAgentTarget` silently returns when `findNearestPoly` finds nothing within
  `(2,4,2)` of the target (`NavWorld.cpp:566`), but the caller sets its own flags unconditionally
  (`SceneNav.cpp:449-450`). With no prior target the agent simply never moves and `nav.arrived`
  never fires; **with** a prior target it keeps steering to the old one and fires `nav.arrived` for
  a destination the script never asked for. Snap with `NearestPoint` first — the chapter's
  recommended pattern. Phase 30 candidate: return a bool through the runtime.
- **Four more manifest gaps, one of a FOURTH kind.** `nav/NavWorld.h`, `nav/NavTypes.h` and
  `scene/SceneNav.h` are D56's third kind again (transitively reachable via
  `scripting/ScriptableEntity.h`) with no chapter anywhere — navigation has no `docs/systems/`
  explainer either, so `navigation-and-ai.md` joins `voxels.md` and `animation.md` as a terminal
  document. The fourth kind is **`physics/ScenePhysics.h`: unlisted, but already fully covered by a
  WRITTEN reference chapter** (`reference/physics.md`'s scope block names it and documents every
  method). Its five siblings all have rows; only the indirect include path hid it. That is a
  one-line manifest fix, not a coverage question, and D5 should treat the two kinds differently.
  Recorded in `reference/README.md`'s gap prose.
- **`reference/physics.md` (D43) held up under a full re-read against source** — the one place worth
  sharpening is `CharacterController::SetGravity`'s note, which correctly separates world gravity
  from controller gravity but does not mention that `ScenePhysics` hard-codes the seed. Left the
  reference alone; the guide states it.
- Smaller, all verified: `RigidBodyComponent::Motion` defaults to **`Static`** while `BodyDesc`'s
  defaults to `Dynamic`, so an Inspector-added rigid body does nothing until you change it;
  `BuildBodies` skips the whole rigid-body pass for any entity with a `CharacterControllerComponent`,
  so the two never coexist; a character's `GetPosition()` is its **feet** (the capsule is shifted up
  by `HalfHeight + Radius` at creation); two non-moving layers never pair, so a trigger only sees
  `Dynamic`/`Kinematic` bodies; Starforge's play accumulator is a fixed `1/60` with a **catch-up
  clamp of 8 steps**, while `PlayerLayer` rides the engine's own `SetFixedTimestepHz`;
  `SceneNavRuntime` binds the **first** built `NavMeshComponent` and ignores any others;
  `Scene::SyncNavMeshes` lazily loads `.cnav` sidecars from the top of **both** `OnRender3D` and
  `BuildRenderDesc`; `FindPath` caps at **512 polygons / 512 straight-path points**; `.cnav` has an
  `AssetTypes` row but `AssetOpen::None` (there is no navmesh viewer); `NavMeshComponent::TileSize`
  is parked — non-zero logs a warning and still builds a solo mesh; and `SceneNav::FinishBake`
  returns `true` for a *failed* build (the job completed), so callers must check
  `nm.Nav && nm.Nav->IsBuilt()`, which the editor does and a naive caller would not.

**D58 ✅ 2026-07-26.** `docs/guide/assets-and-vfs.md`, `docs/guide/audio.md` and
`docs/guide/sim-math-toolkit.md` written from source. `assets-and-vfs.md` covers the three schemes
with dev-vs-packaged resolved examples, both `project://` mount modes, the whole `user://` root
decision (per-app isolation + portable mode — S6, which the README predated entirely), the
`AssetLibrary` cache with a **per-type miss table**, model import and `.cmeta`, the Starforge rich
import as the reference flow, texture staging, `Config`, and `FileDialog`/`FileWatcher`/`ImageIO`/
`DataExport`. `audio.md` covers one-shots, held loops, the four groups, opt-in pausing, the
device-less no-op surface — and carries the **`MA_COINIT_VALUE` gotcha** as its own section with
both tripwires. `sim-math-toolkit.md` covers the integrators, the shared filter contract, lookup
tables, noise, PCG32 randomness and the `Cosmic::Math` frames, with a when-to-use table, a
determinism box, and a doctest citation per header; ViperSim is the exemplar throughout.
README §17's body replaced with a newly-written four-paragraph overview + chapter link, heading and
number kept (§17 was 60 lines). Updated: the ToC, "Most-asked pages" (three new entries), the guide
index (**24 of 29**, three Status cells, the manifest-gap paragraph and a new *Configuration
coverage* note), all three reference skeletons (`assets-io.md`, `audio.md`, `math.md` — Read-first
now points at the guide, plus corrections listed below) and all three systems skeletons
(`assets-vfs.md`, `audio.md`, `math-sim-toolkit.md` — Guide link + a don't-re-derive note; the two
`README §17` pointers in `systems/assets-vfs.md` retired). **Findings that matter to D59–D61 — and
to Phase 30:**

- **A failed texture load is CACHED; a failed shader/mesh/model/material is not.** `GetOrLoad`
  deliberately does not cache a null so a later call retries (`AssetLibrary.cpp:75-79`) — but
  `Texture2D::Create` never *returns* null under OpenGL. `OpenGLTexture`'s constructor logs
  `Failed to load texture at …` and leaves a valid object with `m_Width = m_Height = 0`
  (`OpenGLTexture.cpp:143-150`), so the degraded object occupies the cache slot forever and only
  `AssetLibrary::Reload` evicts it. Two conventions colliding, and the guide states which verbs sit
  on which side.
- **A `.cmat` with a missing texture tells the shader the map EXISTS.** `BuildMaterial`'s `setMap`
  writes `u_Has<X>Map = 1.0f` whenever `GetTexture` returns non-null (`AssetLibrary.cpp:177-182`),
  which for textures is always. A typo'd map path therefore binds a zero-sized texture *and* enables
  its shader branch. Direct consequence of the row above. **Phase 30 candidate** — check
  `GetWidth() != 0`, or give the loader a real failure signal.
- **`Config`'s getters log NOTHING on a type mismatch**, contradicting `Config.h:64-65` ("they log
  every time; fix your file"). No `CS_CORE_WARN` exists anywhere in `Config.cpp` outside `Load`.
  `speed = "fast"` where a float was expected is silently the fallback. Same family as D48's
  `u_Time` and D57's `AutoRepath` — machinery the header advertises and the code does not have.
- **A fifth kind of manifest gap, and the plainest one: four headers `Cosmic.h` includes DIRECTLY
  and UNFENCED have no manifest row** — `utils/FileWatcher.h`, `utils/FileDialog.h`,
  `utils/ImageIO.h`, `utils/ExeResources.h` (`Cosmic.h:167-170`). Not transitive (D56/D57), not
  fenced, not mis-routed (D53) — omitted. Every earlier gap needed an explanation for why a
  `Cosmic.h`-include table missed it; these four are exactly what such a table is *for*. Strongest
  argument yet for D5's checker. Recorded in `guide/README.md` and in `reference/assets-io.md`'s
  scope block.
- **Ogg Vorbis does not decode, but the editor advertises it.** miniaudio gates Vorbis behind
  `STB_VORBIS_INCLUDE_STB_VORBIS_H` (`miniaudio.h:63002-63004`) and `MiniaudioImpl.cpp` never
  includes stb_vorbis, so only WAV/FLAC/MP3 work. `Starforge/src/AssetTypes.cpp` nevertheless lists
  `.ogg` in its audio row, so an `.ogg` gets a proper tile and a preview button and then plays
  silence. **Phase 30 candidate** — drop `.ogg` from the row, or wire stb_vorbis in.
- **The "resolve `project://` in the calling DLL" rule taught by four in-tree comments is
  OBSOLETE.** The A1 fix moved the mount state into the engine DLL, so there is one active project
  per process. `TemplateProject.cpp:40-43`, `SimHub.cpp:30-31`, `Sound.h:16-20` and
  `LookupTable.h:95-96` all still explain the old per-DLL reasoning (as does `Application.cpp:731`,
  already found by D46). The *practice* is harmless — a resolved path passes through `Resolve`
  unchanged — but the reason is wrong and it makes the VFS look fragile. It was also about to be
  copied into a reference chapter: `reference/assets-io.md`'s D16 checklist listed it in bold as a
  rule to document. Corrected there.
- **`DataExport` resolves nothing.** No `FileSystem::Resolve` appears anywhere in
  `DataExport.cpp`; all four verbs take raw disk paths. The header states this for `LoadCSV` only.
  It *does* create the parent directory for all three write verbs (`:60`, `:108`, `:155`). Same
  family as D50's `SceneManager` finding — the engine's path-taking APIs do not agree on whether
  they resolve, and the guide now says which is which per call.
- **`ImageIO` and the GL texture loader disagree about row order, deliberately.**
  `ImageIO::ReadPixels` forces stb's flip-on-load **off** and returns top-left-origin RGBA (the OS
  icon convention) while the texture loader leaves the global flip **on**. `WritePNG` also expects
  top-left, so `ReadPixels → ResizeRgba → WritePNG` is self-consistent — but feeding `ReadPixels`
  output to the renderer is upside down. Third two-conventions-in-one-area find, after D51's
  `ReadPixel`/`ReadPixels` split.
- **Per-app `user://` isolation arms ONLY on a `boot.cfg` boot** — confirming D46 from the other
  side and now stated as a table. `--project` and `COSMIC_STARTUP_PROJECT` boots keep the shared
  root (`Runtime/Main.cpp:100-101`). Also: `portable.txt` only matters when the exe dir is
  **read-only**, since a writable exe dir is already portable; and `GetUserDataRoot` memoises into a
  function-local static, so `SetAppIdentity` after the first `user://` resolve is a silent no-op.
- **The determinism guarantee needs splitting, and `Random.h` slightly over-promises.** The PCG32
  *integer* stream is bit-exact and pinned against the algorithm's canonical reference vector
  (`test_random.cpp:15-29`). Every `Noise` sampler is arithmetic-only (`+ - * /`, `fabs`, `clamp`)
  and therefore deterministic under IEEE-754. But `Random::Gaussian` calls `log`/`sin`/`cos`,
  `LowPassFilter` calls `exp`, and `Biquad`'s setup calls `sin`/`cos` — none required to be
  correctly rounded, so none bit-identical across libm implementations. "The same sensor noise
  everywhere, forever" holds for the stream, not for the Gaussian. Matters to D59's replay material.
- **Half the sim-math tier has no in-tree consumer.** `LookupTable1D`/`2D`, `LowPassFilter`,
  `Derivative`, `MovingAverage`, `Biquad`, `Washout` and `IntegrateSemiImplicitEuler` appear only in
  their doctests — a tree-wide grep outside `math/` and `tests/` finds nothing. Only `RateLimiter`,
  `IntegrateRK4`, `FixedSubstepper`, `Random`, `Noise` and `Cosmic::Math` have real callers. Those
  examples are written against the headers rather than mined, and the chapter says so.
- **`FixedSubstepper` does not create a fixed rate — it divides whatever `dt` it is handed.**
  `h = dt / substeps` is recomputed every `Run` call, so `Run(dt, 8, …)` is 480 Hz *only* when `dt`
  is exactly 1/60. From `OnUpdate` the substep size varies frame to frame; the residual still
  conserves total time. The header's own usage comment ("engine at 60 Hz, physics at 480 Hz")
  quietly assumes the fixed tick. Called out in the chapter.
- **`RateLimiter` on an angle sweeps the long way round a ±180° wrap.** It operates on a plain
  float with no wrap awareness, and ViperSim's `RigOutput` runs one per Euler axis including yaw
  (`fc_glue/RigOutput.h:79`). Only bites when the yaw actually crosses the wrap; documented as a
  pitfall with the unwrap-first fix. Low-severity Phase 30 candidate.
- Smaller, all verified: `AssetLibrary::Clear()` has exactly **one** call site
  (`Application::Shutdown`), so nothing clears the cache between project loads — safe, because keys
  are fully-resolved paths, but a long editor session accumulates; `AssetEntry::Refs` **includes the
  library's own reference**, so an otherwise-unheld asset reports 1; shaders and models report 0
  bytes because neither is size-tracked; `AssetLibrary::GetMaterial` names the built material with
  the **unresolved** path while keying the cache on the resolved one; an *empty* clip set IS cached
  ("this file has no clips" is a valid answer) while a parse failure is not; `LoadOrInitMeta` writes
  the `.cmeta` next to the **resolved** source and warns if that location is read-only;
  `FileDialog::Open/Save` return `nullopt` for cancel and error **indistinguishably**, and resolve
  `InitialDir` only when it contains `://`; `FileWatcher::Poll` returns paths **relative to the
  watched root**, forward-slashed, which is what makes `"project://" + change.Path` correct;
  `AudioEngine::Init` runs **before the window is created** (`Application.cpp:547` vs `:550`), which
  is why the COM apartment is already decided when the first dialog appears; `AudioEngine::Play`
  returns `void` so a one-shot can never be stopped or retuned; finished one-shot voices are swept
  only when the **next** voice starts, so an app that fires a burst and goes quiet holds those
  `ma_sound` objects until shutdown; `GetMasterVolume`/`GetGroupVolume` return **`0.0f`** with no
  device rather than the last value set, so a settings slider reads zero on a headless machine;
  `SetGroupVolume(Master, v)` is literally `SetMasterVolume(v)` and `PauseGroup(Master, true)` stops
  the whole device graph; `LookupTable1D` stable-sorts its points so duplicate `x` values form a
  **step** whose exact hit lands on the *last* duplicate; and `LookupTable1D::FromCSV` resolves its
  VFS path itself even though `DataExport::LoadCSV` underneath it does not.

**D59 ✅ 2026-07-26.** `docs/guide/serial-and-telemetry.md` (**DG-13** built) and
`docs/guide/jobs-and-parallelism.md` (**DG-12** built) written from source. **Three README sections
retired** — §20 (plus its §20.5 and §20.6 subsections, which keep their headings and gain
retirement pointers), §22 and §26 — each body replaced with a newly-written 3–5 paragraph overview +
chapter link, headings and numbers kept; the README drops from **3,696 to 3,172 lines**. Updated:
the ToC (three rows gained the *overview; full chapter* form, and §20.5/§20.6 gained sub-rows),
"Most-asked pages" (+2), the guide index (**26 of 29**, both Status cells, and a new manifest-gap
paragraph), and four inbound pointers — `reference/serial-telemetry.md`, `reference/jobs.md`,
`systems/serial-telemetry.md`, `systems/jobs-parallelism.md` (all four now point *Read first* /
*Guide* at the chapter, carry a *don't re-derive* note, and name the built diagram).
`docs/archive/` and `docs/plans/archive/` references to §20/§22/§26 were left alone — they are
historical records. **Findings that matter to D60–D61 — and to Phase 30:**

- **`--replay <file>` is plumbed and reads nowhere, and the extension it is wired to is one the
  engine never writes.** `Runtime/Main.cpp:47-61` parses the flag and `_putenv_s`es the path into
  `COSMIC_REPLAY_FILE`; a tree-wide grep for that variable returns **only those two lines**. Worse,
  `installer/AppSetup.iss:59-61` (and Starforge's packager, `Packager.cpp:217-218`) register the
  association on **`.cham`**, while `DataRecorder::Flush` writes `scene.bin` + `<name>.csv` and
  `DataPlayer::Load` accepts only a directory or a path whose extension is `.bin`. So a
  double-clicked recording launches the app and nothing else happens — and could not work even if an
  app read the variable. Same family as D57's `AutoRepath` and D55's `TimeOfDay`, but with an
  installer registry key behind it. **Phase 30 candidate** (read the variable in `PlayerLayer`, and
  change `.cham` → `.bin`; both are small). Matters to **D61**, which owns packaging/installer docs.
- **The "v3" warning this work order was given is now down to ONE comment, and the headers are
  clean.** `DataRecorder.h`'s format block says *"BINARY FILE FORMAT v1 — Single supported format"*
  and `DataPlayer.h`'s says *"v1 — all entities in one file"*; README §26 said v1 already. The only
  survivor is **`DataRecorder.cpp:257`**: `// 1. Write scene.bin — v3 format with per-entity
  sample_count`, three lines above `const uint32_t version = 1u;`. One-line fix. The reference
  skeleton's blanket "some telemetry docstrings reference v3" warning was narrowed to name that line.
- **`DataRecorder::Flush` and `DataPlayer::Load` do not resolve VFS paths** — the fourth instance of
  the pattern after `SceneManager` (D50), `Shader::Create` (D51) and `DataExport` (D58). This one is
  worse than the others because it fails *silently in the wrong direction*: `Flush("user://takes")`
  does not error, it creates a literal directory named `user:` next to the exe. Neither `.cpp`
  contains a single `FileSystem::Resolve`. `TelemetryPanel::DrawReplayLoader` resolves only the
  **Browse dialog's start folder**, not the path it hands to `Load`.
- **`SerialLink`'s auto-reconnect can silently move you to a different COM port.** Every 3-second
  retry calls `RefreshPorts()`, which keeps the selection only if the port is still enumerated and
  otherwise falls back to `m_Ports.front()` (`SerialLink.cpp:25-26,65-67`). Unplug the device on
  COM7 while a Bluetooth COM3 exists and the link opens COM3 and reports `RECEIVING` on whatever
  arrives. Documented as a pitfall; a defensible Phase 30 fix is to keep retrying the *named* port
  rather than re-selecting.
- **README §20 carried three wrong claims, all now retired.** (a) *"Write support (planned) … not
  yet implemented or exposed"* — `SerialPort::Write` is implemented (`SerialPort.cpp:231`), exported,
  and has a `SerialLink` pass-through. (b) *"`ReadLoop` treats any `ReadFile` error other than
  `ERROR_TIMEOUT` as a fatal disconnect"* — there is no `ERROR_TIMEOUT` branch anywhere; the loop
  keys on `ERROR_IO_PENDING` and `ERROR_OPERATION_ABORTED`. (c) The *"Job system compatibility"*
  paragraph's ordering argument ("by the time `OnFixedUpdate` fires, the parallel job pass has not
  yet dispatched") is vacuous — the parallel pass lives in `Scene::OnFixedUpdate`, which no engine
  code calls, and where a project *does* call it, it calls it **from inside** its own
  `Layer::OnFixedUpdate`.
- **`ComponentArray<T>::From` returns an EMPTY view once the pool spans more than one EnTT page** —
  it maps page 0 only, and the guard now runs in **every** configuration (it was a Debug-only
  `CS_CORE_ASSERT`, i.e. compiled out everywhere — D47's finding again). It logs
  `pool spans N pages … Returning empty view` and hands back `nullptr`/0, so the symptom is a
  parallel loop that silently stops running as the entity count grows. `FlatComponentArray<T>` is
  the fix and is what the error names.
- **`ReadWriteQuery::Commit`'s structural-change guard does not exist in any build**, sharpening
  D49's note with the call site. `SystemQuery.h:153,169,297` puts `m_StagedEntityCount` behind
  `#ifdef CS_ENABLE_ASSERTS` **and** compares it with `CS_CORE_ASSERT` — one symbol defined nowhere,
  one macro compiled out everywhere. The EnTT ID-recycling hazard it describes is real and entirely
  unpoliced.
- **`Scene::OnUpdate`/`OnFixedUpdate` having no engine caller is not a curiosity for this chapter —
  it is the chapter's most load-bearing fact**, and D49 flagged it for exactly this work order. The
  whole four-pass pipeline lives inside those two methods. Re-verified: the only in-tree caller of
  either is `TemplateTelemetryLayer.cpp:305,313`, and `Scene::AddSystem`'s only call site tree-wide
  is `TemplateTelemetryLayer.cpp:80`. `PlayerLayer.cpp:226` is `SceneManager::OnUpdate`, a different
  method. Written up as a boxed warning rather than a footnote.
- **`ParallelForAsync`'s serial fast path hides dangling-capture bugs**, and the two paths have
  different capture requirements by construction. Below `minChunkSize` (default 64), or on a
  single-worker machine, `func` runs **synchronously** and a by-reference capture of a local is
  perfectly safe; cross the threshold on a multi-core machine and the same code reads freed stack.
  The `static_assert` only catches move-only functors — a *copyable* lambda holding `[&local]` is
  accepted and dangles. The header says this; nothing enforces it.
- **`JobSystem::WaitIdle()` is a global barrier, not a per-caller one.** It waits on the whole
  process queue, so a system tick that calls it also waits for someone else's background terrain
  build. That is the real reason `ParallelSystem::OnParallelExecute` forbids it — worth stating
  wherever `ParallelFor` (which calls it internally) is recommended.
- **`Framing::EncodeFrame` drops an oversized frame in total silence**, and `HilBridge::SendFramed`
  sizes one fixed buffer off `MaxFrameSize(sizeof(SensorPacket))` for *every* packet type. Currently
  safe (`SensorPacket` is the largest at 90 packed bytes vs `CommandPacket`'s 34), but adding a
  bigger packet stops transmission with no log line and no counter anywhere.
- **The old README §26's picking example used the wrong mouse space**, and the header says so
  outright. It subtracted `Application::GetViewportPos()` from `Input::GetMousePosition()`, but
  `Input.h:57-63` documents `GetMousePosition()` as **window-client-relative** and states that it
  "only matches by luck when the window sits at the desktop origin (e.g. borderless maximized)" —
  which is exactly the configuration the sample was presumably tested in.
  `GetMouseScreenPosition()` is the accessor that lives in `GetViewportPos()`'s space. Corrected in
  the chapter; **matters to D60**, which owns the viewport/mouse-contract material, and this is the
  concrete failure case for it.
- **`EntityPicker.h` contradicts itself, and the README repeated the wrong half.** The file-header
  overview (`:9-12`) says `Pick` tests "the entity's 2D AABB (Position ± Scale/2)", while `Pick`'s
  own docstring and the code rotate the query point into the entity's local frame by `Rotation.z`
  first. Old README §26 copied the AABB version. The code and the method docstring are right —
  one-line comment fix. (Fourth stale-source-comment class after D46's, D51's and D56's.)
- **`AssetLibrary` has no locking of any kind** — a grep for `mutex`/`lock_guard` across
  `AssetLibrary.{h,cpp}` returns nothing. Combined with the fact that its getters construct GPU
  resources, that makes it strictly main-thread, which the jobs chapter's contract table now states
  explicitly. Worth a line in D16's reference chapter too.
- **Three guide chapters write `void MyLayer::OnUpdate(Cosmic::Timestep ts)`, and the real hook is
  `Layer::OnUpdate(float deltaTime)`.** With `override` that is a hard compile error; without it, it
  silently defines a different function. `Layer.h:97` declares exactly one signature and **every**
  in-tree project writes `virtual void OnUpdate(float ts) override`. Fixed in D59's own chapter;
  **`docs/guide/audio.md:43` and `docs/guide/assets-and-vfs.md:616` still have it** (both D58, both
  without `override`). One-line fixes, and **D61's final sweep should grep the whole guide tier for
  `Timestep` in a hook signature**.
- Smaller, all verified: `JobSystem::Initialize()` is the **first** statement in
  `Application::Initialize` (before audio, before the window, before the GL context) and
  `Shutdown()` the first in `Application::Shutdown` (before `UnloadProjectDLL`), so a job holding a
  pointer into the game DLL has always finished before the DLL is freed; `~JobSystem` calls
  `Shutdown()` itself as a `std::terminate` guard for exit paths that skip `~Application`;
  `Shutdown` **drains** rather than aborts (workers exit only when stopping *and* the queue is
  empty); worker count is `logical cores − 1` floored at 1, from `GetSystemInfo`, with no override;
  both engine `Submit` sites (`Scene3D.cpp:370`, `SceneNav.cpp:299`) guard on `IsInitialized()` and
  run inline when there is no pool, which is what makes them work headlessly; `Terrain::Create` and
  `Water::Create` are pure CPU (GL is allocated lazily in `EnsureGpuResources` on first render),
  which is the entire basis of the async-world-build pattern; every spdlog sink the engine installs
  is `_mt`, so `CS_CORE_*` is worker-safe; `DataRecorder::Record` zero-fills channels the caller's
  value list did not reach and silently drops extras; a duplicate `Register` name returns the
  existing ID **and ignores the new channel list**; entity/tag names truncate at 63 chars and
  channel names at 31 (`strncpy_s` + `_TRUNCATE`); `DataPlayer::Load` clears the previous recording
  **before** parsing, so a failed load also unloads what was there; `TelemetryPanel::OnUpdate` only
  advances the player when `dt > 0` and only pushes a replay frame when the playhead actually moved;
  `SetMode` clears the channel-name list as well as the ring buffers, deliberately, because leaving
  names set with unsized buffers used to crash on a Live↔Replay flip; `SerialLink::OnUpdate` uses
  `std::fabs(dt)` so a negative `TimeScale` ages the link rather than freezing it; `SerialLink::
  Connect()` returns early on an empty selection **without** setting connect intent; SF_Telem's
  `PumpSerial`/`IngestChunk` split is the pattern that makes a wire protocol unit-testable without a
  COM port (`tests/test_sftelem_hub.cpp` splits a frame at every byte boundary); and
  `EntitySelection::Notify` snapshots the listener list outside the mutex, so subscribing inside a
  handler is safe and does not receive the in-flight notification — the same shape as `EventBus`
  (D53).

**D60 ✅ 2026-07-26.** `docs/guide/windowing-and-viewport.md` and
`docs/guide/editor-ui-and-theming.md` written from source, and **DG-5** built — in
`docs/guide/project-anatomy.md` (see below). **Four README sections retired** — §24, §27, §28 (plus
§28.5, which keeps its heading and gains a retirement pointer, the §20.5/§20.6 precedent) and §29 —
each body replaced with a newly-written 3–5 paragraph overview + chapter links, headings and numbers
kept; the README drops from **3,172 to 3,049 lines**. Updated: the ToC (four rows gained the
*overview; full chapter* form, §28.5 gained a sub-row), "Most-asked pages" (+2), the §1.5 `F11` row
and two §43 pointers (all three pointed into now-retired §24 bodies), the guide index (**28 of 29**,
both Status cells and a new manifest-gap paragraph), and six inbound pointers —
`design/frame-lifecycle.md` ×2 (teardown ordering now points at `project-anatomy.md`, which D47
already covered it in), `guide/game-ui.md` ×2 (its *"until D60 writes…"* placeholders),
`reference/rendering-2d.md`, `reference/ui.md`, `reference/core.md`, `systems/ui-theming.md` ×2 and
`systems/windowing.md` ×2 (the last four now point *Read first* / *Guide* at the chapters and carry a
*don't re-derive* note). `docs/archive/` and `docs/plans/archive/` references were left alone —
historical records. **Findings that matter to D61 — and to Phase 30:**

- **The measurement that justified this entire restructure does not exist.** Doc 12 §1 decision 1b —
  and the D60 prompt built on it — states that *"one section — §29 Viewport Visibility & Center
  Docking — was **1,128 lines**, 23 % of the file."* At `HEAD` (`3066e6a`, README = 4,875 lines) **§29
  runs lines 3312–3338: twenty-seven lines.** The four sections D60 retires total **282** lines
  (§24 = 120, §27 = 56, §28 = 79, §29 = 27), and the *largest single section in the whole file* is
  §26 Telemetry at 365. `1128 / 4875 = 23.1 %`, so the two figures are internally
  consistent with each other and with nothing in the file. The restructure is still obviously right
  — 4,875 lines is 4,875 lines — but the specific claim should be struck rather than re-quoted, and
  **D61's Part II pass should not trust any other line-count claim in §1 without re-measuring.**
- **The old §24's Alt+Enter hotkey example actually binds Ctrl+Enter.** It tests
  `mods & 0x0002` and comments it as ALT; `GLFW_MOD_CONTROL` is `0x0002` and **`GLFW_MOD_ALT` is
  `0x0004`** (`glfw3.h:535-545`). Copy-pasted verbatim it silently binds the wrong chord. Corrected
  in the chapter, which also spells out every raw constant — D48 already established there are **no**
  `CS_MOD_*`/`CS_ACTION_*` engine constants, and the hotkey override is the one place raw GLFW
  `action`/`mods` reach client code.
- **`Application::GetViewportPos`'s own doc comment contradicts the code that produces the value.**
  `Application.h:93` says *"GLFW window-space pixels"*; the value is `ImGui::GetCursorScreenPos()`
  recorded in `WorkspaceLayer.cpp:214-215`, i.e. **ImGui screen (desktop) pixels**, which
  `WorkspaceLayer.h:271-278` states correctly. This is the exact contract D59 flagged for this work
  order, and the stale comment is the most likely reason the mistake keeps recurring. One-line fix;
  noted in `reference/core.md` for D6.
- **A packaged app's in-game UI hit-tests in the wrong space.** `PlayerLayer::UpdateUI`
  (`PlayerLayer.cpp:275-291`) feeds `UiSystem::Update` the **window-client** `Input::GetMousePosition()`
  against a `UiRect` anchored at `(0,0)` sized to the framebuffer — but the image that rect
  corresponds to actually begins at `Application::GetViewportPos()`, below the shell's menu bar and
  the dock tab bar. Rendering is unaffected (it goes through the framebuffer); clicks are offset by
  the chrome height. Same family as D52's world-anchored-UI finding, and it is present in **every
  shipped player-driven build**. **A strong Phase 30 candidate** (thread `GetViewportPos()` in, as
  `ViewportController.cpp:385-386` already does).
- **D47's "the template project still relies on the legacy dock path" is wrong, and the real legacy
  consumer is `PlayerLayer`.** `TemplateProject.cpp:96-104` registers four `DockWindow` bindings —
  it merely keeps the historical *window names* — so `m_DockBindings` is non-empty and
  `BuildDockspace` takes **port** mode. `PlayerLayer` registers **none** (its only `WorkspaceLayer`
  call is `SetProjectName`, `:97-98`), so every packaged, player-driven app builds the legacy fixed
  22 % three-tier left sidebar with `"Project Inspector Top/Mid/Bottom"` — windows it never opens.
  D47's other two claims (legacy fires only at zero bindings; bare `"Project Inspector"` is the
  shell's no-project placeholder) hold.
- **`SetEdgeRatios` and `SetEdgeMinPixels` take their edges in different orders.**
  `SetEdgeRatios(left, right, top, bottom)` vs `SetEdgeMinPixels(top, bottom, left, right)` —
  neighbouring setters on the same class, no warning, silently wrong layout. Documented as a pitfall
  in both the task section and the pitfall list; a defensible Phase 30 fix is to align them.
- **`ShowThemeSelector`'s `DockPort` parameter is dead.** The selector is a floating popout toggled
  from View ▸ Theme Selector; `m_ThemeSelectorPort` is stored and never used by
  `OnImGuiRender` (`WorkspaceLayer.cpp:240-246`). The header says so; the signature does not. Same
  family as D57's `AutoRepath` and D55's `TimeOfDay` — machinery that reads like a feature — except
  here the header is honest and only the API shape misleads.
- **README §27's UI-font claim was inverted.** It said *"ImGui's built-in font stays the global
  default (so existing layouts don't shift); custom faces are opt-in."* `Fonts::Init` assigns
  `io.FontDefault = s_Default` (Roboto-Regular, or the first custom face) at `Fonts.cpp:156-157`, and
  the code comment right above it says the change was the point. ImGui's bitmap font is kept only as
  `Fonts[0]`. Also: `Fonts::Get(name, sizePx)` **ignores `sizePx` entirely** — selection is purely by
  name, the size is applied at draw time.
- **Two more manifest gaps, both D56's third kind**, and — unusually — the *main* headers of both
  chapters are correctly routed. `core/Window.h` and `layers/WorkspaceLayer.h` are in
  `reference/README.md`'s *"Not in `Cosmic.h` but client-reachable"* footnote, pointing at `core.md`
  and `ui.md`. Missing are **`layers/ImGuiThemes.h`** (transitively reachable via
  `layers/ImGuiLayer.h`, and the home of `enum class ImGuiTheme` — the parameter type of the exported
  `ImGuiLayer::SetTheme` / `Cosmic::SetImGuiTheme` overloads — plus `GetBuiltInThemes()` and
  `NameForTheme()`) and **`utils/Branding.h`** (`COSMIC_API`, unit-tested in `tests/test_branding.cpp`,
  called from a project DLL at `StarforgeApp.cpp:2617`/`:2649`, and **not included by `Cosmic.h` at
  all** — the explicit-include sub-flavour, like D56's `VoxelMesher`/`VoxelGenerator`/`VoxelRender`).
- **DG-5 went to `guide/project-anatomy.md`, not to a D60 chapter.** Its subject — the plugin-DLL
  lifecycle — belongs to neither `windowing-and-viewport.md` nor `editor-ui-and-theming.md`. §4's
  *Home(s)* cell named "README §31, build-plugin-packaging", but every other diagram in Phase C was
  built in the **guide** chapter that owns the topic and *reused* by the systems/README docs, and
  `project-anatomy.md` (D47) already carries the verified step-by-step load/unload sequences. So the
  mermaid lives there and README §31 gained a pointer; §4's row is updated to match. **§31's own
  unload ASCII has the order wrong** — it lists `Window::ClearFullscreenHotkeyOverride()` *before*
  `delete m_ActivePluginLayer`, where `Application.cpp` does `ClearViewportLayer` (:785) → `delete`
  (:791) → `ClearFullscreenHotkeyOverride` (:797) → `SetActiveProject("")` (:805) → `FreeLibrary`
  (:808). The critical `delete`-before-`FreeLibrary` claim is right; the neighbouring step is not.
  Both ASCII blocks were **kept** per execution note 5 (they are phase-scoped call-name references;
  DG-5 is lifecycle-scoped) — **D61's Part II pass should fix that one line.**
- **README §24's GPU-teardown subsection was already duplicated** by `guide/project-anatomy.md`
  §"Teardown ordering for GPU resources" (D47) — including the client-dev rule, which §24 itself
  linked. Retiring §24 therefore removed a fork rather than losing content; the two
  `design/frame-lifecycle.md` citations now point at the chapter.
- Smaller, all verified: the fullscreen `F11` path never constructs an `Event` (D48, re-confirmed at
  the call site) and the override sees **press, release and repeat**, so an override that does not
  test `action == GLFW_PRESS` toggles twice per keystroke; `Application::UnloadProjectDLL` *does*
  call `ClearFullscreenHotkeyOverride` before `FreeLibrary`, so the README's "always clear it in
  `OnDetach` or you will crash" is belt-and-braces for the plugin path — but it is the **only**
  protection for Starforge's game-module hot reload, which does not route through `UnloadProjectDLL`;
  `Window`'s constructor logs `CS_CORE_CRITICAL` and **returns early** on a GLFW/context failure,
  leaving a null handle behind a `CS_CORE_ASSERT` that is compiled out in every configuration (D47),
  so an OpenGL-4.5-less machine null-derefs on the first `SwapBuffers` rather than failing cleanly;
  `WindowCloseEvent` is `Handled` before the layer walk, so there is no client veto on close (D48,
  now stated client-side); the DPI story is declared **twice** — `Runtime/CosmicApp.manifest`
  (`PerMonitorV2`, embedded into *both* hosts at `Runtime/CMakeLists.txt:8` and `:51`) and GLFW's own
  `SetProcessDpiAwarenessContext(...PER_MONITOR_AWARE_V2)` in `glfwInit`
  (`glfw/src/win32_init.c:689`); `SetBottomInsetPixels` is the one `WorkspaceLayer` setter that does
  **not** queue a dock rebuild, which is why calling it every frame is correct;
  `BuildDockspace` splits **left → right → top → bottom**, so side columns are full-height and top/
  bottom rows span only the band between them; the edge ratio is clamped to `[0.05, 0.9]` and the
  pixel minimum is multiplied by `viewport->DpiScale` for you; `DockFlags::NoTabBar` is applied to the
  **node**, so it affects every window sharing that port; `SetViewportTitle` uses the
  `"Title###Viewport"` idiom so renaming the tab never resets the layout *and*
  `BeginViewportOverlay`'s `Begin("Viewport")` still appends to the same window; `io.IniFilename`
  points at `user://imgui.ini` held in a `static std::string` because ImGui borrows the pointer;
  `SetIcon` builds exactly the **16/32/48/256** levels and **keeps the current icon on a decode
  failure** (the hot-swap half-written-file case Starforge retries once at 0.5 s);
  `Branding::ResolveIcon` probes four roots with first-hit-wins and is pure filesystem probing, which
  is what makes it headless-testable; `ImageFitted`/`ImageWindow` test `GetWidth() > 0` rather than
  just the `Ref`, which is the correct guard given D58's "a failed texture load returns a degraded
  non-null 0×0 object"; `Fonts::Get` falls back to the default face for an unknown name and never
  returns null after `Init()`, which is why `StatCard`/`SectionHeader` can hard-push `"Roboto-Bold"`;
  and `ThemeManager::SaveToFile`/`LoadFromFile`/`LoadFolder` all take **resolved disk paths** — the
  fifth instance of the engine's inconsistent path-resolution convention after `SceneManager` (D50),
  `Shader::Create` (D51), `DataExport` (D58) and `DataRecorder`/`DataPlayer` (D59).

**D61 ✅ 2026-07-26 — PHASE C CLOSED.** `docs/guide/building-and-shipping.md` written from source
(**DG-14** built), plus the README Part II pass, the §1.5 verification sweep and the final link
sweep. **Two README sections retired** — §40 (body replaced with a six-paragraph overview + chapter
link; the PCH subsection was kept, being genuine Part II internals with no chapter) and §25, which
becomes a four-row routing table into the reference/guide/systems tiers plus §1.5. Part II also
gained a **new §42.5** (a one-line-per-document directory of all 21 `docs/systems/` documents),
**DG-2** in §30 and **DG-6** in §35; §30's source map was rewritten against the current tree and
gained a *2D partition* subsection; §34's `RendererAPI` sketch and GLAD paragraph were corrected;
§31's unload ASCII was fixed; §43 became a pointer to the roadmap and `FEATURE-MATRIX.md`. Updated:
the ToC (§25, §40, §42.5, §43), "Most-asked pages" (+1), the restructure banner (now *complete*),
the guide index (**29 of 29** + a Part-I-fully-retired note + a *What D61 changed in Part II* note),
`docs/README.md` (a per-tier Status table replacing the prose), `docs/systems/README.md` (its format
template said `**Guide:** root README §<n>` — now `../guide/<chapter>.md`), and five inbound
pointers: `installer-guide.md`, `systems/build-plugin-packaging.md`,
`systems/build-2d-3d-split.md`, `reference/assets-io.md`, `reference/rendering-3d.md`.
**Findings that matter — and to Phase 30:**

- **`GLFW_INSTALL` defaults `ON` and nothing in this tree turns it off, so every packaged
  distributable ships developer SDK files.** Verified against the staged `dist/Cosmic.zip`, not
  inferred: `include/GLFW/glfw3.h`, `include/GLFW/glfw3native.h`, `lib/glfw3.lib`,
  `lib/cmake/glfw3/*.cmake` and `lib/pkgconfig/glfw3.pc` — roughly 2 MB — land in an end-user app
  folder. GLFW is the **only** vendored dependency added through its own `add_subdirectory`
  (everything else is an `add_library` we declare, which has no install rules), which is exactly why
  it is the only leak. **README §40 claimed the opposite** in as many words: *"No `.lib`, `.exp`,
  `.pdb`, or CMake files are copied into `dist/`."* The `.lib`/`.exp`/`.pdb` half is right — both our
  `install(TARGETS …)` rules name `RUNTIME`/`LIBRARY` and deliberately omit `ARCHIVE`. One-line fix:
  `set(GLFW_INSTALL OFF)` before `add_subdirectory(dependencies/glfw)`. **Phase 30 candidate.**
- **A shipped app's `user://` does not go where the installer scripts say it does, in two separate
  ways.** `FileSystem::GetUserDataRoot()` branches on (a) whether `SetAppIdentity` ran and (b) a
  live writability probe of the exe directory. `SetAppIdentity` is called **only** from the
  `boot.cfg` path (`Main.cpp:100-101`) — never from `--project` — and `CosmicSetup.iss`'s shortcuts
  pass `--project <App>`, so a `package_installer.bat` app gets **no per-app isolation at all**: its
  root is the shared `%LOCALAPPDATA%\Cosmic`, not `%LOCALAPPDATA%\Cosmic\<ProjectName>` as
  `installer-guide.md` §4 claimed. Worse, a per-user install lands in `%LOCALAPPDATA%\Programs\<App>`,
  which **is writable by the installing user**, so the probe succeeds and the app runs in *portable*
  mode with its data inside the install folder — contradicting `CosmicSetup.iss`'s own header comment
  (*"goes to %LOCALAPPDATA%\Cosmic … NOT into {app}"*). Both `installer-guide.md` §4 and §8 were
  corrected; the chapter states the four-row truth table and tells the reader to trust the
  `user:// root -> …` boot log line instead. A defensible Phase 30 fix is to set the identity from
  `--project` too, or to have `CosmicSetup.iss` write a `boot.cfg` and drop the flag.
- **`build_all_release.bat` silently switches a 2D tree back to 3D.** `build_all.bat` reads
  `COSMIC_2D_ONLY` out of `CMakeCache.txt` *before* deleting `build/` and hands the flag back to the
  fresh configure; `build_all_release.bat` — otherwise its twin — does not. So the one script whose
  entire purpose is "a clean Release" is the one that cannot produce a clean 2D Release. Same family:
  **`package.bat` reconfigures without `-DCOSMIC_2D_ONLY` and therefore always packages the 3D
  engine**, with no `package_2d.bat` anywhere; the chapter documents the three-command manual
  workaround. **Phase 30 candidates**, and the second one is already noted in
  `systems/build-plugin-packaging.md`'s §6 plan as "packaging is single-configuration".
- **`Version.h`'s own KEEP-IN-SYNC list is incomplete, and Starforge's packager ignores it
  entirely.** The header names `Runtime/CosmicApp.rc` and `installer/CosmicSetup.iss`; `Starforge.rc`
  was added later, carries its own `FILEVERSION`/`PRODUCTVERSION`/two string values, and is missing
  from the list — so following the header literally leaves the editor's Explorer version behind.
  Separately, `Packager.h`'s `PackageInputs::Version` defaults to the **string literal `"0.9.0"`**,
  unconnected to `Version.h`; only `package_installer.bat` actually parses the header (`findstr`,
  whitespace token 3 — reformat the line and the version reads `0.0.0`).
- **README §34's OpenGL section was wrong in both directions.** Its `RendererAPI` listing showed
  seven virtuals; the real interface has ~25 — `SetViewport`, the five pipeline-state setters,
  `DispatchCompute`/`GpuMemoryBarrier`, `DrawArrays`, texture-slot and framebuffer-handle binds, the
  GPU-zone trio, and an `indexOffset` on `DrawIndexed`. And its GLAD paragraph said a failed load
  *"fires a `CS_CORE_ASSERT` and terminates immediately"* — that macro is compiled out in **every**
  configuration (D47), so a failed load continues with null function pointers and
  access-violates later. The version claim was the one thing it got right, and now for a stated
  reason: `Window.cpp:327-329` requests **4.5 core** and `dependencies/glad` is glad 0.1.36 generated
  `--api="gl=4.5" --profile="core"`, so `GLAD_GL_VERSION_4_5` is the loader's ceiling — a 4.6 driver
  is used as 4.5.
- **README §30's source map was Phase 1–13 archaeology.** It listed twelve directories; the tree has
  twenty-five. Entirely absent: `physics/`, `scripting/`, `reflect/`, `nav/`, `terrain/`, `water/`,
  `particles/`, `voxel/`, `math/`, `assets/`, `audio/`, `telemetry/`, `ui/`, `codes/` and
  `scene/ui/`. Rewritten against the tree, with 3D-only entries marked and a *2D partition* section
  that summarises `build-2d-3d-split.md` rather than forking it.
- **§1.5 had exactly one flag-level drift, and it is the one flag that does nothing.** `ls *.bat`
  matched the table's ten scripts exactly; `option(` across the three CMakeLists matched all seven
  options plus the two `set(… CACHE)` entries; `CS_KEY_` in `Window.cpp` returned the single `F11`
  the hotkey table lists. Missing was **`--replay <file>`** (`Main.cpp:47-61`) — added, with the
  honest note that nothing reads `COSMIC_REPLAY_FILE` (D59's finding, re-verified). The running
  section also gained **`Starforge.exe`** and the `boot.cfg` / `COSMIC_STARTUP_PROJECT` boot-order
  inputs, which "every command you can run against this SDK" had never mentioned.
- **Starforge's `branding/icon.png` does not survive packaging**, and the mechanism explains why:
  it reaches the dev tree through a `POST_BUILD` copy, not an `install()` rule, and it lives outside
  `assets/`. The generalisation is worth keeping — **the dev tree is populated by `POST_BUILD`
  copies and a package by `install()` rules; they are different mechanisms with different coverage.**
  Same reason `StarforgeEditor` never appears in a dist (only `CosmicApp` is installed) even though
  `projects/Starforge.dll` does.
- **The `.cham` file association is dead on both ends and is registered by two different scripts.**
  `installer/AppSetup.iss:59-61` and Starforge's generated script both write
  `HKCU\Software\Classes\.cham` → `<App>.exe --replay "%1"`, but the engine never writes a `.cham`
  file (`DataRecorder::Flush` writes `scene.bin` + `<name>.csv`; `DataPlayer::Load` accepts a
  directory or `.bin`) and nothing reads `COSMIC_REPLAY_FILE`. D59 flagged this for D61; the chapter
  now tells readers to leave the `[Registry]` section out unless they fix both halves.
- **The final sweep found four broken links in written chapters, none of them Phase C's.**
  `reference/cameras.md` pointed at `README.md#manifest` (the real anchor is
  `#coverage-manifest--every-public-header-maps-to-a-chapter`); `reference/physics.md:220` pointed at
  `#contactevent` where the heading is `### ContactKind / ContactEvent`;
  `reference/rendering-3d.md:41` cited a nonexistent "README §3D" (re-pointed at DG-7); and
  `systems/build-plugin-packaging.md` still listed README §40 as a body "migrating here". All fixed.
  D59's parting instruction — grep the guide tier for `Timestep` in a hook signature — found the two
  it predicted (`audio.md:43`, `assets-and-vfs.md:616`), both now `void MyLayer::OnUpdate(float ts)`.
  A scripted anchor+path check over the whole live `docs/` tree plus `README.md` now returns **zero**
  real failures (the only two hits are placeholder links inside `reference/README.md`'s
  entry-format template fence, which are illustrative by design).
- Smaller, all verified: **`build.bat` reconfigures on an `ENGINE_ONLY` mismatch but only *reports*
  the engine mode**, which is why `[MODE]` is the line to read when a build behaves oddly; the four
  `install()` rules are the entire packaging surface and the per-project pair lives **inside** the
  scanner's `if()`, so a skipped project is neither built nor installed; `package.bat` deletes the
  **whole** `dist/` folder at stage 3, not just its target; only the zip stage is soft-failing
  (`[WARN]` and continue) and the installer flow consumes the staged folder rather than the zip;
  `CosmicApp.manifest` is embedded into **both** hosts by CMake's automatic `mt.exe` handling and
  declares `PerMonitorV2` + `longPathAware`; `CosmicApp.rc` defines the icon twice on purpose —
  `IDI_ICON1` for Explorer and the named `GLFW_ICON` resource that GLFW looks up itself, so no
  `glfwSetWindowIcon` call is needed; `ExeResources::SetIcon` must run **before** signing because
  `UpdateResource` invalidates a signature; `Packager::Stage` falls back to the newest
  `<name>_hotN.dll` when no base project DLL exists, renaming it, because the editor only ever emits
  hot-reload DLLs; `Packager` skips `build`, `.git`, `.starforge`, `src`, `CMakeLists.txt`, `.vs` and
  `.gitignore` when copying project content; only two of the six projects (`Engine3DDemo`,
  `SF_Telem`) ship a standalone `Projects/<name>/build.bat`, and it configures a **separate** cache
  under `Projects/<name>/build/` against `%COSMIC_SDK%`; `setup.bat` sets `COSMIC_SDK` (not
  `COSMIC_SDK_DIR` — D46's finding, re-confirmed) and is build-time only; `CosmicTests` has **no**
  install rule anywhere in the tree, so it can never reach a package; and `CMakePresets.json`'s two
  presets deliberately share one `binaryDir` because `COSMIC_SDK_DIR` is the *source* directory —
  the worktree, not a second build folder, is the supported way to hold both configurations.

---

### 📋 Copy-paste prompts (D46 → D61)

**Prompt 1 / D46 — README overview + `getting-started.md`**

```
Execute work order D46 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md IN FULL first — the authoring contract, the
verification bar and the document format are binding.

WRITE docs/guide/getting-started.md from scratch, against the source: what Cosmic is, first-time
setup (setup.bat / COSMIC_SDK), building, creating a project, project layout, the minimal
plugin skeleton, and the TWO build configurations. Verify every claim by reading Runtime/Main.cpp,
the launcher, the root CMakeLists and the template project — the README's version of this predates
projects-anywhere-on-disk (Phase 16 S1), the dedicated Starforge.exe (S2/S3), packaging v2 (S5),
per-app user:// isolation (S6) and the engine split (Phase 29). Assume all of it is wrong until
checked.

THEN rewrite the README's §1 body as a newly written 2-4 paragraph overview + a link to the
chapter, keeping the heading and its number. Rewrite the "What is Cosmic?" opener honestly against
the tree: C++20, OpenGL 4.5 core, Windows x64, 2D + 3D, plugin-DLL model, two build
configurations, simulation/telemetry tooling. No marketing. Do NOT touch §1.5 or §1.6.

Insert diagram DG-1 into the chapter. Update the README ToC and the Status cell in
docs/guide/README.md. Report per standing rule 7 — especially what the old §1 got wrong; that
list is signal for the fifteen work orders after this one. Leave edits in the working tree.
```

**Prompt 2 / D47 — `project-anatomy.md` + `logging-and-diagnostics.md`**

```
Execute work order D47 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/project-anatomy.md — the plugin-DLL model (how a project is loaded, the export
  macros, hot reload), the Application lifecycle and frame loop, the layer system and LayerStack
  ordering, Ref/Scope and the SHARED-ALLOCATOR rule (why a project must not free engine memory),
  the Safe Zone teardown ordering, and the composite-layer pattern. Sources: core/Application.h,
  core/Layer.h, core/LayerStack.cpp, core/Core.h, layers/*, Runtime/Main.cpp.
- docs/guide/logging-and-diagnostics.md — CS_CORE_* vs CS_* loggers, sinks, log files under
  user://logs, the editor Console panel, renderer stats counters, and the GPU profiler.
  GOTCHA to document: Renderer2D::StatsEnabled defaults to FALSE and nothing arms it
  automatically; Renderer3D's counters are always-on but nothing resets them, so they read as
  lifetime totals. Both have bitten.

Cover what the README never did (A6-class gaps): PlayerLayer and WorkspaceLayer are both
client-reachable now, and owner-ticked services (SceneManager, SerialLink, ScriptHost) are the
established lifecycle pattern.

Retire README §2, §3, §4 and §19 per standing rule 6. Insert DG-3 and DG-11 in project-anatomy.md.
Report per rule 7. Leave edits in the working tree.
```

**Prompt 3 / D48 — `events-and-input.md` + `time-and-ticks.md`**

```
Execute work order D48 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/events-and-input.md — the Event hierarchy and category filtering, how events
  propagate through the layer stack, EventDispatcher, handled-flag semantics, Input polling, and
  the full keyboard / mouse / GAMEPAD code tables. Generate every table from codes/KeyCodes.h,
  codes/MouseButtonCodes.h and codes/GamepadCodes.h — do not copy the README's, assume they have
  drifted. Gamepad support has existed since Phase 2 and the README mentions it ZERO times, so
  that section is entirely new: polling API, deadzones, the template-project example.
  Also cover WindowFileDropEvent (Phase 23).
- docs/guide/time-and-ticks.md — Timestep, the global time scale, pause vs TimeScale(0) (keep a
  table; this distinction is subtle and the old §7 explained it well — verify then reuse),
  per-layer local time, and the fixed-vs-variable dual-rate model. Cross-link
  docs/reference/physics.md: the physics fixed-step contract is the load-bearing consumer.

Retire README §5, §6, §7. Insert DG-4 in events-and-input.md and DG-10 in time-and-ticks.md.
Report per rule 7. Leave edits in the working tree.
```

**Prompt 4 / D49 — `entities-and-components.md`** *(XL — stronger model or your review)*

```
Execute work order D49 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first. This is the XL item — if it runs
long, split after the component catalogue.

WRITE docs/guide/entities-and-components.md from scratch. This is the chapter a game author lives
in, and the README's §15 predates almost all of it.

Must cover:
- entt underneath, Entity as a {handle, scene*} wrapper, Add/Get/Has/Remove, validity.
- THE COMPONENT CATALOGUE — the centrepiece. Every component in scene/Components.h (19) and
  scene/Components3D.h (15), each with its fields, units, defaults, and WHO CONSUMES IT (which
  pass or system reads it). Read both headers exhaustively; do not work from any list.
- Which components exist only in the 3D configuration, and what a 2D build sees.
- Hierarchy: RelationshipComponent, Scene::SetParent, GetWorldTransform, cycle refusal.
- Per-entity Active + Scene::IsActiveInHierarchy, and per-component Enabled gates (Phase 23).
- The System base + ComponentRegistry, and how parallel systems query (jobs/SystemQuery).
- What Scene::OnRender3D / OnRenderSprites draw AUTOMATICALLY — the contract that tells an author
  what NOT to draw by hand.

Retire README §15. Insert DG-9. Do NOT re-derive the Phase 29 split table — link
../systems/ecs-scene.md. Report per rule 7. Leave edits in the working tree.
```

**Prompt 5 / D50 — `scenes-and-serialization.md` + `scripting.md`**

```
Execute work order D50 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/scenes-and-serialization.md — creating and loading scenes, the .cscene format and
  what drives it (the reflection registry, not hand-written code), UUIDs and EntityRef fields,
  PREFABS (create/instantiate/apply/revert), SceneManager async load + fade transitions,
  CommandStack undo/redo, and OpaqueComponentsComponent — the guarantee that a scene opened by a
  build lacking some component type loses nothing. The README documents none of this.
  GOTCHAS: SaveToString uses dump(2) (pretty, not compact); nlohmann sorts object keys on parse,
  so round-trips are semantically stable but not byte-stable against the original file; reflected
  field names are Tag.Tag and Transform.Position.
- docs/guide/scripting.md — the C++ script tier, which has NEVER had client documentation:
  ScriptableEntity and its lifecycle hooks (OnCreate/OnUpdate/OnFixedUpdate/OnDestroy and the
  collision/trigger callbacks), module registration via ModuleMacros (CS_SCRIPT/CS_FIELD),
  ScriptHost, the SystemScript tier for class-of-entity logic, hot reload and its edit-mode-only
  limit, and every proxy: Physics(), Character(), Flow(), Nav(), Signals(), Telemetry(), Animator(),
  Voxels(). Note which proxies do not exist in the 2D build.
  Sources: scripting/*.h, plus the template scripts under assets/templates/src/scripts/ and
  Projects/ForgeIsle/src/scripts/ as worked examples.

Retire README §23 and §21 (the template project is covered by getting-started.md — leave §21's
heading with a pointer). Report per rule 7. Leave edits in the working tree.
```

**Prompt 6 / D51 — `rendering-2d.md` + `materials-and-shaders.md`**

```
Execute work order D51 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/rendering-2d.md — BeginScene/EndScene, every DrawQuad overload, rotated quads,
  SubTexture2D sprite sheets, SDF circles, lines, text, the instanced path and when it wins,
  RenderPass multi-camera, and the stats counters. Then the section Phase 30 depends on:
  EVERY BATCH LIMIT and the flush behaviour at each — MaxQuads, MaxTextureSlots, MaxLines,
  MaxCircles, MaxTextQuads, MaxInstancedQuads, MaxInstancedCircles. Read them out of
  renderer/Renderer2D.cpp and cite the file. Document that StatsEnabled defaults to FALSE.
  Also: 2D now composites through SceneRenderer's HDR -> tonemap -> overlay spine rather than
  drawing straight to the backbuffer, and BlendMode::Multiply exists (Phase 27).
  The old §8/§11-§14 is the best-preserved material in the whole README — mine it hard, but
  verify every signature against renderer/Renderer2D.h first.
- docs/guide/materials-and-shaders.md — loading shaders, creating and configuring Materials,
  cached uniforms, the shader contract (preprocessing, includes, the uniform/binding conventions),
  MaterialAsset/.cmat files, material SLOTS on multi-submesh meshes (Phase 24), and framebuffers
  including MRT and ReadPixels. Link ../reference/graphics-resources.md for BindingPoints.

Retire README §8, §11, §12, §13, §14 and §9, §10, §18. Report per rule 7. Leave edits in the
working tree.
```

**Prompt 7 / D52 — `sprites-and-tilemaps.md` + `game-ui.md`**

```
Execute work order D52 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.
Neither of these topics has EVER had client documentation. Write both from the source.

- docs/guide/sprites-and-tilemaps.md — authoring a 2D game: SpriteRendererComponent, sort order
  and the painter list, SpriteAnimationComponent flipbooks, TilemapComponent (atlas, cell array,
  kMaxGrid = 1024, the culled draw), Light2DComponent + the 2D light composite and Ambient2D,
  and the Camera2DController rig (pan/zoom, ScreenToWorld, FrameBounds). Say which parts are
  editor-authored vs code-driven. ForgePong is the worked example.
- docs/guide/game-ui.md — in-game UI as ENTITIES (not ImGui): CanvasComponent, RectTransform and
  the anchor/pivot model, UiImage/UiText/UiButton, the hit-test and button state machine, canvas
  scaling, UiWorldAnchorComponent for nameplates and prompts, and UiImageComponent::RuntimeTexture
  fed by SceneRenderer::RenderToTexture (the minimap pattern). Sources: scene/ui/*.
  Be explicit that this is a DIFFERENT system from the ImGui editor chrome in
  editor-ui-and-theming.md, and cross-link both ways — the two are easy to confuse.

Report per rule 7. Leave edits in the working tree.
```

**Prompt 8 / D53 — `flow-and-story.md` + `cameras.md`**

```
Execute work order D53 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/flow-and-story.md — never documented. The .cflow screen-flow graph (nodes,
  transitions, guards, actions, the FlowMachine runtime, Scene::ActiveFlow, the Flow() proxy),
  flow VARIABLES (typed blackboard: Bool/Number/String/Enum, guards and setVar), the .cstory
  dialogue runtime (StoryGraph/StoryNode/StoryOption/StoryRunner, guards, Once, signals), the
  StoryUiBinding stock script, and EventBus signals as the glue. Sources: scene/FlowMachine.*,
  scene/StoryGraph.*, scene/EventBus.h. FlowDemo is the worked example.
- docs/guide/cameras.md — the camera class hierarchy, orthographic/perspective, and every
  controller: OrbitCameraController (NavStyle, ViewPreset, frame-snap), FlyCameraController,
  Camera2DController, OrthographicCameraController. Then CAD navigation end to end: the
  NavigationCube widget, entity picking via ScenePicker's ID buffer and its viewport-pixel
  coordinate contract, and Gizmo/ImGuizmo transform manipulation with its hotkeys.
  The README mentions FlyCamera and OrbitCamera zero times. NavigationCube and ScenePicker are
  3D-configuration only — say so.

Retire README §16. Report per rule 7. Leave edits in the working tree.
```

**Prompt 9 / D54 — `rendering-3d.md`** *(XL — stronger model or your review)*

```
Execute work order D54 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE docs/guide/rendering-3d.md from scratch — the single biggest documentation gap in the repo.
The README has 2 incidental mentions of Renderer3D and nothing else.

Must cover:
- Submission, not immediate drawing: DrawMesh/DrawModel submit; the queue culls, sorts by a packed
  key, auto-instances identical runs, and only then touches the GPU. Explain what that means for
  the caller.
- THE BREAKING-CHANGE BOX, prominently: material values are read AT FLUSH, not at submit. Per-draw
  variation requires Material::Clone. Mid-scene state islands require Renderer3D::Flush().
  This has bitten every project that migrated; it is the most important paragraph in the chapter.
- Meshes and models: Mesh, primitive builders, MeshData (GL-free) vs Create* uploaders, Model,
  glTF/FBX import, submesh ranges and per-slot materials.
- Transparency (SetTransparent -> back-to-front, depth-write off), frustum culling and its opt-out,
  auto-instancing preconditions (>=4 run, an instancing-shader twin, entityID -1), InstanceSet,
  LODGroupComponent (casters use the lit level), and the Statistics counters.
Sources: renderer/Renderer3D.*, renderer/RenderQueue.h, renderer/InstanceSet.*, math/Frustum.h,
graphics/Mesh.h, graphics/Model.h, tests/test_render_queue.cpp (the tested truth), and
Projects/Engine3DDemo's panels — each toggle has code, mine it for examples.

Insert DG-7. State up front that this chapter is 3D-configuration only and link
../systems/build-2d-3d-split.md.
Acceptance: a reader can draw a lit glTF model and a 5,000-instance field from this chapter alone.
Report per rule 7. Leave edits in the working tree.
```

**Prompt 10 / D55 — `lighting-and-environment.md` + `world-systems.md`** *(stronger model)*

```
Execute work order D55 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch, both 3D-configuration only (say so, link ../systems/build-2d-3d-split.md):
- docs/guide/lighting-and-environment.md — LEAD with the SceneRenderer quickstart: building a
  SceneRenderDesc and calling Render() is what most apps should do, and driving passes by hand is
  an advanced footnote. Then the pass graph in order, lights (directional/point, the UBO), PBR +
  IBL, sky modes including physical atmosphere (Phase 27), time-of-day, shadows, and the post
  chain with every toggle (SSAO, bloom, FXAA, tonemap, fog, god rays, heat haze, vignette).
  Insert DG-8. Do NOT fork docs/design/frame-lifecycle.md — summarise and link it.
- docs/guide/world-systems.md — minimal working setups for terrain, water and particles, each
  naming its exemplar under Projects/. Terrain MUST state the 32*2^k+1 resolution rule and the
  async-build/loading-screen pattern; water MUST cover the reflection handoff and underwater;
  particles lead with presets, then custom emitters and curl noise. Cover the E18 recipe model —
  components carry PODs, assets are derived — because that is how these are authored now.

Source: Projects/Frontier's IslandWorld is the everything-example; Engine3DDemo's World Systems
panel; docs/plans/archive/05 §5-§9. Report per rule 7. Leave edits in the working tree.
```

**Prompt 11 / D56 — `voxels.md` + `animation.md`**

```
Execute work order D56 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.
Neither topic has ever had client documentation. Both are 3D-configuration only — say so.

- docs/guide/voxels.md — VoxelVolumeComponent and the block palette, chunks and residency, the
  mesher, editing voxels from code and from the editor brush, world generation, and voxel
  collision (one static body per resident chunk, rebuilt when dirty). ForgeBlocks is the sample.
  Sources: src/voxel/*, the voxel half of physics/ScenePhysics.cpp.
- docs/guide/animation.md — skeletal animation end to end: Skeleton and AnimationClip, importing
  skinned meshes (glTF + FBX), AnimatorComponent, playing and scrubbing clips, CrossfadeTo and the
  blend model, joint SOCKETS (attaching entities to animated joints via SocketComponent and
  GetWorldTransform composition), and GPU skinning through SSBO binding 10 with its shadow twin.
  Note that the animation state-machine/blend-tree tier is explicitly parked (FEATURE-MATRIX).
  Sources: graphics/Skeleton.h, graphics/AnimationClip.h, the Animator component and proxy.

Report per rule 7. Leave edits in the working tree.
```

**Prompt 12 / D57 — `physics.md` + `navigation-and-ai.md`**

```
Execute work order D57 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/physics.md — authoring physics in a scene: RigidBodyComponent and the three motion
  types, box/sphere/capsule/mesh/terrain colliders, triggers, the collision filter (category/mask,
  two-sided), the CharacterControllerComponent walk model (Move/Jump/IsGrounded), queries from a
  script via the Physics() proxy, contact callbacks, and the fixed-step tick order. Finish with
  swapping the backend (IPhysicsBackend + the registry) at a usage level.
  IMPORTANT: physics ships in BOTH build configurations — only mesh and terrain-heightfield
  colliders are 3D-only. That is a common wrong assumption; state it plainly.
  Per-call detail already exists in docs/reference/physics.md — LINK it, do not duplicate it.
  This chapter is the task-oriented half: "make a crate fall", "make a character climb stairs",
  "make a trigger fire once".
- docs/guide/navigation-and-ai.md — 3D only. NavMeshComponent and baking (collision-sourced, the
  signature gate, the .cnav sidecar, the editor Regenerate button), NavAgentComponent and the
  crowd, steering an agent from a script via Nav(), and the nav.arrived signal. Note that the
  crowd steps AFTER physics in the play tick. Sources: nav/*, scene/SceneNav.*.

Report per rule 7. Leave edits in the working tree.
```

**Prompt 13 / D58 — `assets-and-vfs.md` + `audio.md` + `sim-math-toolkit.md`**

```
Execute work order D58 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/assets-and-vfs.md — the AssetLibrary cache (textures, shaders, materials, meshes,
  clips) and what it does on a miss, the engine:// project:// user:// schemes and their resolution
  order, dev-tree vs packaged paths (verify against Runtime/Main.cpp), PER-APP user:// isolation
  and portable mode (Phase 16 S6 — the README predates it entirely), importing models and
  textures, .cmeta sidecars, TOML configuration via Config, and the utility surface: FileDialog,
  FileWatcher, ImageIO, DataExport.
- docs/guide/audio.md — one-shots, looping sounds, groups, volume, and headless behaviour.
  DOCUMENT THE GOTCHA: miniaudio is initialised with MA_COINIT_VALUE=STA deliberately, because an
  MTA init put the UI thread in the wrong apartment and deadlocked IFileDialog::Show — every
  native dialog in the app was broken for two weeks. Anyone changing audio init needs to know.
- docs/guide/sim-math-toolkit.md — integrators (RK4, semi-implicit Euler, FixedSubstepper),
  filters (LPF, derivative, rate limit, biquad, washout), LookupTable 1D/2D, Noise, deterministic
  PCG32 Random, and Spatial frame conventions (NED vs Y-up). Include a when-to-use table and a
  determinism box. Projects/ViperSim is the usage exemplar; cite the doctests per header.

Retire README §17. Report per rule 7. Leave edits in the working tree.
```

**Prompt 14 / D59 — `serial-and-telemetry.md` + `jobs-and-parallelism.md`**

```
Execute work order D59 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/serial-and-telemetry.md — SerialPort (open/read/write/close, async BeginOpen),
  SerialLink as an owner-ticked service with auto-reconnect, COBS framing, defining telemetry
  channels, DataRecorder columnar recording + autosave, DataPlayer replay and the --replay flag,
  the TelemetryPanel, and entity selection.
  CRITICAL A3-class gotcha: the docstrings say "v3" but the code writes v1. Verify the format
  against the .cpp, never the comments. SF_Telem is the worked example.
- docs/guide/jobs-and-parallelism.md — JobSystem (submit, wait, the worker pool), ParallelFor,
  ParallelSystem and SystemQuery for parallel ECS iteration, ComponentArray, DoubleBuffer, and
  the JobSystem stats surface (Phase 23 T18). Be explicit about what is safe to touch from a
  worker thread and what is main-thread only — that is the whole reason this chapter exists.
  Insert DG-12 and DG-13.

Retire README §20, §26 and §22. Report per rule 7. Leave edits in the working tree.
```

**Prompt 15 / D60 — `windowing-and-viewport.md` + `editor-ui-and-theming.md`**

```
Execute work order D60 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first.

WRITE from scratch:
- docs/guide/windowing-and-viewport.md — the Window client surface, borderless custom chrome, DPI
  handling, fullscreen (F11 and SetFullscreenHotkeyOverride), the responsive render/pause contract
  during drag and resize (summarise and link docs/design/responsive-rendering-and-pause.md, do not
  fork it), Window::SetIcon/ClearIcon and drop-a-file branding, and viewport visibility + center
  docking with the screen-pixel mouse contract (GetViewportPos/GetViewportSize).
  The old §29 is 1,128 lines — 23% of the README — and is the densest, most load-bearing material
  in it. MINE IT HARD: the contracts are real even where the surrounding prose has drifted. Verify
  each against core/Window.cpp and layers/WorkspaceLayer.* before restating it.
  [D60 CORRECTION, 2026-07-26: the "1,128 lines" claim is false — §29 was 27 lines. The four
   retired sections total 282. The dense material was in §24, not §29. See §1 decision 1b's
   correction box and D60's findings block.]
- docs/guide/editor-ui-and-theming.md — ImGuiLayer, the docking model and port-mode
  DockWindow(name, DockPort::...) with the NEVER-persist-dock-node-ids rule,
  WorkspaceLayer::SetBottomInsetPixels, ThemeManager and the Theme Studio, Fonts and Lucide icons,
  the Widgets and PlotStyle helpers, and Overlay/image helpers.
  Cross-link game-ui.md both ways: this chapter is EDITOR chrome; that one is in-game UI entities.

Retire README §24, §29, §27, §28. Insert DG-5. Report per rule 7. Leave edits in the working tree.
```

**Prompt 16 / D61 — `building-and-shipping.md` + README Part II pass + final sweep**

```
Execute work order D61 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §7's standing rules and docs/guide/README.md in full first. This closes Phase C.

1. WRITE docs/guide/building-and-shipping.md from scratch — the two build configurations and how
   to choose (link README §1.6), every CMake option, the build scripts, packaging (cmake --install
   staging -> dist/<Name> prune -> zip), the Inno installer flow, the exe icon and VERSIONINFO,
   and what a shipped folder actually contains. Verify against the .bat files, the three
   CMakeLists.txt and installer/CosmicSetup.iss. Link docs/installer-guide.md.
2. Refresh README §40 as an overview + link to that chapter (§40 is Part II; its architecture prose
   later goes to systems/build-plugin-packaging.md under D34 — do not move it here).
3. Re-run the §1.5 verification sweep and fix drift: `ls *.bat`; grep `--` flags in
   Runtime/Main.cpp; grep `option(` in the three CMakeLists.txt; grep CS_KEY_ in Window.cpp.
4. README Part II pass: add §42.5, a directory table with one line per docs/systems/ document (21
   now). Insert remaining diagrams DG-2 (§30), DG-6 (§35). Stale-claims sweep — §34's OpenGL facts
   (the loader is 4.5 core) and §43, which becomes a pointer to the roadmap and FEATURE-MATRIX
   rather than a hand-maintained list. Add the Phase 29 partition to §30's source map.
5. Retire §25 (Complete API Reference Tables): replace with a pointer to docs/reference/. Do not
   maintain a second copy of the API surface.
6. FINAL SWEEP: every link in README.md, docs/README.md and docs/guide/README.md resolves; every
   chapter's Status cell is accurate; no `README §<n>` link anywhere in docs/ points at a retired
   body; no content exists in two places.

Report per rule 7, plus a Phase C summary: what the guide tier now covers, and what it does not.
Leave edits in the working tree.
```

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

> **What D61 already did to Part II (2026-07-26) — read before absorbing anything.** Three of the
> sections in the *Absorbs* column above have already changed and the table's expectations are
> partly stale. **§40 was retired**: its how-to went to `guide/building-and-shipping.md` and its
> heading now carries a six-paragraph architecture overview, so **D34 has no §40 body to migrate** —
> it inherits an overview to deepen, not prose to move. **§30 was rewritten** against the current
> tree (the old map listed 12 of 25 directories) and gained **DG-2** plus a *2D partition* section,
> so **D25 absorbs a correct, current §30**. **§34's** `RendererAPI` listing and GLAD paragraph were
> corrected and **§35 gained DG-6**, so **D29 inherits accurate text** — the old §34 claimed a
> compiled-out assert would terminate on GLAD failure, and listed 7 virtuals against a real ~25.
> **§31's** unload ordering was fixed against `Application.cpp`. **§43 is now a pointer** to the
> roadmap and FEATURE-MATRIX, not a hand-maintained list. **New §42.5** indexes all 21 systems
> documents — when you finish an explainer, its row there needs no change (it is a directory, not a
> status board), but **D25's §5 directory table and D36 must stay consistent with it**.

### 📋 Copy-paste prompts (D25 → D34)

**Standing preamble — every D25–D34 prompt inherits it.**

```
STANDING RULES for any D25-D34 system explainer (read once, they apply to every item):

1. Read docs/plans/12-documentation-plan.md §0 and §8, then your explainer's skeleton. The skeleton
   carries its section plan, assigned diagrams and truth sources — follow it. Headers are truth.
2. Use the mandatory shape from
   docs/systems/README.md#document-format-mandatory--every-explainer-uses-this-shape:
   1 Overview, 2 Mental model, 3 How it works step by step, 4 Technical implementation,
   5 Design decisions and trade-offs, 6 Limits and future work.
3. **THE PLAIN-LANGUAGE BAR IS THE POINT OF THIS TIER AND IT IS WHAT RUSHED SESSIONS DROP.**
   §1-§3 must survive the "smart friend test": a reader who has never written a shader follows them.
   Define every term at first use ("a framebuffer — an off-screen image the GPU draws into").
   §4 onward may assume C++ literacy.
4. **THE GUIDE TIER IS WRITTEN. DO NOT RE-DERIVE IT.** All 29 chapters in docs/guide/ landed in
   Phase C, written from source, and most skeletons already carry a "Read first / don't re-derive"
   note naming yours. The division is: the GUIDE is the outside of the API (how do I use this, what
   fails, what are the pitfalls); the EXPLAINER is the inside (how does it actually work, what is
   the data layout, why THIS design, what was rejected). Link the guide chapter; never restate it.
5. Where a design doc exists (docs/design/frame-lifecycle.md, water-rendering-notes.md,
   responsive-rendering-and-pause.md) the explainer SUMMARIZES AND LINKS — it never forks the spec
   (note 7).
6. §4 must be source-grounded: real files, real class names, real binding points. Quote key
   constants WITH the file they come from so a reader can verify.
7. Diagrams: at least one Mermaid per document. THIRTEEN ARE ALREADY BUILT (DG-1..DG-14 except
   DG-15/16) — §4's table says where each lives. REUSE BY LINK; never redraw. Build only one §4
   assigns to you and marks unbuilt.
8. If your item has an "Absorbs README Part II" entry: convert that README section to a faithful
   2-3 paragraph summary + a "Full internals:" link IN THE SAME WORK ORDER, carrying every fact
   into the explainer. Anything dropped must be verified stale and noted. HEADINGS AND NUMBERS STAY
   (note 6, frozen numbering). Read the D61 banner above §8's prompts first — several Part II
   sections already changed.
9. Bookkeeping (note 8): delete the STATUS: SKELETON banner, flip your row in docs/systems/README.md
   to "✅ WRITTEN — D#" (touch ONLY your row), set your item's status in doc 12 §8.
10. Report: what it covers · what you found that contradicts the skeleton or a source comment ·
    what you left out and why · anything unverifiable. Log engine defects into §7 as Phase 30
    candidates with file:line.
11. No git write commands. Leave edits in the working tree.
```

Then one line per item:

```
Execute work order D25 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainer: docs/systems/architecture-overview.md. DO THIS ITEM FIRST — it is the map everything else
is a territory of — but write its §5 directory table LAST, against the docs that have actually
shipped, and revisit its cross-links in D36. Absorbs README §30 (source map), which D61 already
rewrote against the current tree and which now carries DG-2 and a 2D-partition section: absorb the
CURRENT text, not the archaeology the skeleton may still describe. Reuse DG-1 (built in
guide/getting-started.md) and DG-2 (built in README §30). Cover: the module map, the host-exe /
engine-DLL / project-DLL split, and one frame end to end. Note the engine now builds in TWO
configurations and link systems/build-2d-3d-split.md rather than restating the exclusion table.
```

```
Execute work order D26 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/core-runtime.md AND docs/systems/windowing.md — one session. Absorbs README
§32 (time waterfall) and §33 (the double-tick trap). WARNING ON §32: its accumulator code sketch is
STALE — the spiral-of-death clamp is on the FRAME TIME, not the accumulator (D48 verified). Reuse
DG-3, DG-10, DG-11 (all built in guide/project-anatomy.md and guide/time-and-ticks.md) and DG-5
(the plugin-DLL lifecycle, built in project-anatomy.md). Facts to carry: Application::Run's
structure and the s_Instance ordering constraint that makes Get() work during Initialize;
LayerStack insert-index mechanics; transition queueing into the Safe Zone; the GLFW SINGLE-WINDOW
CONSTRAINT carried here from the retired README §24 — glfwTerminate() is called from ~Window
(Window.cpp:565-570), a GLOBAL teardown that is safe only because the engine is single-window.
Window's constructor logs CS_CORE_CRITICAL and returns EARLY on a context failure, leaving a null
handle behind an assert compiled out everywhere. Guide chapters: project-anatomy.md,
time-and-ticks.md, windowing-and-viewport.md.
```

```
Execute work order D27 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/events-input.md AND docs/systems/cameras-navigation.md — one session.
Absorbs README §41 (event system implementation). Reuse DG-4 (built in guide/events-and-input.md).
Cover the event type/category bitmask machinery, EventDispatcher mechanics, and WHY blocking is
conditional (BlockEvents(false) while the viewport is hovered) — that conditionality is the part
a reader cannot infer. Gamepad polling internals and key-repeat semantics. For cameras-navigation:
the camera hierarchy, orbit/fly controllers, the CAD-style navigation model, the ViewCube, picking
and gizmos — and note this document is about CAMERA navigation, NOT pathfinding; navmesh navigation
has NO systems explainer at all and guide/navigation-and-ai.md is its only documentation.
Guide chapters: events-and-input.md, cameras.md.
```

```
Execute work order D28 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/rendering-2d.md AND docs/systems/rendering-3d.md — one session, L, use a
stronger model for rendering-3d. Absorbs README §36 (batch rendering deep dive) and §38 (RenderPass
implementation — put it wherever it fits, likely rendering-2d). Reuse DG-6 (built in README §35) and
DG-7 (built in guide/rendering-3d.md). For 2D: buffer sizes, the texture-slot limit, the SDF circle
path, the line path, when the instanced path wins, text/atlas rendering, RenderPass interaction,
stats counters. For 3D: the sorted queue end to end, and the material-read-at-flush semantics that
guide/rendering-3d.md documents from the outside — explain WHY the queue defers, which is the design
decision behind the pitfall. Both must state the configuration split and link build-2d-3d-split.md.
```

```
Execute work order D29 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainer: docs/systems/rendering-pipeline.md. XL — MAY SPLIT (passes/post first, then PBR-IBL-shadow
theory). Use a stronger model. Absorbs README §34 and §35 — but D61 already CORRECTED §34's
RendererAPI listing and GLAD paragraph and BUILT DG-6 into §35, so absorb the corrected text and
KEEP DG-6 in the README, moving only the prose. THE PBR SECTION MUST EXPLAIN, NOT NAME-DROP: a
reader who does not know what a BRDF is should finish §3 understanding roughly why the split-sum
approximation exists. Reuse DG-8 (built in guide/lighting-and-environment.md). Summarize and LINK
docs/design/frame-lifecycle.md — never restate it. State clearly that SceneRenderer and
PostProcessStack ship in BOTH configurations and only the world-content half fences out.
```

```
Execute work order D30 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/terrain.md AND docs/systems/water.md — one session, L. Absorbs no README
section. Terrain: heightmap composition, quadtree LOD, splat/triplanar materials, CPU height queries,
and the 32·2^k+1 resolution rule with its reason. Water: Gerstner waves, planar reflection and
refraction, underwater rendering, buoyancy — and SUMMARIZE AND LINK docs/design/water-rendering-notes.md
rather than forking it (note 7). Both are 3D-only; say so and link build-2d-3d-split.md. Terrain::Create
and Water::Create are pure CPU with GL allocated lazily in EnsureGpuResources on first render — that
is the design decision that makes async world building possible, so it belongs in §5. Guide chapter:
world-systems.md (currently the only documentation of either subsystem).
```

```
Execute work order D31 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/particles.md AND docs/systems/ecs-scene.md — one session, M. Absorbs no
README section. Particles: GPU pools, compute-shader simulation, billboards and ribbons, presets, and
the curl-noise path that must stay in lockstep between the compute shader and its StepCpu twin —
explain why a CPU twin exists at all. ECS: the entt-backed model, components, systems, scene render
hooks. Reuse DG-9 (built in guide/entities-and-components.md). THE LOAD-BEARING FACT for ecs-scene:
Scene::OnUpdate/OnFixedUpdate have NO ENGINE CALLER — the whole four-pass pipeline lives inside two
methods only a project calls (TemplateTelemetryLayer.cpp:305,313 are the only in-tree callers). That
is a design decision that needs explaining in §5, not a footnote. Particles are 3D-only; the ECS is not.
```

```
Execute work order D32 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/assets-vfs.md AND audio.md AND math-sim-toolkit.md — three small, one
session, M. Absorbs README §37 (shader preprocessing). Cover: AssetLibrary cache keys and lifetime,
the model import path (cgltf/assimp, tangents, PBR factor and texture import, winding fixes), the
#type shader-block preprocessing (mine §37), texture decode-from-memory, and the mip/sRGB policy.
THE VFS SECTION IS THE ONE TO GET RIGHT: three schemes, two project:// mount modes (NAME vs PATH,
last-setter-wins), and the user:// resolution that depends on BOTH SetAppIdentity (set only by
boot.cfg) and a live writability probe. THE OLD "DLL-side resolution rule" IS OBSOLETE — the mount
moved into the engine DLL in Phase 20/A1; four in-tree comments still teach it, do not carry it
forward. AssetLibrary has NO LOCKING and constructs GPU resources in its getters, so it is strictly
main-thread — that is a design decision worth §5 treatment. Guide chapters: assets-and-vfs.md,
audio.md, sim-math-toolkit.md — all three are currently the ONLY documentation of their subsystems.
```

```
Execute work order D33 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/jobs-parallelism.md AND serial-telemetry.md — one session, L. Absorbs
README §39 (parallel pipeline architecture) and §42 (telemetry implementation). Reuse DG-12 and
DG-13 (both built in the guide tier). Jobs: the worker pool (logical cores minus 1, floored at 1,
no override), the four-pass parallel ECS model, double buffering, and WHY WaitIdle is a GLOBAL
barrier rather than per-caller — that is the design decision behind ParallelSystem's prohibition.
Explain the ParallelForAsync serial fast path as a design trade-off: below minChunkSize it runs
synchronously, which makes by-reference captures safe there and dangling above it. Telemetry:
columnar channel storage, the v1 binary format (the headers are clean now; only DataRecorder.cpp:257
still says "v3"), recording, replay. Both chapters' guide counterparts are currently the only written
documentation of these subsystems.
```

```
Execute work order D34 from docs/plans/12-documentation-plan.md. Apply the D25-D34 standing rules.
Explainers: docs/systems/ui-theming.md AND build-plugin-packaging.md — one session, L. Absorbs
README §31 (hot-reloadable DLL architecture). **§40 IS ALREADY RETIRED — D61 moved its how-to to
guide/building-and-shipping.md and left a six-paragraph architecture overview in place, so there is
NO §40 body to migrate.** Read build-plugin-packaging.md's "Read first" note before starting: the
guide chapter already carries the per-option consequences, the four install() rules, DG-14, a
verified staged-folder listing, the boot order and the user:// table. YOUR job is mechanism and
rationale: COSMIC_API export/import, the shared-allocator requirement across the DLL boundary, the
engine GLOB without CONFIGURE_DEPENDS vs project globs with it, why COSMIC_2D_ONLY is the only
PUBLIC define, the mode-derived skip-list with its COSMIC_SKIP_PROJECTS_APPLIED staleness guard,
and /MP (parallel FILES, where cmake --build --parallel gives parallel PROJECTS). Reuse DG-5 and
DG-14. Two verified defects belong in §6: GLFW_INSTALL defaults ON so every dist ships
include/GLFW + lib/glfw3.lib, and build_all_release.bat does not preserve the engine mode.
For ui-theming: ImGui integration, the docking model, ThemeManager, fonts/icons, widgets.
```

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

### 📋 Copy-paste prompts (D35, D36)

```
Execute work order D35 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §0 and §9 first. This runs AFTER D6-D18 and D25-D34 are written.

1. LINK + ANCHOR SWEEP across README.md and docs/**/*.md. Write a script rather than eyeballing it;
   D61 used a PowerShell pass that resolves relative paths and checks #anchors against the target
   file's headings, using GitHub's slug rule: lowercase, spaces -> "-", strip punctuation, and note
   that an em-dash surrounded by spaces yields TWO hyphens (that is why
   "#15-command-reference--every-command" is correct). Getting the slug rule wrong produces a wall
   of false positives — validate your checker against known-good anchors first.
   Also grep the whole repo for "README.md#" and "README §" — code comments, plan docs and
   engineering notes cite section anchors.
   As of D61 the live tree is clean except TWO INTENTIONAL placeholders inside
   docs/reference/README.md's entry-format template fence (#relatedcommand and ../systems/foo.md);
   your checker should either skip fenced blocks or allowlist those two.
2. Root CosmicUML.png is referenced nowhere (verified 2026-07-03 — RE-VERIFY, do not assume).
   Move it to docs/archive/CosmicUML.png with a one-line note in docs/archive/ saying it is
   superseded by DG-2, which D61 built in README §30. FLAG THE MOVE in your summary for user
   approval — do not treat a file move as routine.
3. Confirm every DG-1..DG-14 landed where §4 assigns it and fix strays. Current state: DG-1, DG-3,
   DG-4, DG-5, DG-9, DG-10, DG-11 in guide/; DG-7, DG-8, DG-12, DG-13, DG-14 in guide/; DG-2 in
   README §30 and DG-6 in README §35. DG-15 and DG-16 belong to D39 and may not exist yet.
4. Verify every chapter's Status cell in all three tier indexes matches reality on disk.

ACCEPTANCE: zero dead relative links and anchors — paste the checker output into the status banner.
Report per §0. No git write commands; leave edits in the working tree.
```

```
Execute work order D36 from docs/plans/12-documentation-plan.md in C:\dev\Cosmic.
Read §0 and §9 first. This is the LAST documentation work order — run it only when every
STATUS: SKELETON banner is gone.

1. Confirm no STATUS: SKELETON banner remains anywhere in docs/, then run
   tests/check_docs_coverage.ps1 — with every banner removed it is now FULLY STRICT, so the
   class-name check applies to every chapter. Fix to green. Expect this to surface real gaps: strict
   mode was never exercised during Phase B because every chapter was skeleton-bannered as it landed.
2. Verify both tier indexes (docs/reference/README.md, docs/systems/README.md) show all-✅ Status
   columns, and that docs/README.md's Status TABLE (D61 replaced the prose paragraph with a
   per-tier table) is rewritten to steady state — no "in progress" rows.
3. Update docs/plans/00-MASTER-ROADMAP.md's "Continuous — docs" row: point it at this doc rather
   than doc 06, and add the phase-complete note.
4. Put the §11 PR checklist — now FOUR rules, D61 added the guide-tier one — where contributors
   will see it. README §1.5's preamble is the precedent; extend its contract sentence to name the
   API reference, the manifest and the guide.
5. Mark this plan's header ✅ complete with the date, and reconcile §10's outstanding count to zero.

ACCEPTANCE: check_docs_coverage.ps1 exits 0 in strict mode; roadmap, both tier indexes and
docs/README.md are mutually consistent. Report per §0. No git write commands.
```

---

## 10. Order, sizing, parallelism

**Updated 2026-07-25** for decision 1b (the guide tier) and §15 (D41–D45, already complete).

| Step | Items | Depends on | Parallel? |
| --- | --- | --- | --- |
| 1 | D5 | — | — |
| 2 | D6–D18 (reference) | D5 (checker exists) | ✅ fully parallel |
| ✅ 3 | **D46 → D61** (the guide tier, 29 chapters written from scratch) | reference chapters help but don't block | ❌ was **serial** (shared README ToC + overview) — **done 2026-07-26** |
| 4 | D25–D34 (explainers) | ✅ **unblocked** — D61 closed and Part II is stable | ✅ parallel except README conversions |
| 5 | D35 → D36 | everything | ❌ |
| — | D37–D39 (Starforge manual), D40 (standing hooks) | independent | ✅ |
| ~~—~~ | ~~D19–D24~~ | — | **retired 2026-07-25** — superseded by D46–D61 (decision 1c) |
| ✅ | **D41–D45 (Phase 29 engine split)** | — | **done 2026-07-25** (§15) |

**Count:** 1 (D5) + 13 (reference) + 10 (explainers) + 2 (finale) + 3 (manual)
= **29 work orders outstanding**, plus D40 as a standing rule. (The 16 guide items are done.)

**What the explainers inherit from Phase C.** Every D25–D34 skeleton now has a *written* guide
chapter covering the same subsystem, and most carry a *Read first / don't re-derive* note naming it.
The division of labour is settled by the tier table in §1: the guide is the outside of the API
(usage, failure modes, pitfalls), the explainer is the inside (mechanism, data layout, rationale,
rejected alternatives). **Nine diagrams are already built** and should be *reused*, not redrawn —
DG-1, DG-3, DG-4, DG-5, DG-9, DG-10, DG-11 in the guide tier, DG-2 and DG-6 in README Part II
(§30 and §35), plus DG-7, DG-8, DG-12, DG-13. See §4 for where each one lives.

**AI tier.** D5 low (scripting). Reference chapters medium, except **D10 strong**. Guide tier:
most items medium, but **D49 (the component catalogue) and D54 (rendering-3d) are XL and want the
stronger model or your review**, with **D55 (lighting/world systems) close behind** — 3D semantics
have to be exact and the material has never been written down. Explainers medium, except
**D28(3D)/D29 strong**. Every item is one session; XL items name their split point.

**Sequencing note (rewritten 2026-07-26, now that step 3 is done).** Steps 2 and 4 are both fully
parallel and nothing blocks either, so **D5 is the one item to run first**. It is a single small
session, and Phase C made the case for it about as strongly as it can be made: **every work order
from D52 onward found at least one public header with no manifest row, in five distinct flavours** —
unlisted outright, transitively reachable through another header, reachable only by explicit
include, mis-routed to the wrong chapter, or (D58's four `utils/` headers) included by `Cosmic.h`
**directly and unfenced** and simply omitted. Auditing that by hand does not scale and has now been
demonstrated not to; the checker does it in one pass. Run D5, then fan out D6–D18 and D25–D34
together.

## 11. The upkeep contract (lives beyond this plan)

Four standing rules — copy into any PR checklist:
1. **CLI surface** (scripts/flags/CMake options/hotkeys) changed → update README §1.5
   *(doc 06 D1, unchanged)*, and `docs/guide/building-and-shipping.md` if the *behaviour* changed
   and not just the list.
2. **Public C++ API** changed (anything reachable via `Cosmic.h` or objects it hands out) →
   update the mapped `docs/reference/` chapter **in the same PR**: signature blocks verbatim,
   new symbols get full entries, removed symbols get deleted + a changelog line. New public
   header → manifest row + chapter assignment. CI's `check_docs_coverage.ps1` (D5) enforces
   the mechanical half.
3. **Client-visible usage** changed (a call gains a failure mode, an idiom is replaced, a pitfall is
   fixed) → update the matching `docs/guide/` chapter *(added 2026-07-26, D61 — the tier is complete
   and now needs the same protection as the other two)*. Its `Pitfalls` section is where a known
   limitation lives; a fix should delete the entry, not leave it stale.
4. **Architecture/behavior** changed (pass order, threading, formats, lifecycle) → update the
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

### 📋 Copy-paste prompts (D37 → D39)

**Standing preamble — every D37–D39 prompt inherits it.**

```
STANDING RULES for the Starforge manual (D37-D39):

1. Read docs/plans/12-documentation-plan.md §0 and §13 first. Home is docs/starforge/.
2. **THE READER IS DIFFERENT FROM EVERY OTHER TIER.** This is a PRODUCT MANUAL for people USING
   the editor — not engine clients. No C++ unless the chapter is about scripting. Never assume the
   reader has built the engine or read a header.
3. **VERIFY EVERY CLAIM AGAINST THE RUNNING EDITOR**, not against source and not against a plan doc.
   Every menu path, hotkey and panel name you write must exist in the build you are looking at.
   §5 of docs/plans/29-phase30-2d-hardening-plan.md establishes that driving the user's machine is
   authorized; use it — launch Starforge.exe and check.
4. Screenshots are USER-CAPTURED. The text must stand alone and make sense with none present.
   Leave an explicit marker where one belongs; never describe an image you have not seen.
5. Task-oriented, in the order a user hits things. Section titles are things a user wants to DO.
6. Each chapter ends with an "Under the hood" line linking the matching docs/systems/ explainer or
   docs/guide/ chapter. NO FACT FORKING (note 7) — the manual says what to click; the other tiers
   say how it works.
7. Chapters marked (P14+) document behaviour only after that phase ships. If the feature is not in
   the build in front of you, do not document it — note the gap instead.
8. Bookkeeping: index links resolve, and set the item's Status in doc 12 §13.
9. Report per §0. No git write commands; leave edits in the working tree.
```

Then one line per item:

```
Execute work order D37 from docs/plans/12-documentation-plan.md. Apply the D37-D39 standing rules.
NEW docs/starforge/README.md (index + reading order) plus three chapters:
  01-getting-started.md  launching (Starforge.exe is a real dev-tree exe now — its own icon,
                         VERSIONINFO and taskbar identity, with COSMIC_STARTUP_PROJECT baked in;
                         note it is NOT produced by cmake --install, so it exists only in a dev
                         tree), the homescreen, opening the ForgePlayground sample, a first tour.
  02-projects.md         project anatomy (project.cproj, scenes/, src/, assets/), where projects
                         live, the registry, autosave and .bak.
  03-scenes-entities.md  the hierarchy, the reflection-driven Inspector, the undo model, prefabs,
                         camera navigation, viewport tools/gizmos/bookmarks.
KNOWN ISSUE TO VERIFY AND DOCUMENT OR AVOID: every viewport-strip toggle chip abort()s a Debug
build (unbalanced ImGui style stack, reproduced in both configurations). Do not send a reader into
a crash without warning. Also: the homescreen re-parses projects.toml EVERY FRAME
(StarforgeApp.cpp:3850) — not user-facing, but do not promise a large project list is cheap.
ACCEPTANCE: a reader who has never seen Starforge reaches an edited and saved scene; every hotkey
and menu path named exists in the build; index links resolve.
```

```
Execute work order D38 from docs/plans/12-documentation-plan.md. Apply the D37-D39 standing rules.
NEW docs/starforge/: 04-primitives-import.md (parametric shapes, .cmeta units, assimp import status —
FBX/OBJ/STL/DAE/PLY are the five compiled importers, and MeshImport::AssimpEnabled() reports the gate
at runtime), 05-materials-environment.md (the .cmat workflow, the environment panel, HDRI),
06-world-systems.md (terrain/water/particle recipes, presets, .cemitter — note the terrain resolution
rule is 32·2^k+1 and the editor will reject other values), 07-2d-ui-flow.md (sprites, tilemaps, UI
entities, the flow graph).
WATCH FOR: .ogg appears in Starforge's AssetTypes audio row but miniaudio has NO Vorbis decoder
compiled in, so an .ogg previews as SILENCE — either document that or get it fixed first, but do not
tell a user it works. Terrain/water/particles are 3D-only: in a 2D-configured editor those panels are
fenced out, so say which chapters apply to which configuration.
ACCEPTANCE: each workflow is reproducible start to finish from the text alone.
```

```
Execute work order D39 from docs/plans/12-documentation-plan.md. Apply the D37-D39 standing rules.
NEW docs/starforge/: 08-scripting.md (C++ scripts, SystemScripts, hot reload, telemetry marks),
09-play-telemetry.md (play/pause/step, recording, takes, CSV export), 10-packaging.md (the Package
dialog, icon embedding, installer and zip, the clean-machine checklist), 11-shortcuts.md (generated
from the Help modal's table, with a keep-in-sync rule). BUILD DG-15 and DG-16 here.
REWRITE docs/design/starforge-ui.md into a 5-line pointer to the manual.
FOR 10-packaging.md: docs/guide/building-and-shipping.md already documents the packaging pipeline
from the developer side, including the two INDEPENDENT packagers and how they differ — Starforge's
in-editor Packager renames the exe, writes boot.cfg, re-embeds the icon via ExeResources::SetIcon
and GENERATES its own .iss, while package_installer.bat uses the checked-in CosmicSetup.iss and
passes --project instead. Link that chapter; document the DIALOG. Three things a user will hit:
Inno is found only via `where iscc` on PATH from the editor (the .bat probes Program Files too);
the generated installer registers a .cham file association THAT CANNOT WORK (nothing reads
COSMIC_REPLAY_FILE and the engine never writes .cham); and PackageInputs::Version is hard-coded
"0.9.0", unconnected to Version.h.
ACCEPTANCE: E21's acceptance-demo script is executable by a new user from the manual alone;
check_docs_coverage.ps1 is untouched (the manual is not API-reference scope).
```

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
| 26 (navigation/AI) | NEW §10.7 Navmesh & agents | NEW navigation.md chapter (`nav/NavWorld` service, `.cnav` sidecar) + scene rows (`NavMeshComponent`/`NavAgentComponent`, `SceneNav`/`SceneNavRuntime`, `Scene::OnNav*`) | NEW navigation explainer (bake pipeline + DetourCrowd tick order) | scripting `Nav()` proxy + `nav.arrived` | — |
| ↳ **DUE 2026-07-14** (Phase 26 N1–N5 ✅ code-complete; row queued for the next docs session). New surface to document: vendored **recastnavigation** (its `dependencies/recastnavigation/README.md` pin note), `nav/NavTypes.h`+`nav/NavWorld.h` (pimpl bake/query/crowd service), `scene/SceneNav.h` (`SceneNav::` bake pipeline + `NavBakeJob` async + `SceneNavRuntime` crowd binding), reflected `NavMeshComponent`/`NavAgentComponent` + `NavSourceMode`, `Scene::{SyncNavMeshes,OnNavStart,OnNavStep,OnNavStop,GetNav}`, `ScenePhysics::BuildColliderDesc` (shared collision-view enumeration), `ScriptableEntity::Nav()` + `nav.arrived`, editor Regenerate button + `EditorContext::PendingNavBake` + `StarforgeApp::TickNavMeshes` + nav-poly overlay, `AssetTypes` `.cnav` row, template `NavCritter.h`, the doc 14 J4 tick-order amendment. | | | | |
| 27 (world rendering/2D parity) | §7 sky/environment note (physical sky + polish knobs), §12-style 2D-lighting note | rendering rows (`SkyMode::Physical` + `EnvironmentMap::{PhysicalSkyDesc,SetPhysicalSky}`, `Renderer3D::SetAmbientIntensity` + `SceneRendererSettings::{AmbientIntensity,Gamma}`, `RendererAPI::BlendMode::Multiply`, `renderer/Light2DRenderer`, `SceneRenderer::RenderToTexture`), scene rows (`EnvironmentComponent` X1/X2 fields + `Ambient2D`, `ParticleEmitterSpec`/component curl-noise + bounds + `ParticleEmitter::{CurlNoise,SetTurbulence}`, `Light2DComponent`, `UiWorldAnchorComponent` + `UiSystem::ProjectToCanvas`, `UiImageComponent::RuntimeTexture`) | rendering-pipeline (physical sky + 2D light composite); particles (curl noise CPU/GPU lockstep) | D37/D38: sun Elevation/Azimuth widget, Project Settings left-nav, live curl-noise preview, Entity ▸ 2D ▸ Light + radius-ring gizmo | — |
| ↳ **DUE 2026-07-14** (Phase 27 X1–X7 ✅ code-complete; row queued for the next docs session). New surface to document: `EnvSky.glsl` physical branch (`u_SkyMode`), `Tonemap.glsl` `u_Gamma`, PBR-family `u_AmbientIntensity`, `Light2D.glsl`, `ParticleUpdate.glsl` curl-noise + bounds mirror; `EnvironmentPanel` sun-angle widget, `StarforgeApp::DrawProjectSettingsPopup` left-nav, `WorldSystemsPanel::{DrawNoisePreview,RebuildNoisePreview}`, Entity ▸ 2D ▸ Light + `ViewportController` Light2D ring; the Starforge `/bigobj` + node-editor `/W0` CMake notes. | | | | |

| 29 (engine split / pluggable physics) | §1.5 gains `build_2d.bat` / `build_3d.bat` / `build_all_2d.bat` + `-DCOSMIC_2D_ONLY` / `-DCOSMIC_WITH_JOLT`; NEW §1.6 "The two engine configurations" | NEW [physics.md](../reference/physics.md) chapter (`PhysicsWorld`/`PhysicsTypes`/`PhysicsBody`/`CharacterController`/`ScenePhysics`/`PhysicsBackendRegistry` + the script proxies); manifest rows for the five physics headers | NEW [build-2d-3d-split.md](../systems/build-2d-3d-split.md) + [physics-backends.md](../systems/physics-backends.md); pointer updates in ecs-scene / rendering-2d / rendering-3d / build-plugin-packaging | — (no new editor surface; the 2D editor is the same editor with 3D fenced) | ✅ **done as doc 28 W10 (D41–D45), 2026-07-25** |

Bookkeeping pass now (part of any next docs session): add the table above as tracked rows,
and extend the §11 upkeep contract with rule 4: **"a phase's final work order runs its D40
row"** — the per-phase plan docs' kickoff prompts already end with acceptance/banner
discipline; reviewers enforce the doc row the same way.

**Status (D40 bookkeeping):** ☐

---

## 15. Phase H — the engine split (D41–D45) *(added 2026-07-25)*

Phase 29 (doc 28) split Cosmic into two build configurations and gave physics a swappable backend.
Its own W10 work order wrote the documentation, planned in
[`28-phase29-engine-split-plan.md`](28-phase29-engine-split-plan.md) §10 and registered here.

**All five ✅ complete 2026-07-25.**

| ID | Document | Contents | Status |
| --- | --- | --- | --- |
| **D41** | NEW [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) | What `COSMIC_2D_ONLY` excludes and why; the **classification rule for new code**; `build.bat` / `build_2d.bat` / `build_3d.bat` / presets / the worktree layout; the **recorded** build times (not the estimate) and the `/MP` finding; the branch and carry-over workflow. | ✅ 2026-07-25 |
| **D42** | NEW [`../systems/physics-backends.md`](../systems/physics-backends.md) | `IPhysicsBackend`, `PhysicsBackendRegistry`, `PhysicsSettings::Backend`; the fixed-step contract; `ThreadCount`/determinism; the `RayHit::EntityId` round-trip; a worked example lifted from `tests/test_physics_backend.cpp`. | ✅ 2026-07-25 |
| **D43** | NEW [`../reference/physics.md`](../reference/physics.md) | The per-call reference for `PhysicsWorld` / `PhysicsTypes` / `PhysicsBody` / `CharacterController` / `ScenePhysics` / the registry / the script proxies — a chapter that **did not exist** before. | ✅ 2026-07-25 |
| **D44** | [`28-phase29-engine-split-plan.md`](28-phase29-engine-split-plan.md) | Status banner, per-work-order ✅ lines with dates and results (§11), the **honest deviation record** (§16, 15 entries) and the follow-ups leaving the phase (§17). | ✅ 2026-07-25 |
| **D45** | Index/pointer updates | `00-MASTER-ROADMAP.md` (Phase 29 entry, doc 28 row, the *both configurations green* working-agreement rule), `FEATURE-MATRIX.md` (5 new rows incl. the ⏸ 2D-native particles), this document (§15), `../design/modularity-audit.md` (**G3 closed** + a physics row in the §4 cookbook), `../systems/build-plugin-packaging.md`, `../systems/{ecs-scene,rendering-2d,rendering-3d,README}.md`, `../reference/README.md`, root `README.md` (§1.5 + a new §1.6). | ✅ 2026-07-25 |

**Two deliberate departures from this plan's conventions**, recorded so a later session does not
"fix" them:

1. **D41–D43 are complete documents, not skeletons.** Every other file in `docs/systems/` and
   `docs/reference/` is a SKELETON awaiting its D6–D18 / D25–D34 work order. Writing skeletons for
   material that was fresh in hand — and that no other work order covers — would have meant
   discarding it and re-deriving it later. Both index tables carry a **Status** column, so the
   mixed state is visible rather than confusing. (Doc 28 §16 D-13.)
2. **`docs/reference/physics.md` is not in the D5 coverage manifest's original scope.** The physics
   headers *are* included by `Cosmic.h` (`PhysicsTypes.h`, `PhysicsBody.h`, `PhysicsBackend.h`,
   `PhysicsWorld.h`, `CharacterController.h`) but had no manifest row, so the D5 checker would have
   flagged them the moment it ran. D45 adds the rows.

**Standing consequence for every future docs item:** the engine now has two configurations. A
reference entry for a symbol that only exists in one of them must say so — the convention used
throughout D41–D43 is a plain sentence naming the configuration, not a new badge vocabulary.

## 15.5 Where every copy-paste prompt lives *(added 2026-07-26, D61)*

Every remaining work order has a ready prompt. **29 documentation work orders** here, plus
**10 more** for Phase 30 in [`29-phase30-2d-hardening-plan.md`](29-phase30-2d-hardening-plan.md).

| Phase | Items | Prompts | Parallel? |
| --- | --- | --- | --- |
| **A** — enforcement tooling | **D5** | [§5](#5-phase-a--enforcement-tooling) | **Run this first** |
| **B** — API reference | D6–D18 (13) | [§6](#6-phase-b--api-reference-chapters-d6d18) | ✅ fully parallel |
| ~~C~~ — guide tier | ~~D46–D61 (16)~~ | [§7](#7-phase-c--the-guide-tier-written-from-scratch-d46d61-serial) | ✅ **complete 2026-07-26** |
| **D** — system explainers | D25–D34 (10) | [§8](#8-phase-d--system-explainers-d25d34) | ✅ parallel except README conversions |
| **E** — integration & finale | D35, D36 | [§9](#9-phase-e--integration--enforcement-d35d36) | ❌ last, in order |
| **F** — Starforge manual | D37–D39 (3) | [§13](#13-phase-f--starforge-user-manual-d37d39-added-2026-07-04) | ✅ independent |
| **G** — per-phase hooks | D40 | [§14](#14-phase-g--per-phase-documentation-hooks-d40-standing-added-2026-07-04) | standing rule |

**Suggested order.** D5 alone first — it is one small session and it is what stops the manifest
drifting again. Then **D6–D18 and D25–D34 fan out together** (different files; the only shared ones
are the two tier indexes, where you touch only your own row). D35 → D36 last, in that order. D37–D39
can run any time. Phase 30 (P0–P9) follows the user's *documentation before testing* directive.

**Running these concurrently?** See [§15.6](#156-running-the-remaining-work-in-parallel-added-2026-07-26-d61)
for the wave structure and the one rule that makes it safe: an agent writes **only its own chapter**
and reports shared-file edits for a serial integration pass. 23 of the 29 items are parallel-safe;
D35/D36 must be last and in order, and D37–D39 need the running editor so they are attended work.

Each phase's prompt block opens with a **standing-rules preamble** that every item in that phase
inherits, then one short prompt per item — don't paste an item prompt without its preamble.

---

## 15.6 Running the remaining work in parallel *(added 2026-07-26, D61)*

Phase C had to be serial — every one of its 16 items edited the README ToC and overview. **Phases B
and D do not have that constraint**, and 23 of the 29 remaining work orders can run concurrently if
you respect one rule.

### The one rule: agents write their own chapter and nothing else

Every parallel item touches exactly **one** new or skeleton file. What it must *not* touch is the
shared state:

| Shared file | Who wants to write it | Why it collides |
| --- | --- | --- |
| `docs/reference/README.md` | all 13 Phase B items | the Status column + the coverage manifest |
| `docs/systems/README.md` | all 10 Phase D items | the Status column |
| `README.md` | D25, D26, D27, D28, D29, D32, D33, D34 | Part II section conversions |
| `docs/plans/12-documentation-plan.md` | every item | its own status banner + the §7 findings log |

Concurrent edits to these clobber each other — last writer wins and the loser's row silently
vanishes. So: **each agent writes only its chapter, deletes only its own `STATUS: SKELETON` banner,
and REPORTS the shared-file edits it needs.** A serial **integration pass** then applies every
reported change at once. That pass is cheap (it is table rows and status cells) and it is the only
place the shared files are ever written.

The README Part II conversions in Phase D are the sharpest case: eight items want to edit `README.md`.
Defer all of them to the integration pass, or run those items' conversions serially afterwards.

### Wave structure

| Wave | Items | Concurrency | Notes |
| --- | --- | --- | --- |
| **0** | **D5** | alone | The checker. Run it first and *act on its output* — it tells the Phase B waves which manifest rows are missing, which is information they otherwise each rediscover by hand. |
| **1** | D7, D13, D14, D15 | 4 | M-sized, well-specified, each has a written guide counterpart to lean on. Good shakedown wave. |
| **2** | D6, D8, D9, D16, D18 | 5 | D8's BindingPoints table is load-bearing for D9/D11 — land D8 before or with them. |
| **3** | D12, D17 + D10, D11 | 4 | **D10 and D11 are XL and want a stronger model or your review** — the deferred-flush and pass-graph semantics must be exactly right. Consider running these two attended rather than fanned out. |
| **4** | D25 first, then D26–D33 | 1 then 8 | D25 (architecture-overview) is the map — do it first for orientation, but write its §5 directory table last. **D29 is XL** (the PBR material must explain, not name-drop). |
| **5** | D34 | 1 | Depends on nothing, but its `build-plugin-packaging` half pairs with D25's module map. |
| **6** | D35 → D36 | serial | Must be last and in order: D35 sweeps links, D36 runs the checker in full strict mode. |
| **—** | D37–D39 | **not parallel-safe** | The Starforge manual requires **driving the running editor** to verify menus and hotkeys. That is an attended, one-at-a-time activity, not a fan-out. |

### What parallelism does not buy you

**Verification quality does not parallelise.** Phase C's value was not 29 documents — it was the
~60 verified engine defects in §7's findings log, each found by someone reading a header end to end
and noticing it disagreed with the code. A fanned-out pass produces text faster and *looks*
complete; it will not notice that `Application.h:93`'s doc comment contradicts
`WorkspaceLayer.cpp:214`. Budget review time accordingly, and give the XL items (D10, D11, D29) a
stronger model or a human read.

**Two failure modes to watch for in agent output**, both seen in this codebase's history:
comfortable paraphrase of a header comment instead of the code it contradicts, and a confident claim
with no file:line behind it. Rule 2 of every standing block exists for exactly this. Spot-check by
picking three claims per chapter and grepping for their citation.

---

## 16. Kickoff prompt (paste for each implementation session)

> Read `docs/plans/12-documentation-plan.md` §0 fully, then work order **D\<n\>** only. Open
> the skeleton file(s) named in the item — the skeleton's scope list, checklist, and truth
> sources are binding. Verify every signature and behavior claim against the current headers
> and source before writing (checklists are starting points; headers are truth; never
> document parked/unshipped API). Follow the mandatory entry/explainer format from the tier
> index README. Finish with the item's Acceptance, do the note-8 bookkeeping (banners,
> index status cells, this doc's status banner), run `tests\check_docs_coverage.ps1` if it
> exists, and do NOT run any git write command — leave changes in the working tree with a
> one-paragraph summary.
