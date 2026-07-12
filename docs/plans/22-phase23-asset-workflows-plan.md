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
>
> **STATUS — ✅ code-complete 2026-07-12 (UNcommitted).** All 18 work orders (T1–T18) landed in
> one session. Engine gained only generic/compat-gated surface: reflection metadata v2
> (`FieldUnits` + `.Doc()`, `Field_OmitIfTrue`), `AssetLibrary::Enumerate` + `Texture/Mesh::
> GetGpuBytes` + `Sound::CopyPcm`, `WindowFileDropEvent` (+ GLFW drop callback), reflected
> `Enabled` (renderers/lights/emitter/water/colliders) + `TagComponent::Active` with the render/
> world-FX/UiSystem/physics-bake/script-dispatch gates, and `JobSystem` read-only stats. Starforge
> gained the Content Browser v2 (two-pane/history/search/type-table/rename+retarget/preview+audio/
> import+drops), Inspector v2 (search/tooltips+sliders/asset-slots/copy-paste-reset+enable/
> live-Play), Hierarchy v2 (active eye toggle + dominant-component icons + counts), and the utility
> dock (Console v2 + Profiler port + Jobs/Resources). **Compat gate held** (default-true flags omit
> from serialization → unchanged scenes byte-identical; plain OBJ/flat scenes/shipped apps
> untouched). Build **Debug + Release zero warnings**, `CosmicTests` **301/301** (+11 headless:
> T1 metadata, T2 enumerate/CopyPcm, T3 drop event, T12 gate+compat×2, T13 semantic+compat+gate),
> **GL-conformance clean**. Remaining = the user's on-GPU acceptance list (below) + commit.

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
where not; zero churn in serialized scenes; existing tests green. **Status:** ✅ 2026-07-12 —
the reflection API is the fluent `Reflect::Class<T>().Field().Range().Tooltip()` builder (the
doc's `REFLECT_*`/`ComponentRegistry.h` reference had drifted); `FieldHints` already carried
`Min/Max/Step/Tooltip`, so T1 added `enum class FieldUnits {None,Degrees,Meters,Seconds}` +
`FieldHints::Units` (`reflect/TypeDescriptor.h`) and builder verbs `.Doc()` (spec name; shares
Tooltip storage so existing registrations are untouched) and `.Units()/.Degrees()/.Meters()/
.Seconds()` (`reflect/TypeRegistry.h`). Back-filled units on Transform/Camera/PointLight/
Environment/Terrain/Water/ParticleEmitter/colliders/CharacterController/VoxelVolume. Hints are
registration-time only (never serialized) → scenes byte-identical. New headless test
`T1: reflection metadata v2 …` proves declared metadata is reported and absent metadata defaults
(units None, empty doc, no range). Build Debug zero warnings; `CosmicTests` **291/291** (+1).

### T2 — Asset accounting & enumeration *(gap §14.2)*
**Files:** engine `assets/AssetLibrary.h/.cpp` (`Enumerate(visitor)` over loaded assets: path,
type, refcount, CPU/GPU byte estimate), `graphics/Texture.h`/`Mesh.h` size accessors where
missing, `audio/Sound.h` `CopyPcm(std::vector<float>&, maxSamples)` for waveform preview.
**Spec:** read-only introspection, no lifetime changes. **Acceptance:** headless: enumerate
returns every cached asset with plausible sizes; `CopyPcm` returns decoded samples for a test
WAV. **Status:** ✅ 2026-07-12 —
`AssetLibrary::Enumerate(visitor)` walks all six caches emitting a read-only
`AssetEntry{Path,Type,Refs,CpuBytes,GpuBytes}` (no loads/eviction). Size accessors:
`Texture::GetGpuBytes()` (pure virtual; `OpenGLTexture` computes bpp from the internal
format + a ~1/3 mip tail via a new `m_HasMips` flag set in each ctor), `Mesh::GetGpuBytes()`
(inline: VB+IB+skin buffer; `GetVertexCount/GetIndexCount` already existed), and a clip-set
CPU-byte sum. `Sound::CopyPcm(out,maxSamples)` decodes the file via a standalone `ma_decoder`
(mono f32) and peak-decimates the whole file into ≤maxSamples signed envelope buckets —
**device-independent** (works headless, doesn't disturb the live template). Headless tests
(`test_assetlibrary.cpp`): Enumerate over a CPU-only clip-set (empty-cache visits nothing;
loaded set reports type/path/refs≥2/cpuBytes>0) and CopyPcm on a generated spike WAV (0.5 peak
survives decimation; missing file → 0). Build Debug zero warnings; `CosmicTests` **294/294**.

### T3 — OS file-drop events *(gap §14.5)*
**Files:** engine `events/ApplicationEvent.h` (NEW `WindowFileDropEvent{paths}`),
`core/Window.cpp` (GLFW drop callback — `glfwSetDropCallback`).
**Spec:** generic event through the existing dispatch; no consumer in the engine.
**Acceptance:** headless-safe; a manual drop logs paths in Starforge's Console (T8 consumes
properly). **Status:** ✅ 2026-07-12 — new `WindowFileDropEvent{paths}` (`events/ApplicationEvent.h`,
`EventType::WindowFileDrop`, Application category) carrying the dropped absolute paths;
`core/Window.cpp` registers `glfwSetDropCallback` mirroring the existing GLFW→EventCallback
callbacks (builds the event, dispatches through the same path). No engine consumer (Starforge
routes it in T8). Headless test `test_events.cpp` (new; added to the tests CMake list — not
globbed): value type carries paths, reports the right type/category, and routes through
`EventDispatcher` to the drop handler (non-matching handlers skipped, `Handled` set). Build
Debug zero warnings; `CosmicTests` **296/296** (+2). Console-log-on-drop lands with T8.

### T4 — Content Browser two-pane + history + search *(gap §4.1)*
**Files:** Starforge `panels/ContentBrowserPanel.h/.cpp`; `EditorPrefs.h`.
**Spec:** left folder tree (lazy-expanded), right tile grid; back/forward buttons + mouse-4/5
with a path-history stack; search box → recursive filtered flat view (case-insensitive
contains); tile-size slider (scales today's 84 px `cell`). Tree width/tile size persist.
**Acceptance:** all existing behaviors (drag source, context menus, watcher reload, recycle
delete) survive; search finds nested assets; history works across folder jumps. **Status:** ✅ 2026-07-12 —
`ContentBrowserPanel` reworked into a two-pane layout: a lazy-expanded folder `DrawFolderTree`
(left, recurses only into open nodes; root "Assets" selectable) + a drag-splitter (persists
`m_TreeWidth`) + the tile `DrawGrid` (right). Back/forward via toolbar arrow buttons AND mouse
buttons 4/5 over a `std::vector<fs::path>` history stack (`NavigateTo` truncates forward,
`GoBack/GoForward`); `InputTextWithHint` search switches the grid to a recursive
case-insensitive filtered flat view; a tile-size `SliderFloat` (48–160 px) scales the cell.
Tree width + tile size persist via new `EditorSettings::CbTreeWidth/CbTileSize` +
`ContentBrowserPanel::Load/SavePrefs` (wired in `StarforgeApp` beside the K6 snap prefs). All
prior behaviors preserved (drag source, per-file/dir context menus incl. recycle-delete, create
menu, watcher reload, thumbnails, texture-preview modal). Build Debug zero warnings; engine/
`CosmicTests` unchanged (editor-only). Visual acceptance is on-GPU (user list).

### T5 — Asset type table: identity + create menu + open routing *(gap §4.2)*
**Files:** NEW Starforge `AssetTypes.h` (extension → {Lucide glyph, accent color, display name,
create-fn, open-action}); `ContentBrowserPanel.cpp` consumes it.
**Spec:** replace text badges with glyph-on-color tiles; per-type "New…" context entries
(Material `.cmat` via `AssetLibrary::SaveMaterialAsset`, Emitter `.cemitter`, Flow `.cflow`,
Palette `.cpal`, Scene, Prefab, Folder); double-click routing table replaces the hardcoded
per-ext chain. Later phases append rows (`.cstory`, `.cnav`, skeleton/clips) — keep the table
the single registry. **Acceptance:** every current type renders identified; every create-fn
produces a loadable default asset; open routing unchanged for scenes/prefabs/images.
**Status:** ✅ 2026-07-12 — NEW `AssetTypes.h/.cpp` is the single registry: `AssetTypeForExt(ext)`
→ `{Glyph, Color, Name, Open}` for every current type (scene/prefab/material/mesh set/image set/
hdr/audio/emitter/flow/palette/volume/meta) + `FolderTypeInfo()` + a neutral fallback.
`ContentBrowserPanel` now draws glyph-on-color tiles (`GlyphTile` — accent fill, luminance-picked
glyph color, hover/active blends) for folders + non-thumbnailable files (real image/mesh/material
thumbnails still win); the double-click chain is replaced by `switch(info.Open)` (Scene/Prefab/
Texture unchanged; Model/Material/None inert). The empty-space **New** submenu is built from
`CreatableTypes()` + Folder; each `CreateDefaultAsset(ext,path)` writes a loadable default via the
canonical serializers (`Scene::Save`, `SavePrefab` of a one-entity temp scene, `SaveMaterialAsset`,
reflected emitter save, `FlowAsset::Save` with a Start state, `BlockPalette::CreateDefault()->Save`)
into a de-duplicated `UniquePath`. Later phases append rows to the one table (the `Ext` table also
feeds T14 hierarchy icons). Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU:
tiles/create menu (user list).

### T6 — Rename/move + reference retarget *(gap §4.3)*
**Files:** Starforge `ContentBrowserPanel.cpp`; NEW `ProjectAssets.h/.cpp`
(`RetargetPath(oldVfs, newVfs)`).
**Spec:** F2/context Rename (inline `InputText`), drag tiles onto tree folders to move;
retarget sweeps all `.cscene`/`.cprefab`/`.cmat` in the project via `SceneSerializer`
load→fix→save (schema-safe), behind a confirmation dialog listing hits. Renames stay
non-undoable by design (FEATURE-MATRIX row). **Acceptance:** rename a texture used by a
material + a scene → both re-resolve after reload; cancel touches nothing. **Status:** ✅ 2026-07-12 —
NEW `ProjectAssets.h/.cpp`: `FindReferences(oldVfs)` (dry run) + `RetargetPath(oldVfs,newVfs)`
sweep every `.cscene`/`.cprefab`/`.cmat` under the project root. **Deviation (documented below):**
instead of a full `SceneSerializer` Load/Save round-trip, the sweep parses each file as JSON
(nlohmann) and replaces only exact string *values* equal to `oldVfs` — schema-safe and GL-free,
and because default `json` sorts keys the same way `SceneSerializer::dump(2)` does, the output is
byte-format-identical except the changed path (works uniformly for all three file types without a
live scene). `ContentBrowserPanel`: `BeginRename`/inline `InputText` (F2 or context "Rename", edits
the stem, keeps the ext), tile→tree-folder / tile→dir-tile / tile→root drag-drop moves
(`AcceptMoveDrop`), all routed through `TryRelocate` → when refs exist it defers the *whole* op to
an "Update References?" confirm modal listing the hit files (Rename & Update / Cancel; **cancel
renames nothing**), else it relocates immediately; existence-guarded (never overwrites). Renames
stay non-undoable by design. Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU:
rename-a-referenced-texture acceptance (user list).

### T7 — Preview + metadata pane (+ audio preview) *(gap §4.4; deps doc 19 A4, T2)*
**Files:** Starforge `ContentBrowserPanel.cpp` (bottom pane) or NEW `panels/AssetPreviewPanel`.
**Spec:** selected asset → mesh/material turntable via the A4 `PreviewRig` (interactive orbit),
texture preview with dimensions, audio Play/Stop (engine `AudioEngine` one-shot) + waveform
strip (T2 `CopyPcm`); metadata chips: disk size, type, and for meshes vertex/index counts +
local AABB. **Acceptance:** selecting assets of every type shows a sensible preview; no GL
state leak (A4's screenshot-identical contract). **Status:** ✅ 2026-07-12 — a toggleable bottom
**preview + metadata pane** on `ContentBrowserPanel` (persisted via `EditorSettings::CbShowPreview`;
eye toolbar toggle). Single-click selects a tile (accent-border highlight); the pane shows: mesh
+ `.cmat` **interactive turntables** via a per-panel `PreviewRig` (drag-orbit / wheel-zoom /
double-click-reset — same input pattern as the Material Editor, so A4's state-restore contract
holds), texture preview + dimensions, and audio **Play/Stop** (`AudioEngine::PlayLooping/Stop`) with
a `CopyPcm` **waveform scope** (T2). Metadata chips: disk size (`fs::file_size`), type name, and
per-kind — texture WxH + GPU bytes, mesh vertex/index counts + local AABB + GPU bytes (T2
`GetGpuBytes`), audio duration. Selection/audio cleaned up in `Select`/`Reset`/dtor. Build Debug
zero warnings; engine/`CosmicTests` unchanged. On-GPU: per-type preview + no-GL-leak self-test
(user list).

### T8 — Import UX + Explorer drops *(gap §4.5; dep T3)*
**Files:** Starforge `ContentBrowserPanel.cpp`, `StarforgeApp.cpp` (route
`WindowFileDropEvent`).
**Spec:** browser toolbar **Import** button: models → the E16 `.cmeta` dialog; images/audio/HDR
→ copy into the current folder + `AssetLibrary::Reload`. OS drops onto the browser do the same;
drops onto the viewport defer to K13's spawn rules when Phase 22 is present. **Acceptance:**
drop a PNG + an OBJ from Explorer → both usable without leaving the editor. **Status:** ✅ 2026-07-12 —
Content Browser toolbar **Import** button opens one native dialog (models + images/audio/hdr) →
`ImportFile(ctx, src)`: models set `ctx.PendingImportModel` (the shell seeds + opens the E16
`.cmeta` modal — scale/up-axis, multi-mesh spawn, per-material `.cmat`), other assets copy into
the current folder (dedup via `UniquePath`) + `AssetLibrary::Reload`. `StarforgeApp::OnEvent`
dispatches the T3 `WindowFileDropEvent` (project-open gate) into `ctx.PendingDroppedFiles` +
Console log; the browser drains them into `ImportFile` each frame (shell clears them while the
browser is closed so nothing accrues). Viewport-spawn-on-OS-drop defers to K13's in-editor drag
rules (documented follow-up). Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU:
drop-PNG+OBJ-from-Explorer acceptance (user list).

### T9 — Inspector property search *(gap §5.1)*
**Files:** Starforge `panels/InspectorPanel.cpp`.
**Spec:** filter box at the top; non-empty → draw only matching fields/components, headers
auto-open. **Acceptance:** typing narrows across all components incl. script fields; clearing
restores the full layout. **Status:** ✅ 2026-07-12 — `InspectorPanel` gained a top
`InputTextWithHint` property-search box (`m_Search`). While non-empty, `ComponentMatchesFilter`
(name or any visible field) hides non-matching components; matching component headers force open
(`SetNextItemOpen(true, Always)`); the field loop draws all fields of a name-matched component
else only fields whose own name matches. NativeScript filters its own **dynamic** script fields
(section/class-name hit shows all, else only matching field names; hides entirely when nothing
matches). Case-insensitive `ContainsCI`. Clearing the box restores the full layout. Build Debug
zero warnings; engine/`CosmicTests` unchanged. On-GPU: search narrowing (user list).

### T10 — Field tooltips + metadata-driven widgets *(gap §5.2; dep T1)*
**Files:** Starforge `widgets/PropertyRows.h/.cpp`.
**Spec:** `Doc` → ⓘ hover on the row label; `Min/Max` → `SliderFloat`/`DragFloat` clamped;
`Units_Degrees` renders °; steps honored. **Acceptance:** Environment/Water/Particle panels
show tooltips + bounded sliders; undo unaffected (same commit path). **Status:** ✅ 2026-07-12 —
`widgets/PropertyRows.h` (kept header-only — the doc's `.cpp` reference drifted): `Doc`/Tooltip
now surfaces as a dimmed **ⓘ (`ICON_LC_INFO`)** at the row end that reveals the text on hover
(replacing the always-on widget-hover tooltip). Bounded floats/ints render a **`SliderFloat`/
`SliderInt`** when the span is human-scale (float ≤100, int ≤1000); wider ranges keep a
range-clamped `DragFloat`/`DragInt` (a slider over 1..100000 is unusable). `FieldUnits` appends a
unit suffix to the display format (`°`/` m`/` s` — T1). The undo path is untouched (`FinishItem`
still captures activate/commit from the widget item; the ⓘ is drawn *after* the capture). Build
Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU: Environment/Water/Particle sliders +
tooltips (user list).

### T11 — Asset-slot widget *(gap §5.3; deps T5, doc 19 A4 thumbs)*
**Files:** Starforge `widgets/PropertyRows` (NEW `DrawAssetSlot`), `ContentBrowserPanel`
(`RevealAsset(vfs)`).
**Spec:** `AssetPath`-flagged fields render [thumb | name-button → reveal in browser | ▾ picker
popup filtered by extension | ✕ clear]; still a drag target; edits via `CommitFieldEdit`.
**Acceptance:** MeshPath/MaterialPath/HdriPath/TexturePath slots all upgrade; keyboard-only
assignment possible via the picker. **Status:** ✅ 2026-07-12 — NEW `PropertyRows::DrawAssetSlot`
renders `AssetPath` fields as [thumbnail (image via `AssetLibrary::GetTexture`, mesh/`.cmat` via
the shared `PreviewRig` thumbnail, else a type glyph tile) | name button → sets
`ctx.PendingRevealAsset` | ▾ picker popup (recursive project scan filtered by
`ExtensionsForAssetType`, keyboard-navigable Selectables incl. "(none)") | ✕ clear], still an
`ASSET_PATH` drop target; all discrete edits set `Committed` so the caller's existing
`CommitFieldEdit`/`CommitFieldEditFor` path records one undo step. `DrawField`/`DrawValue` gained
an optional `const SlotContext*` (defaulted null → legacy text input), passed by Inspector
(components + script fields), Environment, WorldSystems, and Material Editor — so Mesh/Material/
Hdri/Texture/collider/terrain slots all upgrade. `ContentBrowserPanel::RevealAsset(vfs)` navigates
+ selects; the shell-free reveal rides `ctx.PendingRevealAsset`. Build Debug zero warnings; engine/
`CosmicTests` unchanged. On-GPU: slot assign/reveal/picker (user list).

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
re-enable restores; headless tests for the gates. **Status:** ✅ 2026-07-12 — reflected
`bool Enabled = true` added to `MeshRenderer`/`SpriteRenderer`/`Directional`+`PointLight`/
`ParticleEmitter`/`Water`/`Box`+`Sphere`+`Capsule`+`MeshCollider` and gated: `SubmitOpaqueMeshes`
(lit + shadow), `GatherSceneLights`, both sprite passes, `OnRenderWorldFX` water/particle draw +
`SyncWorldSystems` build, `ScenePhysics::BuildBodyDesc` (each collider `try_get; c && c->Enabled`;
empty shapes → no body, already graceful). Registered `.HideInInspector().OmitIfTrue()` — a new
reusable `Field_OmitIfTrue` flag makes the serializer skip a default-true bool, so **unchanged
scenes stay byte-identical** (the "Enabled" key is written only once toggled false; absent →
loads true). Inspector: a header checkbox for any descriptor exposing `Enabled` (undoable
`CommitFieldEdit`), and a header context menu **Copy Component** (reflected-JSON clipboard via
`SaveReflectedToString`) / **Paste Values** (type-matched; `ApplyComponentValues` per-field
`CommitFieldEdit`, fans to the selection) / **Reset to Defaults** (default temp instance). Headless
tests: serialization compat (`test_scene_serializer` — Enabled omitted while true, written+
round-tripped when false, absent→true) and a physics gate (`test_physics_scene` — a box falls
through a disabled ground, rests on an enabled one). Build Debug zero warnings; `CosmicTests`
**298/298** (+2). On-GPU: disable-a-mesh + copy-a-light (user list).

### T13 — Per-entity Active semantics + Hierarchy toggle *(gap §6.2, §14.4)*
**Files:** engine `scene/Components.h` (`TagComponent` gains `bool Active = true`),
`scene/Scene.*` (effective-active = own ∧ ancestors, cached per frame beside the transform
walk; gates: render submit, world-FX sync, `UiSystem`, physics bake, script dispatch);
serializer default-compat. Starforge `panels/HierarchyPanel.cpp` (checkbox/eye per row, dimmed
subtree), undoable via `CommitFieldEdit`.
**Spec/Acceptance:** headless: inactive parent hides+freezes the subtree (not rendered, not
ticked, not baked); old scenes load identical; toggling in the editor is one undo step.
**Status:** ✅ 2026-07-12 — `TagComponent::Active` (default true, `.HideInInspector().OmitIfTrue()`
→ every-entity byte-identical compat). `Scene::IsActiveInHierarchy(entity)` = own ∧ every
ancestor's, walking the parent chain (deviation: **walked per query like `WorldOf`**, not a
per-frame cache — `WorldOf` isn't cached either; a memo is a perf follow-up). Gated: mesh/LOD/
voxel submit + both sprite passes + water/particle draw + world-FX sync (Scene), `GatherSceneLights`
(threaded `*this`), `ScriptHost::Tick/FixedTick` per-entity `OnUpdate`, `ScenePhysics::BuildBodies`
(rigid + character), `UiSystem` canvas collection. Hierarchy: a per-row eye toggle
(`ICON_LC_EYE/EYE_OFF`, `CommitFieldEditFor` → one undo step, targets just that entity) + the row
dims (`TextDisabled`) when effectively inactive (own ∧ ancestors). Headless tests: the semantic
(`test_hierarchy` — own ∧ ancestors across a 3-deep chain), serialization compat
(`test_scene_serializer` — Active omitted while true, written/round-tripped when false, absent→
true), and a physics gate (`test_physics_scene` — a box falls through an inactive ground). Build
Debug zero warnings; `CosmicTests` **301/301** (+3). Follow-ups (documented): per-frame active
memo, per-UI-node + SystemScript-membership active filtering. On-GPU: eye-toggle-hides-subtree +
undo (user list).

### T14 — Hierarchy icons + counts *(gap §6.1; dep T5's glyph table)*
**Files:** Starforge `panels/HierarchyPanel.cpp`.
**Spec:** row glyph from the dominant component (camera/light/mesh/terrain/water/emitter/voxel/
UI/script), colored by the T5 table; entity + selected counts in the header. **Acceptance:**
every ForgePlayground entity shows a sensible icon; no layout regressions with deep trees.
**Status:** ✅ 2026-07-12 — `HierarchyPanel::IconFor(e)` returns a `{glyph, color}` from the
entity's dominant component (first match: Camera → light → Terrain → Water → Emitter → Voxel → UI
→ Tilemap → Sprite → Mesh/Primitive/LOD → Script → empty), colored to match the T5 asset-type
palette. Each row draws the colored glyph (`TextColored`) between the T13 eye toggle and the name
(SameLine, so the tree indent/arrow/drag still work). The header shows `N entities · M selected`.
Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU: ForgePlayground icons + deep-tree
layout (user list).

### T15 — Inspector live-Play behavior *(gap §5.6)*
**Files:** Starforge `panels/InspectorPanel.cpp`, `commands/EditorCommands.cpp`.
**Spec:** while playing: value backgrounds tinted (live affordance); edits apply directly
WITHOUT pushing undo entries (they die with the Stop snapshot-restore — today they pollute the
stack). **Acceptance:** tune a field during Play → change visible, undo stack unchanged after
Stop. **Status:** ✅ 2026-07-12 — new `EditorContext::Playing` (the shell mirrors `IsPlaying()`
each frame). While playing, `InspectorPanel` tints the value backgrounds (`ImGuiCol_FrameBg*`) +
shows a `▶ Live — edits are temporary` banner. `Commands::CommitFieldEdit`/`CommitFieldEditFor`
short-circuit when `ctx.Playing`: the value is already applied live by the widget (and
`CommitFieldEdit` still fans it across the selection), but **nothing is pushed to the undo stack
and the scene is not dirtied** — so the edits die with the Stop snapshot-restore and the undo
stack is unchanged after Stop. Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU:
tune-during-Play + undo-stack-unchanged (user list).

### T16 — Console v2 *(gap §13.1)*
**Files:** Starforge `panels/ConsolePanel.h/.cpp` (+ `EditorContext` `ConsoleLine` gains a
`Source` enum where lines are pushed).
**Spec:** text search filter; Engine/Editor/Game source chips; monospace body; ring-buffer cap.
Existing severity filters/timestamps/copy stay. **Acceptance:** filter combinations behave;
10k-line sessions stay responsive. **Status:** ✅ 2026-07-12 — `EditorContext` gained
`enum LogSource {Engine,Editor,Game}` + `ConsoleLine::Source`; `Log()` takes an optional source
(default Editor). `DrainLogQueue` tags sink lines by the `[%n]` prefix (`[COSMIC]` → Engine, else
→ Game — e.g. script `CS_*` logs during Play). `ConsolePanel` adds a right-aligned
`InputTextWithHint` **text search** (case-insensitive `ContainsCI`) + **Engine/Editor/Game source
chips** alongside the severity filters; the body renders in a **monospace** face (ImGui's built-in
ProggyClean, `Fonts[0]`) for column alignment; Copy-visible now prefixes each line with its source
tag. Ring-buffer cap raised to 4000 (drops 800 when exceeded) so long sessions stay bounded/
responsive. Timestamps/severity colors/auto-scroll/copy preserved. Build Debug zero warnings;
engine/`CosmicTests` unchanged. On-GPU: filter combinations (user list).

### T17 — GPU profiler panel (port) *(gap §13.2)*
**Files:** NEW Starforge `panels/ProfilerPanel.h/.cpp` (adapt Frontier's `GpuProfilerPanel`);
View menu + K3 layouts pick it up.
**Spec:** per-pass GPU ms from the `SceneRenderer` timer-query zones (F3 verbs) + CPU frame
breakdown; history sparkline. **Acceptance:** numbers match Frontier's panel on the same
scene; zero cost when closed. **Status:** ✅ 2026-07-12 — NEW Starforge `panels/ProfilerPanel.h/
.cpp` ports Frontier's `GpuProfilerPanel` (F3): CPU frame + fps, GPU frame total, a per-pass
GPU-ms table (`RenderCommand::GetGpuZoneResults()` — same source as Frontier, so numbers match)
with bar overlays, and `Renderer3D::GetStats()` queue telemetry (CPU-side render breakdown), PLUS
rolling **CPU/GPU history sparklines** (`PlotLines` over a 120-frame ring). Wired: `m_Profiler` +
`m_ShowProfiler` (off by default → zero cost when closed), a **View ▸ Profiler** toggle, and the
K3 **Telemetry** preset shows + docks it (BottomRight, tabbed with Console — `LayoutPanels::
Profiler` + `SetAll`). Build Debug zero warnings; engine/`CosmicTests` unchanged. On-GPU:
numbers-vs-Frontier + sparkline (user list).

### T18 — Jobs / Resources panel *(gap §13.3; dep T2)*
**Files:** engine `jobs/JobSystem.h` (read-only stats: queued/active/completed, worker count);
NEW Starforge `panels/SystemPanel.h/.cpp` (two tabs).
**Spec:** Jobs tab: live counters; Resources tab: T2 enumeration table (path/type/refs/bytes)
with per-row Reload. **Acceptance:** terrain rebuild shows job activity; resource totals match
the K5 status-bar chip. **Status:** ✅ 2026-07-12 — engine `JobSystem` gained read-only stats
(`GetQueuedCount` [brief lock; `m_QueueMutex` now mutable], `GetActiveCount`, `GetCompletedCount`
[new monotonic `m_CompletedJobs` atomic, bumped per finished job]; worker/core counts existed).
NEW Starforge `panels/SystemPanel.h/.cpp` — **Jobs** tab (workers/cores + queued/active/completed
counters) + **Resources** tab (a scrollable `AssetLibrary::Enumerate` table: asset/type/refs/CPU/
GPU with a per-row **Reload** button and a totals header). Wired: `m_System` + `m_ShowSystem`
(off by default), **View ▸ System** toggle, and the K3 Telemetry preset docks it (BottomRight,
tabbed). The K5 **status bar** now fills its reserved slot with an asset-memory chip using the
**same** `Enumerate` call, so its totals match the panel by construction. Build Debug zero
warnings; `CosmicTests` **301/301** (JobSystem stats are additive/read-only). On-GPU: terrain-
rebuild job activity + totals-match (user list).

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/22-phase23-asset-workflows-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md` (re-verify file references — they drift).
> Reflection/serializer changes must keep old scenes byte-identical; editor mutations go
> through `commands/EditorCommands`; headless-test what can be. Roadmap cmake recipe; compat
> gate; no git writes. Finish with Acceptance + status banner.
