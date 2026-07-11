# Phase 23 Plan — Asset Workflows, Inspector & Hierarchy v2

> **Created 2026-07-11.** Editor-vision phase 2 of 7 (spec of record:
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) — each
> item cites its §). This phase makes assets and properties feel like the reference editors:
> typed/thumbnailed/searchable everywhere, a two-pane browser with preview + metadata, a
> tooltipped drag-target Inspector, a Hierarchy with real Active semantics, and the utility
> dock (console/profiler/jobs/resources).
>
> **Depends on:** doc 19 **A4** (preview rig + thumbnails — promoted 2026-07-11 to a shared
> `PreviewRig` service per gap §14.3; schedule A4 before/with T7/T11). T1→T10, T2→T18, T3→T8,
> T5→T11 as noted. Everything else independent and PR-sized. Phase 22 is NOT required (panels
> land wherever the chrome currently is).

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules; compat gate; state-restore contract for offscreen
renders (doc 13 §0.5). Reflection changes (T1) must not break existing registrations — metadata
is opt-in with defaults. Undo: every editor mutation routes through `commands/EditorCommands`
(`CommitFieldEdit` / new command types), matching the E7 conventions. No git writes.

## 1. Work orders

### T1 — Reflection metadata v2 *(gap §14.1)*
**Files:** engine `reflect/TypeDescriptor.h` (+`TypeRegistry`), the `REFLECT_*` macros
(`scene/ComponentRegistry.h`), then back-fill the big components (`scene/Components.h`:
Environment/Terrain/Water/ParticleEmitter/physics set).
**Spec:** optional per-field `Doc` (string), `Min/Max/Step` (floats), flags `Units_Degrees`,
`Units_Meters`, `Units_Seconds` — attach via macro overloads; absent = today's behavior
byte-identical. **Acceptance:** headless: registry reports metadata where declared, defaults
where not; zero churn in serialized scenes; existing tests green. **Status:** ☐

### T2 — Asset accounting & enumeration *(gap §14.2)*
**Files:** engine `assets/AssetLibrary.h/.cpp` (`Enumerate(visitor)` over loaded assets: path,
type, refcount, CPU/GPU byte estimate), `graphics/Texture.h`/`Mesh.h` size accessors where
missing, `audio/Sound.h` `CopyPcm(std::vector<float>&, maxSamples)` for waveform preview.
**Spec:** read-only introspection, no lifetime changes. **Acceptance:** headless: enumerate
returns every cached asset with plausible sizes; `CopyPcm` returns decoded samples for a test
WAV. **Status:** ☐

### T3 — OS file-drop events *(gap §14.5)*
**Files:** engine `events/ApplicationEvent.h` (NEW `WindowFileDropEvent{paths}`),
`core/Window.cpp` (GLFW drop callback — `glfwSetDropCallback`).
**Spec:** generic event through the existing dispatch; no consumer in the engine.
**Acceptance:** headless-safe; a manual drop logs paths in Starforge's Console (T8 consumes
properly). **Status:** ☐

