# Example-Images Gap Analysis — what it takes for Cosmic/Starforge to match the reference editors

> **Created 2026-07-11.** The user dropped ten screenshots into `ExampleImages/` (three distinct
> commercial-grade editors, a dialogue-graph tool, and a shipped 2D game) and asked: *what has to
> change in the engine and editor so Cosmic/Starforge has similar functionality and a similar
> feel?* This document is that analysis.
>
> **STATUS — ADOPTED (same day):** the user green-lit integration. This document is now the
> **spec of record for roadmap v4 Phases 22–28** (`docs/plans/` docs 21–27) — every work order
> there cites its § here. Cluster → phase map: §1–§3 → Phase 22 (doc 21, K-items); §4–§6 +
> §13 + §14.1/2/5 → Phase 23 (doc 22, T-items); §8 + §5.5 → Phase 24 (doc 23, M-items); §9 →
> Phase 25 (doc 24, Q-items); §10 → Phase 26 (doc 25, N-items); §7.2–7.5 + §11 + §12 →
> Phase 27 (doc 26, X-items); the capstone showcase → Phase 28 "Forge Isle" (doc 27, Z-items).
> The drop-a-PNG branding request (icon + top-bar logo hot-swap) is Phase 22 **K1**.
> When implementation details here conflict with a phase doc, the phase doc wins (it is newer);
> flag the drift here with a dated note.
>
> **How to read it:** §0 inventories what the images actually show. §1–§13 group that into
> capability clusters; each item states the **Target** (what the reference editor does), **Today**
> (current Cosmic/Starforge state, with file references verified against the working tree on the
> creation date), and the **Change** (what to build, where, and how). §14 collects the
> cross-cutting engine services many items share, and §15 is a sizing/sequencing summary.
> Items that already have a phase home in the live plan docs are *cross-referenced, not
> re-specified* — the pointer is the spec of record. Sizes use the FEATURE-MATRIX legend
> (S ≤ 1 session · M 1–2 · L 3–6 · XL a phase).
>
> **House rules still apply to everything here:** the engine ships generic verbs, apps own domain
> logic (roadmap §"one design rule"); editor-only behavior stays in `Projects/Starforge`; new GPU
> state goes through `RendererAPI`/`RenderCommand` verbs + `renderer/BindingPoints.h`; reflected
> components keep the compat gate (shipped apps unchanged by default).

---

## 0. The reference material (`ExampleImages/`)

| Image | What it shows | Feeds clusters |
| --- | --- | --- |
| `IMG_2208.png` | Unreal-style level editor: centered Play/Pause/Stop/Eject transport, Select-Mode + Platforms dropdowns, viewport with Lighting dropdown, combined move+rotate gizmo, per-operation snap toggles (10 / 15° / 0.25), axis navigator bottom-left; bottom **Asset Browser** (folder tree + tile grid + breadcrumbs + back/forward + search + Add/Import/Save All); right **Details** panel: live actor transform, Static Mesh slot with thumbnail, per-slot **Materials** array (color swatch, material dropdown, Roughness/Metallic sliders inline), Physics asset slot, Simulate Physics (mass/damping/gravity), property **search box** | §1 §2 §3 §4 §5 |
| `IMG_2209.png` | Dedicated **skeletal-mesh/animation asset editor** opened as its own tabbed document: content browser with typed anim assets (skeleton, skeletal mesh, anim clips, anim controller), skeleton tree, mesh+bones preview viewport with a joint gizmo, **joint sockets** (add/remove, socket translation/rotation/scale), animation **timeline with keyframe ticks** + Pause/Stop/Loop | §8 |
| `IMG_2210.png` | "Ignite (Vulkan…) — 238 FPS" native editor: Hierarchy with entity count/Type/Active columns; Inspector with Add Component, Transform, **Mesh Renderer with per-map texture thumbnails** (base color/metallic/roughness/emissive); **Project Settings ▸ Environment**: Load HDR Texture, Sun Angles (Elevation/Azimuth), Color, Intensity, Angular Size, Ambient, Exposure, Gamma; photoreal DamagedHelmet under HDRI | §5 §6 §7 |
| `IMG_2211.png` / `IMG_2213.png` | Web-based engine, "Default" workspace: Project tree with **typed asset rows** (Audio/Material/Mesh/Particles/Scene/Script/Image/Composition) + storage quota bar; Renderer viewport with **Free-camera dropdown** + icon toolbar + grid; toolbar **undo/redo with counts**; workspace **layout tabs** (Default/Coding/Composition/Maps/Custom); Inspector = searchable scene tree (type column, Disabled state) + "3D Object" section (**Active/Static checkboxes**, Layers, Tags, Receive/Cast shadow, per-field ⓘ tooltips, section Copy/Paste); bottom dock tabs (Code/Cache/Resources/Variables/Jobs/Composition/Maps/Asset Store/**Profiler**/**Log**); bottom-right **asset Preview panel with metadata** (mime, size, boundingBox, vertexCount, triangleCount); viewport **stats chip row** (resolution, Geo/Tex counts, Tris, distance) | §1 §2 §4 §5 §6 §13 |
| `IMG_2214.png` | Same engine, "Custom" workspace: **post-processing node graph** (Camera node with Color/Depth/DepthComparison/Normal outputs → Vignette → Bloom, each node enable + params, Add node, preview toggle); Inspector "Sky specific fields" (**physical atmosphere**: Size, Turbidity, Rayleigh, Mie coefficient, Mie directional G, Inclination, Azimuth) + per-object **Add script** button; mesh Preview thumbnail with metadata | §7 §9 |
| `IMG_2215.png` | Same engine: **NavMesh authoring** — Inspector "NavMesh specific fields": Type (Tile cache), Mode (From children), Auto generate, Always render helper, **Regenerate now** trigger, Recast-style params (voxel cell size/height, walkable climb, max edge length, region areas…); raycast-hit object dump in the Log | §10 |
| `IMG_2216.png` | Same engine: **GPU particle authoring** — emitter shape/type combos, Bounding Box settings, **Curl Noise module with a live noise-preview thumbnail** + strength/frequency/octaves; scene uses a "Background Shader" asset; `published/` folder in the tree | §11 |
| `IMG_2217.png` | A **shipped top-down 2D survival game**: night darkness with a warm campfire light radius, tile-built walls/water, minimap with unexplored fog, hotbar with item counts/durability, HP/XP bars, floating character nameplate | §12 |
| `IMG_2218.png` | "StoryFlow Editor": **dialogue node graph** (Start → Dialogue nodes → End) where each node carries a background image, character portrait, audio clip, rich text, and **option buttons with per-option conditions + "once" flags**; left **typed-variables blackboard** (grouped: Player/Monster/World; Integer/String/Boolean/Enum with defaults) + flows list; multi-tab documents; right edit-node panel with **Use Variable** bindings; Play preview | §9 |

