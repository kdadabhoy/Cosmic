# Phase 22 Plan — Editor Shell, Viewport & Branding

> **STATUS 2026-07-11 — PHASE CODE-COMPLETE (UNcommitted).** Doc 18 **R8** (prerequisite) +
> **K1–K13 all ✅** — per-item details in each status line below. Build green Debug + Release,
> zero warnings; `CosmicTests` **276/276** both configs (272→276: +4 K1 branding/ImageIO);
> GL-conformance clean; compat gate held (every engine addition is default-off or call-only:
> `SetPolygonMode`, `SceneRendererSettings::Wireframe`/`Outline*`, `DrawInfiniteGrid`,
> `Window::SetIcon`, `SetBottomInsetPixels`, the filtered `RenderIdPass` overload — shipped
> apps execute none of them unless they opt in; the one always-on change is the boot icon
> resolution, which is a no-op when no `branding/icon.png` exists). Deviations (all dated in
> their status lines): K3's layout "tabs" shipped as one compact dropdown (width budget);
> K2's Eject slot is a LIVE toggle (U7 had landed same-day); K5 needed the tiny generic
> `WorkspaceLayer::SetBottomInsetPixels` verb beyond its editor-only file list ("never
> overlaps panels" is impossible from the app side alone). REMAINING = the user's on-GPU
> acceptance pass (list in the session summary; includes the K1 live icon-swap clip the
> roadmap's Done-when requires) + commit.
>
> **Created 2026-07-11.** First of the editor-vision phases adopted from
> [`../design/example-images-gap-analysis.md`](../design/example-images-gap-analysis.md) (the
> spec of record — each item cites its §; re-read it before starting an item). This phase makes
> Starforge *feel* like the reference editors: product-grade chrome, the viewport as a
> self-contained instrument, and the user's branding requirement (drop a PNG → icon + top-bar
> logo update everywhere, zero code edits).
>
> **Naming rule (decision 2026-07-11):** features adapted from the reference screenshots take
> **Starforge** names — no borrowed branding (no "Ignite", no "StoryFlow") in code, UI, or docs.
>
> **Depends on:** nothing hard. K6's view-mode dropdown consumes doc 18 **R8** (wireframe/ID
> verbs — run R8 first; it stays doc 18's work order). K12 touches `PostProcessStack` (read
> `docs/design/frame-lifecycle.md` first). Items are otherwise independent and PR-sized.

---

## 0. Execution notes

Roadmap build recipe; doc 13 §0 engine rules (RendererAPI verbs, `BindingPoints.h`, no
editor-branded names in the engine); compat gate (shipped apps byte-identical unless they opt
in); state-restore contract (doc 13 §0.5) for anything that renders offscreen. Editor-only items
live in `Projects/Starforge`; engine verbs stay generic. Lucide glyphs come from the merged font
(`Cosmic/src/ui/IconsLucide.h`). No git writes — the user commits.

## 1. Work orders

