# Windowing & the Viewport — Guide

**What this covers:** the `Window` surface a project actually calls — size, title, VSync, the
min/max/restore controls — borderless custom chrome and drawing your own title bar, high-DPI
handling, borderless fullscreen (the built-in `F11` and `SetFullscreenHotkeyOverride`), the
responsive render-while-dragging contract and how it composes with `Pause()`, `Window::SetIcon` /
`ClearIcon` and the drop-a-file branding convention, and finally **viewport space**:
`GetViewportPos` / `GetViewportSize`, the screen-pixel mouse contract, hiding the central viewport
and docking into its place.
**Source of truth:** `Cosmic/src/core/Window.{h,cpp}`, `core/Application.{h,cpp}`,
`core/Input.{h,cpp}`, `layers/WorkspaceLayer.{h,cpp}`, `layers/LauncherLayer.cpp`,
`layers/PlayerLayer.cpp`, `utils/Branding.{h,cpp}`, `utils/ImageIO.h`, `Runtime/CosmicApp.manifest`,
`Runtime/CMakeLists.txt`, `Projects/Starforge/src/ViewportController.cpp`,
`Projects/Engine3DDemo/src/Engine3DDemo.cpp`
**API Reference:** [`../reference/core.md`](../reference/core.md) *(skeleton — D6; `core/Window.h` is
listed there)* and [`../reference/ui.md`](../reference/ui.md) *(skeleton — D18; `WorkspaceLayer` is
listed there)*.
**How it works:** [`../systems/windowing.md`](../systems/windowing.md) *(skeleton — D26)*
**Configuration:** **both.** Nothing in this chapter is fenced by `COSMIC_2D_ONLY` — the window,
the chrome, fullscreen, the modal pump and the workspace viewport are identical in the 2D and 3D
engine builds ([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)).

> **Windows-only in practice.** Borderless chrome, the fullscreen style-strip, the modal frame pump
> and the DPI manifest are all inside `#ifdef _WIN32`. On any other platform `SetCustomChrome` is a
> no-op and `SetFullscreen` falls back to `glfwSetWindowMonitor`, which *does* switch display modes
> (`Window.cpp:981-1005`). The engine ships Windows x64 only today; the fallback exists so the file
> compiles, not because it has been exercised.

---

## Quick start

`Window` is not in `Cosmic.h`'s include list — it arrives through `core/Application.h:28`, so
`#include <Cosmic.h>` is enough:

```cpp
#include <Cosmic.h>

void MyProject::OnAttach()
{
    Cosmic::Window& win = Cosmic::Application::Get().GetWindow();

    win.SetTitle("Orbit Lab");        // OS window / taskbar / Alt-Tab name
    win.SetSize(1600, 900);           // no-op while fullscreen
    win.SetVSync(true);               // on by default

    CS_INFO("client area: {}x{}", win.GetWidth(), win.GetHeight());
}
```

That is the whole everyday surface. Press **F11** and the window covers the monitor; drag its title
bar and the app keeps rendering. Neither needed a line of code.

---

## The three rectangles

Almost every windowing bug in this engine is a confusion between three nested rectangles. Learn
them once:

| Rectangle | How you get it | Origin | Used for |
| --- | --- | --- | --- |
| **The OS window client area** | `Window::GetWidth()` / `GetHeight()`, or `GetSize(&w, &h)` | top-left of the *client*, which under borderless chrome is the whole window | sizing the engine framebuffer, `glViewport` |
| **The desktop** | `Input::GetMouseScreenPosition()`, every ImGui rect | top-left of the virtual desktop | comparing a mouse position against **any** ImGui rectangle |
| **The viewport panel** | `Application::GetViewportPos()` / `GetViewportSize()` | top-left of the *rendered image*, below the dock tab bar — in **desktop** coordinates | picking, gizmos, world↔screen math |