The engines differ, but the *feel* they share is consistent and nameable: **icon-first chrome with a
centered transport bar; the viewport is a self-contained instrument (camera/view/snap/stats live on
it, not in a distant window); assets are typed, thumbnailed, searchable, and previewable
everywhere; every object property panel is searchable, tooltipped, and drag-target-ready; heavyweight
content (anim, flow, post, particles) gets its own dedicated document-style editor rather than one
shared inspector.** That is the bar this document decomposes.

---

## 1. Editor shell & chrome

### 1.1 Icon toolbar with a centered transport bar — **M**
**Target (2208/2211):** one top strip: left = file/tool icons, **center = Play · Pause · Step ·
Stop (· Eject)** as icon buttons, right = Platforms/Package + layout tabs. Reads as a product, not
a debug panel.
**Today:** `StarforgeApp::DrawTopBar` ([StarforgeApp.cpp:1024](../../Projects/Starforge/src/StarforgeApp.cpp))
renders text buttons left-aligned in flow order: `Play`/`Stop`/`Resume`/`Step`
(`DrawPlayControls`), `Build Scripts` + auto checkbox + status text (`DrawBuildControls`),
`Run App`, then the gizmo toolbar. Functionally complete (H5 fixed clipping); visually a debug
strip.
**Change (editor-only):** rework `DrawTopBar` into three `ImGui` groups with measured centering
(compute transport width, `SetCursorPosX((avail - width) * 0.5f)`). Replace text with Lucide
glyphs — the font is already merged engine-wide (`Cosmic/src/ui/IconsLucide.h`, doc: ImGui
modernization); keep text in tooltips with shortcut hints. Fold `Build Scripts` status into a
small colored dot + tooltip; move `Run App`/`Package` right. Eject itself is Phase 17 **U7**
(doc 16) — leave a disabled slot for it so the bar doesn't reflow when it lands.

### 1.2 Workspace layout presets (Default / 2D / Animation / Custom tabs) — **M**
**Target (2211):** named dock layouts switchable from the top-right; users save their own.
**Today:** one hand-built default layout + `View ▸ Reset Layout`
([StarforgeApp.cpp:1214](../../Projects/Starforge/src/StarforgeApp.cpp)); panel visibility bools
persist via `EditorPrefs`. The engine dock system already supports named-port docking
(`WorkspaceLayer::DockWindow(name, DockPort::…)`).
**Change (editor-only):** a `LayoutPresets` helper in Starforge that snapshots/restores layouts
with `ImGui::SaveIniSettingsToMemory()` / `LoadIniSettingsFromMemory()` (plus the panel-visibility
bool set) into `user://starforge/layouts/<name>.ini`. Ship 3 built-ins (Level · Assets · Telemetry —
grow one per new document editor from §8/§9) + "Save layout as…". UI: small tab strip at the top
bar's right end. Gotcha: ini-from-memory restores by window *name*, so panels must keep stable
names (they do — panel titles are fixed strings).

### 1.3 Undo/redo buttons with history visibility — **S**
**Target (2211):** toolbar ⟲/⟳ buttons with pending-count badges.
**Today:** full undo stack exists (`core/CommandStack.h`, editor `commands/EditorCommands.*`) but
is reachable only via `Edit` menu / Ctrl+Z ([StarforgeApp.cpp:1170](../../Projects/Starforge/src/StarforgeApp.cpp)).
**Change (editor-only):** toolbar icon buttons + count badges; `CommandStack` needs tiny
accessors if missing (`UndoCount()`, `RedoCount()`, and `NameAt(i)` for a hover-list of the last
~10 actions). Optional: a History popup listing recent commands (names already exist —
`UndoName()`/`RedoName()`).

