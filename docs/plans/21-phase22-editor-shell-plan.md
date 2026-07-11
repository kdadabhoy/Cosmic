# Phase 22 Plan — Editor Shell, Viewport & Branding

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
the resolution order. **Status:** ☐

### K2 — Product toolbar: icons + centered transport *(gap §1.1)*
**Files:** Starforge `StarforgeApp.cpp` (`DrawTopBar`/`DrawPlayControls`/`DrawBuildControls`).
**Spec:** three measured groups — left: K1 logo + file/tool icon buttons; **center: Play ·
Pause · Step · Stop as Lucide icon buttons** (compute width, `SetCursorPosX((avail-w)*0.5f)`),
plus a reserved disabled Eject slot until doc 16 U7 lands; right: Run App / Package / layout
tabs (K3). Build status becomes a colored dot + tooltip; all buttons get tooltips with shortcut
hints. **Acceptance:** transport visually centered at multiple window widths; every control
reachable as before; play-state coloring preserved. **Status:** ☐

### K3 — Workspace layout presets *(gap §1.2)*
**Files:** NEW Starforge `LayoutPresets.h/.cpp`; `StarforgeApp.cpp` (top-bar right tabs, View
menu); `EditorPrefs.h`.
**Spec:** snapshot/restore via `ImGui::SaveIniSettingsToMemory` / `LoadIniSettingsFromMemory` +
the panel-visibility bools, stored `user://starforge/layouts/<name>.ini`. Built-ins: **Level ·
Assets · Telemetry** (later phases add Animation/Graphs on their first document editors);
"Save layout as…" for user presets; active preset persists per project. Panels keep stable
window names (the ini keys). **Acceptance:** switch presets live without dangling panels;
custom preset survives restart; Reset Layout still works. **Status:** ☐

### K4 — Undo/redo toolbar UI + history popup *(gap §1.3)*
**Files:** engine `core/CommandStack.h` (add `UndoCount()/RedoCount()/NameAt(i)` accessors if
missing — read-only, tiny); Starforge `StarforgeApp.cpp`.
**Spec:** ⟲/⟳ icon buttons with count badges next to the transport; hover shows the last ~10
command names; click-to-undo-N via the popup list. **Acceptance:** counts match Ctrl+Z/Y
behavior exactly; multi-undo via popup lands on the right state. **Status:** ☐

### K5 — Status bar *(gap §1.4)*
**Files:** Starforge `StarforgeApp.cpp` (a `NoDecoration` strip pinned under the dockspace —
mirror the `m_TopBarBottomY` anchoring downward).
**Spec:** FPS/frame-ms, entity + selected counts, build-module state, play state; asset-memory
chip appears once Phase 23 T2 lands (leave the slot). **Acceptance:** visible in every layout,
never overlaps panels, hides on the homescreen. **Status:** ☐

### K6 — Viewport header strip + per-operation snapping *(gap §2.1, §2.5; prereq doc 18 R8)*
**Files:** Starforge `ViewportController.h/.cpp` (`DrawToolbar` → overlay strip via
`WorkspaceLayer::BeginViewportOverlay`), `StarforgeApp.cpp` (drop the top-bar call).
**Spec:** translucent top-left strip inside the viewport: gizmo op icons (incl. K11's
Universal), World/Local, **three snap chips with independent values** (`m_SnapMove=0.25 m`,
`m_SnapRotate=15°`, `m_SnapScale=0.1` — `Gizmo::Manipulate` already takes per-call snap), grid/
colliders/physics toggles, the R8 view-mode dropdown (Lit · Unlit · Wireframe · Entity-ID), and
K7's camera dropdown. Hidden while playing (same rule as today). **Acceptance:** all toggles
work from the strip; snap values persist in `EditorPrefs`; strip never blocks gizmo hit-testing
(ImGuizmo rect etiquette per `graphics/Gizmo.h` frame protocol). **Status:** ☐

### K7 — Editor camera rig: orbit + fly + possess *(gap §2.2)*
**Files:** NEW Starforge `EditorCameraRig.h/.cpp` (owns `OrbitCameraController` +
`FlyCameraController`); `StarforgeApp.cpp`, `ViewportController.cpp` (consume the rig).
**Spec:** modes {Orbit (default CAD nav), Fly, Possess(camera entity)}. RMB-hold in the viewport
= temporary Fly (WASD+QE, scroll scales speed, speed shown on the K6 strip); Possess renders
from a `CameraComponent`'s pose read-only. Dropdown lists Free (Orbit) / Free (Fly) / every
camera entity by Tag. Bookmarks (Ctrl+1..9) keep working. **Acceptance:** seamless
orbit↔fly↔possess switches with no pose jumps; gizmo/picking still correct in every mode.
**Status:** ☐

