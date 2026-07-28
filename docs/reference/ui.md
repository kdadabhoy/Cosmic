# API Reference — UI & Theming

> **STATUS: WRITTEN** — work order **D18** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/layers/ImGuiLayer.h`, `layers/ImGuiThemes.h`,
`layers/WorkspaceLayer.h` *(client-reachable via `Application::GetWorkspaceLayer()`)*,
`ui/Fonts.h`, `ui/Theme.h`, `ui/ThemeManager.h`, `ui/Widgets.h`, `ui/PlotStyle.h`, `ui/Overlay.h`,
`ui/IconsLucide.h`, plus `Cosmic.h`'s `HostContext` struct and its two `SetImGuiTheme` helpers.

**Read first:** the guide chapter
[`../guide/editor-ui-and-theming.md`](../guide/editor-ui-and-theming.md) — it owns the task half
(quick start, the docking model in prose, the Theme Studio workflow, the widget catalogue, the
pitfall list) and covers every class in scope here. **This chapter does not repeat it**: it is the
per-call lookup behind it — signature, exact behaviour, clamp, failure mode, linkage. The
window/viewport half is [`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md).
Systems explainer: [ui-theming](../systems/ui-theming.md) *(skeleton — D34)*.

> **Scope boundary.** This chapter is **ImGui editor/tool chrome only**. The engine's *other* UI
> system — in-game menus and HUDs built from scene entities (`CanvasComponent`,
> `RectTransformComponent`, `UiImage`/`UiText`/`UiButton`, `scene/ui/UiSystem.h`) — is unrelated and
> is documented in [`../guide/game-ui.md`](../guide/game-ui.md). Nothing here themes or draws it.

---

## Contents