### 1.4 Status bar (bottom strip) — **S**
**Target (2211):** persistent bottom edge: storage/memory quota, play state, counts.
**Today:** nothing; stats live in a floating Statistics window
([StarforgeApp.cpp:1232](../../Projects/Starforge/src/StarforgeApp.cpp)).
**Change (editor-only):** a fixed `ImGuiWindowFlags_NoDecoration` strip pinned under the dockspace
(same pattern as the top bar's `m_TopBarBottomY` anchoring, mirrored to the bottom): FPS,
entity count, selected count, build-module state, play state, and — once §14.2's asset accounting
exists — "assets: N (X MiB CPU / Y MiB GPU)". Engine change: none (accounting is §14.2).

---

## 2. The viewport as an instrument

### 2.1 In-viewport header strip (camera · view mode · gizmo · snaps) — **M**
**Target (2208/2211):** the viewport carries its own header row: camera selector, lighting/view
dropdown, gizmo mode buttons, snap toggles. The rest of the chrome never needs to be visible to
fly around a level.
**Today:** gizmo/snap/grid/collider toggles render in the *top bar* — `ViewportController::DrawToolbar`
([ViewportController.cpp:404](../../Projects/Starforge/src/ViewportController.cpp)) with
`RadioButton("Move"/"Rotate"/"Scale")`, one shared `m_SnapValue`, `Front/Top/Iso/Frame` buttons.
The engine already provides an in-viewport ImGui surface: `WorkspaceLayer::BeginViewportOverlay()`
(used for the gizmo and the UI-canvas preview).
**Change (editor-only):** move `DrawToolbar` content into a translucent strip drawn inside
`BeginViewportOverlay()` (top-left, like 2208), as icon buttons. Split snapping into
**per-operation values** (`m_SnapMove = 0.25 m`, `m_SnapRotate = 15°`, `m_SnapScale = 0.1`) —
`Gizmo::Manipulate` already takes a per-call `snap` float
([Gizmo.h:78](../../Cosmic/src/graphics/Gizmo.h)), so this is pure editor state; show the three
values as editable chips exactly like 2208's `10 | 15° | 0.25`.

### 2.2 Camera selector + editor fly camera — **M**
**Target (2211):** a "Free camera" dropdown that can also possess any scene camera; fast WASD fly
with adjustable speed.
**Today:** Starforge is orbit-only (`camera/OrbitCameraController`, pose-based since H1);
`camera/FlyCameraController` exists (F1, Frontier-proven) but Starforge never instantiates it.
Scene cameras (`CameraComponent`) are only used by Play/game view.
**Change (editor-only, engine reuse):** a small `EditorCameraRig` in Starforge owning both
controllers and a mode enum {Orbit, Fly, Possess(entity)}; RMB-hold-in-viewport temporarily
enters Fly (the Unreal idiom), scroll while flying scales speed; Possess renders the scene from
the selected `CameraComponent`'s pose (read-only — no writing back). Dropdown lists
`Free (Orbit)` / `Free (Fly)` / every camera entity by Tag. Hook point:
`StarforgeApp::OnUpdate` already gates camera control on viewport hover
([StarforgeApp.cpp:805](../../Projects/Starforge/src/StarforgeApp.cpp)).

### 2.3 Axis navigator widget (view cube) — **S**
**Target (2208/2211):** clickable orientation gizmo in a viewport corner.
**Today:** the engine *has* one — `camera/NavigationCube.h` (S5 CAD-nav) — but only
`Projects/Engine3DDemo` uses it; Starforge exposes `Front/Top/Iso` text buttons instead.
**Change (editor-only):** instantiate `NavigationCube` in the viewport overlay (bottom-left like
2208), wire its face-clicks to `OrbitCameraController::SnapView(ViewPreset…)`; delete the three
text buttons. Verify its hit-testing works from `BeginViewportOverlay` (it predates that API —
if it draws via `Renderer3D` in its own corner viewport, keep that path and just feed it the
editor camera + input rect).

### 2.4 Viewport stats chip row — **S**
**Target (2211/2213):** live chips on the viewport bottom edge: `1920×1016 · Geo … · Tris … ·
Distance …`.
**Today:** `Renderer3D::GetStats()` (draws/submitted/culled/instanced) exists and feeds the
floating Statistics window; GPU pass timings exist via the F3 profiler verbs
(`SceneRenderer` zones — surfaced today only by Frontier's `GpuProfilerPanel`).
**Change (editor-only):** render the chip row inside the viewport overlay (toggle in the View
menu): resolution (`Application::GetViewportSize`), Renderer3D stats, camera-to-pivot distance
(`OrbitCameraController::GetDistance()`), and frame ms. Fuller profiler surfacing is §13.2.

### 2.5 View modes (Lit / Unlit / Wireframe / Entity-ID) — **S**, planned home exists
**Target (2208):** a "Lighting" dropdown on the viewport.
**Today / plan:** not implemented; **already specced as doc 18 R8** (`SetPolygonMode` verb +
Starforge view-mode menu). This document adds only the *placement* requirement: the selector
belongs on the §2.1 viewport strip, not in a menu.

### 2.6 Editor grid/overlay polish — **S**
**Target:** infinite-feeling grid that fades with distance and adapts spacing to zoom.
**Today:** `Renderer3D::DrawGrid(50, 1, …)` — fixed 50 m extent, fixed 1 m step
([ViewportController.cpp:253](../../Projects/Starforge/src/ViewportController.cpp)).
**Change (engine, small):** either extend `DrawGrid` with camera-aware fade (distance-alpha per
line, step decade switching on zoom like CAD apps) or add a dedicated one-quad
`InfiniteGrid.glsl` (ray-plane in the fragment shader — the standard trick) drawn by a new
`Renderer3D::DrawInfiniteGrid(desc)`; the shader route is cheaper at large extents and is what
the references visually do. Editor passes the camera; default off for apps (compat).

---

## 3. Selection & manipulation feel

### 3.1 Combined ("universal") transform gizmo — **S**
**Target (2208):** translate arrows + rotate rings + plane handles in ONE gizmo.
**Today:** `Gizmo::Operation{Translate,Rotate,Scale}` single-op only
([Gizmo.h:57](../../Cosmic/src/graphics/Gizmo.h)); vendored ImGuizmo *already supports*
`OPERATION::UNIVERSAL` (bitmask).
**Change (engine, tiny):** add `Operation::Universal` mapping to
`ImGuizmo::TRANSLATE | ROTATE | SCALEU` in `graphics/Gizmo.cpp`; editor adds a fourth mode button
(shortcut `Q` or `T`). Per-op snap from §2.1 passes the *move* snap when universal (ImGuizmo
limitation: one snap vector per call — acceptable; document it).

### 3.2 Selection outline (post-process silhouette) — **M**
**Target (2208/2210):** selected meshes get a crisp colored outline, not a bounding box.
**Today:** an oriented wire AABB per selected mesh
([ViewportController.cpp:259](../../Projects/Starforge/src/ViewportController.cpp)). The two
ingredients already exist: `scene/ScenePicker` renders an entity-ID buffer on demand, and
`renderer/PostProcessStack` owns fullscreen passes.
**Change (engine + editor):** new optional pass `Outline.glsl`: render selected entities' IDs (or
a 1-bit mask) into a small FBO (reuse `ScenePicker`'s ID pass with a selection filter — add a
`RenderIdPass(scene, cam, w, h, const std::vector<entt::entity>* only = nullptr)` overload), then
edge-detect (4-tap neighbor compare) and composite over the LDR target. Engine surface:
`SceneRendererSettings{ OutlineEnabled, OutlineColor, OutlineWidthPx }` + a
`SceneRenderDesc::SelectedEntities` span, default off (compat). Editor: feed
`EditorContext::Selection`, drop the wire box (keep it for lights/colliders which have no mesh).
Keep the wire-box path as the fallback when the outline pass is off.

### 3.3 Drag-and-drop into the viewport — **M**
**Target (2208-style UX, implied by every reference):** drag a mesh/prefab from the browser into
the world to spawn it at the hit point; drag a material onto a mesh to assign it.
**Today:** drag *sources* exist (`ASSET_PATH` payload,
[ContentBrowserPanel.cpp:219](../../Projects/Starforge/src/panels/ContentBrowserPanel.cpp)); the
only *targets* are Inspector asset slots. The world-point probe already exists:
`ViewportController::ProbeWorldPoint` (ID-pass depth readback,
[ViewportController.cpp:219](../../Projects/Starforge/src/ViewportController.cpp)).
**Change (editor-only):** accept the payload over the viewport — inside the viewport overlay use
`ImGui::BeginDragDropTargetCustom(viewportRect, id)`; on drop: `.obj/.gltf/...` & `.cprefab` →
spawn via the existing create/instantiate commands at `ProbeWorldPoint` (fallback: 10 m along the
camera ray), `.cmat` → ID-pick the entity under the cursor and `CommitFieldEdit` its
`MaterialPath`, images → assign to `SpriteRenderer` if hit entity has one. All spawns/assigns go
through `commands/EditorCommands` so they're undoable.

---

## 4. Content Browser v2

The references treat the asset browser as the editor's second-most-important surface. Today's
panel ([ContentBrowserPanel.cpp](../../Projects/Starforge/src/panels/ContentBrowserPanel.cpp)) is
a single-pane tile grid: breadcrumbs, double-click actions, drag source, recycle-delete,
texture-only thumbnails, folder/file creation context menus, FileWatcher-driven texture reload.
Solid core; the gaps below are additive. (Doc 19 **A4** already owns "real thumbnails via an
offscreen preview rig"; §14.3 makes that rig a shared service because four clusters need it.)

### 4.1 Two-pane layout: folder tree + tiles, history, search — **M**
**Target (2208/2211):** persistent folder tree on the left, tile grid right, back/forward
buttons, a search box filtering recursively, tile-size slider.
**Change (editor-only):** split the panel with `ImGui` columns/child windows; tree = recursive
`fs::directory_iterator` over directories only (lazy-expand); maintain a `std::vector<fs::path>`
history + cursor for back/forward (mouse buttons 4/5 too); search box switches the grid to a
recursive filtered flat view (name `contains`, case-insensitive — reuse the Hierarchy's
`ToLower` filter idiom); tile size slider scales the `cell` constant (84 px today). Persist tree
width / tile size in `EditorPrefs`.

### 4.2 Typed visual identity + per-type create menu — **S**
**Target (2208/2211):** every asset type is instantly recognizable (colored icon/badge); the
"Add" button offers Material / Scene / Prefab / Emitter / Flow / Palette creation in place.
**Today:** 4-char text badges (`SCN`, `MAT`, `MSH`…, [ContentBrowserPanel.cpp:49](../../Projects/Starforge/src/panels/ContentBrowserPanel.cpp));
creation menu covers Folder + Scene only. Cosmic already has more first-class types than the
menu admits: `.cscene`, `.cprefab`, `.cmat`, `.cmeta`, `.cemitter`, `.cflow`, `.cpal`, `.cvox`,
`.cseq` (future), audio, images, models.
**Change (editor-only):** a `AssetTypes.h` table in Starforge mapping extension → {Lucide glyph,
accent color, display name, create-fn?, open-action}. Tiles draw glyph-on-color; the same table
drives the create menu ("New Material" writes a default `.cmat` via
`AssetLibrary::SaveMaterialAsset`, "New Emitter" a default `.cemitter`, "New Flow" a minimal
`.cflow`, "New Palette" a default `.cpal`) and double-click routing (today hardcoded per-ext).

### 4.3 Rename-in-place + move — **S**
**Target:** F2/slow-click rename; drag files between folders.
**Today:** no rename/move (delete-to-recycle only; renames are by-design non-undoable —
FEATURE-MATRIX "✖ revisit only if it bites" row covers *undo*, not the feature).
**Change (editor-only):** F2/context "Rename" swaps the label for an `InputText`;
`fs::rename` on commit. Accept tile-drops onto tree folders for move. **Reference fixup is the
real work:** scenes store VFS string paths (`MeshPath`, `MaterialPath`, `HdriPath`…), so add a
`ProjectAssets::RetargetPath(oldVfs, newVfs)` sweep in Starforge that rewrites matches across all
`.cscene`/`.cprefab`/`.cmat` JSON in the project (they're line-oriented JSON; do it via
`SceneSerializer` load→fix→save to stay schema-safe), with a confirmation dialog listing hits.

### 4.4 Asset preview + metadata panel — **M**
**Target (2211/2214):** selecting an asset shows a live preview (mesh turntable, texture, audio
waveform/play) + metadata chips (size on disk, bounding box, vertex/triangle count, mime).
**Today:** double-click texture → modal preview; nothing else.
**Change:** a "Preview" child pane at the panel's bottom (or a separate dockable panel).
Meshes/materials render via the §14.3 preview rig with drag-orbit; textures show dimensions +
`AssetLibrary` byte size; audio gets a Play button (engine `audio/AudioEngine` one-shot) —
waveform drawing needs a small engine accessor `Sound::CopyPcm(std::vector<float>&, maxSamples)`
(decode-once, cached). Metadata needs §14.2's accounting accessors (`Mesh::GetVertexCount()` /
`GetIndexCount()` exist? verify — add if missing; disk size from `fs::file_size`).

### 4.5 Import UX — **S**
**Target (2208):** an Import button on the browser; drops from Explorer.
**Today:** `File ▸ Import Model…` (E16 dialog + `.cmeta`); images/audio must be hand-copied.
**Change (editor-only):** toolbar Import button = same dialog, but generalized: models route
through the E16 path; images/audio/HDR copy into the current folder (+ `AssetLibrary::Reload`).
Accept OS file drops: engine `Window` already surfaces GLFW-style drop events? — verify; if not,
add a `WindowFileDropEvent` in `events/ApplicationEvent.h` + Win32 `WM_DROPFILES` handling in
`platform/` (small, generic, useful everywhere).

---

## 5. Inspector v2 (the Details-panel feel)

Foundation is genuinely good — reflected auto-UI with undo (`CommitFieldEdit`), multi-select
intersection with mixed-value display, categorized Add Component
([InspectorPanel.cpp](../../Projects/Starforge/src/panels/InspectorPanel.cpp),
[widgets/PropertyRows.h](../../Projects/Starforge/src/widgets/PropertyRows.h)). The references
add polish that mostly lands in `PropertyRows` + reflection metadata (§14.1).

### 5.1 Property search box — **S**
**Target (2208):** type in the Details panel to filter to matching properties across all
components.
**Change (editor-only):** search field at the panel top; when non-empty, iterate components but
draw only fields whose name (or component name) matches; auto-open matching headers. Pure
`InspectorPanel` logic.

### 5.2 Per-field tooltips (ⓘ) + nicer widgets from metadata — **M** *(needs §14.1)*
**Target (2211):** every row explains itself on hover; sliders have sane ranges/units.
**Today:** `Reflect::FieldDescriptor` carries name/kind/flags only; `PropertyRows` guesses widget
params.
**Change:** after §14.1 adds `Doc`, `Min/Max`, `Step`, `Units`, `Degrees` metadata to the
reflection registry, `PropertyRows::DrawField` renders a dimmed ⓘ (or hover on the label) with
`Doc`, and switches widget per metadata (bounded → `SliderFloat`, color already handled, degrees
→ show °). Then *populate* the metadata for the big components (`EnvironmentComponent`,
`TerrainComponent`, `WaterComponent`, `ParticleEmitterComponent`, physics set) — that's the bulk
of the M.

### 5.3 Asset-slot widget with thumbnail + picker — **M** *(needs §14.3 for thumbs)*
**Target (2208/2210):** asset references render as [thumbnail | name | browse ▾ | clear ⟲], are
drop targets, and click-to-locate in the browser.
**Today:** `AssetPath`-flagged string fields draw as text inputs that accept drops (E16/E17
convention).
**Change (editor-only):** a dedicated `PropertyRows::DrawAssetSlot(field, comp, typeHint)`:
thumbnail from the §14.3 cache (type glyph fallback), name button → highlight in Content Browser
(add `ContentBrowserPanel::RevealAsset(vfs)`), ▾ opens a filtered picker popup (recursive scan by
extension), ✕ clears. All edits still go through `CommitFieldEdit` (undo preserved).

### 5.4 Component-block QoL: enable toggles, copy/paste, reset — **M** *(engine bit in §14.4)*
**Target (2210/2211):** each component header has an enable checkbox; sections have
Copy/Paste; fields reset to default.
**Today:** headers offer Remove only; no per-component enable exists anywhere in the engine.
**Change:**
- *Copy/paste (editor-only):* serialize the component's reflected fields to a JSON clipboard
  string (the generic reflected-struct serializer from E17 — `SceneSerializer`'s
  `*Reflected` helpers) and paste via `CommitFieldEdit` per field (undoable, works across
  entities). Context-menu items on the header.
- *Reset (editor-only):* default-construct a temp component, copy per-field with undo.
- *Enable checkbox:* honest scoping — this is **per-component runtime semantics the engine
  doesn't have** (§14.4). Ship it only for components where "disabled" has an obvious meaning
  (renderers → skip submit; emitters → skip update; lights → skip collect; colliders → skip
  bake). Add a reflected `bool Enabled = true` to those components (compat: default true) and
  gate the corresponding `Scene::OnRender3D`/`SyncWorldSystems`/`ScenePhysics` paths. The
  Inspector then draws the checkbox for any component whose descriptor has an `Enabled` field —
  no special cases.

### 5.5 Material slots on meshes (multi-material models) — **L** (engine)
**Target (2208):** a "Materials" array section on the mesh renderer: one row per slot with
inline color/roughness/metallic and per-slot override.
**Today:** `MeshRendererComponent` = ONE `MeshAsset` + ONE `MaterialAsset`/`MaterialPath`
([Components.h:221](../../Cosmic/src/scene/Components.h)); multi-material sources import as
*child entities* (E16: multi-mesh → parent + child MeshRenderers), which works but means "the
gun's 4 materials" are 4 entities.
**Change (engine + editor):** teach `Mesh` submeshes (ranges + material index — `graphics/Model`
already understands per-part materials at load; `MeshData`/`Mesh` flatten them). Add
`std::vector<std::string> MaterialPaths` (slot-indexed) alongside the legacy single path
(compat: empty vector → old behavior byte-identical; serializer reads both). `Renderer3D::DrawMesh`
gains a submesh loop (each submesh = its own queue entry so sorting/instancing still work).
Editor: Inspector "Materials" list per slot using §5.3 slots. This is the one Inspector item
that's genuinely engine-architectural — schedule deliberately (it touches `MeshData`, `Mesh`,
import, draw submission, serialization).