### K1 — Runtime branding: drop-a-file icon + top-bar logo *(user request 2026-07-11; gap §1.1/§7.5 adjacent)*
**Files:** engine `core/Window.h/.cpp` (`SetIcon(const std::string& pngPath)` →
`glfwSetWindowIcon` with 16/32/48/256 px images decoded/downscaled via `utils/ImageIO` — GLFW
window-class icons default to the GLFW logo in dev builds, which is the "some default" the user
sees), `layers/PlayerLayer.cpp` (boot: resolve + apply the app icon), Starforge
`StarforgeApp.cpp` (editor brand + top-bar logo + FileWatcher hot-swap), `Packager` untouched
(exe-embed stays the Explorer/pin icon source).
**Spec:** one convention, three consumers. **(a) Resolution order** (first hit wins):
`branding/icon.png` next to the exe → `user://<app>/branding/icon.png` override → project
`manifest.Icon` / `project://icon.png` (the existing S5 keys) → engine default. Any common
format `ImageIO` reads (PNG/JPG/BMP/TGA) is accepted. **(b) Apply at boot** in every host
(Starforge, Launcher, PlayerLayer/packaged apps): `Window::SetIcon` sets the live window +
taskbar icon. **(c) Hot-swap:** Starforge watches the resolved file (`utils/FileWatcher`, the
Content-Browser pattern) — replacing the PNG on disk re-applies the icon and re-uploads the
top-bar texture within a second, **no code, no restart**. **(d) Top-bar logo:** Starforge draws
the same image (as a `Texture2D`) at the left end of its menu bar (`DrawTopBar`,
[StarforgeApp.cpp:1024](../../Projects/Starforge/src/StarforgeApp.cpp)) with a fixed row-height
fit + tooltip (app name/version); reuse in the homescreen header and About box. Ship a default
`branding/icon.png` for Starforge itself (the molten-orange mark) so the mechanism is visibly
live out of the box.
**Acceptance:** replace `branding/icon.png` while Starforge runs → taskbar icon + window icon +
top-bar logo all update without restart; a packaged app shows its project icon in the taskbar at
runtime (not just on the exe file); deleting the override falls back cleanly; headless test for
the resolution order. **Status:** ✅ 2026-07-11 — engine `utils/Branding` (documented candidate
order, pure + headless-tested in `tests/test_branding.cpp`), `ImageIO::ReadPixels`/`ResizeRgba`
(flip-safe decode + box/bilinear resample), `Window::SetIcon` (16/32/48/256 via
`glfwSetWindowIcon`; decode failure KEEPS the current icon so hot-swap races never blank the
brand) + `ClearIcon`; applied at boot in `Application::Initialize` (covers Launcher + every
host) and manifest-aware in `PlayerLayer::OnAttach` (`icon` key + `project://icon.png`);
Starforge `ApplyBrand()` = resolve → icon + top-bar `Texture2D` + FileWatcher on the resolved
folder (0.35 s debounce, one retry on partial writes), logo drawn in the menu bar, homescreen
header, and About box; default molten-orange `Projects/Starforge/branding/icon.png` staged next
to the exe by CMake. Live-swap clip = user's on-GPU acceptance (listed at phase end).

### K2 — Product toolbar: icons + centered transport *(gap §1.1)*
**Files:** Starforge `StarforgeApp.cpp` (`DrawTopBar`/`DrawPlayControls`/`DrawBuildControls`).
**Spec:** three measured groups — left: K1 logo + file/tool icon buttons; **center: Play ·
Pause · Step · Stop as Lucide icon buttons** (compute width, `SetCursorPosX((avail-w)*0.5f)`),
plus a reserved disabled Eject slot until doc 16 U7 lands; right: Run App / Package / layout
tabs (K3). Build status becomes a colored dot + tooltip; all buttons get tooltips with shortcut
hints. **Acceptance:** transport visually centered at multiple window widths; every control
reachable as before; play-state coloring preserved. **Status:** ✅ 2026-07-11 — `DrawTopBar`
rebuilt as three measured groups (left: K1 logo + Save/Build-hammer + status dot + Auto-zap +
2D toggle; center: Flow + Play·Pause·Step·Stop·Eject as FIXED square Lucide icon slots, width
measured and truly centered with an overlap clamp; right: aspect/capture while playing +
Run-App rocket + Package). U7 had landed same-day, so the "reserved disabled Eject slot" is a
LIVE eject toggle (enabled while playing) at the same fixed position — no reflow either way.
Play-state coloring = green Play glow / amber Pause + the existing viewport border. NOTE: the
gizmo/snap strip stays in the bar's left group until K6 relocates it (its own work order), so
exact centering at narrow widths arrives with K6; the clamp guarantees no overlap meanwhile.