- [Configuration](#configuration) — both builds, no exceptions
- [**Linkage: what actually links from a project DLL**](#linkage-what-actually-links-from-a-project-dll) — the chapter's most practically important fact
- [`HostContext` and the plugin exports](#hostcontext)
- [`Cosmic::SetImGuiTheme`](#cosmicsetimguitheme)
- [`ImGuiLayer`](#imguilayer) — the ImGui/ImPlot host layer
- [`ImGuiTheme` and the built-in theme builders](#imguitheme) *(`layers/ImGuiThemes.h`)*
- [**The docking model**](#the-docking-model) — the `DockPort` table, the build order, `DockFlags`
- [**Never persist a dock-node id**](#never-persist-a-dock-node-id)
- [`WorkspaceLayer`](#workspacelayer) — every client-reachable member
- [`Theme` / `ThemeStyle`](#theme) — the theme data model
- [`ThemeManager`](#thememanager) — the registry, and the `.ctheme` format
- [`UI::Fonts`](#uifonts) — the ImGui font registry
- [`ui/IconsLucide.h`](#uiiconslucideh) — the icon macros
- [`UI` widgets](#ui-widgets) — `ui/Widgets.h`
- [`UI::ApplyPlotStyle`](#uiapplyplotstyle) — `ui/PlotStyle.h`
- [`UI` overlay helpers](#ui-overlay-helpers) — `ui/Overlay.h`
- [Failure-mode summary](#failure-mode-summary)
- [Manifest & coverage notes](#manifest--coverage-notes)

---

## Configuration

**Every header in this chapter ships in both engine builds.** All of them are included by
`Cosmic.h` *outside* any `#ifndef COSMIC_2D_ONLY` fence (`Cosmic.h:182`, `:200-206`), and the CMake
2D partition block (`Cosmic/CMakeLists.txt:178-211`) filters only `renderer/`, `graphics/`,
`camera/NavigationCube`, `scene/`, `reflect/TypeRegistry3D` and `assets/MeshImport` — it touches
nothing under `layers/` or `ui/`. So neither the ³ᴰ (fenced → compile error) nor the ³ᴰ⁺ (unfenced,
`.cpp` dropped → link error) failure applies to anything documented here. Background:
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md), README §1.6.

> **No pre-condition in this chapter is enforced by an assertion.** `CS_ASSERT` / `CS_CORE_ASSERT`
> are compiled out in *every* configuration, and no file in this scope uses them at all. Where a
> header docstring writes "Pre: a valid GLFW window and OpenGL context exist", that is a documented
> expectation, not a check. The guards that do exist are ordinary runtime `if`s, and each one is
> named in the entry that owns it.

---

## Linkage: what actually links from a project DLL

`WorkspaceLayer` is declared **without** `COSMIC_API` (`WorkspaceLayer.h:109`). Nothing about it is
exported from the engine DLL. What a project DLL can call is therefore decided **per member**, by
whether the member is defined inline in the header:

| Member | Defined | Reachable from a project DLL? |
| --- | --- | --- |
| `DockWindow`, `ClearDockWindows`, `RequestExtraDockedPanel`, `ShowThemeSelector`, `IsThemeSelectorVisible` | inline (`.h:159-218`) | ✅ compiles into your DLL |
| `SetEdgeRatios`, `SetEdgeMinPixels`, `SetBottomInsetPixels`, `GetBottomInsetPixels` | inline (`.h:222-246`) | ✅ |
| `SetChromeMenusVisible`, `AreChromeMenusVisible`, `SetViewportTitle`, `GetViewportTitle` | inline (`.h:254-261`) | ✅ |
| `SetApplyCodedLayoutOnLoad`, `GetApplyCodedLayoutOnLoad` | inline (`.h:268-269`) | ✅ |
| `SetViewportVisible`, `IsViewportVisible`, `HasViewportLayer` | inline (`.h:127-138`) | ✅ |
| `SetProjectName`, `GetProjectName` | inline (`.h:143-144`) | ✅ |
| `GetViewportPos`, `GetViewportSize`, `IsViewportHovered`, `IsViewportFocused` | inline (`.h:279-289`) | ✅ |
| `BeginViewportOverlay`, `EndViewportOverlay` | inline (`.h:314-329`) | ✅ |
| `RequestLayoutReset`, `IsReadyForDeletion`, `ResetLayout` | inline (`.h:334-343`) | ✅ |
| **`SetViewportLayer`, `ClearViewportLayer`** | `.cpp:39-67` | ❌ **unresolved external** |
| **the constructor, and every `Layer` hook override** (`OnAttach`, `OnDetach`, `OnUpdate`, `OnFixedUpdate`, `OnImGuiRender`, `OnEvent`) | `.cpp` | ❌ **unresolved external** |

This is deliberate — the header says so twice (`WorkspaceLayer.h:178-179`, `:311-312`). The
non-inline members are the engine's own plumbing: `Application::LoadProjectDLL` calls
`SetViewportLayer` to mount your layer, and the hooks are driven by the LayerStack. You never call
them. If you see `LNK2019: unresolved external symbol ... WorkspaceLayer::SetViewportLayer`, you
reached for an engine-internal member; there is no export to add and no workaround.

Every entry below states its linkage in the same words, so you can answer the question from the
entry alone.

**You must also include the header explicitly.** `Cosmic.h` does *not* pull in
`layers/WorkspaceLayer.h` (`WorkspaceLayer.h:51` includes `Cosmic.h`, not the other way round), and
`Application.h:49` only forward-declares the class:

```cpp
#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"   // required: Application.h only forward-declares it

Cosmic::WorkspaceLayer* ws = Cosmic::Application::Get().GetWorkspaceLayer();   // may be nullptr
```

`Application::GetWorkspaceLayer()` (`Application.h:91`) returns the raw pointer and is **null until
the workspace shell is pushed** — it is null on the Launcher screen and after a teardown. Every
shipped project null-checks it (`FrontierApp.cpp:262-263`).

---

## `HostContext`

```cpp
// Cosmic.h:213-217
struct COSMIC_API HostContext
{
    ImGuiContext* ImGuiCtx;
    ImPlotContext* ImPlotCtx;
};
```

**What it does** — carries the host process's two live UI context pointers across the DLL boundary.
ImGui and ImPlot each keep their entire state behind a *per-module* global pointer, so a freshly
loaded project DLL starts with both set to null even though the engine's contexts are alive. The
engine hands this struct to the DLL's `InitializePluginContexts` export before it creates your layer;
the export's body calls `ImGui::SetCurrentContext` / `ImPlot::SetCurrentContext` with the two
members.

**Why you'd use it** — you almost never construct one. You need to know it exists because it is the
reason your DLL may call `ImGui::Begin` at all. It is declared alongside the two exports every
project DLL must provide (`Cosmic.h:235-239`):

```cpp
extern "C" {
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
}
```

**Example**

```cpp
// Generated for you by the module macros; shown here so the shape is on the record.
extern "C" __declspec(dllexport)
void InitializePluginContexts(Cosmic::HostContext context)
{
    ImGui::SetCurrentContext(context.ImGuiCtx);
    ImPlot::SetCurrentContext(context.ImPlotCtx);
}
```

**Notes & pitfalls**

- **Skip it and the first `ImGui::Begin` in your DLL dereferences a null context and crashes.** There
  is no diagnostic and no fallback; the contexts are not discoverable from the DLL side.
- The struct is `COSMIC_API`-exported but is a plain two-pointer aggregate — it is passed **by
  value**, so no lifetime question arises.
- `ImPlotCtx` is populated unconditionally, whether or not your project draws a chart.
- The **lifecycle** — when the engine scans for the DLL, calls the exports, and deletes your layer
  before `FreeLibrary` — belongs to [core.md](core.md) and is drawn as **DG-5** in
  [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-5--the-plugin-dll-lifecycle).

**See also** — [core.md](core.md) (the plugin-export boundary),
[`../guide/project-anatomy.md`](../guide/project-anatomy.md#the-exports)

---

## `Cosmic::SetImGuiTheme`

```cpp
// Cosmic.h:220-231 — two inline free functions
inline void SetImGuiTheme(Cosmic::ImGuiTheme theme);
inline void SetImGuiTheme(const std::string& name);
```

**What it does** — a one-line front door onto [`ImGuiLayer::SetTheme`](#imguilayersettheme). Both
overloads forward directly and add nothing.

**Why you'd use it** — it is the shortest way to set the look from a project that has only included
`<Cosmic.h>`. Prefer the **string** overload: the enum cannot name a theme registered by a client or
by the in-app editor, only the eleven built-ins.

**Example**

```cpp
Cosmic::SetImGuiTheme("Neon HUD");                       // preferred
Cosmic::SetImGuiTheme(Cosmic::ImGuiTheme::SleekPro);     // legacy enum path
```

**Notes & pitfalls**

- Inline, so both link from a project DLL regardless of export tables.
- Silent on an unknown name — the underlying `ThemeManager::Apply` logs a core warning and returns
  `false`, but that `bool` is **discarded** here. Call
  [`ThemeManager::Apply`](#thememanagerapply) directly if you need to know whether it worked.
- Safe to call before `ImGuiLayer::OnAttach`: `SetTheme` calls `ThemeManager::Init()` first.

**See also** — [`ImGuiLayer::SetTheme`](#imguilayersettheme), [`ThemeManager`](#thememanager)

---

## `ImGuiLayer`

```cpp
// Cosmic/src/layers/ImGuiLayer.h:55
class COSMIC_API ImGuiLayer : public Layer
```

The engine's ImGui host. `Application::Initialize` constructs it and pushes it as an **overlay**
before any project loads; `Application::GetImGuiLayer()` (`Application.h:137`) hands out the raw
pointer. It owns the ImGui and ImPlot contexts, the GLFW+OpenGL3 backends, the per-frame
begin/render pair, and the event-capture gate. `COSMIC_API`-exported, so every member below links
from a project DLL — but you only ever call [`BlockEvents`](#imguilayerblockevents) and
[`SetTheme`](#imguilayersettheme). The lifecycle and frame hooks are driven by `Application`.

**Declared in** `Cosmic/src/layers/ImGuiLayer.h` · **defined in** `ImGuiLayer.cpp` · both engine
configurations.

### `ImGuiLayer::OnAttach`

```cpp
virtual void	OnAttach() override;
```

**What it does** — the UI bootstrap, in this exact order (`ImGuiLayer.cpp:37-70`):

1. `IMGUI_CHECKVERSION()`, `ImGui::CreateContext()`, `ImPlot::CreateContext()` (`:39-41`).
2. Sets `ImGuiConfigFlags_NavEnableKeyboard | DockingEnable | ViewportsEnable` (`:44-46`).
   **Multi-viewport is why every ImGui rectangle in the engine is in desktop coordinates.**
3. Points `io.IniFilename` at `FileSystem::Resolve("user://imgui.ini")`, held in a
   **`static std::string`** because ImGui borrows the pointer rather than copying the string
   (`:53-54`). ImGui's default writes `imgui.ini` into the working directory, which is read-only for
   an app installed under `Program Files`; in a dev/portable tree `user://` still resolves to
   `./imgui.ini`, i.e. unchanged behaviour.
4. `ThemeManager::Init()` then `SetTheme("Sleek Pro")` (`:57-58`).
5. `ImGui_ImplGlfw_InitForOpenGL(window, true)` and `ImGui_ImplOpenGL3_Init("#version 410")`
   (`:63-64`).
6. `UI::Fonts::Init()` (`:69`) — **last, but still before the first frame**, because the OpenGL
   backend bakes the glyph atlas lazily on the first `NewFrame`.

**Why you'd use it** — you don't. It is listed so you know what is already true when your
`OnImGuiRender` runs, and so you can find the one line that surprises people (the `static`
ini-path string).

**Notes & pitfalls**

- The header's docstring pre-condition ("a valid GLFW window and OpenGL context exist") is satisfied
  by `Application`'s ordering and is **not asserted**.
- Step 3 is the reason `io.IniFilename` must never be reassigned by a project to a temporary's
  `c_str()`.

**See also** — [`UI::Fonts::Init`](#uifontsinit), [`ThemeManager::Init`](#thememanagerinit)

### `ImGuiLayer::OnDetach`

```cpp
virtual void	OnDetach() override;
```

**What it does** — shuts down the OpenGL3 and GLFW backends, then destroys the ImPlot and ImGui
contexts, in that order (`ImGuiLayer.cpp:79-86`).

**Notes & pitfalls**

- After this returns, **no ImGui call is valid anywhere**. `WorkspaceLayer::OnDetach` carries a
  comment to the same effect and deliberately makes no ImGui calls (`WorkspaceLayer.cpp:32`).
- There is no re-attach path: the contexts are destroyed, not reset.

### `ImGuiLayer::Begin`

```cpp
void		Begin();
```

**What it does** — starts the UI frame: `ImGui_ImplOpenGL3_NewFrame()`,
`ImGui_ImplGlfw_NewFrame()`, `ImGui::NewFrame()`, then **`ImGuizmo::BeginFrame()`**
(`ImGuiLayer.cpp:95-105`).

**Why you'd use it** — you don't; `Application` calls it once per frame. It is documented because of
the fourth call: the engine owns ImGuizmo's per-frame reset, exactly once per ImGui frame, so client
code can use `Cosmic::Gizmo` with **no per-frame bookkeeping of its own**. Calling
`ImGuizmo::BeginFrame()` yourself would reset state the shell already set up.

**See also** — `Gizmo` in [cameras.md](cameras.md), [`WorkspaceLayer::BeginViewportOverlay`](#workspacelayerbeginviewportoverlay)

### `ImGuiLayer::End`

```cpp
void		End();
```

**What it does** — `ImGui::Render()` + `ImGui_ImplOpenGL3_RenderDrawData(...)`, then, when
`ImGuiConfigFlags_ViewportsEnable` is set, `ImGui::UpdatePlatformWindows()` +
`ImGui::RenderPlatformWindowsDefault()` with the current GL context saved and restored around them
(`ImGuiLayer.cpp:115-138`).

**Notes & pitfalls**

- **Do not assign `io.DisplaySize` anywhere.** `ImGuiLayer.cpp:119-126` records why: the GLFW backend
  already sets `DisplaySize` and `DisplayFramebufferScale` from the live window in `Begin()`, before
  layout. Re-assigning it here — *after* the frame was laid out and hit-tested — made ImGui render in
  a different coordinate space than it laid out in whenever the cached size was stale, which clipped
  the custom title bar off-screen and offset every mouse click until a resize refreshed the cache.
  That is a fixed bug preserved as a comment; the fix is the *absence* of the assignment.
- The GL context save/restore is what lets a panel dragged out of the main window keep rendering.

### `ImGuiLayer::OnEvent`

```cpp
virtual void	OnEvent(Event& event) override;
```

**What it does** — when [`BlockEvents`](#imguilayerblockevents) is on (the default), marks the event
`Handled` if ImGui wants that class of input (`ImGuiLayer.cpp:150-161`):

```cpp
event.Handled |= event.IsInCategory(EventCategoryMouse)    & io.WantCaptureMouse;
event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
```

Because `ImGuiLayer` is an **overlay**, it sees events before the layer stack, so a click on a
slider does not also reach your game layer.

**Notes & pitfalls**

- `Handled` is only ever **set**, never cleared — both here and in `EventDispatcher::Dispatch`
  (`Event.h:137-139`).
- The private `OnMouseButtonPressed` handler dispatched at `:159` returns `io.WantCaptureMouse`,
  which is **redundant**: `MouseButtonPressedEvent` carries `EventCategoryMouse`
  (`MouseEvent.h:97`), so the first line above has already produced the same result. It is harmless
  dead weight, not a second gate.
- With `BlockEvents(false)` this method does nothing at all — including no dispatch.

**See also** — [events-input.md](events-input.md), **DG-4** in
[`../guide/events-and-input.md`](../guide/events-and-input.md#dg-4--event-propagation)

### `ImGuiLayer::BlockEvents`

```cpp
void		BlockEvents(bool block) { m_BlockEvents = block; }
```

**What it does** — turns the capture gate above on or off. Defaults to **`true`**
(`ImGuiLayer.h:96`). Inline in the header.

**Why you'd use it** — for a captured-cursor first-person mode, where you want raw mouse and key
events to reach your layer even though ImGui thinks it wants them.

**Example**

```cpp
Cosmic::Application::Get().GetImGuiLayer()->BlockEvents(false);   // let everything through
```

**Notes & pitfalls**

- **The workspace shell already writes this flag every frame.** `WorkspaceLayer::OnImGuiRender` calls
  `BlockEvents(!m_ViewportFocused && !m_ViewportHovered)` while the viewport is visible
  (`WorkspaceLayer.cpp:210-211`) and `BlockEvents(true)` while it is hidden (`:232`). A value you set
  from `OnUpdate` or `OnAttach` is therefore **overwritten on the next ImGui frame**. To hold it
  off, set it every frame from `OnImGuiRender` *after* the shell has run, or hide the viewport.
- This gates the **event** path only. Polled input (`Input::IsKeyPressed`) is never blocked — gate
  that yourself with [`IsViewportHovered`](#workspacelayerisviewporthovered--isviewportfocused).

**See also** — [`WorkspaceLayer::IsViewportHovered`](#workspacelayerisviewporthovered--isviewportfocused),
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md#hover-and-focus-gates)

### `ImGuiLayer::SetTheme`

```cpp
static void     SetTheme(Cosmic::ImGuiTheme theme);
static void     SetTheme(const std::string& name);
```

**What it does** — applies a theme. The **name** overload calls `ThemeManager::Init()` then
`ThemeManager::Apply(name)` (`ImGuiLayer.cpp:179-185`). The **enum** overload resolves through
[`NameForTheme`](#namefortheme) and calls the name overload (`:173-177`).

**Why you'd use it** — static, exported, and `Init()`-safe, so it is the call that works from
anywhere at any time — including before the ImGui layer has attached. Reach for
[`ThemeManager::Apply`](#thememanagerapply) instead when you want the success `bool`.

**Example**

```cpp
Cosmic::ImGuiLayer::SetTheme("Cosmic Emerald");
```

**Notes & pitfalls**

- **Returns nothing.** An unknown name logs `ThemeManager: theme '<name>' not found` at core-warn
  level and leaves the current look untouched.
- `Init()` is idempotent, so the double-init on repeated calls costs nothing.
- The enum overload can only reach the eleven built-ins; a client- or editor-registered theme has no
  enumerator.

**See also** — [`Cosmic::SetImGuiTheme`](#cosmicsetimguitheme), [`ThemeManager::Apply`](#thememanagerapply)

---

## `ImGuiTheme`

`layers/ImGuiThemes.h` is **header-only and entirely inline** — no `COSMIC_API`, no `.cpp`. It
compiles into whichever module includes it, and `ImGuiLayer.h:49` includes it, so `Cosmic.h` puts
every symbol below in reach of a project DLL.

### `enum class ImGuiTheme`

```cpp
// ImGuiThemes.h:30-43
enum class ImGuiTheme
{
    DefaultDark = 0,
    CosmicEmerald, DeepEmbedded, CorporateLight, CyberpunkNeon, RetroTerminal,
    DraculaDark, SolarizedAsh, SleekPro, NeonHUD, CleanFlat
};
```

**What it does** — the legacy identifier set for the eleven built-in themes. It is the parameter
type of `ImGuiLayer::SetTheme(ImGuiTheme)` and `Cosmic::SetImGuiTheme(ImGuiTheme)` and has no other
use: applying a theme goes through the **name**-based registry either way.

**Notes & pitfalls**

- **The enumerator order is not the picker order.** Enumerators are declared in historical order;
  [`GetBuiltInThemes()`](#getbuiltinthemes) returns them in *display* order, led by `Sleek Pro`.
- `DefaultDark = 0` is the only pinned value; the rest are implicit. Do not serialize the integer.
- Adding a new built-in theme does **not** require an enumerator — the header's own instructions say
  so (`ImGuiThemes.h:18-21`).

### `SeedDark` / `SeedLight`

```cpp
inline void SeedDark(Theme& t);
inline void SeedLight(Theme& t);
```

**What it does** — fills `t.colors` with a complete `ImGuiCol_` table from `ImGui::StyleColorsDark`
/ `StyleColorsLight` (`ImGuiThemes.h:49-61`). Every colour index is written; nothing else on `t` is
touched.

**Why you'd use it** — as the first line of your own theme builder, so that the colours you *don't*
override still have sensible values. `ThemeManager::LoadFromFile` uses `SeedDark` for exactly this
reason (`ThemeManager.cpp:237`), which is what lets a hand-written `.ctheme` set only the keys it
cares about.

**Example**

```cpp
Cosmic::Theme t;
t.name   = "Studio Amber";
t.accent = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
Cosmic::SeedDark(t);
t.colors[ImGuiCol_Button] = t.accent;
Cosmic::ThemeManager::Register(t);
```

**Notes & pitfalls**

- Both construct a throwaway `ImGuiStyle` on the stack, so **neither needs an ImGui context** and
  neither disturbs the live style.
- They do not set `t.style` — the `ThemeStyle` defaults stand unless you change them.

### `BuildSleekPro` and the other ten builders

```cpp
inline Theme BuildSleekPro();      inline Theme BuildNeonHUD();        inline Theme BuildCleanFlat();
inline Theme BuildCosmicEmerald(); inline Theme BuildDeepEmbedded();   inline Theme BuildDraculaDark();
inline Theme BuildSolarizedAsh();  inline Theme BuildCyberpunkNeon();  inline Theme BuildRetroTerminal();
inline Theme BuildCorporateLight();inline Theme BuildDefaultDark();
```

**What it does** — each returns a fully-populated [`Theme`](#theme) **by value**: `Seed*` for the
full colour table, then the overrides that give the theme its identity, with `builtIn = true`.

**Why you'd use it** — to seed an editable copy from a built-in without going through the registry,
or as a worked template when writing your own (`ImGuiThemes.h:14-23` is the recipe).

**Notes & pitfalls**

- Cheap but not free — each call constructs an `ImGuiStyle` and copies `ImGuiCol_COUNT` colours.
  Don't call one per frame; call `ThemeManager::Find(name)` instead.
- `t.name` is the display string, **not** the enumerator spelling: `BuildCosmicEmerald()` yields
  `"Cosmic Emerald"` with a space.

### `GetBuiltInThemes`

```cpp
inline std::vector<Theme> GetBuiltInThemes();
```

**What it does** — returns all eleven built-ins **in picker order** (`ImGuiThemes.h:450-465`):
Sleek Pro *(the engine default)*, Neon HUD, Clean Flat, Cosmic Emerald, Deep Embedded, Dracula Dark,
Solarized Ash, Cyberpunk Neon, Retro Terminal, Corporate Light, Default Dark.

**Why you'd use it** — almost never directly; `ThemeManager::Init` consumes it
(`ThemeManager.cpp:86-87`) and `ThemeManager::All()` gives you the same list plus everything anyone
registered afterwards, without rebuilding.

**Notes & pitfalls**

- Builds all eleven themes on every call. Prefer [`ThemeManager::All`](#thememanagerall).

### `NameForTheme`

```cpp
inline const char* NameForTheme(ImGuiTheme theme);
```

**What it does** — maps an enumerator to the theme's registered display name
(`ImGuiThemes.h:469-486`).

**Notes & pitfalls**

- **Never returns null.** The `default:` arm falls through with `DefaultDark` to `"Default Dark"`, so
  an out-of-range cast yields a valid, applicable name rather than a crash — and silently applies the
  wrong theme.
- The returned pointer is a string literal with static storage duration; it is safe to hold.

---

## The docking model

The shell offers **13 fixed ports**: four optional edges × three sections, plus the centre. A client
binds a window *name* to a port with [`DockWindow`](#workspacelayerdockwindow); the shell owns every
node id.

```cpp
// WorkspaceLayer.h:70-77
enum class DockPort
{
    LeftTop,    LeftMiddle,    LeftBottom,
    RightTop,   RightMiddle,   RightBottom,
    TopLeft,    TopCenter,     TopRight,
    BottomLeft, BottomCenter,  BottomRight,
    Center
};
```

| Enumerator | Screen region | Section order within the edge |
| --- | --- | --- |
| `DockPort::LeftTop` | left column, upper | `ImGuiDir_Up` split → top **→** bottom |
| `DockPort::LeftMiddle` | left column, middle | " |
| `DockPort::LeftBottom` | left column, lower | " |
| `DockPort::RightTop` | right column, upper | `ImGuiDir_Up` split → top **→** bottom |
| `DockPort::RightMiddle` | right column, middle | " |
| `DockPort::RightBottom` | right column, lower | " |
| `DockPort::TopLeft` | top row, left | `ImGuiDir_Left` split → left **→** right |
| `DockPort::TopCenter` | top row, centre | " |
| `DockPort::TopRight` | top row, right | " |
| `DockPort::BottomLeft` | bottom row, left | `ImGuiDir_Left` split → left **→** right |
| `DockPort::BottomCenter` | bottom row, centre | " |
| `DockPort::BottomRight` | bottom row, right | " |
| `DockPort::Center` | tabbed with the central `Viewport` — or **in its place** when the viewport is hidden | n/a |

**The split order decides the shape**, and it is left → right → top → bottom
(`WorkspaceLayer.cpp:461-464`):

```
+----------------------------------------------------------+
| menu / title bar (hidden in fullscreen)                   |
+---------+--------------------------------------+---------+
|         |  TopLeft | TopCenter  | TopRight      |         |   <- top row spans only
| LeftTop |----------+------------+---------------| RightTop|      the CENTRAL band
|         |                                       |         |
|---------|            Center + "Viewport"        |---------|
|LeftMid  |                                       |RightMid |
|---------|                                       |---------|
|LeftBot  |  BottomLeft | BottomCenter | BottomRt |RightBot |
+---------+--------------------------------------+---------+
| bottom inset band — SetBottomInsetPixels, client-drawn    |
+----------------------------------------------------------+
```

**Left and right are full-height columns; top and bottom rows span only the band between them.** A
top toolbar therefore never sits above the side panels. (There is no assigned Mermaid diagram ID for
this layout in doc 12 §4, so it stays ASCII per the diagram rules — do not promote it to a DG-*n*
without adding the row first.)

Three consequences of `BuildDockspace`, all verified at `WorkspaceLayer.cpp:428-514`:

1. **Only ports that receive a window are carved out** (`:439-442` compute `useLeft/useRight/useTop/
   useBottom` from whether *any* of an edge's three sections has a binding). An unused edge takes
   **zero** space; there are no empty panels to close.
2. **Two windows on the same port become tabs**, in binding order (`dockAll`, `:434-437`).
3. **Sections within a used edge are split near-equally among the sections that are used**
   (`SplitIntoSections`, `:364-379`) — binding only `LeftTop` and `LeftBottom` gives you two
   half-height panels, not a gap where `LeftMiddle` would be.

### `enum class DockFlags`

```cpp
// WorkspaceLayer.h:82-86
enum class DockFlags : uint32_t { None = 0, NoTabBar = 1u << 0 };
inline DockFlags operator|(DockFlags a, DockFlags b);
inline bool HasFlag(DockFlags v, DockFlags f);
```

**What it does** — `NoTabBar` strips the "▼ Name" tab header from the dock node a window lands in,
for chrome-less docks (a top toolbar, a full-bleed panel) where the tab row is wasted vertical
space. `operator|` combines flags; `HasFlag` tests one.

**Notes & pitfalls**

- **The flag is applied to the NODE, not the window** — `BuildDockspace` sets
  `ImGuiDockNodeFlags_NoTabBar` in the node's `LocalFlags` (`WorkspaceLayer.cpp:493-501`). So it
  affects **every window sharing that port**, including ones bound without the flag. If you want a
  tab bar on one of them, give it a different port.
- Only `None` and `NoTabBar` exist; there is no other flag today.

### `struct DockBinding` / `struct DockedPanelRequest`

```cpp
// WorkspaceLayer.h:89-94
struct DockBinding
{
    std::string WindowName; // must match the client's ImGui::Begin("...")
    DockPort    Port  = DockPort::LeftTop;
    DockFlags   Flags = DockFlags::None;
};

// WorkspaceLayer.h:102-107
struct DockedPanelRequest
{
    std::string WindowName;     // Must match the ImGui::Begin("...") call in the client
    ImGuiDir    SplitDir;       // Direction to split from the main viewport area
    float       SplitRatio;     // Fraction of the viewport to give to this panel
};
```

**What it does** — `DockBinding` is the record [`DockWindow`](#workspacelayerdockwindow) stores; you
never construct one. `DockedPanelRequest` is the argument to
[`RequestExtraDockedPanel`](#workspacelayerrequestextradockedpanel), and you do — aggregate
initialisation is the idiom.

**Notes & pitfalls**

- `DockedPanelRequest` has **no default member initialisers for `SplitDir` and `SplitRatio`** —
  brace-initialise all three fields (`{"Timeline", ImGuiDir_Down, 0.25f}`) or you get indeterminate
  values.
- `SplitRatio` is passed straight to `ImGui::DockBuilderSplitNode` with **no clamp**, unlike the edge
  ratios.

---

## Never persist a dock-node id

**Do not capture an `ImGuiID` from the dock builder and reuse it on a later frame.** This is the one
hard rule of the docking model, and it is a *structural* consequence of how the shell rebuilds:

```cpp
// WorkspaceLayer.cpp:383-385 — the first three lines of BuildDockspace
ImGui::DockBuilderRemoveNode(dockspaceId);
ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);
```

Every node id produced by the previous build is invalidated the moment a rebuild runs. A rebuild is
queued by **eleven** distinct calls, more than anyone expects:

| Trigger | Where |
| --- | --- |
| `DockWindow(...)` (new binding **or** re-binding an existing name) | `.h:181-187` |
| `ClearDockWindows()` | `.h:190-194` |
| `RequestExtraDockedPanel(...)` | `.h:159-163` |
| `SetEdgeRatios(...)` | `.h:222-226` |
| `SetEdgeMinPixels(...)` | `.h:232-236` |
| `SetViewportVisible(...)` — only on an actual change | `.h:132-137` |
| `ResetLayout()` | `.h:343` |
| **View ▸ Reset Layout** menu item | `.cpp:302-303` |
| the teardown handshake (`RequestLayoutReset` → `DockBuilderRemoveNode`) | `.cpp:124-131` |

A stored id therefore names a node that no longer exists, and ImGui will happily dock a window into
nothing — the symptom is a layout that "looked right, then collapsed" after a reset or a screen
change. **Bind by window name and let the shell own the ids.** For a position the fixed ports do not
reach, use [`RequestExtraDockedPanel`](#workspacelayerrequestextradockedpanel): it is re-applied on
every rebuild, which is exactly what a captured id is not.

The one place the engine reads a node id back is internal and **same-frame**: applying
`DockFlags::NoTabBar` through `DockBuilderGetNode` inside the same build pass
(`WorkspaceLayer.cpp:493-501`).

> **The `imgui.ini` file is not an exception.** It persists the *user's* arrangement, keyed by window
> name, and the default `SetApplyCodedLayoutOnLoad(true)` re-runs the coded layout over it on every
> load anyway. Node ids are never the persistence key.

---

## `WorkspaceLayer`

```cpp
// Cosmic/src/layers/WorkspaceLayer.h:109 — note: NO COSMIC_API
class WorkspaceLayer : public Cosmic::Layer
```

The editor shell: a full-screen transparent host window with a custom menu/title bar, an ImGui
dockspace, and a central `Viewport` panel that displays the application framebuffer. The engine
creates and owns exactly one; `Application::GetWorkspaceLayer()` hands out the pointer, which may
be **null**. Read [Linkage](#linkage-what-actually-links-from-a-project-dll) before you call
anything.

**Declared in** `Cosmic/src/layers/WorkspaceLayer.h` — *not* included by `Cosmic.h`; include it
yourself. Both engine configurations.

### The frame it draws

Per ImGui frame, `OnImGuiRender` runs these steps (`WorkspaceLayer.cpp:119-269`), which is the
context every entry below sits in:

| Step | What happens | Lines |
| --- | --- | --- |
| 0 | teardown handshake, if requested: `DockBuilderRemoveNode` and **return** — nothing else draws this frame | `:124-131` |
| 1 | host window `##CosmicWorkspace` at the main viewport, height reduced by the bottom inset, zero padding/rounding/border | `:136-164` |
| 2 | menu bar + custom title bar — **skipped entirely in fullscreen** | `:169-172` |
| 3 | `DockSpace("CosmicDockSpace")`; if a rebuild is queued **and** `m_ApplyCodedLayoutOnLoad`, run `BuildDockspace` | `:177-189` |
| 4 | the `Viewport` panel (or, when hidden, `BlockEvents(true)` and hover/focus forced false) | `:197-234` |
| 4.5 | the floating theme selector, if shown | `:240-246` |
| 5 | the client layer's `OnImGuiRender()` — or a `"Project Inspector"` placeholder when no project is mounted | `:256-268` |

Because the client draws in **step 5**, anything the shell wrote in step 4 (notably
[`BlockEvents`](#imguilayerblockevents)) has already happened by the time your code runs.

### `WorkspaceLayer::DockWindow`

```cpp
void DockWindow(const std::string& windowName, DockPort port, DockFlags flags = DockFlags::None)
```

**What it does** — binds an ImGui window *name* to a [`DockPort`](#the-docking-model) and queues a
dockspace rebuild for the next frame. Re-binding a name that is already bound **updates it in
place** (port and flags) rather than adding a duplicate (`WorkspaceLayer.h:183-186`).

**Why you'd use it** — this is the preferred way to place a panel. Reach for
[`RequestExtraDockedPanel`](#workspacelayerrequestextradockedpanel) only for a split position the 13
ports cannot express.

**Example** — `FrontierApp::ApplyDockLayout`, `FrontierApp.cpp:265-282`, verbatim in shape:

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
if (!ws) return;

ws->ClearDockWindows();
ws->SetViewportVisible(true);
ws->SetEdgeRatios(0.18f, 0.20f, 0.16f, 0.20f);          // left, right, top, bottom
ws->DockWindow("Frontier",       Cosmic::DockPort::LeftTop);
ws->DockWindow("World Settings", Cosmic::DockPort::LeftBottom);
ws->DockWindow("GPU Profiler",   Cosmic::DockPort::RightBottom);
```

**Notes & pitfalls**

- **Inline in the header** (`.h:181`) — links from a project DLL.
- **The name must match your `ImGui::Begin("…")` exactly**, including case and any `###` id suffix.
  A mismatch produces a floating window and **no diagnostic of any kind**.
- Registering **at least one** binding is what switches `BuildDockspace` out of its legacy branch —
  see [the legacy path](#the-legacy-project-inspector-path).
- Bindings are additive and survive until `ClearDockWindows()`. Rebinding per screen is the intended
  pattern: `SF_Telem.cpp:207-272` and `Starforge/LayoutPresets.cpp:82-142` both rebuild the whole set
  on every screen change.
- Never store the resulting node id — see [Never persist a dock-node id](#never-persist-a-dock-node-id).

**See also** — [`ClearDockWindows`](#workspacelayercleardockwindows),
[`DockFlags`](#enum-class-dockflags),
[`../guide/editor-ui-and-theming.md`](../guide/editor-ui-and-theming.md#dock-a-panel-into-a-port)

### `WorkspaceLayer::ClearDockWindows`

```cpp
void ClearDockWindows()
```

**What it does** — drops **all** port bindings and queues a rebuild (`WorkspaceLayer.h:190-194`).

**Why you'd use it** — before re-registering a different screen's layout, so stale panels do not
keep their slots.

**Notes & pitfalls**

- Inline; links from a project DLL.
- It does **not** clear `RequestExtraDockedPanel` requests — those accumulate for the lifetime of the
  layer and there is **no API to clear them**.
- Clearing back to **zero** bindings puts the next rebuild on the [legacy
  path](#the-legacy-project-inspector-path). If you call `ClearDockWindows()` and then bind nothing,
  you get the fixed 22 % three-tier sidebar, not an empty dockspace.

### `WorkspaceLayer::RequestExtraDockedPanel`

```cpp
void RequestExtraDockedPanel(const DockedPanelRequest& request)
```

**What it does** — appends a custom split request and queues a rebuild
(`WorkspaceLayer.h:159-163`). During the build, each request splits off the **remaining central
node** in registration order, with successive requests chaining off each other
(`WorkspaceLayer.cpp:504-510`).

**Why you'd use it** — the escape hatch for a position the fixed ports do not cover.

**Example**

```cpp
void ShowcaseProject::OnAttach()
{
    if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
        ws->RequestExtraDockedPanel({ "Timeline", ImGuiDir_Down, 0.25f });
}
```

**Notes & pitfalls**

- Inline; links from a project DLL.
- **Requests are never removed.** The vector only grows; there is no `ClearExtraDockedPanels`. Call
  it once, from `OnAttach`, and not in a per-screen rebuild.
- Applied **after** the ports, off the central node — so it eats into the viewport area, not into an
  edge.
- On the [legacy path](#the-legacy-project-inspector-path) the three magic names are intercepted and
  routed to the sidebar tiers instead of splitting (`WorkspaceLayer.cpp:410-412`).
- `SplitRatio` is unclamped; a value ≥ 1 or ≤ 0 is passed to ImGui as-is.

### `WorkspaceLayer::SetEdgeRatios`

```cpp
void SetEdgeRatios(float left, float right, float top, float bottom)
```

**What it does** — sets each edge's size as a fraction of the dockspace and queues a rebuild.
Defaults are `left 0.20`, `right 0.20`, `top 0.18`, `bottom 0.22` (`WorkspaceLayer.h:391-394`).

**Notes & pitfalls**

- Inline; links from a project DLL.
- **The effective ratio is clamped to `[0.05, 0.9]`** at build time
  (`WorkspaceLayer.cpp:452`) — a ratio of `0.0` or `1.5` does not error, it silently becomes `0.05`
  or `0.9`.
- The value is combined with the pixel minimum first:
  `max(ratio, minPx × viewport->DpiScale / axisSize)`, *then* clamped (`:447-453`).
- **Mind the argument order** — see the next entry.

### `WorkspaceLayer::SetEdgeMinPixels`

```cpp
void SetEdgeMinPixels(float top, float bottom, float left, float right)
```

**What it does** — sets a per-edge **minimum** size in DPI-independent pixels, so a docked
menu-plus-toolbar row cannot clip under a small ratio on a large monitor. `0` (the default for all
four) means "ratio only". Queues a rebuild.

> **The two edge setters take their edges in DIFFERENT ORDERS.**
> `SetEdgeRatios(`**`left, right, top, bottom`**`)` · `SetEdgeMinPixels(`**`top, bottom, left, right`**`)`
> Nothing warns you — both take four `float`s. Name the arguments in a comment, as Starforge does.

**Example** — `Starforge/LayoutPresets.cpp:61-65`, verbatim:

```cpp
ws->SetEdgeMinPixels(/*top*/ 78.0f, /*bottom*/ 0.0f, /*left*/ 0.0f, /*right*/ 0.0f);
ws->DockWindow("Starforge", Cosmic::DockPort::TopCenter, Cosmic::DockFlags::NoTabBar);
```

**Notes & pitfalls**

- Inline; links from a project DLL.
- **The engine multiplies by `viewport->DpiScale` for you** (`WorkspaceLayer.cpp:446-452`, falling
  back to `1.0` when `DpiScale <= 0`), so pass an **unscaled** number. This is the opposite of
  [`SetBottomInsetPixels`](#workspacelayersetbottominsetpixels--getbottominsetpixels), which does not.
- The minimum is applied only when `minPx > 0` **and** the axis is longer than 1 px, and the result
  is still subject to the `[0.05, 0.9]` clamp — so on a small window a large minimum caps at 90 %.

### `WorkspaceLayer::SetBottomInsetPixels` / `GetBottomInsetPixels`

```cpp
void  SetBottomInsetPixels(float px) { m_BottomInsetPx = px < 0.0f ? 0.0f : px; }
float GetBottomInsetPixels() const   { return m_BottomInsetPx; }
```

**What it does** — reserves a horizontal band of `px` at the **bottom of the OS window, below the
dockspace**. The host window shrinks by that much (`hostSize.y = max(1.0f, viewport->Size.y - px)`,
`WorkspaceLayer.cpp:139-140`) so docked panels can never underlap it. **The engine reserves the
space; you own the drawing.**

**Why you'd use it** — status bars. It is the only way to put content below the dockspace without
it being covered.

**Example** — the shape of `StarforgeApp::DrawStatusBar` (`:2397-2425`):

```cpp
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
```

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- **This is the one setter that does NOT queue a dock rebuild.** The host window is simply drawn
  shorter next frame. That is precisely why calling it every frame — as the example does — is correct
  and cheap, where doing the same with `SetEdgeRatios` would rebuild the layout every frame.
- **Negative input is clamped to `0`**; there is no warning.
- `0` (the default) restores the historical full-height host, byte-identical for any app that never
  calls this. Release the band with `SetBottomInsetPixels(0.0f)` on screens that have no strip.
- **No DPI multiply here** — derive the height from `ImGui::GetFrameHeight()`, which already carries
  the DPI-scaled font size. (`SetEdgeMinPixels` is the opposite: it scales for you.)

### `WorkspaceLayer::SetViewportVisible` / `IsViewportVisible`

```cpp
void SetViewportVisible(bool visible)
bool IsViewportVisible() const { return m_ShowViewport; }
```

**What it does** — shows or hides the central `Viewport` panel. When hidden, the panel is neither
drawn nor docked, leaving the central node for whatever the client bound to `DockPort::Center`.
Queues a rebuild — but **only when the value actually changes** (`WorkspaceLayer.h:132-137`).

**Why you'd use it** — a homescreen or a tool-only screen with no 3D scene to show.
`FrontierApp.cpp:270-273` hides it for the homescreen and docks a single `"Home"` window to
`Center`.

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- **Hiding the viewport forces `BlockEvents(true)` every frame** and pins `IsViewportHovered()` /
  `IsViewportFocused()` to `false` (`WorkspaceLayer.cpp:229-234`). Any input gate built on hover
  goes permanently closed.
- [`BeginViewportOverlay`](#workspacelayerbeginviewportoverlay) returns `false` while hidden.
- The framebuffer is still resized and cleared by `OnUpdate` regardless of visibility
  (`WorkspaceLayer.cpp:73-104`); hiding the panel stops the *display*, not the render.

### `WorkspaceLayer::GetViewportPos` / `GetViewportSize`

```cpp
glm::vec2 GetViewportPos()  const { return m_ViewportPos; }
glm::vec2 GetViewportSize() const { return m_ViewportSize; }
```

**What it does** — the rectangle of the rendered image *content* inside the `Viewport` panel, in
**ImGui SCREEN pixels** (OS virtual-desktop coordinates). `m_ViewportPos` is
`ImGui::GetCursorScreenPos()` captured just below the tab bar; `m_ViewportSize` is
`ImGui::GetContentRegionAvail()`, and is only updated when both components are positive
(`WorkspaceLayer.cpp:213-219`).

**Why you'd use it** — to map the mouse into viewport space for picking, gizmos and zoom-to-cursor.
Compare against `Input::GetMouseScreenPosition()`, **not** `Input::GetMousePosition()`.

**Notes & pitfalls**

- Both inline; both link from a project DLL. `Application::GetViewportPos/Size` forward to them and
  return `{0,0}` when there is no workspace layer (`Application.cpp:468-476`).
- **`Application.h:93`'s comment calling these "GLFW window-space pixels" is WRONG.**
  `WorkspaceLayer.h:271-278` states it correctly: multi-viewport is enabled, so every ImGui rect is
  in desktop coordinates. Window-relative and screen coordinates agree only when the window sits at
  the desktop origin (e.g. borderless maximized) — which is why this bug reproduces on one machine
  and not another. This is a known recurring source of picking bugs; the stale comment is logged as a
  Phase 30 candidate under [core.md](core.md).
- Both are refreshed during **step 4** of the ImGui frame, so a read in `OnUpdate` is one frame
  stale. Read them in `OnImGuiRender` when a frame of lag matters.
- Zero-sized while the viewport is hidden or the window is minimized — guard division.

**See also** — [cameras.md](cameras.md) (the controller contract, rule 3),
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md#the-mouse-contract)

### `WorkspaceLayer::IsViewportHovered` / `IsViewportFocused`

```cpp
bool IsViewportHovered() const { return m_ViewportHovered; }
bool IsViewportFocused() const { return m_ViewportFocused; }
```

**What it does** — the `Viewport` panel's hover/focus state, refreshed each ImGui frame from
`ImGui::IsWindowHovered()` / `IsWindowFocused()` (`WorkspaceLayer.cpp:207-208`).

**Why you'd use it** — to gate **polled** input. The event path is already gated by
[`ImGuiLayer::BlockEvents`](#imguilayerblockevents); polling is not. A camera rig should orbit only
while the viewport is under the cursor, not while the user drags a slider in a side panel.

**Example**

```cpp
rig.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());
rig.SetControlEnabled(ws->IsViewportHovered() || rig.IsDragging());
rig.OnUpdate(ts);
```

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- **Worth one frame of lag when read in `OnUpdate`** — the header says so (`.h:283-284`), and the
  values are written in step 4 of the *previous* ImGui frame.
- Both are forced `false` while the viewport is hidden (`.cpp:233`).
- `|| IsDragging()` in the example is what lets a drag that started inside the viewport survive the
  cursor leaving it.

### `WorkspaceLayer::BeginViewportOverlay`

```cpp
bool BeginViewportOverlay()
{
    if (!m_ShowViewport)
        return false;
    ImGui::Begin("Viewport");   // appends to this frame's existing window
    m_OverlayOpen = true;
    return true;
}
```

**What it does** — re-`Begin`s the `Viewport` window so subsequent ImGui calls **append** to it,
putting your content on top of the rendered image with the draw list clipped to the panel. Returns
`false` and pushes **nothing** when the viewport is hidden.

**Why you'd use it** — transform gizmos, nav cubes, HUD chips, drag-and-drop targets. It exists so
client code never hard-codes the viewport window's identity, and so `Cosmic::Gizmo` gets the host
window its hover logic requires (see `graphics/Gizmo.h`'s FRAME PROTOCOL).

**Example** — the shape of `Engine3DDemo::DrawViewportOverlay` (`:1417-1463`):

```cpp
void MyProject::OnImGuiRender()
{
    auto& app = Cosmic::Application::Get();
    auto* ws  = app.GetWorkspaceLayer();
    if (!ws) return;

    if (ws->BeginViewportOverlay())
    {
        const glm::vec2 p = app.GetViewportPos();
        const glm::vec2 s = app.GetViewportSize();
        Cosmic::Gizmo::SetRect(p.x, p.y, s.x, s.y);
        // ... Gizmo::Manipulate, ImGui::SetCursorScreenPos + ImGui::Image, widgets ...
    }
    ws->EndViewportOverlay();   // ALWAYS pair — unconditionally
}
```

**Notes & pitfalls**

- Inline; links from a project DLL — the header calls this out explicitly (`.h:311-312`).
- **Call it from `OnImGuiRender` only.** The client runs in step 5, after the shell has created the
  window in step 4; from `OnUpdate` there is no window to append to and you would create a *new*
  floating one named "Viewport".
- It hard-codes `ImGui::Begin("Viewport")`, which matches the shell's `"Title###Viewport"` idiom —
  `ImHashStr` resets at `###`, so both resolve to `hash("Viewport")` no matter what
  [`SetViewportTitle`](#workspacelayersetviewporttitle--getviewporttitle) was set to.
- **Never pair it with a bare `ImGui::End()`** — use `EndViewportOverlay`, which tracks the pairing
  guard.

### `WorkspaceLayer::EndViewportOverlay`

```cpp
void EndViewportOverlay()
{
    if (m_OverlayOpen)
    {
        ImGui::End();
        m_OverlayOpen = false;
    }
}
```

**What it does** — closes the overlay if one was opened. **A no-op when nothing was pushed.**

**Notes & pitfalls**

- Inline; links from a project DLL.
- **Call it unconditionally.** Guarding it on `BeginViewportOverlay()`'s return value is the shape
  that leaks: the guard drifts out of sync the moment the code between them grows an early return.
  The engine's own callers do it unconditionally (`StarforgeApp.cpp:1799-1800`).
- The guard is a single `bool`, so overlays **do not nest**. Two `BeginViewportOverlay()` calls in
  one frame push two ImGui windows and one `EndViewportOverlay()` pops one.

> ### ⚠ Style-stack balance inside an overlay — a live crash
>
> ImGui's colour/var stacks must balance within a frame; an imbalance is an `IM_ASSERT` and an
> `abort()` in a Debug build (and silent style corruption in Release). The classic way to get one is
> to push conditionally on a flag, then **pop conditionally on the same flag after a button has
> already flipped it**:
>
> ```cpp
> if (on) ImGui::PushStyleColor(...);       // pushed while `on` was true
> if (ImGui::Button(icon)) on = !on;        // the click flips it
> if (on) ImGui::PopStyleColor();           // WRONG: re-reads the flipped flag
> ```
>
> **Every click on such a chip unbalances the stack by one, in either direction.** Starforge hit
> this on its viewport strip; the `toggle` lambda was fixed in the Phase 29 W7 on-GPU pass by
> latching the pushed state (`Projects/Starforge/src/ViewportController.cpp:1314-1335`, with the bug
> written up in the comment at `:1316-1321`). **The identical pattern still ships two lambdas above
> it** in `snapChip` (`ViewportController.cpp:1285-1293`) — logged below as a Phase 30 candidate.
> Latch the pushed state:
>
> ```cpp
> const bool pushed = on;
> if (pushed) ImGui::PushStyleColor(...);
> if (ImGui::Button(icon)) on = !on;
> if (pushed) ImGui::PopStyleColor();
> ```

### `WorkspaceLayer::ShowThemeSelector` / `IsThemeSelectorVisible`

```cpp
void ShowThemeSelector(bool show, DockPort port = DockPort::RightTop,
                       const char* windowName = "Themes")
bool IsThemeSelectorVisible() const { return m_ShowThemeSelector; }
```

**What it does** — toggles the engine-hosted theme picker: a **floating popout** window that renders
[`UI::ThemeSelector()`](#uithemeselector), sized `240 × 360` on first use
(`WorkspaceLayer.cpp:240-246`). A null or empty `windowName` falls back to `"Themes"`.

> **The `port` parameter is DEAD.** It is stored in `m_ThemeSelectorPort` and read back only to be
> passed to `ShowThemeSelector` again from the View menu (`WorkspaceLayer.cpp:313`). The window is
> **never docked** — step 4.5 draws it as a plain floating `ImGui::Begin` with a close button
> (`:240-246`). The header says so at `.h:208-210`; the *doc comment above it* (`.h:196-207`) still
> describes a dockable panel and is stale. The parameter is kept for source compatibility only.

**Example**

```cpp
ws->ShowThemeSelector(true);                 // port argument omitted — it does nothing
```

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- Off by default; also toggled by the engine's **View ▸ Theme Selector** item, which will not exist
  if you called [`SetChromeMenusVisible(false)`](#workspacelayersetchromemenusvisible--arechromemenusvisible).
- The window's own close button writes straight back to the flag (`&m_ShowThemeSelector` at `:243`),
  so `IsThemeSelectorVisible()` tracks user closes.
- To dock a picker, draw `UI::ThemeSelector()` in your own window and bind that name with
  `DockWindow`.

### `WorkspaceLayer::SetChromeMenusVisible` / `AreChromeMenusVisible`

```cpp
void SetChromeMenusVisible(bool visible) { m_ShowChromeMenus = visible; }
bool AreChromeMenusVisible() const       { return m_ShowChromeMenus; }
```

**What it does** — shows or hides the engine's **File** and **View** menus in the host title bar.
Default `true`.

**Why you'd use it** — an app that supplies its own menu bar (Starforge) hides these so the user
does not see two *File* menus.

**Notes & pitfalls**

- Both inline; both link from a project DLL. No rebuild is queued — the menu bar is redrawn every
  frame anyway.
- **Only the two menus are hidden.** The centred project name, `UI::WindowControls()`
  (minimize/maximize/close) and the title-bar drag region are all drawn unconditionally
  (`WorkspaceLayer.cpp:319-353`).
- **Restore `true` on project exit** — the Launcher relies on the chrome menus, and hiding them also
  removes the only built-in route to **Reset Layout**, **Show Viewport** and **Theme Selector**.
- In **fullscreen** the entire menu bar is skipped regardless of this flag
  (`WorkspaceLayer.cpp:147`, `:157-158`, `:169-172`).

### `WorkspaceLayer::SetViewportTitle` / `GetViewportTitle`

```cpp
void SetViewportTitle(const std::string& title) { m_ViewportTitle = title.empty() ? "Viewport" : title; }
const std::string& GetViewportTitle() const     { return m_ViewportTitle; }
```

**What it does** — changes the **displayed** tab text of the central panel. The window is begun as
`m_ViewportTitle + "###Viewport"` (`WorkspaceLayer.cpp:204-205`), so the ImGui **id stays**
`hash("Viewport")` — renaming per scene never resets the dock layout, and
[`BeginViewportOverlay`](#workspacelayerbeginviewportoverlay)'s `Begin("Viewport")` still appends to
the same window.

**Example**

```cpp
ws->SetViewportTitle("Main.cscene *");    // dirty-marker in the tab, layout untouched
```

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- **An empty string resets to `"Viewport"`**, it does not blank the tab.
- A `###` sequence inside your title would break the idiom — don't put one there.
- The dock-builder calls still use the literal `"Viewport"` (`.cpp:405`, `:467`), which is correct
  precisely because the id is title-independent.

### `WorkspaceLayer::SetApplyCodedLayoutOnLoad` / `GetApplyCodedLayoutOnLoad`

```cpp
void SetApplyCodedLayoutOnLoad(bool enable) { m_ApplyCodedLayoutOnLoad = enable; }
bool GetApplyCodedLayoutOnLoad() const { return m_ApplyCodedLayoutOnLoad; }
```

**What it does** — when `true` (the default), a queued rebuild runs `BuildDockspace` and re-applies
the client-coded layout. When `false`, the rebuild flag is consumed and **nothing is built**
(`WorkspaceLayer.cpp:181-189`).

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- **Only the coded-layout path is wired today.** The header calls `false` a "future" mode
  (`.h:263-269`) and the `.cpp` has a comment where the restore-from-`imgui.ini` branch would go
  (`:186-188`). Setting `false` today does not restore a saved arrangement — it just skips the
  build, leaving whatever ImGui already had. Treat it as a hook, not a feature.

### `WorkspaceLayer::SetProjectName` / `GetProjectName`

```cpp
void SetProjectName(const std::string& name) { m_ProjectName = name; }
const std::string& GetProjectName() const { return m_ProjectName; }
```

**What it does** — sets the human-readable caption drawn centred in the title bar as
`  [ Name ]  ` in green (`WorkspaceLayer.cpp:319-334`). Default `"Untitled Project"`.

**Notes & pitfalls**

- Both inline; both link from a project DLL.
- `Application` sets this after a DLL load; `PlayerLayer` sets it from the packaged app's manifest
  title (`PlayerLayer.cpp:97-98`).
- **It is only centred if there is room** — when the menus are wide the caption is appended after
  them instead (`:329-330`).
- This is the *in-window* caption. The OS/taskbar title is `Window::SetTitle` — see
  [core.md](core.md).

### `WorkspaceLayer::ResetLayout` / `RequestLayoutReset` / `IsReadyForDeletion`

```cpp
void ResetLayout() { m_DockspaceInitialized = false; }
void RequestLayoutReset()
bool IsReadyForDeletion() const { return m_TeardownComplete; }
```

**What it does** — two very different resets with confusingly similar names:

| Call | Effect |
| --- | --- |
| **`ResetLayout()`** | *soft*: queues a `BuildDockspace` next frame. No destruction. Same as the View ▸ Reset Layout menu item. |
| **`RequestLayoutReset()`** | *full teardown handshake*: sets `m_PendingTeardown`, so the next `OnImGuiRender` calls `DockBuilderRemoveNode` and **returns before drawing anything else**, then flips `m_TeardownComplete` (`.cpp:124-131`). Used by the engine when returning to the Launcher. |
| **`IsReadyForDeletion()`** | polls that handshake — `true` once the teardown frame has run. |

**Notes & pitfalls**

- All three inline; all three link from a project DLL — but `RequestLayoutReset` and
  `IsReadyForDeletion` are the **engine's** handshake. Calling `RequestLayoutReset()` from a project
  costs you a completely blank frame (no menus, no viewport, no client panels) and leaves the
  dockspace node removed. Use `ResetLayout()`.
- After a teardown the flag is not re-armed; the shell is expected to be destroyed.

### `WorkspaceLayer::HasViewportLayer`, `SetViewportLayer`, `ClearViewportLayer`

```cpp
void SetViewportLayer(Cosmic::Layer* layer);              // .cpp — DOES NOT LINK from a project DLL
void ClearViewportLayer();                                // .cpp — DOES NOT LINK from a project DLL
inline bool HasViewportLayer() const { return m_ClientViewportLayer != nullptr; }
```

**What it does** — `SetViewportLayer` mounts a client layer into the shell: it **evicts** any
previous one by calling its `OnDetach()`, stores the new pointer, and calls the new layer's
`OnAttach()` (`WorkspaceLayer.cpp:39-56`). `ClearViewportLayer` detaches and nulls it (`:58-67`).

**Why you'd use it** — you don't. `Application::LoadProjectDLL` calls `SetViewportLayer` with the
layer your `CreatePluginLayer()` returned; `OnDetach` calls `ClearViewportLayer`.

**Notes & pitfalls**

- **`SetViewportLayer` and `ClearViewportLayer` are engine-internal and will NOT link from a project
  DLL** — `WorkspaceLayer` carries no `COSMIC_API` and these are defined in the `.cpp`. Only
  `HasViewportLayer` (inline) is reachable.
- **A layer mounted this way is never on the LayerStack.** The shell forwards every hook by hand:
  `OnUpdate` (with the layer's own `GetTimeScale()` applied, `.cpp:96-100`), `OnFixedUpdate`
  (`:106-113`), `OnImGuiRender` (`:256-259`) and `OnEvent` (`:521-527`, short-circuiting on
  `e.Handled`). **`OnRender()` is declared on `Layer` but is never called by anything.**
- Eviction logs at core-**warn** level, which is why swapping projects prints "Evicting previous
  client layer" — that is informational, not an error.

**See also** — `Layer` and `Application` in [core.md](core.md), **DG-2** in README §30

### The legacy `Project Inspector` path

**When a project registers ZERO `DockWindow` bindings, `BuildDockspace` takes a completely different
branch** (`WorkspaceLayer.cpp:392-422`): a fixed **22 %**-wide left column split three ways (bottom
33 %, then the remainder halved), docking the magic window names

| Magic name | Slot |
| --- | --- |
| `"Project Inspector Top"` | left column, upper |
| `"Project Inspector Mid"` | left column, middle |
| `"Project Inspector Bottom"` | left column, lower |
| `"Viewport"` | the rest, when visible |

`SetEdgeRatios`, `SetEdgeMinPixels` and `DockFlags` are **all ignored** on this branch; the ratios
are hard-coded. `RequestExtraDockedPanel` still works, and requests naming one of the three magic
strings are routed to the matching tier instead of splitting (`:410-412`). The log line is
`WorkspaceLayer: Dockspace built (legacy 3-tier left sidebar).`

**This path is not dead.** `PlayerLayer` registers **no** dock bindings — it touches the workspace
only to call `SetProjectName` (`PlayerLayer.cpp:97-98`) — so **every packaged, player-driven app
builds this legacy layout**, complete with three sidebar slots whose windows it never opens.

> **Correction to a claim in the guide tier.** D47 recorded that the *template project* relies on the
> legacy path. It does not: `TemplateProject.cpp:96-104` registers **four** explicit bindings
> (including the two historical names, bound to `LeftTop`/`LeftMiddle`), so the template runs in port
> mode like everything else. `PlayerLayer` is the real inhabitant of the legacy branch.

Bare `"Project Inspector"` — no suffix — is **not** one of the magic names. That is the shell's own
placeholder window, drawn in step 5 when no client layer is mounted (`WorkspaceLayer.cpp:262-268`).

**For new code, always bind.** The magic names exist for compatibility only.

---

## `Theme`

```cpp
// ui/Theme.h:58-65
struct Theme
{
    std::string name;
    ImVec4      accent  = ImVec4(0.14f, 0.65f, 0.35f, 1.00f);
    ImVec4      colors[ImGuiCol_COUNT] = {};
    ThemeStyle  style;
    bool        builtIn = false;
};
```

**What it does** — a complete, data-driven description of an ImGui look. Because every theme carries
the **whole** `ImGuiCol_` table rather than a subset, applying one is deterministic: switching at
runtime fully replaces the previous look, with no stale colours left behind.

| Field | Meaning |
| --- | --- |
| `name` | the **stable identity** used by `Apply()`, `Find()` and `Register()`. Two themes with the same name are the same theme. |
| `accent` | a semantic highlight reused by [`UI` widgets](#ui-widgets) and by the ImPlot sync. Not an `ImGuiCol_` index. |
| `colors` | the full `ImGuiCol_` table, **zero-initialised** by default — a `Theme` you build by hand without calling [`SeedDark`/`SeedLight`](#seeddark--seedlight) is fully transparent black. |
| `style` | the structural knobs, below. |
| `builtIn` | set `true` by the eleven builders; `false` for anything captured, loaded or client-registered. Advisory — nothing in the engine gates on it. |

```cpp
// ui/Theme.h:30-51
struct ThemeStyle
{
    float  WindowRounding = 5.0f;   ChildRounding = 0.0f;   FrameRounding = 4.0f;
    float  PopupRounding  = 4.0f;   ScrollbarRounding = 9.0f;
    float  GrabRounding   = 3.0f;   TabRounding   = 4.0f;
    float  WindowBorderSize = 1.0f; FrameBorderSize = 0.0f; ChildBorderSize = 1.0f;
    float  ScrollbarSize  = 14.0f;  GrabMinSize   = 12.0f;
    ImVec2 WindowPadding    = ImVec2(8.0f, 8.0f);
    ImVec2 FramePadding     = ImVec2(4.0f, 3.0f);
    ImVec2 ItemSpacing      = ImVec2(6.0f, 4.0f);
    ImVec2 ItemInnerSpacing = ImVec2(4.0f, 4.0f);
};
```

*(Reformatted above for width; the header declares one member per line at `Theme.h:32-50`. The
defaults are verbatim.)*

**Notes & pitfalls**

- **`ThemeStyle` is a 16-field subset of `ImGuiStyle`, not a mirror of it.** Anything not listed —
  `IndentSpacing`, `CellPadding`, `WindowMenuButtonPosition`, alpha, and the rest — is **never
  written** by `ThemeManager::Apply` and **never captured** by `CaptureCurrentStyle`
  (`ThemeManager.cpp:52-70`, `:161-176`). If you set one of those globally it survives every theme
  change; if you were relying on a theme to restore it, it will not.
- Neither struct is `COSMIC_API`-marked, but both are plain aggregates defined entirely in the
  header, so they cross the DLL boundary by value without an export.
- `sizeof(Theme)` is dominated by `ImGuiCol_COUNT` × 16 bytes. Pass by `const&`; `ThemeManager::All()`
  returns a reference for this reason.

---

## `ThemeManager`

```cpp
// Cosmic/src/ui/ThemeManager.h:33
class COSMIC_API ThemeManager
```

The engine's runtime theme registry: the list of available themes plus the currently applied one.
**All members are `static`; the storage lives in `ThemeManager.cpp`'s anonymous namespace**, so the
engine and every client project share exactly one registry across the DLL boundary — a theme your
app registers shows up in the engine's picker, and vice versa. `COSMIC_API`-exported; every member
links from a project DLL.

**Declared in** `Cosmic/src/ui/ThemeManager.h` · both engine configurations.

### `ThemeManager::Init`

```cpp
static void Init();
```

**What it does** — registers the eleven built-ins from
[`GetBuiltInThemes()`](#getbuiltinthemes) and logs the count (`ThemeManager.cpp:81-95`).

**Notes & pitfalls**

- **Idempotent** — an unconditional early return after the first call (`:83-84`). Every entry point
  (`ImGuiLayer::SetTheme`, `Cosmic::SetImGuiTheme`) calls it first, which is what makes them safe
  before the ImGui layer has attached.
- It does **not** load project themes. `Init` runs at ImGui-layer attach, *before* any project is
  mounted, so `project://` cannot resolve yet — `Application::LoadProjectDLL` calls
  `ThemeManager::LoadFolder(FileSystem::Resolve("project://themes"))` from the Safe Zone
  (`Application.cpp:762`).
- Needs **no** ImGui context: `SeedDark`/`SeedLight` construct their own `ImGuiStyle`.

### `ThemeManager::Register`

```cpp
static void Register(const Theme& theme);
```

**What it does** — adds a theme, or **replaces an existing one with the same name in place**,
preserving list order (`ThemeManager.cpp:97-108`).

**Why you'd use it** — to publish a client- or editor-authored theme so it appears in every picker.

**Example**

```cpp
Cosmic::Theme t = Cosmic::ThemeManager::CaptureCurrentStyle("Studio Amber");
t.accent = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
Cosmic::ThemeManager::Register(t);
```

**Notes & pitfalls**

- Copies the theme by value into the registry; the caller keeps ownership of its own object.
- **Registering does not apply.** Follow with [`Apply(t.name)`](#thememanagerapply).
- Name collisions **silently overwrite**, including over a built-in. There is no diagnostic and no
  way to restore the built-in short of `Register(BuildSleekPro())`.
- **There is no `Unregister`.** Registries are additive across project mounts on purpose.

### `ThemeManager::Apply`

```cpp
static bool Apply(const std::string& name);
```

**What it does** — looks the theme up and applies it, returning `false` if the name is unknown
(`ThemeManager.cpp:127-137`).

**Failure mode** — on an unknown name it logs `ThemeManager: theme '<name>' not found` at
core-**warn** level, changes nothing, and returns **`false`**. This is the *only* call in the theme
API that reports failure to the caller.

**Example**

```cpp
if (!Cosmic::ThemeManager::Apply(saved))
    Cosmic::ThemeManager::Apply("Sleek Pro");   // fall back to the engine default
```

**Notes & pitfalls**

- Requires a live **ImGui** context (it writes `ImGui::GetStyle()`). ImPlot is optional — the sync
  no-ops without a context.
- Takes effect immediately, mid-frame included; ImGui reads the style per widget.

**See also** — [`ApplyTheme`](#thememanagerapplytheme), [`ImGuiLayer::SetTheme`](#imguilayersettheme)

### `ThemeManager::ApplyTheme`

```cpp
static void ApplyTheme(const Theme& theme);
```

**What it does** — applies a `Theme` object directly **without registering it**: writes the 16
`ThemeStyle` fields and all `ImGuiCol_COUNT` colours into the live `ImGuiStyle`, records
`CurrentName()` and `Accent()`, then calls [`UI::ApplyPlotStyle`](#uiapplyplotstyle)
(`ThemeManager.cpp:139-147`).

**Why you'd use it** — **live preview**. A theme editor calls this on every slider drag so a
discarded experiment leaves no trace in the registry. That is exactly what the template's
`TemplateThemeShowcaseLayer` does.

**Notes & pitfalls**

- **Returns nothing and cannot fail** — an all-zero `Theme` is applied faithfully, giving you an
  invisible UI. Seed the colours.
- It writes `CurrentName()` to the object's `name` even though the object is not in the registry, so
  `Find(CurrentName())` can return `nullptr` after a preview.

### `ThemeManager::CurrentName` / `Accent`

```cpp
static const std::string& CurrentName();
static const ImVec4&      Accent();
```

**What it does** — the name of the last-applied theme (**empty before the first apply**) and its
accent colour.

**Why you'd use it** — `Accent()` in your own drawing keeps custom widgets consistent across all
eleven built-ins and anything a user authors. The whole [widget kit](#ui-widgets) reads it.

**Notes & pitfalls**

- Both return references into engine-DLL statics — valid for the process lifetime, but they
  **change** under you on the next apply. Copy the `ImVec4` if you need a stable value.
- `Accent()` starts at the `Theme` default `(0.14, 0.65, 0.35, 1.0)` before any apply
  (`ThemeManager.cpp:23`).

### `ThemeManager::All`

```cpp
static const std::vector<Theme>& All();
```

**What it does** — every registered theme, in **registration order** (built-ins first, in
[`GetBuiltInThemes()`](#getbuiltinthemes) order, then anything registered since).

**Why you'd use it** — to build a picker. `UI::ThemeSelector()` is exactly a loop over this.

**Notes & pitfalls**

- **The reference is invalidated by any later `Register` that appends** (`std::vector` growth). Do
  not hold it across a `Register` call, and do not hold a `Theme*` from it — use
  [`Find`](#thememanagerfind) again.
- Empty before `Init()`.

### `ThemeManager::Find`

```cpp
static const Theme* Find(const std::string& name);
```

**What it does** — exact, case-**sensitive** name lookup.

**Failure mode** — returns **`nullptr`** when the name is unknown. No log.

**Notes & pitfalls**

- Points into the same vector as `All()` and carries the same invalidation rule.
- Linear scan over ~11 entries; fine per frame, but cache the accent rather than the pointer.

### `ThemeManager::CaptureCurrentStyle`

```cpp
static Theme CaptureCurrentStyle(const std::string& name);
```

**What it does** — snapshots the live `ImGuiStyle` into a new `Theme`: the 16 `ThemeStyle` fields,
all `ImGuiCol_COUNT` colours, `builtIn = false`, and `accent` taken from the manager's **current**
accent (`ThemeManager.cpp:153-182`).

**Why you'd use it** — the editor's "save as": whatever the user has tweaked live becomes a named
theme.

**Notes & pitfalls**

- **The accent is NOT captured from the style** — there is no `ImGuiCol_` for it. It is copied from
  `s_Accent`, i.e. from the theme that was last *applied*. If the user edited colours without
  applying, the accent is stale; set `t.accent` yourself afterwards.
- Requires a live ImGui context.
- Does not register the result. `Register` it, then `Apply` or `SaveToFile` it.

### `ThemeManager::SaveToFile`

```cpp
static bool SaveToFile(const Theme& theme, const std::string& resolvedPath);
```

**What it does** — writes the theme as `.ctheme` text, creating parent directories first and
truncating any existing file (`ThemeManager.cpp:188-227`).

> **`resolvedPath` is a REAL DISK PATH.** `SaveToFile`, `LoadFromFile` and `LoadFolder` **do not**
> run the VFS — call `FileSystem::Resolve("project://themes/My Theme.ctheme")` yourself. Passing a
> `project://` URI writes a directory literally named `project:` (or fails to open, depending on the
> platform).

**Failure mode** — returns **`false`** and logs `ThemeManager: could not write '<path>'` at
core-warn level when the stream will not open. `create_directories` failures are swallowed into an
`std::error_code` that is never checked (`:190-192`), so the open failure is the only signal.

**Example**

```cpp
const std::string path =
    Cosmic::FileSystem::Resolve("project://themes/" + t.name + ".ctheme");
if (Cosmic::ThemeManager::SaveToFile(t, path))
    Cosmic::ThemeManager::Apply(t.name);
```

**Notes & pitfalls**

- `builtIn` is **not** written; every loaded theme comes back as non-built-in.
- Colour keys use ImGui's own `GetStyleColorName(i)` strings, so a file written by one ImGui version
  may name colours a later version renamed. Unknown `col.*` keys are ignored on load, not rejected.
- Floats are written with default `ostream` precision (6 significant digits) — a round-trip is not
  bit-exact.

### `ThemeManager::LoadFromFile`

```cpp
static bool LoadFromFile(const std::string& resolvedPath, Theme& out);
```

**What it does** — parses a `.ctheme` into `out`. It **seeds `out` from a complete dark table
first** (`ThemeManager.cpp:237`) and sets `builtIn = false`, so a hand-written file may set only the
keys it cares about. Recognised keys: `name`, `accent`, `style.<field>` (16 of them), and
`col.<ImGuiCol name>`. Lines that are empty, start with `#`, or contain no `=` are skipped.

**Failure mode** — returns **`false` only when the file cannot be opened**. A file that opens is
always a "success": unrecognised keys are ignored, malformed vectors leave the field partly written,
and an absent `name` falls back to the filename stem (`:295-296`).

> **A corrupt numeric value throws.** `style.*` floats are parsed with `std::stof`
> (`ThemeManager.cpp:257`) inside a lambda with **no `try`/`catch` anywhere in the file**. A line
> like `style.FrameRounding=abc` raises `std::invalid_argument` straight out of `LoadFromFile` — and
> out of `LoadFolder`, and therefore out of `Application::LoadProjectDLL`. Logged as a Phase 30
> candidate below.

**Notes & pitfalls**

- `out` is an in/out parameter and is **not** cleared beyond the dark seed — pass a fresh `Theme`.
- Colour matching is exact against `ImGui::GetStyleColorName(i)`; a typo silently leaves the seeded
  value.

### `ThemeManager::LoadFolder`

```cpp
static void LoadFolder(const std::string& resolvedDir);
```

**What it does** — loads and `Register`s every `*.ctheme` in a resolved directory, logging each one
at core-info level (`ThemeManager.cpp:301-324`). Extension matching is case-insensitive.

**Failure mode** — **silent no-op** when the path does not exist or is not a directory (`:305-306`).
Returns `void`; there is no way to learn how many themes loaded except from the log or by diffing
`All()`.

**Notes & pitfalls**

- `resolvedDir` is a real disk path — see the box under [`SaveToFile`](#thememanagersavetofile).
- Iteration order is the filesystem's, so registration order among project themes is not guaranteed.
- Inherits `LoadFromFile`'s uncaught-`std::stof` hazard.
- The engine calls this once per project mount; calling it again is harmless (`Register` replaces
  by name).

### The `.ctheme` format

Line-oriented `key=value` text. Comments start with `#`.

```
# Cosmic theme
name=Studio Amber
accent=1,0.65,0,1
style.WindowRounding=6
style.FramePadding=8,5
col.WindowBg=0.08,0.09,0.1,1
col.Button=0.17,0.19,0.22,1
... one col.* line per ImGuiCol_ index ...
```

| Key | Value |
| --- | --- |
| `name` | the theme's identity (falls back to the filename stem when absent) |
| `accent` | four comma-separated floats, `x,y,z,w` |
| `style.<Field>` | one of the 16 `ThemeStyle` fields; `float` or `x,y` for the four `ImVec2`s |
| `col.<ImGuiColName>` | four floats, keyed by `ImGui::GetStyleColorName` |

---

## `UI::Fonts`

```cpp
// Cosmic/src/ui/Fonts.h:32
class COSMIC_API Fonts
```

The engine's ImGui font registry. Drop any `.ttf`/`.otf` into `Cosmic/assets/fonts` (addressed as
`engine://fonts`) or a project's `project://fonts`, and it is registered under its **file stem**.
The engine bundles `Roboto-Regular`, `Roboto-Medium`, `Roboto-Bold` and `lucide.ttf`.

All members are `static` and the storage lives in the engine DLL on purpose: `ImFont*` and the atlas
belong to the one shared ImGui context, so the registry must be a single instance. `COSMIC_API`;
every member links from a project DLL. **Namespace is `Cosmic::UI`.**

This is ImGui's **atlas-baked** text path. It is unrelated to `Cosmic::Font` / `Renderer2D::DrawString`,
which is world-space SDF text — see [rendering-2d.md](rendering-2d.md).

### `UI::Fonts::Init`

```cpp
static void Init();
```

**What it does**, in order (`Fonts.cpp:120-161`):

1. Idempotent guard — returns immediately on a second call (`:122-123`).
2. Resolves `engine://fonts/lucide.ttf` and records whether it exists (`:129-133`).
3. `io.Fonts->AddFontDefault()` — ImGui's bitmap font stays as `Fonts[0]`, a last-resort fallback
   (`:137`).
4. `LoadFolder(FileSystem::Resolve("engine://fonts"))` (`:142`).
5. Picks the default face: `Roboto-Regular` → else the first custom face → else `io.Fonts->Fonts[0]`
   (`:146-150`).
6. **Assigns `io.FontDefault = s_Default`** (`:156-157`).

**Step 6 is the one people get wrong: the custom face is the GLOBAL DEFAULT, not an opt-in.** Every
panel that pushes nothing renders in Roboto (with merged icons). Before this line the UI rendered in
ImGui's chunky `ProggyClean`, which is what the comment at `:152-155` is about.

**Notes & pitfalls**

- Called by `ImGuiLayer::OnAttach` (`ImGuiLayer.cpp:69`). **Must happen before the first frame** —
  the OpenGL backend bakes the atlas lazily on the first `NewFrame`.
- Cannot see project fonts: it runs before any project is mounted.
- **Never fails.** A missing `engine://fonts` folder or a missing `lucide.ttf` just leaves
  `Available()` / `HasIcons()` false.

### `UI::Fonts::LoadFolder`

```cpp
static void LoadFolder(const std::string& resolvedDir);
```

**What it does** — registers every `.ttf`/`.otf` in a **resolved disk directory** at the base size,
merging the icon glyphs into each face as it goes (`Fonts.cpp:74-118`). Extension matching is
case-insensitive.

**Failure mode** — **silent no-op** for a non-existent or non-directory path (`:78-79`). A face
ImGui refuses to load logs `Fonts: failed to load '<path>'` at core-warn and is skipped; the rest of
the folder still loads.

**Notes & pitfalls**

- **First registration of a stem wins** (`:101-104`), compared case-insensitively. Engine faces load
  before project faces, so a project cannot shadow `Roboto-Bold`.
- **A font whose stem is `lucide` is skipped** as a body face (`:52-55`, `:97`) — it is merged into
  the text faces instead, never offered for selection.
- Each face is added **once**, at `k_BaseSize = 18.0f` (`:34`). ImGui 1.92 renders one `ImFont` at
  any size on demand, so this is only the fallback size.
- Adding faces is only safe **outside a frame**. See [`LoadProjectFonts`](#uifontsloadprojectfonts).

### `UI::Fonts::LoadProjectFonts`

```cpp
static void LoadProjectFonts();
```

**What it does** — `LoadFolder(FileSystem::Resolve("project://fonts"))` for the currently mounted
project (`Fonts.cpp:163-175`).

**Failure mode** — **returns immediately and silently if `Init()` has not run** (`:171-172`).

**Notes & pitfalls**

- Called by the engine from `Application::LoadProjectDLL` (`Application.cpp:763`), in the **Safe
  Zone between frames** — which is the only place adding faces is safe. Do not call it from
  `OnImGuiRender`.
- Idempotent (the stem-dedupe in `LoadFolder`), and loaded faces **stay registered after the project
  unmounts** — dropping them would dangle `ImFont*` handles other systems hold.

### `UI::Fonts::Get`

```cpp
static ImFont* Get(const std::string& name, float sizePx);
```

**What it does** — returns the registered face whose file stem matches `name`, compared
**case-insensitively** (`Fonts.cpp:177-186`).

> **`Get` IGNORES `sizePx` entirely.** The parameter is `float /*sizePx*/` in the definition
> (`Fonts.cpp:177`) — selection is purely by name, because ImGui 1.92 applies the size at *draw*
> time. Pass whatever you like; it changes nothing. The size you actually want goes to
> `ImDrawList::AddText(font, size, …)`, to [`Push`](#uifontspush--pop), or to
> [`UI::Text`](#ui-overlay-helpers).

**Failure mode** — **an unknown name returns the DEFAULT face, not `nullptr`**, and after `Init()`
it never returns null at all. A typo'd stem therefore degrades to plain default text with no
diagnostic — which is exactly why `UI::StatCard` and `UI::SectionHeader` can hard-push
`"Roboto-Medium"` / `"Roboto-Bold"` unconditionally.

**Example**

```cpp
ImFont* bold = Cosmic::UI::Fonts::Get("Roboto-Bold", 0.0f);   // size argument is ignored
ImGui::GetWindowDrawList()->AddText(bold, 26.0f, pos, IM_COL32_WHITE, "12,480");
```

**Notes & pitfalls**

- Before `Init()` it can return `nullptr` (the default is unset). Every caller in the engine tolerates
  that.
- The stem is the **file name**, e.g. `"Roboto-Bold"` — not a family plus a weight.

### `UI::Fonts::Default`

```cpp
static ImFont* Default();
```

**What it does** — the default UI face: the one `Init()` chose, or `ImGui::GetIO().FontDefault` if
none was (`Fonts.cpp:188-191`).

**Failure mode** — can return `nullptr` before `Init()` (both sources are null then). Every
`Overlay.h` helper handles that by falling back to `ImGui::CalcTextSize` / `ImDrawList::AddText`'s
current-font overload.

### `UI::Fonts::Available` / `HasIcons`

```cpp
static bool Available();
static bool HasIcons();
```

**What it does** — `Available()` is true once at least one **custom** face is registered
(`Fonts.cpp:193-196`). `HasIcons()` is true when `engine://fonts/lucide.ttf` was found at `Init()`
and merged into the text faces (`:198-201`).

**Why you'd use it** — `HasIcons()` is what the widget kit tests before drawing a glyph
(`Widgets.cpp:63`, `:139`), so a tree without `lucide.ttf` silently loses the icons and keeps the
text.

**Notes & pitfalls**

- `HasIcons()` records **file existence at `Init()` time**, not merge success. If `AddFontFromFileTTF`
  rejected the icon file, `HasIcons()` still returns `true` and the glyphs render as `?`.
- Both are `false` before `Init()`.

### `UI::Fonts::Push` / `Pop`

```cpp
static void Push(const std::string& name, float sizePx);
static void Pop();
```

**What it does** — `Push` resolves the face by name and calls
`ImGui::PushFont(font, sizePx > 0 ? sizePx : 0)` (`Fonts.cpp:203-207`); `Pop` is
`ImGui::PopFont()`.

**Why you'd use it** — this is where `sizePx` actually matters, unlike in `Get`.

**Example**

```cpp
Cosmic::UI::Fonts::Push("Roboto-Bold", Cosmic::UI::Fonts::SizeHeading);
ImGui::TextUnformatted("Section Title");
Cosmic::UI::Fonts::Pop();
```

**Notes & pitfalls**

- **`Push` always pushes exactly one entry, so it always balances with one `Pop`** — even for an
  unknown name (it falls back to `Default()`, and ImGui 1.92's `PushFont(NULL)` reuses the current
  font, `imgui.cpp:9778-9779`). There is no early-return path that skips the push.
- **`sizePx <= 0` keeps the current size** rather than reverting to 18 px (`imgui.cpp:9784-9785`).
- An unbalanced `Push`/`Pop` is an `IM_ASSERT` and an `abort()` in a Debug build — see the
  [style-stack warning](#workspacelayerendviewportoverlay).

### `UI::Fonts` size constants

```cpp
static constexpr float SizeSmall   = 13.0f;
static constexpr float SizeBody    = 16.0f;
static constexpr float SizeHeading = 22.0f;
static constexpr float SizeBig     = 32.0f;
```

The standard UI type hierarchy (`Fonts.h:73-76`). Any face renders crisp at any of them because
ImGui 1.92 applies size per draw. They are **not** DPI-scaled — ImGui's own global scale handles
that.

---

## `ui/IconsLucide.h`

Auto-generated from `lucide-static`'s `info.json` (Lucide icons, ISC licence). Header-only, all
preprocessor, no namespace:

| Macro | Value | Purpose |
| --- | --- | --- |
| `ICON_LC_<NAME>` | a UTF-8 string literal, e.g. `"\xee\x80\xb8"` | one per icon — **1,986 of them** |
| `ICON_MIN_LC` | `0xe038` | first codepoint, used when merging the atlas range |
| `ICON_MAX_LC` | `0xe715` | last codepoint |
| `FONT_ICON_FILE_NAME_LC` | `"lucide.ttf"` | the file the merge looks for |

**What it does** — each macro expands to the UTF-8 encoding of one private-use codepoint. Because the
icon font is *merged* into every text face at load (`Fonts.cpp:60-71`), those codepoints render
inline in any label, under any pushed face, with no font switch.

**Example**

```cpp
if (ImGui::Button(ICON_LC_ROCKET "  Launch")) { /* ... */ }        // two spaces: house convention
ImGui::Text(ICON_LC_ACTIVITY " %.1f Hz", hz);
Cosmic::UI::SectionHeader(ICON_LC_SLIDERS_HORIZONTAL, "Tuning");
```

**Notes & pitfalls**

- **String-literal concatenation is the whole API** — `ICON_LC_X "  Label"`. There is no function
  form and no lookup-by-name.
- **Finding an icon:** browse [lucide.dev](https://lucide.dev), take the kebab-case name, upper-snake
  it, prefix `ICON_LC_` (`arrow-up-right` → `ICON_LC_ARROW_UP_RIGHT`). Or grep the header:
  `grep -i "ICON_LC_.*GAUGE" Cosmic/src/ui/IconsLucide.h`.
- **Aliases share codepoints.** `ICON_LC_ALARM_CHECK` and `ICON_LC_ALARM_CLOCK_CHECK` are both
  `"\xee\x87\xac"` (`:26-27`). Deprecated Lucide names are kept, so an old name still compiles.
- `ICON_MIN_LC`/`ICON_MAX_LC` bound the *whole* private-use range; the merge requests it as one
  contiguous span (`Fonts.cpp:64`), so unused codepoints in the gap cost atlas lookup, not glyphs.
- With no `lucide.ttf` present the macros still compile and emit the raw bytes, which render as the
  font's missing-glyph box. Test [`Fonts::HasIcons()`](#uifontsavailable--hasicons) before drawing a
  bare glyph.

---

## `UI` widgets

`ui/Widgets.h` — free functions in `namespace Cosmic::UI`, each individually `COSMIC_API`-exported
(the functions carry the macro, not a class), compiled into the engine DLL. They read
[`ThemeManager::Accent()`](#thememanagercurrentname--accent) and the [font
registry](#uifonts), so they restyle automatically on a theme change. Both engine configurations.

All seven require a live ImGui frame; none is safe outside `OnImGuiRender`.

### `UI::StatCard`

```cpp
COSMIC_API void StatCard(const char* id, const char* icon, const char* label,
                         const char* value, const char* sub, const ImVec4& accent,
                         const ImVec2& size = ImVec2(0.0f, 0.0f),
                         const ImVec4& valueColor = ImVec4(0, 0, 0, 0));
```

**What it does** — a framed indication card: a left accent bar, an optional icon plus a small dimmed
label, a large crisp value in `Roboto-Bold` at 26 px, and a dimmed sub-line
(`Widgets.cpp:39-86`).

**Example**

```cpp
Cosmic::UI::StatCard("rpm", ICON_LC_GAUGE, "Weapon RPM", "12,480", "avg 9,120",
                     Cosmic::ThemeManager::Accent());
```

**Notes & pitfalls**

- **Zero or negative `size` components default to `180 × 92`** (`:43-45`); it is not auto-sizing.
- Corner rounding comes from the theme's `ChildRounding`, falling back to `8.0f` when that is `0`
  (`:48`) — so the card stays rounded under `Sleek Pro`, whose `ChildRounding` is 6, and under
  themes that leave it at 0.
- **`valueColor` is used only when `valueColor.w > 0`** (`:74`); the documented `w <= 0` sentinel is
  the default `ImVec4(0,0,0,0)`.
- The icon draws only when `icon` is non-empty **and** `Fonts::HasIcons()` (`:63`).
- `label`, `value` and `sub` tolerate `nullptr` (`:69`, `:77`, `:81`). **`id` does not** — it is
  passed straight to `ImGui::BeginChild` and must be unique per card.

### `UI::ToggleSwitch`

```cpp
COSMIC_API bool ToggleSwitch(const char* label, bool* v);
```

**What it does** — an animated on/off switch, `1.8 ×` frame height wide. Returns **`true` on the
frame it is clicked** (`Widgets.cpp:88-134`).

**Notes & pitfalls**

- **The visible label is the part of `label` before any `##`** (`:126-132`). Pass `"##hidden"` for a
  switch with no text at all.
- `v == nullptr` is tolerated: the widget draws in the off state, and a click still returns `true`
  without writing anything (`:97-98`).
- The knob position is animated through `ImGuiStorage` keyed on `ImGui::GetID(label)` at
  `IO.DeltaTime × 12` per frame (`:102-109`), so **two switches sharing a label id share one
  animation**.
- `label` is also the `InvisibleButton` id — it must be unique within the window.

### `UI::SectionHeader`

```cpp
COSMIC_API void SectionHeader(const char* icon, const char* text);
```

**What it does** — an accent-coloured optional icon, then `text` in `Roboto-Bold` at
`SizeBody + 1`, then spacing, a separator and more spacing (`Widgets.cpp:136-150`).

**Notes & pitfalls**

- The icon draws only when non-empty **and** `Fonts::HasIcons()`. Pass `nullptr` to omit it.
- `text == nullptr` renders an empty heading (still with the separator), it does not crash.
- Pure output — no id, no return value; safe to call repeatedly with identical arguments.

### `UI::IconButton`

```cpp
COSMIC_API bool IconButton(const char* str_id, const char* icon,
                           const char* tooltip = nullptr,
                           const ImVec2& size = ImVec2(0.0f, 0.0f));
```

**What it does** — a compact square button showing just a glyph, with an optional hover tooltip.
Returns `true` on the click frame (`Widgets.cpp:152-168`).

**Notes & pitfalls**

- **`str_id` is what keeps the id unique** when several buttons share a glyph — it is pushed with
  `ImGui::PushID` around the button (`:161-163`), so the *label* is the icon and the *id* is
  `str_id`.
- **A zero or negative component in `size` makes the button a `GetFrameHeight()` square** — note the
  `||`, so passing `ImVec2(40, 0)` gives you a square, not a 40-wide button (`:155-159`).
- `icon == nullptr` renders `"?"` rather than an empty button (`:162`).

### `UI::AccentButton`

```cpp
COSMIC_API bool AccentButton(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));
```

**What it does** — a filled primary-action button tinted with the theme accent, with hover/active
tints derived by lighten/darken 15 % (`Widgets.cpp:170-182`).

**Notes & pitfalls**

- **The label colour flips to near-black automatically on a light accent** — luminance
  `0.299r + 0.587g + 0.114b > 0.6` (`:33-36`, `:173`). You do not need to manage contrast.
- Pushes and pops exactly four style colours; balanced on every path.
- `label` doubles as the ImGui id — use `"Connect##net"` for two buttons with the same text.

### `UI::ThemeSelector`

```cpp
COSMIC_API void ThemeSelector();
```

**What it does** — a full-width row per registered theme (accent swatch + name), applying the
clicked one immediately via `ThemeManager::Apply` (`Widgets.cpp:184-210`). The current theme's row
is drawn selected.

**Why you'd use it** — drop it into any window of yours; or let the engine host it as a floating
popout via [`WorkspaceLayer::ShowThemeSelector`](#workspacelayershowthemeselector--isthemeselectorvisible).

**Notes & pitfalls**

- Iterates `ThemeManager::All()` and calls `Apply` **inside** the loop. `Apply` does not modify the
  registry, so the iteration is safe — but do not copy this shape for anything that registers.
- Rows use a fixed `ImGui::GetFrameHeight()` row height and draw the name into the window draw list,
  so a very long theme name is **not** clipped to the column.
- No scrolling of its own; put it in a child or a sized window.

### `UI::WindowControls`

```cpp
COSMIC_API void WindowControls();
```

**What it does** — right-aligned minimize / maximize-restore / close buttons, already wired to
`Application::Get().GetWindow()` (`Widgets.cpp:212-245`). Flat and square-edged, transparent until
hovered, with a **red** hover on close.

**Why you'd use it** — inside a custom title bar when using borderless chrome. The shell already
draws it in its own menu bar (`WorkspaceLayer.cpp:337`).

**Notes & pitfalls**

- **The maximize glyph flips** between `ICON_LC_SQUARE` and `ICON_LC_COPY` with
  `Window::IsWindowMaximized()` (`:231-233`).
- Right-alignment only happens when the remaining content width exceeds the three buttons' total
  (`:219-221`); in a cramped bar they simply flow inline.
- **`Close()` is not vetoable** — see [core.md](core.md) and
  [`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md#there-is-no-client-veto-on-close).
- Style pushes are balanced across all paths.

---

## `UI::ApplyPlotStyle`

```cpp
// ui/PlotStyle.h:23
COSMIC_API void ApplyPlotStyle(const Theme& theme);
```

**What it does** — syncs the **global** ImPlot style to a Cosmic theme (`PlotStyle.cpp:12-47`):
transparent `FrameBg`, `PlotBg` and `PlotBorder` so charts blend into their panel; `PlotBorderSize =
0`; `MinorAlpha = 0.18`; padding `(8,6)` / label `(5,4)` / legend `(8,8)`; grid and ticks from the
theme's `ImGuiCol_Border` at **30 % alpha**; axis text from `TextDisabled`; legend from `PopupBg` /
`Border` / `Text`; title and inlay text from `Text`; the selection rectangle in the accent at 35 %;
crosshairs in the text colour at 50 %.

**Why you'd use it** — **you usually don't**: `ThemeManager::ApplyTheme` calls it for you
(`ThemeManager.cpp:146`), so charts follow the theme with no work on your side. Call it directly
only when you drive ImPlot with a `Theme` you never registered or applied.

**Failure mode** — **silent no-op when `ImPlot::GetCurrentContext()` is null** (`PlotStyle.cpp:14-15`),
which is what makes it safe to call before ImPlot is up.

**Example**

```cpp
if (const Cosmic::Theme* t = Cosmic::ThemeManager::Find("Neon HUD"))
    Cosmic::UI::ApplyPlotStyle(*t);     // style the charts without changing the UI
```

**Notes & pitfalls**

- **It writes the global `ImPlot::GetStyle()`**, not a per-plot style. There is no scoping; the last
  call wins for every chart in the process.
- **The header's docstring over-promises.** `PlotStyle.h:9-11` claims "a clean line weight", but
  `LineWeight` and `FillAlpha` are **commented out** at `PlotStyle.cpp:19-20` with the note that they
  no longer take effect in the vendored ImPlot. Neither is set. Everything else in the docstring is
  accurate.
- It only ever writes colours and metrics — it never touches ImPlot's *colormap*, so per-series
  colours are unaffected by a theme change.

**See also** — [`ThemeManager::ApplyTheme`](#thememanagerapplytheme),
[serial-telemetry.md](serial-telemetry.md)

---

## `UI` overlay helpers

`ui/Overlay.h` is **header-only and entirely inline** — no `COSMIC_API`, no `.cpp`. It compiles into
whichever module includes it and talks to the shared ImGui context, so it behaves identically in
engine code and in a project DLL. `Cosmic.h:201` includes it, after ImGui, so the types are visible.
Both engine configurations. General-purpose drawing, not tied to any panel.

### `UI::Align`

```cpp
// Overlay.h:40-45
enum class Align
{
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight
};
```

How an anchor point maps onto a drawn element. `Align::Center` means "the given position is the
element's centre", not "centre it in the window".

### `UI::Rect` and `UI::AlignPos`

```cpp
struct Rect
{
    ImVec2 Min{ 0.0f, 0.0f };
    ImVec2 Max{ 0.0f, 0.0f };
    float  Width()  const;
    float  Height() const;
    ImVec2 Size()   const;
    ImVec2 Center() const;
    ImVec2 At(float nx, float ny) const;
};

inline ImVec2 AlignPos(ImVec2 pos, ImVec2 size, Align a);
```

**What it does** — `Rect` is an on-screen rectangle with a **normalized-coordinate lookup**:
`At(nx, ny)` turns image-relative coordinates in `[0,1]` into screen pixels, which is what makes
hand-tuned overlay positions trivial (`0.5, 0.2` = top-centre of the image). `AlignPos` shifts a
position so an element of a given size is anchored there (`Overlay.h:66-82`).

**Notes & pitfalls**

- `At` is a plain lerp with **no clamping** — `At(1.5f, -0.2f)` returns a point outside the rect,
  which is occasionally what you want.
- `Rect` has no validity concept; a default-constructed one is a zero rect at the origin.

### `UI::MeasureText`

```cpp
inline ImVec2 MeasureText(ImFont* font, float sizePx, const char* text);
```

**What it does** — measures `text` in a specific face and size **without pushing it**
(`Overlay.h:85-91`).

**Failure mode** — `font == nullptr` falls back to `Fonts::Default()`; if that is also null it
returns `ImGui::CalcTextSize(text)` in the current font. `sizePx <= 0` uses
`ImGui::GetFontSize()`.

### `UI::ImageFitted`

```cpp
inline Rect ImageFitted(const Ref<Texture2D>& tex, ImVec2 region = ImVec2(0, 0));
```

**What it does** — draws a texture aspect-fitted (letterboxed and centred) into `region`, reserves
the **whole** region in the ImGui layout so following widgets flow below it, and returns the
on-screen image rect (`Overlay.h:97-130`). A `region` component `<= 0` means "use the remaining
content region".

**Failure mode** — **a null `Ref` *or* a texture of zero width/height draws an
`ImGui::Dummy(avail)` and returns the region rect**, not the image rect (`:106-112`). This is
deliberate: `Texture2D::Create` returns a **degraded, non-null 0 × 0 object** on a failed load
rather than `nullptr` (see [graphics-resources.md](graphics-resources.md)), so a `Ref`-only check
would silently sample a black texture.

**Notes & pitfalls**

- **UVs are flipped vertically** — `(0,1)` to `(1,0)` (`:122`) — because the engine loads textures
  bottom-up. Do not re-flip.
- It moves the ImGui cursor twice (to centre the image, then back to reserve the region), so the
  returned rect and the layout cursor disagree by design.
- The returned `Rect` comes from `GetItemRectMin/Max`, i.e. the *actual* drawn image, which is what
  you want for `At()`.

### `UI::Text` / `UI::TextThick`

```cpp
inline void Text(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text,
                 ImFont* font = nullptr, float sizePx = 0.0f,
                 Align align = Align::TopLeft);

inline void TextThick(ImDrawList* dl, ImVec2 pos, ImU32 color, const char* text,
                      ImFont* font = nullptr, float sizePx = 0.0f,
                      float weight = 1.0f, Align align = Align::TopLeft);
```

**What it does** — `Text` is the core reusable text primitive: draw `text` at `pos`, anchored per
`align`, in the given face and size (`Overlay.h:134-145`). `TextThick` is a font-agnostic faux-bold
that redraws at four offsets `±weight` plus the centre — **five draw calls**, not one
(`:149-166`).

**Notes & pitfalls**

- Both return early on a null or empty string; **neither checks `dl`**, so a null `ImDrawList*` is a
  crash.
- `font = nullptr` → `Fonts::Default()`; `sizePx <= 0` → `ImGui::GetFontSize()`.
- **Use `TextThick` only when no real bold face is available.** A bold `ImFont` looks better and
  costs one draw — the header says so at `:147-148`.
- These write to an `ImDrawList` directly and **reserve no layout space**. They do not advance the
  ImGui cursor.

### `UI::ReadoutStyle` / `UI::ReadoutBox`

```cpp
struct ReadoutStyle
{
    ImU32   Fill            = IM_COL32(255, 255, 255, 235);
    ImU32   Border          = IM_COL32(20, 20, 24, 200);
    ImU32   LabelColor      = IM_COL32(70, 75, 90, 255);
    ImU32   ValueColor      = IM_COL32(10, 10, 12, 255);
    float   Rounding        = 6.0f;
    float   BorderThickness = 1.5f;
    ImVec2  Padding         = ImVec2(10.0f, 6.0f);
    float   LabelSize       = 13.0f;
    float   ValueSize       = 26.0f;
    float   Spacing         = 2.0f;
    ImFont* LabelFont       = nullptr;
    ImFont* ValueFont       = nullptr;
    bool    FauxBold        = false;
    Align   Anchor          = Align::Center;
    ImVec2  MinSize         = ImVec2(0.0f, 0.0f);
};

inline Rect ReadoutBox(ImDrawList* dl, ImVec2 posPx, const char* label,
                       const char* value, const ReadoutStyle& s = ReadoutStyle());
```

**What it does** — a framed label-over-value box anchored at `posPx`, auto-sized to its content and
clamped up to `MinSize`. Returns the box rect (`Overlay.h:193-237`). Internally it is one consumer
of [`Text`](#uitext--uitextthick)/`TextThick`, which is the point: the text system stands on its
own.

**Example**

```cpp
Cosmic::UI::Rect r = Cosmic::UI::ImageFitted(m_Diagram);
ImDrawList* dl = ImGui::GetWindowDrawList();

Cosmic::UI::ReadoutStyle s;
s.ValueFont = Cosmic::UI::Fonts::Get("Roboto-Bold", 0.0f);   // size is ignored by Get
Cosmic::UI::ReadoutBox(dl, r.At(0.5f, 0.2f), "RPM", "10445", s);
```

**Notes & pitfalls**

- The default `ReadoutStyle` is a **light** box (white fill, near-black value) designed to sit over
  an image, so it does *not* follow the theme. Override the four colours yourself if you want it to.
- Both `label` and `value` are optional; the box shrinks accordingly and the inter-line `Spacing` is
  applied **only when both are present** (`:205`).
- `FauxBold` routes the value through `TextThick` — set `ValueFont` to a real bold face instead when
  one exists.
- Null fonts fall back to `Fonts::Default()` per field (`:196-197`).

### `UI::ImageWindow`

```cpp
inline void ImageWindow(const char* title, const Ref<Texture2D>& tex, bool* p_open,
                        const char* caption = nullptr,
                        ImVec2 firstSize = ImVec2(460.0f, 720.0f));
```

**What it does** — a resizable floating pop-out showing a texture aspect-fitted, with an optional
wrapped caption below a separator (`Overlay.h:243-277`). The caller owns the open flag; passing
`&flag` is what makes the window's close button work.

**Failure mode** — **returns immediately when `p_open` is null or `*p_open` is false** (`:247`).
A missing texture (null, or width 0 — the degraded-object case again) draws the wrapped text
`"Image not found. Place the file in assets/images and rebuild."` instead of the image (`:267`).

**Notes & pitfalls**

- That fallback string is **hard-coded and project-flavoured** — it names `assets/images` and tells
  the user to rebuild. Nothing in the engine enforces that layout; treat the message as a hint, not a
  contract.
- `firstSize` applies with `ImGuiCond_FirstUseEver`, so the user's resize wins from then on and the
  size is persisted in `imgui.ini` under `title`.
- When the caption would leave under 48 px for the image, the caption's reserved height is dropped
  and the image takes the whole region (`:261-262`).

---

## Failure-mode summary

Every call in this chapter that can fail, and how it tells you.

| Call | On failure |
| --- | --- |
| `Application::GetWorkspaceLayer()` | returns **`nullptr`** before the shell is pushed / after teardown |
| `WorkspaceLayer::DockWindow` (name mismatch) | **nothing** — the window floats, no log |
| `WorkspaceLayer::BeginViewportOverlay` | returns **`false`**, pushes nothing (viewport hidden) |
| `WorkspaceLayer::SetEdgeRatios` / `SetEdgeMinPixels` | out-of-range values are **clamped** to `[0.05, 0.9]`, silently |
| `WorkspaceLayer::SetBottomInsetPixels` | negative is **clamped to `0`**, silently |
| `ImGuiLayer::SetTheme` / `Cosmic::SetImGuiTheme` | **returns nothing**; unknown name logs a core warning, look unchanged |
| `ThemeManager::Apply` | returns **`false`** + core warning |
| `ThemeManager::Find` | returns **`nullptr`**, no log |
| `ThemeManager::SaveToFile` | returns **`false`** + core warning (open failure only) |
| `ThemeManager::LoadFromFile` | returns **`false`** only if the file will not open; **throws `std::invalid_argument`** on a malformed `style.*` number |
| `ThemeManager::LoadFolder` / `Fonts::LoadFolder` | **silent no-op** on a missing directory |
| `Fonts::Get` | returns the **default face**, never `nullptr` after `Init()` |
| `Fonts::Default` | `nullptr` **before** `Init()` |
| `Fonts::LoadProjectFonts` | **silent return** if `Init()` has not run |
| `Fonts::LoadFolder` (bad font file) | core warning per file, folder continues |
| `UI::ApplyPlotStyle` | **silent no-op** without an ImPlot context |
| `UI::ImageFitted` / `UI::ImageWindow` | null **or 0 × 0** texture → `Dummy` / "Image not found" text |
| `UI::Text` / `TextThick` / `ReadoutBox` (null `ImDrawList*`) | **crash** — not guarded |
| `InitializePluginContexts` not called | **crash** on the DLL's first `ImGui::Begin` |

---

## Manifest & coverage notes

**Two skeleton claims are now stale and are struck.** The skeleton reported "two manifest gaps found
by D60" — `layers/ImGuiThemes.h` and `utils/Branding.h` — and separately that `scene/ui/` had "no
manifest row at all". All four rows exist as of the D61 integration pass:
`layers/ImGuiThemes.h` → `ui.md` (README.md:245), `utils/Branding.h` → `assets-io.md` (:219), and
`scene/ui/UiComponents.h` + `scene/ui/UiSystem.h` → `../guide/game-ui.md` (:264-265). The manifest is
complete at **144 rows** against **147 public headers**, and
`powershell -File tests\check_docs_coverage.ps1` exits **0**. No manifest edit was needed for this
chapter.

**Two skeleton checklist rows did not survive contact with the headers:**

- *"`ImGuiLayer` — … image helpers"*: **`ImGuiLayer.h` declares no image helpers.** Its entire public
  surface is the constructor/destructor, three `Layer` overrides, two `SetTheme` statics, `Begin`,
  `End` and `BlockEvents`. The image helpers the row was reaching for are
  [`UI::ImageFitted`](#uiimagefitted) and [`UI::ImageWindow`](#uiimagewindow) in `ui/Overlay.h`.
- *"`WorkspaceLayer` client surface — … legacy magic names … work without bindings"*: true, but the
  row understates it. The legacy branch is not a fallback *alongside* port mode — it **replaces**
  port mode entirely whenever zero bindings are registered, and it ignores `SetEdgeRatios`,
  `SetEdgeMinPixels` and `DockFlags`. See [the legacy path](#the-legacy-project-inspector-path).

**Strict-mode coverage.** The four `COSMIC_API` class/struct names the checker requires for this
chapter — `ImGuiLayer`, `Fonts`, `ThemeManager` and `HostContext` — each have an entry above.
`WorkspaceLayer`, `Theme`, `ThemeStyle`, `ImGuiTheme`, `DockPort`, `DockFlags`, `DockBinding`,
`DockedPanelRequest`, `Align`, `Rect`, `ReadoutStyle` and the eleven exported `UI` free functions
carry no `COSMIC_API` *class/struct* declaration and are therefore invisible to the checker — they
are documented here because the headers are in scope, not because the script asked.

**Nothing was deliberately left out** of the scope headers. The one class of symbol not enumerated
individually is `ui/IconsLucide.h`'s 1,986 `ICON_LC_*` macros, which are described as a family with
a lookup recipe rather than listed.

### Engine defects found while writing this chapter — Phase 30 candidates

1. **`ThemeManager::LoadFromFile` throws on a corrupt `.ctheme`.**
   `Cosmic/src/ui/ThemeManager.cpp:257` parses every `style.*` value with `std::stof` inside a lambda,
   with no `try`/`catch` anywhere in the file. A single bad line (`style.FrameRounding=abc`) raises
   `std::invalid_argument` out of `LoadFromFile` → `LoadFolder` → `Application::LoadProjectDLL`
   (`Application.cpp:762`), i.e. **a hand-edited theme file crashes project mount**. Catch per line
   and warn.
2. **`ViewportController::snapChip` still has the style-stack imbalance that W7 fixed in `toggle`.**
   `Projects/Starforge/src/ViewportController.cpp:1285-1293` pushes on `on`, lets the button flip
   `on`, then pops on the re-read `on`. Every click on a snap chip unbalances ImGui's colour stack by
   one — `IM_ASSERT` + `abort()` in Debug, silent corruption in Release. The fix is the same latch
   already applied two lambdas below at `:1322`. **This means the known crash is only half-fixed**:
   the *view toggles* are safe, the *three snap chips* are not. Reproduces in both engine
   configurations (the code is outside the `COSMIC_2D_ONLY` fence).
3. **`Application.h:93`'s "GLFW window-space" comment.** Already logged by D14 under
   [cameras.md](cameras.md); repeated here because
   [`GetViewportPos`](#workspacelayergetviewportpos--getviewportsize) is this chapter's most likely
   place to be misread. `WorkspaceLayer.h:271-278` is correct.
4. **`WorkspaceLayer.h:196-207`'s `ShowThemeSelector` docstring contradicts the code three lines
   below it.** The block describes a dockable panel ("the client just chooses where it docks"); the
   comment at `:208-210` and the implementation at `.cpp:240-246` both say floating, never docked.
   Delete the stale block.
5. **`PlotStyle.h:9-11` promises "a clean line weight"** that `PlotStyle.cpp:19-20` no longer sets
   (both lines commented out with "apparently doesnt work anymore"). Either restore the styling
   against the vendored ImPlot's current API or drop the claim from the docstring.
6. **`ImGuiLayer::OnMouseButtonPressed` is redundant.** `ImGuiLayer.cpp:158-159` dispatches a handler
   that returns `io.WantCaptureMouse` for an event class already covered by the
   `EventCategoryMouse` line two lines above (`MouseEvent.h:97`). Harmless, but it is a private
   virtual-looking hook that suggests a second gate exists. Delete it or document why it stays.

---

*Changelog:*
*2026-07-26 — D18: chapter written from the headers and sources. Scope expanded per the D61
integration to include `layers/ImGuiThemes.h` and `Cosmic.h`'s `HostContext`. Centrepieces: the
`DockPort` table with the left→right→top→bottom build order, the never-persist-dock-node-ids rule,
and the per-member linkage table for the un-exported `WorkspaceLayer`.*