### 5.6 Live values during Play — **S**
**Target (2208):** "Live Location/Rotation/Scale" readback while simulating.
**Today:** mostly works already — Play swaps `ctx.Scene` to the runtime scene
(`EditorSnapshot` restores on Stop), so the Inspector reads live components. Physics-driven
transforms come back through the play-session sync.
**Change (editor-only):** make it explicit and safe: while `PlayMode::Playing`, tint value
backgrounds (the 2208 "live" affordance) and suppress `CommitFieldEdit` history churn (edits
during play are intentionally transient — they die with the snapshot restore; today they still
push undo entries — gate `Commands::CommitFieldEdit` on play mode, apply directly instead).

---

## 6. Hierarchy v2

**Target (2210/2211):** entity count in the header; per-row type icon + type column; per-row
**Active** checkbox (grayed subtree when off); disabled rows render dimmed; search (have it).
**Today:** tree + search + create menu + drag-reparent + prefab affordances
([HierarchyPanel.cpp](../../Projects/Starforge/src/panels/HierarchyPanel.cpp)); no icons, no
visibility/active concept (grep confirms no `Visible`/`Enabled`/`Active` flags on any component).

- **6.1 Icons + count (editor-only) — S:** pick the row glyph from the entity's dominant
  component (Camera → 🎥-glyph, lights, mesh, terrain, water, emitter, voxel, UI, script…);
  Lucide glyphs, colored by the §4.2 type table. Entity/selected counts in the header (the
  Statistics window already computes them).
