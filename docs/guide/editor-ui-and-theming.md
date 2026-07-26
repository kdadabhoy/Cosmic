# Editor UI & Theming — Guide

**What this covers:** the **ImGui** side of Cosmic — `ImGuiLayer` and what it has already done for
you before your first `ImGui::Begin`, the workspace docking model and port-mode
`DockWindow(name, DockPort::…)` (including the rule that you must **never store a dock-node id**),
`WorkspaceLayer::SetBottomInsetPixels` for status bands, drawing on top of the viewport image,
`ThemeManager` and the Theme Studio, the `Fonts` registry and Lucide icons, the `Widgets` kit,
`PlotStyle` for ImPlot, and the header-only `Overlay` image/text helpers.
**Source of truth:** `Cosmic/src/layers/ImGuiLayer.{h,cpp}`, `layers/ImGuiThemes.h`,
`layers/WorkspaceLayer.{h,cpp}`, `layers/LauncherLayer.cpp`, `ui/Theme.h`,
`ui/ThemeManager.{h,cpp}`, `ui/Fonts.{h,cpp}`, `ui/Widgets.{h,cpp}`, `ui/PlotStyle.{h,cpp}`,
`ui/Overlay.h`, `ui/IconsLucide.h`, `Cosmic.h`,
`Cosmic/templates/ExampleProject/src/TemplateThemeShowcaseLayer.cpp`,
`Projects/Starforge/src/{LayoutPresets,StarforgeApp}.cpp`, `Projects/SF_Telem/src/SF_Telem.cpp`
**API Reference:** [`../reference/ui.md`](../reference/ui.md) *(skeleton — D18)*
**How it works:** [`../systems/ui-theming.md`](../systems/ui-theming.md) *(skeleton — D34)*
**Configuration:** **both.** Every header here is unfenced and compiles in the 2D and 3D engine
builds ([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)).

> ### This is not the game's UI system
>
> Cosmic has **two** unrelated UI stacks, and mixing them up is the single most common orientation
> mistake in the codebase.
>
> | | **Editor chrome** — this chapter | **In-game UI** — [`game-ui.md`](game-ui.md) |
> | --- | --- | --- |
> | Built from | **ImGui** immediate-mode calls | scene **entities** + components |
> | Lives in | `ui/`, `layers/ImGuiLayer.*`, `layers/WorkspaceLayer.*` | `scene/ui/` |
> | Authored by | C++ in `OnImGuiRender` | the Inspector, the serializer, prefabs, undo, scripts |
> | Drawn through | ImGui's own draw lists, over the window | `Renderer2D`, into the game's render target |
> | Themed by `ThemeManager` | **yes** | no |
> | In a packaged build | present, but it looks like a tool | it *is* the game's HUD and menus |
>
> **If a developer sees it, it belongs here. If a player sees it, it belongs in
> [`game-ui.md`](game-ui.md).** A profiler window, a debug slider, an asset browser → ImGui. A pause
> menu, a health bar, a dialogue box → entities.

---

## Quick start

A docked panel is two calls: bind the window name to a port once, then draw a window with that
exact name every frame.

```cpp
#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"   // Application.h only FORWARD-DECLARES WorkspaceLayer

void MyProject::OnAttach()
{
    if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
    {
        ws->DockWindow("Controls", Cosmic::DockPort::LeftTop);
        ws->DockWindow("Log",      Cosmic::DockPort::BottomCenter);
    }
}

void MyProject::OnImGuiRender()
{
    ImGui::Begin("Controls");                     // name MUST match the binding
    Cosmic::UI::SectionHeader(ICON_LC_SLIDERS_HORIZONTAL, "Tuning");
    Cosmic::UI::ToggleSwitch("Live capture", &m_Live);
    if (Cosmic::UI::AccentButton(ICON_LC_PLUG "  Connect")) Connect();
    ImGui::End();

    ImGui::Begin("Log");
    ImGui::TextUnformatted(m_Log.c_str());
    ImGui::End();
}
```

`#include "layers/WorkspaceLayer.h"` is not optional — `Cosmic.h` does not pull it in, and
`Application.h:49` only forward-declares the class, so `GetWorkspaceLayer()` returns a pointer you
cannot dereference without it. Every shipped project has that include (`Engine3DDemo.cpp:6`,
`FrontierApp.cpp:14`, `SF_Telem.cpp:8`, `ViperSim.cpp:10`, `TemplateProject.cpp:8`).