### K8 — Axis navigator adoption *(gap §2.3)*
**Files:** Starforge `ViewportController.cpp` (+ engine `camera/NavigationCube.h` reuse — verify
its input path works from the viewport overlay; adapt if it predates `BeginViewportOverlay`).
**Spec:** the engine's existing `NavigationCube` drawn bottom-left of the viewport;
face/edge/corner clicks call `OrbitCameraController::SnapView(...)`; delete the Front/Top/Iso
text buttons. **Acceptance:** clicking faces snaps as labeled at any viewport size/DPI; no
interference with gizmo or picking. **Status:** ☐

### K9 — Viewport stats chips *(gap §2.4)*
**Files:** Starforge `ViewportController.cpp` (bottom overlay row), View-menu toggle.
**Spec:** chips: resolution, `Renderer3D::GetStats()` draws/submitted/culled/instanced, camera
distance, frame ms. (Full per-pass GPU timings stay in Phase 23 T17's profiler panel.)
**Acceptance:** matches the Statistics window numbers; negligible cost (text only).
**Status:** ☐

### K10 — Infinite editor grid *(gap §2.6)*
**Files:** engine NEW `InfiniteGrid.glsl` + `Renderer3D::DrawInfiniteGrid(desc)` (one-quad
ray-plane fragment grid, distance fade, decade step switching); Starforge
`ViewportController::DrawOverlayContent` swaps `DrawGrid`.
**Spec:** default-off engine verb (compat); editor enables it. Keep `DrawGrid` untouched.
**Acceptance:** grid readable from 0.1 m to 5 km without shimmer; axis lines highlighted; GL
conformance script green. **Status:** ☐

### K11 — Universal gizmo operation *(gap §3.1)*
**Files:** engine `graphics/Gizmo.h/.cpp` (`Operation::Universal` →
`ImGuizmo::TRANSLATE|ROTATE|SCALEU`).
**Spec:** fourth mode button on K6's strip (shortcut `Q`); universal uses the move snap
(ImGuizmo takes one snap per call — document it). **Acceptance:** drag translate/rotate/scale
handles of one gizmo; undo coalesces per drag exactly like single ops. **Status:** ☐

### K12 — Selection outline pass *(gap §3.2)*
**Files:** engine `renderer/PostProcessStack` or `renderer/SceneRenderer` (NEW `Outline.glsl`,
mask FBO), `scene/ScenePicker.h/.cpp` (selection-filtered `RenderIdPass` overload),
`SceneRendererSettings{OutlineEnabled,Color,WidthPx}` + `SceneRenderDesc::SelectedEntities`;
Starforge feeds `EditorContext::Selection`.
**Spec:** render selected entities into a mask target, 4-tap edge detect, composite over LDR.
Default off (compat). Wire-box overlay remains for un-meshed selections (lights, colliders).
**Acceptance:** crisp 2 px outline on meshes incl. instanced/LOD paths; scene renders
byte-identical with the setting off; conformance script green; no state leak (doc 13 §0.5).
**Status:** ☐

### K13 — Drag-and-drop into the viewport *(gap §3.3)*
**Files:** Starforge `ViewportController.cpp` (+ `commands/EditorCommands` reuse).
**Spec:** accept the existing `ASSET_PATH` payload over the viewport
(`BeginDragDropTargetCustom` on the viewport rect): meshes/prefabs spawn at
`ProbeWorldPoint` (fallback 10 m along the ray) via undoable commands; `.cmat` assigns
`MaterialPath` on the ID-picked entity under the cursor; images assign to a hit
`SpriteRenderer`. **Acceptance:** every drop is a single undo step; drops outside geometry
still spawn sensibly; no accidental drops while a gizmo drag is active. **Status:** ☐

## Kickoff prompt

> You are implementing ONE work order from `docs/plans/21-phase22-editor-shell-plan.md` in
> `C:\dev\Cosmic`. Read §0, your item, and its cited § in
> `docs/design/example-images-gap-analysis.md` (the spec of record — re-verify its file
> references against the tree; they drift). Engine changes = generic verbs only, default-off,
> conformance script green; editor work lives in `Projects/Starforge`; Starforge naming only.
> Roadmap cmake recipe; no git writes. Finish with Acceptance demonstrated + the status banner
> updated in this doc.