- **6.2 Per-entity Active toggle (ENGINE, §14.4) — M:** an eye/checkbox per row needs engine
  semantics: add a tiny reflected `ActiveComponent { bool Self = true; }` **or** a `bool Active`
  on `TagComponent` (pick the latter — every entity has a Tag; serializer default keeps old
  scenes identical). Effective-active = own flag ∧ parents' (compute alongside
  `Scene::GetWorldTransform`'s parent walk, cached per frame). Consumers gate on it:
  `Scene::OnRender3D`/`SceneRenderer` submit, `SyncWorldSystems`, `UiSystem`, `ScenePhysics`
  bake, script `OnUpdate` dispatch. Compat: default true everywhere; shipped apps never touch
  it. The editor checkbox commits through `CommitFieldEdit` (undoable, multi-selectable).

---

## 7. Environment, sky & project settings parity

**Today:** `EnvironmentComponent` is already close to 2210's Environment block
([Components.h:404](../../Cosmic/src/scene/Components.h)): sun dir/color/intensity,
Procedural/Detailed/HDRI sky + `HdriPath` (H4), IBL toggle+intensity, exposure, height fog,
bloom/SSAO/FXAA/lens-flare toggles; `EnvironmentPanel` auto-UIs it with undo.

- **7.1 Sun-angle authoring (editor-only) — S:** 2210 edits **Elevation/Azimuth**, not a raw
  vec3. Add a paired widget in `EnvironmentPanel`: two sliders (+ a small analemma-style disc
  drag) that read/write `SunDirection` through spherical conversion; keep `TimeOfDay` scrub.
  Pure UI on the existing field, undo intact.
- **7.2 Physical atmosphere parameters (engine) — M/L:** 2214 exposes Turbidity / Rayleigh /
  Mie coefficient / Mie directional-G. Cosmic's procedural sky (`renderer/EnvironmentMap` +
  `EnvSky`/`SkyDetail` shaders) is an artistic gradient + sun-disc model, not a scattering
  model. Change: extend the procedural path with a Preetham/Hosek-style analytic scattering
  option — new fields on `EnvironmentComponent` (`Turbidity`, `RayleighScale`, `MieScale`,
  `MieG`, gated behind `SkyMode::Physical`), shader work in the sky + IBL bake so lighting
  matches the visuals. Compat: existing modes untouched. (Pairs naturally with doc 18 R11's
  sky-depth verb if scheduled together.)
- **7.3 Ambient + gamma (engine, tiny) — S:** 2210 has Ambient and Gamma knobs. Add
  `AmbientIntensity` (scales the IBL/flat-ambient term — plumb through
  `Renderer3D::ApplySceneBindings` uniforms) and expose gamma (`Tonemap.glsl` hardcodes 2.2)
  as a `SceneRendererSettings`/`EnvironmentComponent` float. Default values keep output
  byte-identical.
- **7.4 Angular sun size (engine, tiny) — S:** `SkyDetailDesc` already supports a sun disc
  (F7); surface `SunAngularSize` on `EnvironmentComponent` for the Detailed/Physical modes and
  (optionally) soften shadow penumbra with it later.
- **7.5 Project Settings organization (editor-only) — S:** 2210/2211 put Pipeline/Environment
  under a tabbed **Project Settings** window. Starforge has Project Settings (S5-era) plus
  separate Environment/World panels. Reorganize Project Settings into a left-nav (General ·
  Window · Packaging · Input(future) · Physics defaults) — cosmetic consolidation, no new
  state.