`Input::GetMousePosition()` lives in the **first** space (window-client). Every ImGui rect,
including `GetViewportPos()`, lives in the **second**. They coincide only when the window sits at
the desktop origin — a borderless maximized window, which is exactly the configuration most bugs of
this class were "tested" in. `Input.h:59-65` says so outright, and
[Work in viewport space](#work-in-viewport-space) below is the section that matters.

```
┌─ OS window (client area == whole window under borderless chrome) ────────┐
│  custom title bar / menu bar          ← WorkspaceLayer draws this        │
│ ┌─ dockspace ──────────────────────────────────────────────────────────┐ │
│ │ ┌ Hierarchy ┐ ┌─ Viewport ────────────────────┐ ┌ Inspector ┐        │ │
│ │ │           │ │ ▼ tab bar                     │ │           │        │ │
│ │ │           │ │ ┌───────────────────────────┐ │ │           │        │ │
│ │ │           │ │ │ GetViewportPos() is HERE  │ │ │           │        │ │
│ │ │           │ │ │ GetViewportSize() is this │ │ │           │        │ │
│ │ │           │ │ └───────────────────────────┘ │ │           │        │ │
│ │ └───────────┘ └───────────────────────────────┘ └───────────┘        │ │
│ └──────────────────────────────────────────────────────────────────────┘ │
│  status band reserved by SetBottomInsetPixels (optional)                  │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## Query and configure the window

| Call | Behaviour worth knowing |
| --- | --- |
| `GetWidth()` / `GetHeight()` → `unsigned int` | **Cached.** Seeded from `glfwGetFramebufferSize` at the end of construction (`Window.cpp:498-506`) and refreshed by the GLFW window-size callback. Cheap; safe every frame. |
| `GetSize(int* w, int* h)` | **Live** `glfwGetFramebufferSize` query. Use this when you are about to size an FBO — it is what `Application::SynchronizeRenderingState` uses (`Application.cpp:828`). |
| `SetVSync(bool)` / `IsVSync()` | `glfwSwapInterval`. On by default (set in the constructor *and* again in `Application::Initialize`), and deliberately re-applied after every fullscreen transition (`Window.cpp:979`). |
| `SetTitle(const std::string&)` | Idempotent — an equal string returns immediately, so per-frame calls are free. This is the **OS/taskbar** name; the name in the custom title bar comes from `WorkspaceLayer::SetProjectName`. |
| `SetSize(int w, int h)` | **No-op while fullscreen**, and silently ignores anything outside `[64, 16384]` on either axis (`Window.cpp:637-638`). |
| `Minimize()` / `Maximize()` / `Restore()` / `ToggleMaximize()` | Thin `glfwIconify/Maximize/RestoreWindow` wrappers. |
| `IsWindowMaximized()` | Named the long way on purpose: `<windows.h>` defines `IsMaximized` as a macro. |
| `Close()` | Sets the GLFW should-close flag, which ends `Application::Run`. |
| `GetHandle()` → `GLFWwindow*` | The raw handle, for anything the wrapper does not expose. |

### There is no client veto on close

`Application::OnWindowClose` returns `true`, so the `WindowCloseEvent` is marked `Handled` before
the layer walk ever starts (`Application.cpp:637-641`). A layer cannot see it and cannot cancel it.
If you need "are you sure?", drive it from your own UI and call `Window::Close()` when the user
confirms.

---

## Go fullscreen

```cpp
auto& win = Cosmic::Application::Get().GetWindow();
win.SetFullscreen(!win.IsFullscreen());
```

**F11 already does this** and you do not have to wire it. The technique is *borderless windowed
fullscreen*: the `WS_OVERLAPPEDWINDOW` style bits are stripped and the window is stretched over the
monitor with `SetWindowPos`. There is **no display-mode switch**, no `HWND_TOPMOST` and no
`ClipCursor` — so Alt-Tab, Win+Shift+S, capture overlays and multi-monitor cursor movement all keep
working, and the taskbar hides because of the stripped style, not because of the size.

Four behaviours you get for free, each of which was a bug once:

- **The frame is painted inside the toggle.** `SetFullscreen` calls `ModalFrameTick()` immediately
  after applying the new rect (`Window.cpp:841`). The `SetWindowPos` already dispatched `WM_SIZE`
  synchronously, so the engine is at the new size and presents a correct frame rather than letting
  DWM show a stale one. Both `SetWindowPos` calls also pass `SWP_NOCOPYBITS`, so old client pixels
  are never blitted into the new rect.
- **Maximized round-trips as maximized.** Entering fullscreen from a maximized window saves the
  *normal* (restored) rect out of `GetWindowPlacement` plus the maximize flag, and re-maximizes on
  exit (`Window.cpp:886-898`, `:967-971`).
- **The saved rect cannot strand the window.** On exit it is clamped into the work area of the
  nearest monitor, which covers "the monitor was unplugged / the resolution changed while
  fullscreen" (`:938-955`).
- **Display and DPI changes re-assert the cover.** `WM_DISPLAYCHANGE` and `WM_DPICHANGED` while
  fullscreen let GLFW process the message and then call `ReassertFullscreenCoverWin32`
  (`Window.cpp:215-237`).

The monitor is chosen by the **window centre**, not its top-left (`FindCurrentMonitor`,
`:1062-1097`) — a window straddling two screens goes fullscreen on the one holding most of it.

### Compat mode: why the cover is 1 px too tall

The default cover rect is **monitor height + 1 px**
(`FullscreenCompatMode::OversizeByOne`, `Window.h:115-119`). An exactly-monitor-sized borderless
window gets promoted by DWM to independent-flip "fullscreen optimizations", and the forced
demotion when a capture overlay appears black-flashes the legacy GL present path — GL has no API to
opt out of the promotion heuristic. One extra off-screen row means the window is never classified
as fullscreen, so overlays composite normally. The taskbar still hides. The only cost is
composed-present latency, which a tools engine does not need.

```cpp
win.SetFullscreenCompatMode(Cosmic::FullscreenCompatMode::ExactCover);  // A/B live
```

Set `COSMIC_FULLSCREEN_COMPAT=exact` or `=oversize` in the environment to pick a mode for one run
with no rebuild (`Window.cpp:307-318`). Rationale:
[`../engineering-notes/borderless-window-dpi.md`](../engineering-notes/borderless-window-dpi.md)
§"Fullscreen compat mode (W3)".

---

## Bind your own fullscreen key

`SetFullscreenHotkeyOverride` gets **first refusal on every key event** — press, release and repeat
— with the raw GLFW `key`, `action` and `mods`. Return `true` to consume the key; it then never
becomes an `Event` at all.

```cpp
// Alt+Enter toggles fullscreen; F11 keeps working because we return false for it.
void MyProject::OnAttach()
{
    auto& win = Cosmic::Application::Get().GetWindow();
    win.SetFullscreenHotkeyOverride([](int key, int action, int mods) -> bool
    {
        if (key == CS_KEY_ENTER && action == 1 /*GLFW_PRESS*/ && (mods & 0x0004 /*GLFW_MOD_ALT*/))
        {
            auto& w = Cosmic::Application::Get().GetWindow();
            w.SetFullscreen(!w.IsFullscreen());
            return true;   // consumed — the engine's F11 handler is not reached
        }
        return false;      // not ours
    });
}

void MyProject::OnDetach()
{
    Cosmic::Application::Get().GetWindow().ClearFullscreenHotkeyOverride();
}
```

**The engine has no `CS_MOD_*` or `CS_ACTION_*` constants.** This callback is the only place raw
GLFW `action`/`mods` values reach client code, so you must spell them out: `GLFW_RELEASE = 0`,
`GLFW_PRESS = 1`, `GLFW_REPEAT = 2`; `GLFW_MOD_SHIFT = 0x0001`, `GLFW_MOD_CONTROL = 0x0002`,
**`GLFW_MOD_ALT = 0x0004`**, `GLFW_MOD_SUPER = 0x0008`. Key codes *are* engine constants — use
`CS_KEY_*` from `codes/KeyCodes.h`, whose values are the GLFW ones.

> **Clear it before your DLL goes away.** The callback is a `std::function` whose target lives in
> your module. `Application::UnloadProjectDLL` does call `ClearFullscreenHotkeyOverride()` before
> `FreeLibrary` (`Application.cpp:797`, `:808`) and `~Window` clears it first thing, so the normal
> plugin path is already safe — but clear it in `OnDetach` anyway. That is the only protection if
> your code is unloaded by another route, such as Starforge's game-module hot reload, which does
> not go through `UnloadProjectDLL`.

### The F11 press is not an event

`Window::HandleFullscreenHotkey` runs **inside the GLFW key callback**, before any `Event` object
is constructed (`Window.cpp:444-459`). It consumes the key only when it returns `true`, and the
built-in branch returns `true` for exactly `key == CS_KEY_F11 && action == GLFW_PRESS`
(`Window.cpp:1113`).

So the precise contract is narrower than "F11 is not an event": **the fresh press** never reaches
`Application::OnEvent`, while **auto-repeat still delivers `KeyPressedEvent(F11, 1)` and the release
still delivers `KeyReleasedEvent(F11)`**. A handler that does not test the action will therefore see
a release with no matching press. `Input::IsKeyPressed(CS_KEY_F11)` observes the physical key
regardless.

A registered override is offered **press, release and repeat**, so an override that does not test
`action == GLFW_PRESS` toggles fullscreen twice per keystroke. See
[`events-and-input.md`](events-and-input.md) for the rest of the event path.

---

## Keep painting while the window is dragged or resized

Dragging the title bar or a resize border does **not** freeze the app. This is on by default and
needs no code.

The problem it solves is a pure Win32 one: pressing the caption or a resize border puts Windows
into its own modal `GetMessage`/`DispatchMessage` loop, which blocks inside `glfwPollEvents`, so
`Application::Run`'s body never executes until you let go. The fix is a `WM_TIMER` set on
`WM_ENTERSIZEMOVE` / `WM_ENTERMENULOOP` and killed on the matching exit message; each tick calls
`Application::RenderSingleFrame` through `Window::SetModalFrameCallback`
(`Window.cpp:200-214`, `Application.cpp:611`).

**What runs during a drag:** the fixed-step pass, the variable pass (which is where drawing
happens in this engine), ImGui, and the swap.
**What does not:** `PollEvents` and the **Safe Zone**. No DLL load/unload and no layer push/pop can
happen mid-drag; those flags stay queued for the normal loop. That is deliberate and it is why the
feature is safe.

```cpp
// Opt out — the window freezes while dragged, as it did before 2026-07-01.
Cosmic::Application::Get().SetRenderWhileDragging(false);
bool live = Cosmic::Application::Get().IsRenderWhileDragging();
```

### It composes with pause

`Pause()` and the modal pump gate the *same* function, so a **paused** window that you **drag**
still paints: fixed updates are skipped, `OnUpdate` runs with `dt = 0` so the scene keeps drawing
frozen, and ImGui stays fully interactive. Nothing special-cases the combination.

The full decision record — including why a render thread was rejected — is
[`../design/responsive-rendering-and-pause.md`](../design/responsive-rendering-and-pause.md). The
pause semantics table (what freezes, what keeps advancing) lives in
[`time-and-ticks.md`](time-and-ticks.md); the frame passes themselves are in
[`project-anatomy.md`](project-anatomy.md#one-frame-end-to-end). This chapter does not restate
either.

> **Minimizing is a separate switch, and its default surprises people.**
> `Application::SetPauseOnMinimize` defaults to **`false`** (`Application.h:144`) — the engine keeps
> ticking while minimized, which is what a simulation or a telemetry logger wants. Set it `true` for
> a game. A `0×0` resize is what sets `m_Minimized`, and `OnWindowResize` returns **`false`** for it
> (`Application.cpp:648-654`), so the `WindowResizeEvent` still reaches every layer with zero
> height — guard your aspect-ratio math.

---

## Draw your own title bar

Borderless custom chrome is **on by default on Windows**: the constructor calls
`SetCustomChrome(true)` (`Window.cpp:487`). The OS title bar and frame are gone, but the window
keeps native resize, Aero Snap, minimize/maximize animations and the drop shadow. Your app draws
the caption.

Two pieces, and both already exist if you use the workspace shell:

1. **Draw the bar.** `WorkspaceLayer` draws a menu bar with the centred project name and
   `UI::WindowControls()` on the right (`WorkspaceLayer.cpp:275-339`). See
   [`editor-ui-and-theming.md`](editor-ui-and-theming.md) for replacing its menus with your own.
2. **Report the draggable region** with `SetTitlebarHitTestCallback`. The predicate takes a point in
   **client pixels** and returns `true` where a press should drag the window. Both shipped shells
   use the same shape — recompute a flag while drawing the bar, and let the predicate return it:

```cpp
// OnAttach
Cosmic::Application::Get().GetWindow().SetTitlebarHitTestCallback(
    [this](int, int) { return m_TitlebarDrag; });

// While drawing your bar, in OnImGuiRender
const ImGuiViewport* vp = ImGui::GetMainViewport();
const ImVec2 mouse = ImGui::GetMousePos();
const float  barH  = ImGui::GetFrameHeight();
const bool   inBar = mouse.x >= vp->Pos.x && mouse.x < vp->Pos.x + vp->Size.x &&
                     mouse.y >= vp->Pos.y && mouse.y < vp->Pos.y + barH;
m_TitlebarDrag = inBar && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

// OnDetach
Cosmic::Application::Get().GetWindow().ClearTitlebarHitTestCallback();
```

The `!IsAnyItemHovered() && !IsAnyItemActive()` half is what stops your menus and window buttons
from dragging the window. A one-frame lag between drawing and hit-testing is fine for dragging.

**What the OS is told.** The window's real Win32 style still contains `WS_OVERLAPPEDWINDOW`
(re-added in `EnableCustomChromeWin32`), so Windows keeps giving you native behaviour; a WndProc
subclass then removes the frame *visually* in `WM_NCCALCSIZE` and re-implements resize borders
(8 px grips) plus the caption drag in `WM_NCHITTEST`. `DwmExtendFrameIntoClientArea` with 1 px
margins restores the drop shadow. Resize grips are disabled while maximized and the whole hit test
returns `HTCLIENT` while fullscreen.

`SetCustomChrome(false)` gives you the standard OS frame back; `HasCustomChrome()` reports the
state.

---

## High-DPI

There is no DPI API for you to call. The engine's position is that **you should never scale
anything by hand** — three things make that work:

1. **The process is Per-Monitor V2 aware, declared twice.** `Runtime/CosmicApp.manifest` declares
   `PerMonitorV2` and is embedded into *both* hosts (`Runtime/CMakeLists.txt:8`, `:51`), and GLFW
   additionally calls `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
   during `glfwInit`. The manifest matters because it applies before any of your code runs, so the
   first frame is never DWM-scaled.
2. **GLFW models the window as borderless.** It is created with `GLFW_DECORATED = GLFW_FALSE`
   *before* the native style bits are re-added. GLFW computes every window rect with
   `AdjustWindowRectExForDpi` using its **own** notion of the style, and for a decorated window that
   adds a DPI-scaled caption + frame. Modelled borderless, that adjustment is zero frame at every
   DPI, so GLFW's client model always matches the real client. The window is also created hidden
   (`GLFW_VISIBLE = GLFW_FALSE`) and shown only after the chrome is applied, so there is no
   first-show race.
3. **ImGui owns its own scale.** The GLFW backend sets `io.DisplaySize` and
   `io.DisplayFramebufferScale` from the live window/framebuffer size in `NewFrame`, and nothing in
   the engine re-assigns them afterwards. `ImGuiLayer::End` carries a comment explaining exactly
   why not (`ImGuiLayer.cpp:119-125`).

What that leaves for you is small but real: **derive sizes from the font, not from constants.**
`ImGui::GetFrameHeight()` already includes the DPI-scaled font size, which is why Starforge's status
bar reserves `ImGui::GetFrameHeight() + 2.0f` pixels rather than a hard 26 (`StarforgeApp.cpp:2410`).
Where you must give a pixel number — `WorkspaceLayer::SetEdgeMinPixels` — the engine multiplies it
by `viewport->DpiScale` for you (`WorkspaceLayer.cpp:446-453`).

> **If a click offset ever comes back**, suspect one of exactly two things: the window is being
> modelled as *decorated* again, or something writes `io.DisplaySize` between `Begin()` and
> `Render()`. That is the failure this design exists to prevent — the full trace is
> [`../engineering-notes/borderless-window-dpi.md`](../engineering-notes/borderless-window-dpi.md).

---

## Brand the window from an image file

```cpp
auto& win = Cosmic::Application::Get().GetWindow();
if (!win.SetIcon(Cosmic::FileSystem::Resolve("project://branding/logo.png")))
    CS_WARN("icon decode failed — the previous icon is still in place");

win.ClearIcon();   // back to the platform default (the exe icon)
```

`SetIcon` decodes through `ImageIO::ReadPixels` (any stb-supported format — PNG/JPG/BMP/TGA),
resamples to the standard **16 / 32 / 48 / 256 px** levels with `ImageIO::ResizeRgba`, and hands all
four to `glfwSetWindowIcon` (`Window.cpp:642-671`). It takes a **real disk path** — it does not
resolve `project://` for you.

**A decode failure keeps the current icon** and returns `false`. That is deliberate: a half-written
file caught mid-copy during a hot-swap must not blank the brand.

### The drop-a-file convention

`utils/Branding.h` defines one resolution order that all three hosts share. **First hit wins:**

| # | Candidate | Who it is for |
| --- | --- | --- |
| 1 | `<exe dir>/branding/icon.png` | the app's shipped brand |
| 2 | `<user:// root>/branding/icon.png` | a per-user override (`user://` is already per-app once `SetAppIdentity` has run) |
| 3 | the `project.cproj` `icon` key, resolved by the caller | a packaged app's manifest icon |
| 4 | `project://icon.png` | the project-icon convention |
| — | none found | `""` → keep the platform default |

```cpp
// Exactly what the engine does at boot (Application.cpp:559-561).
const std::string icon = Cosmic::Branding::ResolveProcessIcon();
if (!icon.empty())
    Cosmic::Application::Get().GetWindow().SetIcon(icon);
```

Candidates 3 and 4 are opt-in — pass the manifest path and `includeProjectIcon = true`, which is
what `PlayerLayer` does once a project is mounted so a packaged app shows *its* icon rather than the
host's (`PlayerLayer.cpp:105-109`). `ResolveIcon(IconQuery)` takes explicit roots and touches
nothing global, which is why it is unit-tested headlessly in `tests/test_branding.cpp`.

Because resolution is pure filesystem probing, **replacing the image on disk re-brands a running
app**. Starforge watches the resolved file's folder with a `FileWatcher` and re-applies on change,
retrying once after 0.5 s when the decode fails — the half-written-file case
(`StarforgeApp.cpp:2611-2657`).

> **Two different icons.** `Window::SetIcon` is the **live window and taskbar** icon. The icon
> Explorer shows on the `.exe`, and the one a pinned shortcut uses, is embedded into the binary by
> `ExeResources::SetIcon` at package time — a different mechanism with a different lifetime. See
> `building-and-shipping.md` *(D61)*.

---

## Capture the cursor for mouse-look

```cpp
Cosmic::Application::Get().GetWindow().SetCursorCaptured(true);   // hidden, locked, raw deltas
```

GLFW's disabled-cursor mode: the cursor is hidden, locked to the window, and `MouseMovedEvent`
deltas keep flowing with no edge clamping. The call is idempotent, and `IsCursorCaptured()` reports
the state. A packaged app can turn it on from boot with `capture_cursor = true` in
`project.cproj` (`PlayerLayer.cpp:85-87`).

Pair it with `Application::Get().GetImGuiLayer()->BlockEvents(false)` so raw input still reaches
your layer while no ImGui window has focus, and undo both when a menu opens — the convention is
that Escape releases. Details in [`events-and-input.md`](events-and-input.md).

---

## Work in viewport space

When a project runs inside the workspace shell, your world is **not** drawn to the backbuffer. It is
drawn into `Application::GetFrameBuffer()`, which `WorkspaceLayer` then displays as an
`ImGui::Image` inside the **Viewport** panel (`WorkspaceLayer.cpp:73-104`, `:197-228`). The shell
resizes that framebuffer to the panel every frame and calls `Renderer2D::SetViewportSize` to match.

Two accessors give you the panel's rectangle, both available on `Application` (which forwards to the
workspace layer and returns zero vectors when there is none):