### T4 — Content Browser two-pane + history + search *(gap §4.1)*
**Files:** Starforge `panels/ContentBrowserPanel.h/.cpp`; `EditorPrefs.h`.
**Spec:** left folder tree (lazy-expanded), right tile grid; back/forward buttons + mouse-4/5
with a path-history stack; search box → recursive filtered flat view (case-insensitive
contains); tile-size slider (scales today's 84 px `cell`). Tree width/tile size persist.
**Acceptance:** all existing behaviors (drag source, context menus, watcher reload, recycle
delete) survive; search finds nested assets; history works across folder jumps. **Status:** ☐

### T5 — Asset type table: identity + create menu + open routing *(gap §4.2)*
**Files:** NEW Starforge `AssetTypes.h` (extension → {Lucide glyph, accent color, display name,
create-fn, open-action}); `ContentBrowserPanel.cpp` consumes it.
**Spec:** replace text badges with glyph-on-color tiles; per-type "New…" context entries
(Material `.cmat` via `AssetLibrary::SaveMaterialAsset`, Emitter `.cemitter`, Flow `.cflow`,
Palette `.cpal`, Scene, Prefab, Folder); double-click routing table replaces the hardcoded
per-ext chain. Later phases append rows (`.cstory`, `.cnav`, skeleton/clips) — keep the table
the single registry. **Acceptance:** every current type renders identified; every create-fn
produces a loadable default asset; open routing unchanged for scenes/prefabs/images.
**Status:** ☐

### T6 — Rename/move + reference retarget *(gap §4.3)*
**Files:** Starforge `ContentBrowserPanel.cpp`; NEW `ProjectAssets.h/.cpp`
(`RetargetPath(oldVfs, newVfs)`).
**Spec:** F2/context Rename (inline `InputText`), drag tiles onto tree folders to move;
retarget sweeps all `.cscene`/`.cprefab`/`.cmat` in the project via `SceneSerializer`
load→fix→save (schema-safe), behind a confirmation dialog listing hits. Renames stay
non-undoable by design (FEATURE-MATRIX row). **Acceptance:** rename a texture used by a
material + a scene → both re-resolve after reload; cancel touches nothing. **Status:** ☐

### T7 — Preview + metadata pane (+ audio preview) *(gap §4.4; deps doc 19 A4, T2)*
**Files:** Starforge `ContentBrowserPanel.cpp` (bottom pane) or NEW `panels/AssetPreviewPanel`.
**Spec:** selected asset → mesh/material turntable via the A4 `PreviewRig` (interactive orbit),
texture preview with dimensions, audio Play/Stop (engine `AudioEngine` one-shot) + waveform
strip (T2 `CopyPcm`); metadata chips: disk size, type, and for meshes vertex/index counts +
local AABB. **Acceptance:** selecting assets of every type shows a sensible preview; no GL
state leak (A4's screenshot-identical contract). **Status:** ☐

### T8 — Import UX + Explorer drops *(gap §4.5; dep T3)*
**Files:** Starforge `ContentBrowserPanel.cpp`, `StarforgeApp.cpp` (route
`WindowFileDropEvent`).
**Spec:** browser toolbar **Import** button: models → the E16 `.cmeta` dialog; images/audio/HDR
→ copy into the current folder + `AssetLibrary::Reload`. OS drops onto the browser do the same;
drops onto the viewport defer to K13's spawn rules when Phase 22 is present. **Acceptance:**
drop a PNG + an OBJ from Explorer → both usable without leaving the editor. **Status:** ☐

### T9 — Inspector property search *(gap §5.1)*
**Files:** Starforge `panels/InspectorPanel.cpp`.
**Spec:** filter box at the top; non-empty → draw only matching fields/components, headers
auto-open. **Acceptance:** typing narrows across all components incl. script fields; clearing
restores the full layout. **Status:** ☐

### T10 — Field tooltips + metadata-driven widgets *(gap §5.2; dep T1)*
**Files:** Starforge `widgets/PropertyRows.h/.cpp`.
**Spec:** `Doc` → ⓘ hover on the row label; `Min/Max` → `SliderFloat`/`DragFloat` clamped;
`Units_Degrees` renders °; steps honored. **Acceptance:** Environment/Water/Particle panels
show tooltips + bounded sliders; undo unaffected (same commit path). **Status:** ☐

### T11 — Asset-slot widget *(gap §5.3; deps T5, doc 19 A4 thumbs)*
**Files:** Starforge `widgets/PropertyRows` (NEW `DrawAssetSlot`), `ContentBrowserPanel`
(`RevealAsset(vfs)`).
**Spec:** `AssetPath`-flagged fields render [thumb | name-button → reveal in browser | ▾ picker
popup filtered by extension | ✕ clear]; still a drag target; edits via `CommitFieldEdit`.
**Acceptance:** MeshPath/MaterialPath/HdriPath/TexturePath slots all upgrade; keyboard-only
assignment possible via the picker. **Status:** ☐

### T12 — Component QoL: copy/paste/reset + `Enabled` convention *(gap §5.4, §14.4 part)*
**Files:** Starforge `panels/InspectorPanel.cpp`, `commands/EditorCommands`; engine
`scene/Components.h` + consumers for the enable gates.
**Spec:** header context menu gains Copy / Paste values / Reset (reflected-JSON clipboard via
the E17 generic serializer; per-field undoable commits). Engine: add reflected
`bool Enabled = true` ONLY where "disabled" is well-defined — `MeshRenderer`, `SpriteRenderer`,
lights, `ParticleEmitter`, `Water`, colliders — gated in `Scene::OnRender3D`/`SceneRenderer`
submit, `SyncWorldSystems`, `ScenePhysics` bake. Inspector draws a header checkbox for any
descriptor exposing `Enabled`. Compat: defaults true, old scenes byte-identical.
**Acceptance:** copy a tuned light across entities; disable a mesh → invisible in editor+Play,
re-enable restores; headless tests for the gates. **Status:** ☐

### T13 — Per-entity Active semantics + Hierarchy toggle *(gap §6.2, §14.4)*
**Files:** engine `scene/Components.h` (`TagComponent` gains `bool Active = true`),
`scene/Scene.*` (effective-active = own ∧ ancestors, cached per frame beside the transform
walk; gates: render submit, world-FX sync, `UiSystem`, physics bake, script dispatch);
serializer default-compat. Starforge `panels/HierarchyPanel.cpp` (checkbox/eye per row, dimmed
subtree), undoable via `CommitFieldEdit`.
**Spec/Acceptance:** headless: inactive parent hides+freezes the subtree (not rendered, not
ticked, not baked); old scenes load identical; toggling in the editor is one undo step.
**Status:** ☐

### T14 — Hierarchy icons + counts *(gap §6.1; dep T5's glyph table)*
**Files:** Starforge `panels/HierarchyPanel.cpp`.
**Spec:** row glyph from the dominant component (camera/light/mesh/terrain/water/emitter/voxel/
UI/script), colored by the T5 table; entity + selected counts in the header. **Acceptance:**
every ForgePlayground entity shows a sensible icon; no layout regressions with deep trees.
**Status:** ☐

### T15 — Inspector live-Play behavior *(gap §5.6)*
**Files:** Starforge `panels/InspectorPanel.cpp`, `commands/EditorCommands.cpp`.
**Spec:** while playing: value backgrounds tinted (live affordance); edits apply directly
WITHOUT pushing undo entries (they die with the Stop snapshot-restore — today they pollute the
stack). **Acceptance:** tune a field during Play → change visible, undo stack unchanged after
Stop. **Status:** ☐

### T16 — Console v2 *(gap §13.1)*
**Files:** Starforge `panels/ConsolePanel.h/.cpp` (+ `EditorContext` `ConsoleLine` gains a
`Source` enum where lines are pushed).
**Spec:** text search filter; Engine/Editor/Game source chips; monospace body; ring-buffer cap.
Existing severity filters/timestamps/copy stay. **Acceptance:** filter combinations behave;
10k-line sessions stay responsive. **Status:** ☐

### T17 — GPU profiler panel (port) *(gap §13.2)*
**Files:** NEW Starforge `panels/ProfilerPanel.h/.cpp` (adapt Frontier's `GpuProfilerPanel`);
View menu + K3 layouts pick it up.
**Spec:** per-pass GPU ms from the `SceneRenderer` timer-query zones (F3 verbs) + CPU frame
breakdown; history sparkline. **Acceptance:** numbers match Frontier's panel on the same
scene; zero cost when closed. **Status:** ☐

### T18 — Jobs / Resources panel *(gap §13.3; dep T2)*
**Files:** engine `jobs/JobSystem.h` (read-only stats: queued/active/completed, worker count);
NEW Starforge `panels/SystemPanel.h/.cpp` (two tabs).
**Spec:** Jobs tab: live counters; Resources tab: T2 enumeration table (path/type/refs/bytes)
with per-row Reload. **Acceptance:** terrain rebuild shows job activity; resource totals match
the K5 status-bar chip. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/22-phase23-asset-workflows-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md` (re-verify file references — they drift).
> Reflection/serializer changes must keep old scenes byte-identical; editor mutations go
> through `commands/EditorCommands`; headless-test what can be. Roadmap cmake recipe; compat
> gate; no git writes. Finish with Acceptance + status banner.