---

## 8. Skeletal animation + the asset-editor framework — the largest single gap

`IMG_2209` is a *document-style asset editor*: its own tab, its own viewport, its own panels
(skeleton tree, socket details, timeline). Cosmic has **zero skeletal runtime**
(`graphics/Model.h` explicitly: skins/animation not handled) and Starforge has no concept of
"open an asset in an editor tab". Doc 19 **A2** already specs the runtime v1 (cgltf skins/clips,
`Skeleton.h`/`AnimationClip.h` pure sampling, `AnimatorComponent`, skinning-matrix SSBO +
`PBRSkinned.glsl` twin + shadow twin, Inspector clip picker + scrub). **A2 remains the spec of
record for the runtime.** What the image adds beyond A2:

- **8.1 Asset-editor host framework (editor) — L:** a Starforge `AssetEditorHost` that owns a
  tab bar of open documents (`IAssetEditor` interface: `Path()`, `OnUpdate`, `OnImGuiRender`,
  `Dirty()`, `Save()`), each with its own offscreen viewport when needed (a per-editor
  `SceneRenderer` + FBO — the §14.3 rig generalized from "thumbnail" to "interactive
  mini-viewport with orbit input"). Double-click routing from the Content Browser (§4.2 table
  gains an `open-in-editor` action). This framework is *load-bearing for four clusters* (anim
  editor here, flow/dialogue editors §9, particle editor §11 if promoted, future sequencer
  C6) — build it once, first consumer wins.
- **8.2 Skeleton/animation editor document (editor) — L (after A2 + 8.1):** panels: skeleton
  tree (joint hierarchy from `Skeleton`), preview viewport with bone overlay (line-drawn via
  `Renderer3D::DrawLine` over the skinned mesh) + joint selection/gizmo, clip list, timeline
  strip (play/pause/loop, time ruler, per-clip keyframe ticks read from `AnimationClip`'s key
  times), and an eventual socket panel (8.3). The timeline widget should be written as a
  *reusable* `widgets/Timeline.h` (tracks, ticks, scrub, snap) — C6 (sequencer) and §11
  (particle preview scrub) reuse it.
- **8.3 Joint sockets / attachments (engine) — M (after A2):** 2209's Add Socket → `hand.l`,
  then attach entities. Engine: `SocketComponent { std::string Joint; glm::vec3 Pos; quat Rot;
  vec3 Scale; }` on a child entity of an animated entity — `Scene::GetWorldTransform` composes
  parent world × current joint pose × socket offset (needs the animator to publish a pose
  palette per frame — A2's skinning matrices double as that). Compat: entities without the
  component unchanged. Editor: socket add/remove UI in 8.2, gizmo-editable offsets.
- **8.4 Anim controller asset — parked, note only:** 2209 shows an "ANIM CTRL" asset (state
  machine). FEATURE-MATRIX already parks blend trees/state machines (✖ until a real character
  project post-A2). When it unlocks, the §9 node-graph canvas is the natural editor; the asset
  (`.cactl`) should follow the `.cflow` pattern (states = clips, transitions = conditions on
  reflected fields/signals). No work now beyond keeping the file-type table (§4.2) extensible.

---

## 9. Node-graph tooling (flow, dialogue, post-FX)

Doc 16 **U6** already plans vendoring **imgui-node-editor** for the `.cflow` panel — that vendor
choice is the foundation for everything here; do U6 first and write the canvas as a reusable
Starforge widget (`widgets/NodeCanvas` wrapping the vendor lib: node body builder, pin/link
model, selection, context menus), not a flow-only panel.

- **9.1 Flow editor completion (editor) — the U6 work order, unchanged.** The images only add
  layout polish targets: node header colors by type, inline enable checkboxes, a mini-map,
  multi-tab documents via §8.1.
- **9.2 Typed-variables blackboard (engine + editor) — M:** 2218's left panel (grouped, typed,
  default-valued variables; conditions/bindings reference them). Engine: `FlowAsset` gains a
  `Variables` table (name → `FlowValue` default + optional group label — `FlowValue` already
  models Bool/Number/String, [FlowMachine.h:47](../../Cosmic/src/scene/FlowMachine.h); add an
  Enum-of-strings kind); `FlowMachine` holds runtime values, `FlowGuard` grows a variant that
  compares a *variable* (today guards read reflected entity fields only), actions grow
  `setVar`; script/EventBus access via `Signals()`-adjacent `Flow().GetVar/SetVar`. Serializer:
  `.cflow` JSON extension, versioned. Editor: a Variables side panel on the flow editor
  (add/remove/group, default editors per type, drag a variable onto a guard field).
- **9.3 Dialogue graph (engine + editor) — L, new scope:** 2218 is dialogue-shaped (portrait,
  background, audio, rich text, option buttons with conditions + once-flags). Doctrine check:
  dialogue *content* is app domain, but a data-driven dialogue *runner* is as generic as
  FlowMachine (visual novels, NPC talk, tutorials all reuse it). Engine: a `.cdialog` asset +
  `DialogueGraph`/`DialogueRunner` (GL-free, headless-testable like FlowMachine): nodes
  {Speaker, Text, PortraitPath, BackgroundPath, AudioPath, Options[{Text, Guard (reuse
  FlowGuard/variables), Once, → next}]}, plus emit-signal actions; the runner exposes "current
  node + valid options" and the app (or the U1 UI entities via a stock binding script) renders
  it. Editor: a dialogue document editor on the §9.1 canvas with 2218's edit-node side panel.
  *Alternative if scope must shrink:* extend `.cflow` states with an optional dialogue payload —
  but separate assets keep both formats simple; recommend separate.
- **9.4 Post-FX chain: data-driven stack + graph view (engine + editor) — M/L:** 2214's
  Camera→Vignette→Bloom graph. Rewriting `PostProcessStack` into an arbitrary pass-graph
  executor is NOT recommended (the fixed HDR chain is a documented, conformance-tested
  contract). Pragmatic parity in two steps: **(a)** add the missing cheap passes so the node
  set matches — **Vignette** (new `Vignette.glsl` folded into the tonemap like fog, params
  amount/radius/feather/color) and optional **chromatic aberration**; surface on
  `EnvironmentComponent` + `SceneRendererSettings` (default off). **(b)** editor "Post Chain"
  document view: render the *fixed* pipeline as a node graph on the §9.1 canvas (Scene → SSAO →
  Bloom → GodRays → Tonemap(fog/vignette/haze) → FXAA), each node showing its enable + params
  and editing the same reflected `EnvironmentComponent` fields (undo for free). Users get the
  2214 mental model + live tuning; the engine keeps its verified frame shape. A true
  arbitrary-graph compositor stays out of scope until a real need appears (record as such if
  this ever migrates to FEATURE-MATRIX).

---

## 10. NavMesh & agents — new phase-sized scope