```cpp
const glm::vec2 vpPos  = Cosmic::Application::Get().GetViewportPos();   // desktop pixels
const glm::vec2 vpSize = Cosmic::Application::Get().GetViewportSize();  // pixels
```

### The mouse contract

**`GetViewportPos()` is in desktop (ImGui screen) coordinates, so compare it against
`Input::GetMouseScreenPosition()` — never `Input::GetMousePosition()`.**

```cpp
const glm::vec2 mouse = Cosmic::Input::GetMouseScreenPosition();
const glm::vec2 local = mouse - vpPos;          // viewport-local: x from LEFT, y from TOP
```

That subtraction is the whole contract, and `local` is the space every consumer wants —
`ScenePicker::Pick`, `Camera2DController::ScreenToWorld`, `UiSystem::HitTest`. Starforge does
exactly this at `ViewportController.cpp:385-386`.

> **`Application::GetViewportPos`'s own doc comment is wrong.** `Application.h:93` calls the result
> *"GLFW window-space pixels"*; `WorkspaceLayer.h:271-278` — the code that produces the value, from
> `ImGui::GetCursorScreenPos()` — correctly calls it ImGui **screen** pixels. Multi-viewport is
> enabled, so all ImGui rects live in desktop space. Believe the `WorkspaceLayer` comment.

