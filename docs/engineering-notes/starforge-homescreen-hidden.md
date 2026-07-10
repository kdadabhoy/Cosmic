# Starforge homescreen (project library) invisible when no project is open

> **Verified against commit:** working tree of 2026-07-10 (Phase 18 branch `phase-7-3d-foundations`,
> uncommitted). `file:line` references to `Projects/Starforge/src/StarforgeApp.cpp` are live as of then.
> **Status:** FIXED 2026-07-10 — for real this time. The 2026-07-08 fix (anchor to the OS work area)
> repaired a *secondary* fragility but the homescreen stayed invisible; the primary cause was **z-order**,
> not size. Repro + fix verified live on-GPU 2026-07-10 (screenshots of both states; Voxel Sample →
> ForgeBlocks opens; Close Project → homescreen returns). Pre-existing since Phase 16 / S3 — the
> homescreen likely **never** rendered on this machine; nobody noticed until hunting for the Phase 18
> "Voxel Sample" button, because projects were being opened via the Welcome modal / Recent Projects menu.

## Symptom

After **File ▸ Close Project (Home)** (or first launch with no project), Starforge shows the top menu bar
plus the hint text *"No project open — use the homescreen."*, but the **homescreen itself does not appear** —
no `STARFORGE` header, no `New / Open… / Open Sample / Voxel Sample` button row, no project grid. The central
area is empty except for a thin docked **"Viewport"** strip at the top.

Reproduced on the user's machine (Windows 11) and re-reproduced 2026-07-10 on a build that *contained* the
2026-07-08 anchor fix — windowed and maximized. Consequence: the **"Voxel Sample"** button
(`BuildForgeBlocks` → the Phase 18 ForgeBlocks sample) was **unreachable**, because it lives only inside this
homescreen panel — there is no menu entry for it.

## Root cause (two stacked defects)

### 1. Primary — z-order: `NoBringToFrontOnFocus` pins the window to the BACK of the z-stack

`DrawHomescreen()` created `##StarforgeHome` with `ImGuiWindowFlags_NoBringToFrontOnFocus`. Dear ImGui
gives that flag hard back-of-stack semantics (vendored `imgui.cpp`):

- **On creation** (`CreateNewWindow`, imgui.cpp:6966): `g.Windows.push_front(window)` — the window is
  inserted at the *bottom* of the display stack, i.e. rendered first, behind everything.
- **Forever after** (`FocusWindow`, imgui.cpp:13725): the flag vetoes `BringWindowToDisplayFront`, so no
  click or appearance ever raises it.

`WorkspaceLayer` (pushed before the project layer) creates the fullscreen, opaque `##CosmicWorkspace`
dockspace host first; the homescreen is therefore *always* created after it and lands *behind* it. Every
pixel of the homescreen is painted under the dock host's background — "present in the ImGui tree but
visually absent". This is independent of position/size, which is why the 07-08 anchor fix changed nothing
visible. (The saved `imgui.ini` from the failing session showed `##StarforgeHome` at `Pos=0,165
Size=1280,555` — perfectly placed, fully hidden.)

### 2. Secondary — sizing coupled to the collapsible Viewport dock node (fixed 2026-07-08)

`DrawHomescreen()` originally sized the window to `GetViewportPos/Size()` — the editor Viewport dock
node's rect, which can collapse to a thin strip in saved layouts. Real fragility, fixed by anchoring to the
OS window work area below the top bar (`m_TopBarBottomY` recorded in `DrawTopBar`), but it was not the
reason the homescreen was invisible.

## Fix (applied 2026-07-10)

**1. Correct z-order flags** — `DrawHomescreen()` (StarforgeApp.cpp ~1860) now begins the window with:

```cpp
ImGui::Begin("##StarforgeHome", nullptr,
             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
```

- Dropped `NoBringToFrontOnFocus` (the bug) and `NoNavFocus` (cargo-culted from the dockspace-host
  pattern, where those flags belong). The window is now created *above* the dock tree and re-fronted by
  `FocusWindow` every time it re-appears (the Close-Project mid-session path). Modals ("New Project",
  "Welcome to Starforge"), menus, popups, and tooltips still stack above it by ImGui's separate stacks.
- Added `NoDocking` + `NoSavedSettings`: a product screen is fully code-positioned; no dock capture, no
  `imgui.ini` interference, ever.

**2. Hide the scene Viewport panel while no project is open** — the homescreen is a full-window product
page, not viewport content. Starforge now calls the engine's existing verb `WorkspaceLayer::
SetViewportVisible(false)` in `OnAttach` and `CloseProject`, and `SetViewportVisible(true)` in
`MountProject` (and restores `true` in `OnDetach` for the next app in-process). This removes the confusing
empty "Viewport" strip behind the homescreen and stops event-routing to a dead viewport.
`WorkspaceLayer::BuildDockspace` already handled the hidden case in both legacy and port mode
(WorkspaceLayer.cpp:401, :463) — the verb existed since H-era precisely for "the scene isn't used";
Starforge just never called it. Engine untouched; the whole fix is app-side.

The 07-08 anchor change (top bar → OS work-area) is kept — it is the right sizing model.

## Verification (performed live, 2026-07-10, Debug build)

- Boot `CosmicApp.exe --project Starforge` with no project: `STARFORGE` header + `New/Open…/Open Sample/
  Voxel Sample` row + project grid render fully, no Viewport strip. ✔ (was: empty dock area — screenshot
  taken on the pre-fix binary the same day)
- `Voxel Sample` scaffolds **ForgeBlocks** and opens it on first click: hierarchy (Environment/HUD/Sun/
  Voxel World/Player), voxel terrain renders in the restored Viewport panel. ✔
- **File ▸ Close Project (Home)** mid-session: homescreen returns, now listing the ForgeBlocks project
  card. This is the path the old flag made *permanently* invisible. ✔
- Regression watch: viewport overlay/gizmo/picking are `ProjectOpen`-gated and unaffected; `Exit to
  Launcher` path restores viewport visibility via `OnDetach`.

## Debugging lesson

The 07-08 session diagnosed by *reading* the sizing code and shipped a fix without an on-GPU repro of the
fixed binary. The size theory was plausible — and wrong as the primary cause. What settled it in minutes on
07-10: run the failing binary, screenshot (window present in ImGui, absent on screen ⇒ z-order, not
layout), then read the vendored `imgui.cpp` for the flag's real semantics instead of assuming them.
**"NoBringToFrontOnFocus" does not mean "don't steal focus"; it means "live at the bottom of the world".**
Reserve it for dockspace hosts and true background canvases.