**Target (2215):** author-time navmesh from scene geometry with Recast-grade knobs, tile cache,
"from children" source filtering, auto-regenerate, always-render-helper debug draw, and (implied)
runtime agents. **Today:** nothing; FEATURE-MATRIX row says *"Navmesh / AI pathfinding — ✖ —
first AI-driven project (plan a phase then)"*. The images make it explicit user intent, so this
document scopes it (the matrix row should flip to "planned — see this doc §10" whenever roadmap
integration happens):

- **10.1 Vendor Recast/Detour (engine) — M:** the Jolt pattern exactly (doc 14 J1): vendored
  PRIVATE-static into `Cosmic.dll`, pimpl service `nav/NavWorld.h` (no Recast types in public
  headers), zlib license fine.
- **10.2 Bake pipeline (engine) — L:** input geometry = the *physics* view of the scene (the
  honest source: collider shapes via `ScenePhysics`'s bake enumeration, terrain heightfield
  samples, voxel chunk meshes), filtered "from children" of the navmesh entity like 2215.
  `NavMeshComponent` (reflected recipe: cell size/height, agent radius/height/max climb/max
  slope, region min/merge sizes, max edge len/error, tile size, auto-generate bool) + built
  data serialized to a `.cnav` sidecar (the `.cvox` pattern — big binary out of scene JSON).
  Build runs on `JobSystem` (the WorldSystemsPanel one-shot async precedent). Debug draw =
  translucent poly soup via `Renderer3D` lines/tris, gated by an "always render helper" bool +
  editor toggle.
- **10.3 Agents + queries (engine) — M:** `NavAgentComponent { Radius, Height, MaxSpeed,
  MaxAccel, Target }` stepped by DetourCrowd inside the play-session tick (the physics
  tick-order contract gains one line); script proxy `Nav()` → `FindPath`, `SetTarget`,
  `RandomPointAround`, raycast. Headless tests: bake a known greybox, assert path lengths.
- **10.4 Editor authoring (editor) — S:** the E1 dividend — the reflected recipe auto-UIs;
  add a "Regenerate now" button row (the 2215 Trigger) in the Inspector for `NavMeshComponent`
  (the `Fit to mesh` per-component-button precedent in
  [InspectorPanel.cpp:200](../../Projects/Starforge/src/panels/InspectorPanel.cpp)), plus
  Entity ▸ World ▸ Nav Mesh menu item and the debug-draw toggle on the §2.1 strip.

---

## 11. Particle authoring parity

**Today:** `ParticleEmitterComponent`'s reflected recipe (shape/cone/box, spawn, life, gravity,
drag, wind, flipbook, soft-fade, stretch — [Components.h:545](../../Cosmic/src/scene/Components.h))
already matches most of 2216's Inspector; `.cemitter` presets save/load; WorldSystemsPanel edits
it. Doc 18 **R7** owns indirect-draw/sorting. Gaps:

- **11.1 Curl-noise turbulence module (engine) — M:** 2216's Curl Noise (enable, strength,
  frequency, octaves). Add fields to the recipe + implement in the spawn/step compute shader
  AND the unit-tested `StepCpu` mirror (the S10 invariant: CPU fallback matches GPU). Curl of a
  3D value-noise field (`math/Noise` has the primitives; port a `Curl3D` helper into the shader
  via a shared GLSL include or duplicated constants — keep CPU/GPU seeds identical).
- **11.2 Live noise-preview thumbnail (editor) — S:** 2216 shows the noise field as a small
  image. CPU-render a 128² slice of the same curl field (magnitude → color LUT) into a
  `Texture2D` whenever noise params change (debounced); draw next to the fields. Pure editor.
- **11.3 Local-space bounds (engine, tiny) — S:** 2216's "Bounding Box Settings" (emitter
  volume clamp). Add optional `BoundsExtents` (kill/wrap particles beyond it) — useful for
  vignette-style ambient effects; default off.
- **11.4 Dedicated particle editor document — defer:** the recipe + §14.3 preview inside the
  Inspector/WorldSystems panel is enough until emitters grow modules; if/when promoted, it's a
  §8.1 document with a §8.2 timeline scrub.

---

## 12. 2D game parity (make `IMG_2217` buildable)

Phase 17's remainder already owns most of this — **U3** (editor 2D mode: ortho, pixel-perfect,
sorting), **U4** (Tilemap + painter), **U7** (game view/eject), **U8** (samples). Cross-reference
those as the spec; the image exposes three genuine engine gaps beyond them:

- **12.1 2D lighting (engine) — M/L:** campfire radius + night darkness. `Renderer2D` has no
  light model. Minimal generic version: a screen-space light pass for the 2D path — scene
  renders lit=1.0, then a light buffer (additive radial/sprite lights from a new
  `Light2DComponent { Color, Radius, Intensity, Falloff }`) multiplies the composite, with a
  global `Ambient2D` color on the environment (night = dark blue ambient). Implementation:
  render light quads into a half-res R11G11B10F target, multiply in the existing 2D composite
  (`Renderer2D` flush or the PostFx path when active). Normal-mapped 2D lights are explicitly
  out of scope v1.
- **12.2 World-anchored UI labels (engine) — S/M:** the "Frouty" nameplate. `UiSystem` is
  screen-space canvas only ([UiComponents.h](../../Cosmic/src/scene/ui/UiComponents.h)). Add a
  `UiWorldAnchorComponent { TargetEntity(UUID) or WorldOffset; }` on a rect: before layout,
  project the anchor's world position through the active camera into canvas space and treat it
  as the rect's origin (one hook in `UiSystem::ResolveRect`'s canvas pass). Works for 3D too
  (health bars over units) — generic.
- **12.3 Minimap / render-to-texture verb (engine) — M:** fog-of-war minimap is app logic, but
  it needs a generic verb: `SceneRenderer::RenderToTexture(scene, cameraDesc, target)` (or a
  `CameraComponent::TargetTexture` path) so an app can render an ortho top-down view into a
  texture and draw it in UI (`UiImageComponent` already takes a texture path — add a runtime
  `Ref<Texture2D>` slot). The FBO plumbing exists (`FrameBuffer`, offscreen passes everywhere);
  this is surfacing it as a stable public verb. Fog-of-war itself: app-side (a mask texture the
  game updates — the engine ships nothing game-shaped).

---

## 13. Console & the utility dock

- **13.1 Console v2 (editor) — S:** today's panel already has timestamps, severity filters,
  auto-scroll, copy ([ConsolePanel.cpp](../../Projects/Starforge/src/panels/ConsolePanel.cpp)).
  Add: a text search filter, per-source chips once more sinks exist (Engine/Editor/Game —
  `ConsoleLine` would need a Source enum plumbed where lines are pushed), monospace font for
  alignment, and a max-line ring (guard long sessions).