Multi-viewport is also why this is not academic: an ImGui window dragged out of the main window gets
its own OS window, and its rects are only meaningful in desktop coordinates.

### Hover and focus gates

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();   // needs layers/WorkspaceLayer.h
if (ws && ws->IsViewportHovered())
    m_Camera.OnUpdate(ts);      // only orbit while the cursor is over the image
```

`IsViewportHovered()` / `IsViewportFocused()` are refreshed once per ImGui frame, so reading them in
`OnUpdate` is worth one frame of lag — fine for gating, wrong for anything frame-exact.

Use them for **polled** input. The **event** path is already gated: `WorkspaceLayer` calls
`ImGuiLayer::BlockEvents(!focused && !hovered)` every frame, so mouse and keyboard events stop at
the ImGui layer whenever the cursor is over a panel instead of the image
(`WorkspaceLayer.cpp:210-211`).

### Hide the viewport and dock into its place

A screen with no 3D scene should not show an empty Viewport tab:

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
ws->SetViewportVisible(false);                          // not drawn, not docked
ws->DockWindow("Dashboard", Cosmic::DockPort::Center);  // takes the central node
```

`SetViewportVisible(false)` re-runs the dock builder and, because there is no image to interact
with, also forces `BlockEvents(true)` and clears hover/focus for the frame
(`WorkspaceLayer.cpp:229-234`). Multiple windows bound to `DockPort::Center` become tabs, with the
Viewport among them when it is visible. Frontier, SF_Telem and ViperSim all flip this per screen —
`FrontierApp.cpp:271-278`, `SF_Telem.cpp:215-216`, `ViperSim.cpp:110-111` are three worked examples.