### K3 — Workspace layout presets *(gap §1.2)*
**Files:** NEW Starforge `LayoutPresets.h/.cpp`; `StarforgeApp.cpp` (top-bar right tabs, View
menu); `EditorPrefs.h`.
**Spec:** snapshot/restore via `ImGui::SaveIniSettingsToMemory` / `LoadIniSettingsFromMemory` +
the panel-visibility bools, stored `user://starforge/layouts/<name>.ini`. Built-ins: **Level ·
Assets · Telemetry** (later phases add Animation/Graphs on their first document editors);
"Save layout as…" for user presets; active preset persists per project. Panels keep stable
window names (the ini keys). **Acceptance:** switch presets live without dangling panels;
custom preset survives restart; Reset Layout still works. **Status:** ✅ 2026-07-11 — NEW
`LayoutPresets.h/.cpp`: built-ins (Level · Assets · Telemetry) are CODE-DEFINED dock layouts
(vis set + dock-port bindings + edge ratios → DockBuilder; DPI-safe, no captured pixel sizes);
user presets are `SaveIniSettingsToMemory` snapshots + a `# panels:` visibility header at
`user://starforge/layouts/<name>.ini`, restored via `LoadIniSettingsFromMemory` deferred to the
top of the next frame (vendored ImGui 1.92.8 ApplyAll re-docks live; the `###Viewport` idiom
keeps the viewport's ini key stable across scene renames). Picker = top-bar dropdown at the
bar's right end (the doc's "tab strip", folded into one control for width budget — deviation
noted) + a View ▸ Layout menu mirror; right-click deletes a user preset; "Save layout as…"
modal validates filename-safe names. Active preset persists PER PROJECT in
`layouts/active.toml`, restored by `MountProject`. Reset Layout re-applies the active preset.

### K4 — Undo/redo toolbar UI + history popup *(gap §1.3)*
**Files:** engine `core/CommandStack.h` (add `UndoCount()/RedoCount()/NameAt(i)` accessors if
missing — read-only, tiny); Starforge `StarforgeApp.cpp`.
**Spec:** ⟲/⟳ icon buttons with count badges next to the transport; hover shows the last ~10
command names; click-to-undo-N via the popup list. **Acceptance:** counts match Ctrl+Z/Y
behavior exactly; multi-undo via popup lands on the right state. **Status:** ✅ 2026-07-11 —
`CommandStack` gained read-only `UndoNameAt(i)`/`RedoNameAt(i)` (`UndoCount/RedoCount` already
existed); Starforge draws ⟲/⟳ squares left of the transport (inside the measured center group,
so centering holds), count badges on the corner, hover tooltip = the last ≤10 names,
right-click popup = "Undo N steps to 'X'" rows that loop the same `Undo()/Redo()` calls
Ctrl+Z/Y drive (state-identical by construction).

### K5 — Status bar *(gap §1.4)*
**Files:** Starforge `StarforgeApp.cpp` (a `NoDecoration` strip pinned under the dockspace —
mirror the `m_TopBarBottomY` anchoring downward).
**Spec:** FPS/frame-ms, entity + selected counts, build-module state, play state; asset-memory
chip appears once Phase 23 T2 lands (leave the slot). **Acceptance:** visible in every layout,
never overlaps panels, hides on the homescreen. **Status:** ✅ 2026-07-11 — "never overlaps
panels" needed one tiny GENERIC engine verb beyond the doc's editor-only file list:
`WorkspaceLayer::SetBottomInsetPixels(px)` shrinks the dock-host window by a bottom band
(default 0 = byte-identical for every other app; Starforge releases it on detach + homescreen).
The strip itself is a `NoDecoration` window pinned in that band: play state (colored) ·
FPS/ms · entity + selected counts · build-module chip (mirrors the K2 dot) · right-aligned
project/scene identity where T2's asset-memory chip will land. Visible in all three presets
(it lives outside the dock tree), hidden on the homescreen.