- **13.2 Profiler panel (editor port) — S/M:** 2211 has a Profiler tab; the engine already has
  GPU timer-query zones (F3) and Frontier ships `GpuProfilerPanel` — **port it into Starforge**
  as a dockable panel (per-pass GPU ms from `SceneRenderer` zones + CPU frame breakdown), View-menu
  toggle. Mostly a copy-adapt of the Frontier panel.
- **13.3 Jobs / Resources tabs (engine introspection + editor) — M:** 2211's Jobs and
  Resources/Cache tabs. Engine additions (small, read-only): `JobSystem` stats accessors
  (queued/active/completed counts, worker count) and `AssetLibrary::Enumerate(visitor)` +
  byte-size accounting (§14.2). Editor: two small panels (or one "System" panel with tabs)
  listing live jobs and loaded assets (path, type, refcount, CPU/GPU bytes, reload button —
  reload exists per-path today).
- **13.4 Asset Store / Code tabs — explicitly out of scope.** Starforge's doctrine is external
  IDE for code (decision #5, C++-first) and no marketplace; note them so nobody mistakes the
  omission for an oversight.

---

## 14. Cross-cutting engine services (build once, many clusters consume)

1. **Reflection metadata v2** — `Reflect::FieldDescriptor` gains optional `Doc` (string),
   `Min/Max/Step` (floats), `Units`/`Degrees` flags; `REFLECT_*` macros get overloads to attach
   them (defaulted — zero churn for existing registrations). Consumers: §5.2 tooltips/sliders,
   §9 graph property panes, doc-gen later. **M** including back-filling metadata on the big
   components.
2. **Asset accounting & enumeration** — `AssetLibrary`: enumerate loaded assets with type +
   CPU/GPU byte estimates (`Texture2D` W×H×bpp(+mips), `Mesh` VB/IB bytes — add
   `GetVertexCount/GetIndexCount/GetGpuBytes` accessors where missing); `Sound::CopyPcm` for
   waveform preview. Consumers: §1.4 status bar, §4.4 metadata, §13.3 Resources. **S/M**
3. **Offscreen preview rig as a shared service** — doc 19 **A4** already specs the rig (tiny
   FBO + SceneRenderer-lite: one mesh, key light, IBL) for material preview + browser thumbs
   with a `.starforge/thumbs/` cache. Promote its design to a Starforge `PreviewRig` service
   with two modes: batch-thumbnail (A4 as written) and *interactive* (per-§8.1-document
   viewport with orbit). The GL-state-restore contract A4 states (screenshot-identical scene
   after a thumbnail pass) is the acceptance for both. Consumers: §4.1/§4.4, §5.3, §8, §11.4.
   **A4's M grows to M/L; still one work order.**
4. **Per-entity/per-component enable semantics** — the `TagComponent::Active` flag (§6.2) and
   the per-component `Enabled` convention (§5.4), plus the gate points listed there. One
   engine work order so every consumer lands consistently, headless-tested (an inactive
   entity: not rendered, not ticked, not baked, children inherit). **M**
5. **OS file-drop events** — `WindowFileDropEvent` + Win32 plumbing (§4.5). **S**
6. **Reusable editor widgets** — `NodeCanvas` (§9, vendored via U6), `Timeline` (§8.2, reused
   by C6/§11), `AssetSlot` (§5.3), the §4.2 asset-type table. Editor-side, but design them as
   widgets from day one — the references' "feel" is largely these four widgets being
   everywhere.

---

## 15. Sizing & sequencing summary (NOT a roadmap — a shopping list with prices)

**Quick wins (S, high feel-per-hour):** 1.3 undo buttons · 1.4 status bar · 2.3 axis navigator ·
2.4 stats chips · 3.1 universal gizmo · 4.2 type table/badges · 4.3 rename · 4.5 import button ·
5.1 property search · 5.6 live-play tint · 6.1 hierarchy icons · 7.1 sun-angle widget ·
7.3/7.4 ambient/gamma/sun-size · 10.4 (once 10.1–10.3 exist) · 11.2 noise preview ·
13.1 console search · 13.2 profiler port · 14.5 file drop. Plus already-planned small items:
**R8 view modes** (doc 18), **U7 game view/eject** (doc 16).

**Medium (M):** 1.1 toolbar rework · 1.2 layout presets · 2.1 viewport strip + per-op snap ·
2.2 camera rig · 2.6 grid shader · 3.2 selection outline · 3.3 viewport drag-drop · 4.1 browser
two-pane · 4.4 preview+metadata panel · 5.2 tooltips (w/ 14.1) · 5.3 asset slots · 5.4 component
QoL (w/ 14.4) · 6.2 active toggle (w/ 14.4) · 7.2 physical sky (M/L) · 8.3 sockets · 9.2
blackboard · 9.4 post chain (a)+(b) · 11.1 curl noise · 11.3 bounds · 12.1 2D lights (M/L) ·
12.2 world-anchored UI · 12.3 render-to-texture verb · 13.3 jobs/resources · 14.1/14.2/14.3
services · 10.1/10.3 recast vendor + agents.

**Large (L/XL, sequence deliberately):** 5.5 material slots (engine-architectural) ·
8.1 asset-editor host → 8.2 anim editor (after **doc 19 A2**, which stays the runtime
anchor — and A2 itself is XL-adjacent) · 9.3 dialogue graph · 10.2 navmesh bake pipeline ·
plus the already-planned **U3/U4/U6/U8** (doc 16) and **A1/A4** (doc 19) this document leans on.

**Suggested dependency spine** (if/when this becomes phases): U6 node canvas and 14.3 preview
rig unlock the most downstream items; A1/A2 unlock all of §8; 14.1/14.4 unlock the Inspector/
Hierarchy feel items; §10 is self-contained; §12 rides Phase 17's remainder.

**Deliberately out of scope (named so silence isn't ambiguity):** arbitrary post-FX graph
executor (§9.4), anim state machines (§8.4, parked per FEATURE-MATRIX), Asset Store/in-editor
code editing (§13.4), cloud sync/quota (2211's quota bar — the local-disk model stands,
FEATURE-MATRIX ✖ row), platform tabs beyond Windows (matrix ✖ row).

> **Integration note (resolved 2026-07-11, same day):** the user green-lit planning. Done as
> written: FEATURE-MATRIX gained rows for every new scope (navmesh's ✖ flipped to Phase 26;
> story graphs, flow variables, 2D lights, world-anchored UI, RTT verb, material slots,
> sockets, branding, chrome/viewport/browser/inspector clusters, physical sky, vignette, curl
> noise, the Forge Isle capstone); the master roadmap is v4 with Phases 22–28 (docs 21–27);
> fired unlocks annotated in docs 16 (U6/U8), 18 (R8), 19 (A2/A4). Items already homed (A1,
> A2, A4, U3–U8, R7, R8, C6) stayed where they live — this document's sections are their feel
> addenda, and the spec of record for the new K/T/M/Q/N/X/Z work orders.