---

## What `ImGuiLayer` already did

`ImGuiLayer` is an engine **overlay**, pushed by `Application::Initialize` before any project
loads. By the time your `OnImGuiRender` runs for the first time it has, in `OnAttach`:

- created the ImGui **and** ImPlot contexts;
- enabled **keyboard navigation**, **docking** and **multi-viewport** (`ImGuiLayer.cpp:44-46`) — the
  last one is why every ImGui rectangle is in desktop coordinates, and why a panel can be dragged
  out of the window into its own;
- pointed `io.IniFilename` at `FileSystem::Resolve("user://imgui.ini")`, held in a `static
  std::string` because ImGui borrows the pointer. ImGui's default would write into the working
  directory, which is read-only for an installed app; in a dev/portable tree this still resolves to
  `./imgui.ini`, i.e. unchanged behaviour;
- called `ThemeManager::Init()` and applied **"Sleek Pro"**;
- initialised the GLFW and OpenGL3 backends (`#version 410` GLSL for ImGui's own shaders);
- called `UI::Fonts::Init()` — which must happen before the first frame, because the OpenGL backend
  bakes the glyph atlas lazily on the first `NewFrame`.

Per frame, `Begin()` starts the ImGui frame **and** calls `ImGuizmo::BeginFrame()` so client code
can use `Cosmic::Gizmo` with no per-frame bookkeeping; `End()` renders, then drives
`UpdatePlatformWindows` / `RenderPlatformWindowsDefault` for the detached viewports and restores the
GL context.

You never call any of that. What you *do* sometimes call is:

```cpp
Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(false);
```

`BlockEvents(true)` (the default) makes `ImGuiLayer::OnEvent` mark mouse events `Handled` when
`io.WantCaptureMouse` and keyboard events `Handled` when `io.WantCaptureKeyboard`, so a click on a
slider does not also shoot your player. The workspace shell already toggles it every frame from the
viewport's hover/focus state (`WorkspaceLayer.cpp:210-211`) — see
[`windowing-and-viewport.md`](windowing-and-viewport.md#hover-and-focus-gates). Turn it off by hand
only for a captured-cursor first-person mode.

> **`InitializePluginContexts` is what makes any of this work in your DLL.** ImGui and ImPlot keep
> their state behind a per-module pointer, so a project DLL starts with a *null* context. The export
> the module macros generate copies the host's live pointers in before your layer is created. Skip
> it and the first `ImGui::Begin` in your DLL crashes. See
> [`project-anatomy.md`](project-anatomy.md#the-exports).

---

## Dock a panel into a port

The engine offers **13 fixed ports**: four optional edges × three sections, plus the centre.

| Port | Region |
| --- | --- |
| `DockPort::LeftTop` · `LeftMiddle` · `LeftBottom` | left column, stacked top → bottom |
| `DockPort::RightTop` · `RightMiddle` · `RightBottom` | right column, stacked top → bottom |
| `DockPort::TopLeft` · `TopCenter` · `TopRight` | top row, left → right |
| `DockPort::BottomLeft` · `BottomCenter` · `BottomRight` | bottom row, left → right |
| `DockPort::Center` | tabbed with the central Viewport (or in its place when it is hidden) |

Three rules follow from how `BuildDockspace` is written (`WorkspaceLayer.cpp:424-514`):

1. **Only ports that receive a window are carved out.** An unused edge takes *zero* space; there are
   no empty panels to close.
2. **Two windows on the same port become tabs**, in binding order.
3. **Left and right are full-height columns; top and bottom rows span only the band between them.**
   The builder splits left, then right, then top, then bottom, so a top bar never sits above the
   side panels.

`DockWindow` is idempotent by name — calling it again for a window that is already bound just
updates its port and flags. Every binding change queues a rebuild for the next frame.

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();

ws->ClearDockWindows();                 // drop the previous screen's layout
ws->SetEdgeRatios(0.18f, 0.20f, 0.16f, 0.20f);          // left, right, top, bottom
ws->DockWindow("Frontier",       Cosmic::DockPort::LeftTop);
ws->DockWindow("World Settings", Cosmic::DockPort::LeftBottom);
ws->DockWindow("GPU Profiler",   Cosmic::DockPort::RightBottom);
```

That is `FrontierApp.cpp:265-282` verbatim. SF_Telem rebuilds the whole layout on every screen
change (`SF_Telem.cpp:207-272`) and Starforge ships four named presets built the same way
(`LayoutPresets.cpp:82-142`) — re-binding per screen is the intended pattern, not an abuse.

### Sizing the edges

| Call | Units | Default |
| --- | --- | --- |
| `SetEdgeRatios(left, right, top, bottom)` | fraction of the dockspace | `0.20, 0.20, 0.18, 0.22` |
| `SetEdgeMinPixels(top, bottom, left, right)` | DPI-independent pixels, `0` = ratio only | all `0` |

**Mind the argument order — the two functions disagree.** `SetEdgeRatios` is
*left, right, top, bottom*; `SetEdgeMinPixels` is *top, bottom, left, right*. Nothing warns you.

The effective ratio is `max(ratio, minPx × viewport->DpiScale / axisSize)`, clamped to
`[0.05, 0.9]`. The pixel minimum exists so a docked menu-plus-toolbar row cannot clip under a small
ratio on a large monitor; Starforge reserves 78 px for its top bar
(`LayoutPresets.cpp:63`). The DPI multiply is the engine's, so pass an unscaled number.

### Chrome-less docks

```cpp
ws->DockWindow("Starforge", Cosmic::DockPort::TopCenter, Cosmic::DockFlags::NoTabBar);
```

`DockFlags::NoTabBar` strips the "▼ Name" tab header from the node the window docks into — for a
top toolbar or a full-bleed panel where the tab row is wasted vertical space. The flag is applied to
the *node*, so it affects every window sharing that port.

### The escape hatch, and the legacy path

`RequestExtraDockedPanel({ "Timeline", ImGuiDir_Down, 0.25f })` splits an arbitrary node off the
central area for a position the fixed ports do not cover. It is applied *after* the ports, and
successive requests chain off each other.

If a project registers **zero** `DockWindow` bindings, `BuildDockspace` takes a **legacy** branch
instead: a fixed 22 %-wide left column split three ways, docking the magic names
`"Project Inspector Top"`, `"Project Inspector Mid"` and `"Project Inspector Bottom"`, with
`"Viewport"` in the centre (`WorkspaceLayer.cpp:392-422`). Bare `"Project Inspector"` is **not** one
of them — that is the shell's own placeholder window, drawn when no project is mounted.

The legacy path is not dead: **`PlayerLayer` registers no bindings**, so every packaged,
player-driven app builds that layout. The template project keeps the historical *names* but binds
them explicitly (`TemplateProject.cpp:96-104`), so it runs in port mode like everything else. For
new code, always bind — the magic names exist for compatibility only.

---

## Never store a dock-node id

**Do not capture an `ImGuiID` from the dock builder and reuse it on a later frame.** This is the one
hard rule of the docking model.

`BuildDockspace` begins with `DockBuilderRemoveNode(dockspaceId)` followed by `DockBuilderAddNode`
(`WorkspaceLayer.cpp:383-385`), so **every node id from the previous build is invalidated**. A
rebuild is queued by far more things than you would guess:

- any `DockWindow` / `ClearDockWindows` call,
- `SetEdgeRatios`, `SetEdgeMinPixels`, `SetViewportVisible`, `RequestExtraDockedPanel`,
- `ResetLayout()`, the engine **View ▸ Reset Layout** menu item,
- and the full teardown handshake used when returning to the Launcher, which calls
  `DockBuilderRemoveNode` on its own (`:124-131`).

A stored id therefore refers to a node that no longer exists, and ImGui will happily dock a window
into nothing. **Bind by window name and let the shell own the ids.** If you need a window somewhere
the ports do not reach, use `RequestExtraDockedPanel` — it is re-applied on every rebuild, which is
exactly what a captured id is not.

The one place the engine reads a node back is internal and same-frame: applying
`DockFlags::NoTabBar` through `DockBuilderGetNode` inside the same build pass (`:493-501`).

---

## Reserve a band for a status bar

`SetBottomInsetPixels(px)` shrinks the **dock host** by `px` at the bottom of the OS window, so
docked panels can never underlap whatever you draw in the freed band. The engine reserves the space;
**you own the drawing.**

```cpp
void MyApp::DrawStatusBar()
{
    auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
    if (!ws) return;

    const float h = ImGui::GetFrameHeight() + 2.0f;   // font-derived => DPI-safe
    ws->SetBottomInsetPixels(h);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::Begin("##Status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::Text("%.0f FPS", ImGui::GetIO().Framerate);
    ImGui::End();
}
```

That is the shape of `StarforgeApp::DrawStatusBar` (`:2397-2425`). Note the two details that make it
behave: the height is derived from the **font**, so it tracks the UI scale for free, and it is
released with `SetBottomInsetPixels(0.0f)` on any screen that has no strip. `0` is the default and
gives the historical full-height host, byte-identical for every app that never calls it. Negative
values are clamped to `0`.

Unlike the edge and viewport setters, `SetBottomInsetPixels` does **not** queue a dock rebuild — the
host window is simply drawn shorter next frame, so it is safe to call every frame.

---

## Draw on top of the viewport image

ImGui lets you append to a window by re-`Begin`ing it in the same frame. `BeginViewportOverlay()`
wraps that so client code never hard-codes the viewport window's identity, and so `Cosmic::Gizmo`
gets the host window its hover logic requires.

```cpp
void MyProject::OnImGuiRender()
{
    auto& app = Cosmic::Application::Get();
    auto* ws  = app.GetWorkspaceLayer();
    if (!ws) return;

    const glm::vec2 vpPos  = app.GetViewportPos();
    const glm::vec2 vpSize = app.GetViewportSize();

    if (ws->BeginViewportOverlay())
    {
        // The current window IS the Viewport; the draw list is clipped to it.
        Cosmic::Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);
        // ... Gizmo::Manipulate, ImGui::SetCursorScreenPos + ImGui::Image, widgets ...
    }
    ws->EndViewportOverlay();   // ALWAYS pair, even when Begin returned false
}
```

**Always pair the calls** — `EndViewportOverlay` is a no-op when nothing was pushed, so the
unconditional call above is correct and the guarded one is a leak waiting to happen.
`BeginViewportOverlay` returns `false` and pushes nothing when the viewport is hidden via
`SetViewportVisible(false)`. Call it from `OnImGuiRender` only, after the shell has created the
window for the frame. `Engine3DDemo::DrawViewportOverlay` (`:1397-1460`) is the reference
implementation — gizmo plus a nav-cube image pinned to the top-right corner.

Both `BeginViewportOverlay` and `DockWindow` are **inline in the header on purpose**:
`WorkspaceLayer` is not `COSMIC_API`-exported, so only its inline members are reachable from a
project DLL. `SetViewportLayer`, `ClearViewportLayer` and the hook overrides are engine-internal and
will not link from your code.

---

## Replace the engine's menus with your own

The shell's menu bar doubles as the borderless title bar, so it is always drawn (except in
fullscreen, where it is hidden so content fills the monitor). Three knobs let an app take it over:

```cpp
ws->SetChromeMenusVisible(false);          // hide the engine File/View menus — keep everything else
ws->SetViewportTitle("Main.cscene *");     // rename the viewport TAB, not its identity
ws->SetProjectName("Orbit Lab");           // the centred caption
```

`SetChromeMenusVisible(false)` hides only the engine's **File** and **View** menus, so an app that
supplies its own menu bar does not show two *File* menus. The centred project name, the
minimize/maximize/close controls and the title-bar drag region are unaffected. Restore `true` on
project exit — the Launcher relies on those menus.

`SetViewportTitle` uses the `"Title###Viewport"` idiom: the *displayed* tab text changes while the
ImGui id stays `hash("Viewport")`, so renaming per scene never resets the dock layout — and
`BeginViewportOverlay`'s `Begin("Viewport")` still appends to the same window.

To draw your own window buttons anywhere, `Cosmic::UI::WindowControls()` emits a right-aligned
minimize / maximize-restore / close group already wired to `Application::Get().GetWindow()`, flat
and square-edged with a red close hover. The maximize glyph flips between
`ICON_LC_SQUARE` and `ICON_LC_COPY` with the window state.

Everything about the surrounding chrome — the hit-test predicate, fullscreen, DPI — is in
[`windowing-and-viewport.md`](windowing-and-viewport.md#draw-your-own-title-bar).

---

## Themes

A `Cosmic::Theme` is **plain data**: a name, an accent colour, a *complete* `ImGuiCol_` table, and
the structural style knobs (rounding, padding, border sizes, scrollbar/grab metrics). Because every
theme carries the whole colour array, applying one is deterministic — switching at runtime fully
replaces the previous look with no stale colours left behind.

`ThemeManager`'s storage lives in the **engine DLL**, so the engine and every project share exactly
one registry across the DLL boundary: a theme your app registers shows up in the engine's picker,
and vice versa.

```cpp
Cosmic::ThemeManager::Apply("Deep Embedded");     // false if the name is unknown (it warns)
const ImVec4 accent = Cosmic::ThemeManager::Accent();
Cosmic::UI::ThemeSelector();                      // a ready-made picker, drop into any window
```

| `ThemeManager` member | What it does |
| --- | --- |
| `Init()` | Registers the built-ins. Idempotent — a no-op after the first call. Called by `ImGuiLayer::OnAttach`. |
| `Apply(name)` | Writes the full colour table + style into the live ImGui style, syncs ImPlot, records the accent. `false` + a warning if unknown. |
| `ApplyTheme(theme)` | Applies a `Theme` object **without registering it** — this is what live preview is built on. |
| `Register(theme)` | Adds, or replaces in place by name (ordering is preserved). |
| `All()` / `Find(name)` / `CurrentName()` | Enumerate / look up / query. `All()` order is picker order. |
| `Accent()` | The applied theme's accent — use it in custom widgets so they track the theme. |
| `CaptureCurrentStyle(name)` | Snapshots the live ImGui style into a new non-built-in `Theme`. |
| `SaveToFile` / `LoadFromFile` / `LoadFolder` | `.ctheme` text persistence. **All three take a resolved disk path** — call `FileSystem::Resolve` yourself. |

**Built-ins, in picker order:** Sleek Pro *(the engine default)*, Neon HUD, Clean Flat, Cosmic
Emerald, Deep Embedded, Dracula Dark, Solarized Ash, Cyberpunk Neon, Retro Terminal, Corporate
Light, Default Dark.

`Cosmic::SetImGuiTheme(name)` and `ImGuiLayer::SetTheme(name)` are equivalent front doors that call
`Init()` first, so they are safe before the ImGui layer has attached. The legacy
`SetImGuiTheme(ImGuiTheme::SleekPro)` enum overload still works — `NameForTheme` maps it onto the
name-based registry — but the enum cannot name a client- or editor-registered theme, so prefer the
string.

### Where project themes come from

`ThemeManager::Init()` runs at ImGui-layer attach, **before any project is mounted**, so
`project://` cannot resolve yet. Project themes are loaded by the engine's project-mount rescan
(`Application::LoadProjectDLL` calls `ThemeManager::LoadFolder("project://themes")` after resolving
it). `LoadFolder` reads every `*.ctheme` in a resolved directory and registers it; a missing
directory is silently fine.

`.ctheme` is line-oriented `key=value` text — `name`, `accent`, `style.*`, and one `col.<ImGuiCol
name>` line per colour, using ImGui's own `GetStyleColorName` strings. On load the theme is seeded
from a complete dark table first, so a hand-written file may set only the keys it cares about.
Comment lines start with `#`, and a file with no `name` key falls back to its filename stem.

### The Theme Studio

The template project ships a live theme editor (`TemplateThemeShowcaseLayer`) that is worth reading
before writing your own — it is the whole authoring workflow in ~150 lines:

1. `LoadFolder(Resolve("project://themes"))` on attach, then `CaptureCurrentStyle` or `Find` to seed
   an editable copy.
2. Sliders and colour pickers mutate that copy; on any change it calls **`ApplyTheme(m_Edit)`** for
   live preview — deliberately *not* `Register`, so a discarded experiment leaves no trace.
3. **Save** sets the name, `Register`s it, and writes
   `Resolve("project://themes/<name>.ctheme")` with `SaveToFile`, then `Apply`s it.

The engine can also host the built-in picker for you as a floating popout, toggled from the
**View ▸ Theme Selector** menu item:

```cpp
ws->ShowThemeSelector(true);   // the `port` argument is accepted for source compatibility only
```

It is a floating window, off by default, and it is **never docked** despite the `DockPort` parameter
in the signature — the parameter is dead weight kept so old call sites still compile
(`WorkspaceLayer.h:208-217`).

---

## Fonts and Lucide icons

Drop a `.ttf` / `.otf` into `Cosmic/assets/fonts` (addressed as `engine://fonts`) or into a
project's `project://fonts`, and it is registered at startup by its **file stem**. The engine
bundles Roboto Regular / Medium / Bold.

```cpp
Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
ImGui::TextUnformatted("Section Title");
Cosmic::UI::Fonts::Pop();

ImFont* bold = Cosmic::UI::Fonts::Get("Roboto-Bold", 26.0f);   // for ImDrawList::AddText
```

The size ladder is `SizeSmall = 13`, `SizeBody = 16`, `SizeHeading = 22`, `SizeBig = 32`. ImGui 1.92
renders one face at any size on demand, so each face is baked **once** and the size you pass is
applied at draw time — `Get(name, sizePx)` selects purely by name and **ignores the size argument**.

**Roboto is the global default**, not an opt-in. `Fonts::Init` prefers `Roboto-Regular`, falls back
to the first custom face, then to ImGui's built-in, and assigns the winner to `io.FontDefault`
(`Fonts.cpp:144-157`). Every panel that pushes nothing renders in it. ImGui's bitmap font is kept as
`Fonts[0]` purely as a last-resort fallback.

`Get` on an unknown name returns the **default face** rather than null, so a missing font degrades
to plain text instead of crashing — which is exactly why `UI::StatCard` and `UI::SectionHeader` can
hard-push `"Roboto-Medium"` / `"Roboto-Bold"` unconditionally.

**Icons.** If `engine://fonts/lucide.ttf` is present it is *merged* into every registered text face
at load, so `ICON_LC_*` glyphs from `ui/IconsLucide.h` render inline in any label under any pushed
face:

```cpp
if (ImGui::Button(ICON_LC_ROCKET "  Launch")) { /* … */ }
if (Cosmic::UI::Fonts::HasIcons()) { /* the icon font was found and merged */ }
```

The icon font is never offered as a selectable body face — a font whose stem is `lucide` is skipped
by the normal registration path. `HasIcons()` is what the widget kit tests before drawing a glyph,
so a tree without `lucide.ttf` silently loses the icons and keeps the text.

**First registration of a stem wins** — engine faces load before project faces, and the
project-mount rescan (`Fonts::LoadProjectFonts`) is idempotent. That rescan runs from the Safe Zone
between frames, which is the only safe place to add faces.

---

## Widgets

`Cosmic::UI` (compiled into the engine DLL, exported) ships a small kit that reads the active
theme's accent and the font registry, so everything restyles when the theme changes:

| Widget | Notes |
| --- | --- |
| `StatCard(id, icon, label, value, sub, accent, size, valueColor)` | A framed card: left accent bar, small dimmed label with optional icon, a large `Roboto-Bold` value, dimmed sub-line. Defaults to 180 × 92. `icon = nullptr` omits it; a `valueColor` with `w <= 0` uses the theme's text colour. |
| `ToggleSwitch(label, bool*)` | Animated on/off switch; returns `true` on the frame it is clicked. The visible label is the part of `label` **before** any `##` id suffix — pass `"##hidden"` for no label at all. |
| `SectionHeader(icon, text)` | Bold heading with an optional accent-coloured icon, a separator and spacing. |
| `IconButton(str_id, icon, tooltip, size)` | Square icon button, default one frame-height square, with an optional hover tooltip. `str_id` keeps the id unique when several buttons share a glyph. |
| `AccentButton(label, size)` | Filled primary-action button tinted with the theme accent; the label colour flips to dark automatically on a light accent (luminance > 0.6). |
| `ThemeSelector()` | The full theme list — accent swatch + name per row — applying the clicked one immediately. |
| `WindowControls()` | Right-aligned minimize / maximize-restore / close, wired to the app window. |

---

## Plot styling

`UI::ApplyPlotStyle(theme)` syncs the global ImPlot style to a Cosmic theme: transparent frame,
plot background and border so charts blend into their panel; grid and ticks derived from the theme's
border colour at 30 % alpha; axis text in `TextDisabled`; legend from `PopupBg` / `Text`; and the
selection rectangle in the accent at 35 %.

**`ThemeManager::Apply` calls it for you**, so charts follow the theme with no work on your side.
It is a no-op when there is no current ImPlot context, which makes it safe to call early. Call it
directly only if you drive ImPlot with a `Theme` you never registered.

---

## Overlay and image helpers

`ui/Overlay.h` is **header-only and inline on purpose** — it compiles into whichever module includes
it and talks to the shared ImGui context, so it behaves identically in engine code and in a project
DLL. It is general-purpose drawing, not tied to any panel:

```cpp
Cosmic::UI::Rect r = Cosmic::UI::ImageFitted(texture);            // aspect-fit; returns its rect
ImDrawList* dl = ImGui::GetWindowDrawList();
Cosmic::UI::ReadoutBox(dl, r.At(0.5f, 0.2f), "RPM", "10445");     // framed box at a normalized point
Cosmic::UI::Text(dl, r.At(0.1f, 0.9f), IM_COL32_WHITE, "label");  // the core text primitive
```

| Helper | Notes |
| --- | --- |
| `ImageFitted(tex, region)` → `Rect` | Letterboxes a texture into `region` (`x`/`y` ≤ 0 = "the remaining content region"), reserves the whole region in the layout, and returns the on-screen image rect. UVs are flipped vertically because engine textures load bottom-up. A null or zero-sized texture draws a `Dummy` and returns the region. |
| `Rect::At(nx, ny)` | Maps a normalized `[0,1]` coordinate to a screen pixel inside the rect — this is what makes hand-tuned overlay positions trivial (`0.5, 0.2` = top-centre). Also `Width/Height/Size/Center`. |
| `Text(dl, pos, color, text, font, sizePx, align)` | Draw a string in a chosen face + size, anchored by a nine-way `Align`. `font = nullptr` → the default face; `sizePx = 0` → the current font size. |
| `TextThick(...)` | Font-agnostic faux-bold — redraws at four offsets. Use only when no real bold face exists; a bold `ImFont` looks better. |
| `MeasureText(font, sizePx, text)` | Measure in a specific face without pushing it. |
| `ReadoutBox(dl, pos, label, value, style)` → `Rect` | A framed label-over-value box that auto-sizes to content. `ReadoutStyle` exposes fill/border/label/value colours, both fonts, sizes, padding, rounding, spacing, faux-bold, the anchor and a minimum size. |
| `ImageWindow(title, tex, &open, caption, firstSize)` | A resizable floating pop-out showing a texture aspect-fitted, with an optional wrapped caption below. The caller owns the open flag. |

`ImageWindow` and `ImageFitted` both test `tex->GetWidth() > 0` rather than only the `Ref`, which
matters: `Texture2D::Create` returns a **degraded, non-null 0×0 object** on a failed load rather
than `nullptr` (see [`assets-and-vfs.md`](assets-and-vfs.md)), so a `Ref`-only check would sample a
black texture instead of showing the "image not found" hint.

---

## Common patterns

**Rebuild the layout per screen.** `ClearDockWindows()` → `SetEdgeRatios(...)` → a fresh set of
`DockWindow` calls → let the queued rebuild happen. Three shipped apps do exactly this on every
screen change; there is no cheaper "move one panel" API and you do not need one.

**Persist a layout as an ImGui ini.** `ImGui::SaveIniSettingsToMemory()` captures the user's live
arrangement; Starforge writes it under `user://starforge/layouts/<name>.ini` with a `#`-comment
header recording which panels were open (`LayoutPresets.cpp:161-193`). Coded presets and saved inis
coexist because `SetApplyCodedLayoutOnLoad(true)` — the default — re-applies the coded layout on
load; the flag is the hook for a future "remember my arrangement" toggle and only the coded path is
wired today.

**Use the accent, not a literal.** `ThemeManager::Accent()` in your own drawing keeps custom widgets
consistent across all eleven built-ins and anything a user authors.

**Icon-first labels.** `ICON_LC_X "  Label"` — two spaces — is the spacing convention used
throughout the tree, and it degrades to a leading blank when the icon font is missing.

---

## Pitfalls

**"`GetWorkspaceLayer()` returns an incomplete type."** Add `#include "layers/WorkspaceLayer.h"`.
`Cosmic.h` does not include it and `Application.h` only forward-declares the class.

**"`SetViewportLayer` is an unresolved external."** `WorkspaceLayer` is not `COSMIC_API`-exported —
only its **inline** members (`DockWindow`, `SetViewportVisible`, `SetBottomInsetPixels`,
`BeginViewportOverlay`, `SetEdgeRatios`, …) are reachable from a project DLL. The non-inline members
are engine-internal by design.

**A docked window never appears.** The `DockWindow` name must match your `ImGui::Begin("…")`
**exactly**, including case and any `###` id suffix. A mismatch produces a floating window and no
error.

**A layout looked right, then collapsed after a "Reset Layout".** You stored a dock-node id. See
[Never store a dock-node id](#never-store-a-dock-node-id).

**Your bottom band is the wrong height at 150 % scaling.** You passed a constant to
`SetBottomInsetPixels`. Derive it from `ImGui::GetFrameHeight()`, which already carries the DPI-scaled
font size. (The *edge* minimums are different — `SetEdgeMinPixels` multiplies by `DpiScale` for you,
so pass those unscaled.)

**Your edges came out on the wrong sides.** `SetEdgeRatios` is *(left, right, top, bottom)* and
`SetEdgeMinPixels` is *(top, bottom, left, right)*.

**Fonts do not appear / the atlas looks empty.** Faces must be registered before the first ImGui
frame, or from the Safe Zone between frames. `Fonts::Init` handles the engine folder and the
project-mount rescan handles `project://fonts`; adding a face mid-frame is not safe.

**`Fonts::Get("Whatever", 24)` returned something.** It falls back to the default face for an
unknown name and never returns null after `Init()`. If your text looks wrong rather than missing,
check the stem — it is the **file name**, e.g. `"Roboto-Bold"`, not a family plus weight.

**`ThemeManager::SaveToFile` wrote nothing / to the wrong place.** All three persistence calls take a
**resolved** path. `FileSystem::Resolve("project://themes/My Theme.ctheme")` first — the engine's
path-taking APIs do not agree on whether they resolve, and these do not.

**A theme you registered vanished after a project reload.** `Register` replaces by name, and the
registries are additive across project mounts on purpose (dropping them would dangle `ImFont*` and
`Ref<Font>` handles other systems still hold). If a name collides with a built-in, yours wins until
something re-registers the built-in.

**`ShowThemeSelector(true, DockPort::RightTop)` did not dock anything.** The selector is a floating
popout; the port argument is vestigial.

**`BeginViewportOverlay()` returned `false` and the next `ImGui::End()` blew up.** Call
`EndViewportOverlay()` unconditionally — it is a no-op when nothing was pushed — and never pair
`BeginViewportOverlay` with a bare `ImGui::End()`.

**Your game's menus are themed like the editor.** You built them with ImGui. Game UI is entities —
[`game-ui.md`](game-ui.md).

---

## See also

- [`game-ui.md`](game-ui.md) — **the other UI system**: canvases, rect transforms, buttons, and
  in-game menus that ship inside a packaged app.
- [`windowing-and-viewport.md`](windowing-and-viewport.md) — the window this shell lives in: custom
  chrome and the title-bar hit test, DPI, fullscreen, and the viewport rectangle these panels
  surround.
- [`project-anatomy.md`](project-anatomy.md) — `InitializePluginContexts` and why a project DLL
  needs the host's ImGui context; layer ordering and `OnImGuiRender`.
- [`logging-and-diagnostics.md`](logging-and-diagnostics.md) — the Console and Profiler panels these
  ports host.
- [`serial-and-telemetry.md`](serial-and-telemetry.md) — `TelemetryPanel` and the ImPlot charts
  `PlotStyle` themes.
- [`cameras.md`](cameras.md) — `Gizmo`'s frame protocol, which `BeginViewportOverlay` exists to
  satisfy.
- [`../reference/ui.md`](../reference/ui.md) — per-call entries *(skeleton — D18)*.
- [`../systems/ui-theming.md`](../systems/ui-theming.md) — internals *(skeleton — D34)*.