### K6 — Viewport header strip + per-operation snapping *(gap §2.1, §2.5; prereq doc 18 R8)*
**Files:** Starforge `ViewportController.h/.cpp` (`DrawToolbar` → overlay strip via
`WorkspaceLayer::BeginViewportOverlay`), `StarforgeApp.cpp` (drop the top-bar call).
**Spec:** translucent top-left strip inside the viewport: gizmo op icons (incl. K11's
Universal), World/Local, **three snap chips with independent values** (`m_SnapMove=0.25 m`,
`m_SnapRotate=15°`, `m_SnapScale=0.1` — `Gizmo::Manipulate` already takes per-call snap), grid/
colliders/physics toggles, the R8 view-mode dropdown (Lit · Unlit · Wireframe · Entity-ID), and
K7's camera dropdown. Hidden while playing (same rule as today). **Acceptance:** all toggles
work from the strip; snap values persist in `EditorPrefs`; strip never blocks gizmo hit-testing
(ImGuizmo rect etiquette per `graphics/Gizmo.h` frame protocol). **Status:** ✅ 2026-07-11 —
`DrawToolbar` deleted; NEW `ViewportController::DrawViewportOverlays` draws a translucent
auto-width child at the viewport top-left: four op icons (incl. K11 Universal, shortcut Q),
World/Local toggle, THREE independent snap chips (toggle + drag value; defaults 0.25 m / 15° /
0.1; persisted via new `EditorPrefs` snap keys, loaded on attach/saved on detach; 2D mode's
ArmPixelSnap arms the MOVE chip at 1.0), grid/collider/physics toggles, the R8 view-mode combo,
and the K7 camera dropdown + fly-speed chip. Hidden while playing. Etiquette: `DrawGizmo` runs
AFTER the strip and yields whenever any overlay item is hovered/active (never interrupting an
in-progress gizmo drag — ImGuizmo holds no ImGui ActiveId); the top bar's transport is now
exactly centered at every width (closing K2's note).

### K7 — Editor camera rig: orbit + fly + possess *(gap §2.2)*
**Files:** NEW Starforge `EditorCameraRig.h/.cpp` (owns `OrbitCameraController` +
`FlyCameraController`); `StarforgeApp.cpp`, `ViewportController.cpp` (consume the rig).
**Spec:** modes {Orbit (default CAD nav), Fly, Possess(camera entity)}. RMB-hold in the viewport
= temporary Fly (WASD+QE, scroll scales speed, speed shown on the K6 strip); Possess renders
from a `CameraComponent`'s pose read-only. Dropdown lists Free (Orbit) / Free (Fly) / every
camera entity by Tag. Bookmarks (Ctrl+1..9) keep working. **Acceptance:** seamless
orbit↔fly↔possess switches with no pose jumps; gizmo/picking still correct in every mode.
**Status:** ✅ 2026-07-11 — NEW `EditorCameraRig.h/.cpp` owning both engine controllers + a
possess pose camera. No-jump math is exact, not approximate: the two controllers' conventions
mirror (`fly(yaw,pitch) == (−orbitYaw, −orbitPitch)` for the same look direction), so
Orbit→Fly seeds fly at the orbit EYE and Fly→Orbit re-targets the pivot `distance` m along
the fly direction. RMB-press edge = temporary fly, release commits back. Possess resolves the
UUID each frame (vanished entity → clean Orbit fallback); scroll routes to fly speed while
flying, orbit zoom otherwise; W/E/R/Q gizmo hotkeys yield while flying (WASD is movement).
Picking/gizmo take `rig.ActiveCamera()` in every mode; F-frame and bookmarks recall through
the rig so they stay seamless mid-flight. `StarforgeApp` and `ViewportController` consume the
rig (the loose `m_Camera` orbit member is gone).

### K8 — Axis navigator adoption *(gap §2.3)*
**Files:** Starforge `ViewportController.cpp` (+ engine `camera/NavigationCube.h` reuse — verify
its input path works from the viewport overlay; adapt if it predates `BeginViewportOverlay`).
**Spec:** the engine's existing `NavigationCube` drawn bottom-left of the viewport;
face/edge/corner clicks call `OrbitCameraController::SnapView(...)`; delete the Front/Top/Iso
text buttons. **Acceptance:** clicking faces snaps as labeled at any viewport size/DPI; no
interference with gizmo or picking. **Status:** ✅ 2026-07-11 — the S5.3 cube pre-renders its
own FBO pass via `ViewportController::PrerenderNavCube` (called before the viewport FBO binds,
per its self-contained contract — it works fine from the overlay because the WIDGET is just an
`ImGui::Image` + `PickFace(u,v)`, exactly its designed usage), drawn bottom-left, clicks snap
via `rig.SnapView` (fly-seamless). Front/Top/Iso text buttons deleted with the old toolbar.
The image is an ImGui item, so the gizmo's overlay-etiquette gate (K6) and the click-pick's
hover checks already exclude it; hidden in Play/2D. Picking is DPI-safe (u,v are fractions of
the drawn image rect).

### K9 — Viewport stats chips *(gap §2.4)*
**Files:** Starforge `ViewportController.cpp` (bottom overlay row), View-menu toggle.
**Spec:** chips: resolution, `Renderer3D::GetStats()` draws/submitted/culled/instanced, camera
distance, frame ms. (Full per-pass GPU timings stay in Phase 23 T17's profiler panel.)
**Acceptance:** matches the Statistics window numbers; negligible cost (text only).
**Status:** ✅ 2026-07-11 — a single draw-list chip (rounded backdrop + one text line) at the
viewport bottom-right: resolution · draws/submitted/culled/instanced (the same
`Renderer3D::GetStats()` frame the Statistics window reads) · orbit distance · frame ms;
View ▸ "Viewport Stats Chips" toggle (default on). Cost = one AddRectFilled + one AddText.

### K10 — Infinite editor grid *(gap §2.6)*
**Files:** engine NEW `InfiniteGrid.glsl` + `Renderer3D::DrawInfiniteGrid(desc)` (one-quad
ray-plane fragment grid, distance fade, decade step switching); Starforge
`ViewportController::DrawOverlayContent` swaps `DrawGrid`.
**Spec:** default-off engine verb (compat); editor enables it. Keep `DrawGrid` untouched.
**Acceptance:** grid readable from 0.1 m to 5 km without shimmer; axis lines highlighted; GL
conformance script green. **Status:** ✅ 2026-07-11 — NEW `InfiniteGrid.glsl` (fullscreen
triangle; per-fragment ray-plane intersection, exported fragment depth so scene geometry
occludes it, decade cell switching cross-faded by `fract(log10)` so zoom never pops,
fwidth-antialiased ~1.2 px lines, X/Z axis highlighting, distance fade auto-scaled by camera
height so both a 0.1 m close-up and a multi-km overview read) + `Renderer3D::DrawInfiniteGrid
(InfiniteGridDesc)` — immediate draw, depth-test ON / depth-write OFF (restored), default
alpha blend; call-only verb = nothing changes for apps that never call it; `DrawGrid`
untouched. Editor `DrawOverlayContent` swaps to it (axis tripod kept for Y). Conformance
green; the no-shimmer readability check is on the user's on-GPU pass.

### K11 — Universal gizmo operation *(gap §3.1)*
**Files:** engine `graphics/Gizmo.h/.cpp` (`Operation::Universal` →
`ImGuizmo::TRANSLATE|ROTATE|SCALEU`).
**Spec:** fourth mode button on K6's strip (shortcut `Q`); universal uses the move snap
(ImGuizmo takes one snap per call — document it). **Acceptance:** drag translate/rotate/scale
handles of one gizmo; undo coalesces per drag exactly like single ops. **Status:** ✅
2026-07-11 — engine `Gizmo::Operation::Universal` → `ImGuizmo::UNIVERSAL`
(`TRANSLATE|ROTATE|SCALEU`; the move-snap limitation documented on the enum); fourth strip
button + `Q` hotkey (K6). Undo path is untouched — the same drag-start capture /
release-commit in `DrawGizmo` coalesces a universal drag into one `TransformEdit` exactly like
single ops.

### K12 — Selection outline pass *(gap §3.2)*
**Files:** engine `renderer/PostProcessStack` or `renderer/SceneRenderer` (NEW `Outline.glsl`,
mask FBO), `scene/ScenePicker.h/.cpp` (selection-filtered `RenderIdPass` overload),
`SceneRendererSettings{OutlineEnabled,Color,WidthPx}` + `SceneRenderDesc::SelectedEntities`;
Starforge feeds `EditorContext::Selection`.
**Spec:** render selected entities into a mask target, 4-tap edge detect, composite over LDR.
Default off (compat). Wire-box overlay remains for un-meshed selections (lights, colliders).
**Acceptance:** crisp 2 px outline on meshes incl. instanced/LOD paths; scene renders
byte-identical with the setting off; conformance script green; no state leak (doc 13 §0.5).
**Status:** ✅ 2026-07-11 — `ScenePicker::RenderIdPass` gained the selection-filter overload
(draws ONLY the listed entities: MeshRenderer + camera-distance LOD level + voxel chunks — the
id attachment becomes the mask; `GetIdTextureID()` exposes it); NEW `Outline.glsl` (fullscreen
4-tap + diagonal silhouette detect on the `isampler2D` mask — RED_INTEGER is NEAREST-filtered
by the FBO contract — growing OUTWARD so the surface stays untinted);
`SceneRendererSettings{OutlineEnabled,OutlineColor,OutlineWidthPx}` +
`SceneRenderDesc::SelectedEntities`, composited in `PassPostAndComposite` AFTER the tonemap and
BEFORE `DrawOverlay2D` (UI never outlined over), in its own "Outline" GPU zone; mask unit
registered as `Bindings::TexUnitOutlineMask = 13`. Default off + lazy resources = zero code
runs and zero allocations happen for apps that never enable it (byte-identical). State
restore: the id pass is self-contained, the composite re-binds the final FBO/viewport and
restores depth test (doc 13 §0.5). Instanced note: id-carrying draws never auto-batch (S12.3
contract), so per-entity masks are exact. Starforge feeds `EditorContext::Selection`, always
on in the SceneRenderer path; mesh wire boxes now draw only when the pass is off (Entity-ID
view) — lights/colliders keep their glyphs.

### K13 — Drag-and-drop into the viewport *(gap §3.3)*
**Files:** Starforge `ViewportController.cpp` (+ `commands/EditorCommands` reuse).
**Spec:** accept the existing `ASSET_PATH` payload over the viewport
(`BeginDragDropTargetCustom` on the viewport rect): meshes/prefabs spawn at
`ProbeWorldPoint` (fallback 10 m along the ray) via undoable commands; `.cmat` assigns
`MaterialPath` on the ID-picked entity under the cursor; images assign to a hit
`SpriteRenderer`. **Acceptance:** every drop is a single undo step; drops outside geometry
still spawn sensibly; no accidental drops while a gizmo drag is active. **Status:** ✅
2026-07-11 — `ViewportController::UpdateViewportDragDrop` accepts `ASSET_PATH` over the
viewport rect via `BeginDragDropTargetCustom`, refused while playing / mid-gizmo-drag. Drop
point = 2D-mode XY plane, else the ID-pass depth probe with the 10 m ray fallback. Routing:
`.cprefab` → `Prefabs::Instantiate` + position + NEW `Commands::RecordSpawn` (a Snapshot/
RestoreCommand of the placed subtree = ONE undo, redo lands at the drop point — also the
first UNDOABLE prefab instantiation); mesh files (.obj/.gltf/.glb/.fbx/.stl/.dae/.ply) →
one `Commands::Create` with `MeshPath` (the sync resolves); `.cmat` → NEW
`Commands::AssignMaterial` (path + resolved `MaterialAsset` move together so undo/redo are
visually exact — the plain reflected string write would leave the old asset live); images →
`SetField("TexturePath")` on the hit `SpriteRenderer` (2D mode prefers the topmost sprite
under the cursor; the sprite path lazily re-resolves by design). Misses log actionable
Console warnings instead of failing silently.

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/21-phase22-editor-shell-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md` (the spec of record — re-verify its file
> references against the tree; they drift). Engine changes = generic verbs only, default-off,
> conformance script green; editor work lives in `Projects/Starforge`; Starforge naming only.
> Roadmap cmake recipe; no git writes. Finish with Acceptance demonstrated + the status banner
> updated in this doc.