`DockPort` and the rest of the docking model belong to
[`editor-ui-and-theming.md`](editor-ui-and-theming.md); this chapter only claims the two calls that
concern the *viewport*.

---

## Debug a window problem

Set `COSMIC_WINDOW_TRACE=1` in the environment, or call `Cosmic::Window::SetTraceEnabled(true)`, and
every window-state transition is logged with millisecond timestamps and the framebuffer size the
engine believes it is rendering at. Lines are tagged `[WinTrace]`:

| Traced | What it tells you |
| --- | --- |
| `WM_WINDOWPOSCHANGED`, `WM_SIZE`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE` | geometry and scale changes, with the flags |
| `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_SYSCOMMAND` | focus and shell commands |
| `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`, pump start/stop | the modal move/size loop |
| every fullscreen step | saved rect, style bits, cover rect, clamped restore rect |
| `SwapBuffers` slower than **25 ms** | the signature of driver throttling on an occluded or demoted swapchain |

The environment variable is read once, at the first `Window` construction. The trace is the first
thing to reach for on any windowing report — it costs one boolean test per message when off.

---

## Common patterns

**Screen-relative UI that follows the window.** Take `ImGui::GetMainViewport()`'s `Pos`/`Size`
rather than `Window::GetWidth()`. Under multi-viewport those are the values ImGui itself lays out
against, and they already account for the desktop origin.

**A borderless app that is not the workspace shell.** Draw your own bar, call `UI::WindowControls()`
for the min/max/close buttons, and register a hit-test predicate. `LauncherLayer::OnImGuiRender`
(`:266-288`) is the smallest complete example in the tree — a child window one frame-height tall,
an icon + caption on the left, `WindowControls()` on the right, and the drag flag computed after.

**Open at an authored size.** A packaged app reads `[window] title/width/height` out of
`project.cproj` and applies them on attach (`PlayerLayer.cpp:72-98`) — you do not need to call
`SetSize` yourself unless you are overriding the manifest.

**Gate camera input on hover, not on focus.** `IsViewportHovered()` is what you want almost always;
focus lags behind a click and will feel wrong for scroll-to-zoom.

---

## Pitfalls

**"The picking is off by exactly the title bar height."** You subtracted `GetViewportPos()` from
`Input::GetMousePosition()`. Use `Input::GetMouseScreenPosition()`. The two agree only when the
window's client origin is the desktop origin, which a borderless *maximized* window happens to
satisfy — so this bug is invisible in the configuration people test in and appears the moment the
window is restored down.

**In-game UI in a packaged app is offset by the shell's chrome.** `PlayerLayer::UpdateUI` hit-tests
`UiSystem` with `Input::GetMousePosition()` against a rect anchored at `(0,0)` sized to the
framebuffer (`PlayerLayer.cpp:275-291`), while the image it corresponds to actually starts at
`Application::GetViewportPos()` — below the menu bar and the dock tab bar. Rendering is unaffected
(it goes through the framebuffer). Until that is fixed, prefer keyboard/gamepad for shipped menus,
or drive your own hit test from `GetViewportPos()`. This is the same class of bug
[`game-ui.md`](game-ui.md) records for world-anchored UI.

**Nothing draws and the log ends at "Failed to initialise GLFW" or "glfwCreateWindow failed".** The
constructor logs `CS_CORE_CRITICAL` and returns *early*, leaving a `Window` with a null handle and
no graphics context. `SwapBuffers` guards that with `CS_CORE_ASSERT` — which is **compiled out in
every configuration** — so the next frame dereferences null. The usual cause is a driver without
OpenGL 4.5: the context hints are set to 4.5 core at `Window.cpp:327-329` and creation simply fails
below that.

**`SetSize` did nothing.** It is a no-op while fullscreen, and it silently rejects anything under
64 px or over 16384 px per axis.

**Your fullscreen hotkey fires on key *release* too.** The override sees every action. Test
`action == 1` (`GLFW_PRESS`), as the engine's own F11 handler does — otherwise a press/release pair
toggles twice and looks like nothing happened.

**Your Alt chord is actually a Ctrl chord.** `GLFW_MOD_ALT` is `0x0004`. `0x0002` is
`GLFW_MOD_CONTROL`. The root README's own §24 example got this backwards for as long as it existed.

**A layer's aspect ratio goes `inf` after minimizing.** A `0×0` `WindowResizeEvent` reaches every
layer. `OrbitCameraController` and `FlyCameraController` guard with `if (e.GetHeight() > 0)`;
`OrthographicCameraController` does not (see [`cameras.md`](cameras.md#pitfalls)). Guard your own.

**The window freezes while dragged after you "cleaned up" startup.** Something called
`SetRenderWhileDragging(false)`, or the modal callback was never installed because the window was
constructed outside `Application::Initialize`.

**A crash on the first keypress after unloading a project.** The fullscreen hotkey override
outlived its DLL. Clear it in `OnDetach`.

---

## See also

- [`events-and-input.md`](events-and-input.md) — the event path, why `F11` never becomes an
  `Event`, the two mouse-position accessors, and `BlockEvents`.
- [`editor-ui-and-theming.md`](editor-ui-and-theming.md) — the dockspace this viewport lives in:
  `DockPort`, `SetBottomInsetPixels`, `BeginViewportOverlay`, themes and fonts.
- [`project-anatomy.md`](project-anatomy.md) — `Application`'s construction order, the frame passes
  the modal pump replays, and the Safe Zone it deliberately excludes.
- [`time-and-ticks.md`](time-and-ticks.md) — `Pause()` vs `SetTimeScale(0)`, and what freezes.
- [`cameras.md`](cameras.md) — `ScenePicker`'s viewport-local pixel contract and the gizmo host rect.
- [`game-ui.md`](game-ui.md) — the *other* UI system: in-game menus and HUDs built from entities.
- [`../design/responsive-rendering-and-pause.md`](../design/responsive-rendering-and-pause.md) —
  the decision record for the modal frame pump and first-class pause.
- [`../engineering-notes/borderless-window-dpi.md`](../engineering-notes/borderless-window-dpi.md) —
  the HiDPI title-bar/click-offset investigation, plus the W-series fullscreen addendum.
- [`../reference/core.md`](../reference/core.md) — per-call `Window` entries *(skeleton — D6)*.
- [`../systems/windowing.md`](../systems/windowing.md) — internals *(skeleton — D26)*.
