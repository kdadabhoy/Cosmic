# Borderless custom title bar disappears + mouse offset on HiDPI displays

> **Verified against commit:** `b168754` (file:line references to `Cosmic/src/core/Window.cpp` and the
> bundled `Cosmic/dependencies/glfw/src/win32_window.c` are live as of this commit).
> **Status:** Fixed. Keep this note — the failure mode is subtle and easy to reintroduce.

## Symptom

On a **laptop running Windows display scaling at 125%**, launching the engine (packaged build, no debugger)
showed the launcher **without its custom title bar** (no rocket/"Cosmic Engine" caption, no min/max/close
buttons), and **every mouse click landed with a vertical offset** of roughly the caption height — you'd click
a button and the UI reacted as if you'd clicked lower. Toggling fullscreen **F11 in, then F11 out** fixed it
for the rest of that session.

Critically:
- The **desktop PC at 100% scaling never reproduced it.**
- A first fix attempt — removing an `io.DisplaySize` override in `ImGuiLayer::End()` and adding a boot-time
  `SynchronizeRenderingState()` — **did nothing on the laptop.** That ruled out "stale ImGui display size /
  engine viewport" as the cause and pointed below ImGui, into the GLFW↔Win32 window layer.

## Why F11 "fixing" it was the key clue

`Window::ApplyFullscreenWin32()` **saves the windowed rect on the way in and restores the same rect on the
way out** (`GetWindowRect` → `m_SavedWidth/Height`, then `SetWindowPos` back to it — `Window.cpp` ~484-489
and ~527-530). So after an F11 in/out cycle the window is the **exact same size** as before. If a pure resize
fixed it, the size would have had to change — it didn't. Therefore the bug was **window geometry/DPI state**,
not size. What F11 actually does is issue a manual `SetWindowPos(... SWP_FRAMECHANGED)`, which overrides
whatever geometry GLFW had computed.

## Root cause

The window was created **decorated** (`WS_OVERLAPPEDWINDOW`) and the OS frame was removed only *visually* by a
WndProc subclass handling `WM_NCCALCSIZE` (client area == whole window) — `Window.cpp` ~116-132.

The problem: GLFW computes **every** window rectangle using its **own** idea of the window style, via
`AdjustWindowRectExForDpi(getWindowStyle(window), dpi)`. For a decorated window, `getWindowStyle()` returns
`WS_CAPTION | WS_THICKFRAME | …` (`glfw/src/win32_window.c:41-63`), and `AdjustWindowRectExForDpi` therefore
adds a **DPI-scaled** caption + resize frame. GLFW does this in many places:

- window creation — `win32_window.c:1438-1444`
- `WM_DPICHANGED` (resizes to the suggested rect) — `win32_window.c:1197-1219`
- `WM_GETDPISCALEDSIZE` (keeps client constant across DPI change) — `win32_window.c:1169-1195`
- `WM_GETMINMAXINFO` — `win32_window.c:1088-1118`
- `glfwSetWindowSize` — `win32_window.c:1640-1647`

So GLFW's internal model carried a **phantom frame** — space for a caption/border that our `WM_NCCALCSIZE`
had already reclaimed for the client. The size of that phantom frame **scales with monitor DPI**:

| Scale | DPI | Phantom top frame ≈ | Result |
| ----- | --- | ------------------- | ------ |
| 100% | 96 | ~31 px, and the create path needs no DPI transition so it nets out | looked fine |
| 125% | 120 | ~38 px, **and** Windows drives a 96→120 DPI transition through GLFW's decorated-frame math at first show | persistent caption-height offset |

That is why it was HiDPI-only and persistent: GLFW's geometry and the real frameless client disagreed by a
scale-dependent amount, and nothing reconciled them until a manual `SetWindowPos` (F11).

## Fix

Make GLFW **model the window as borderless** so its DPI math is frame-free, while keeping the real Win32
window fully styled for native behaviors. This is the Windows Terminal / Chromium approach: decouple *what the
framework models* from *what Windows renders*. (`Cosmic/src/core/Window.cpp`)

1. **Create borderless + hidden.** Before `glfwCreateWindow`:
   ```cpp
   glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);  // getWindowStyle() => WS_POPUP => AdjustWindowRectExForDpi adds 0 frame at any DPI
   glfwWindowHint(GLFW_VISIBLE,   GLFW_FALSE);  // show after chrome is applied; no first-show DPI race
   ```
2. **Re-add the native style on the real window** in `EnableCustomChromeWin32()`, after subclassing:
   ```cpp
   LONG style = GetWindowLong(hwnd, GWL_STYLE);
   style |= WS_OVERLAPPEDWINDOW;  // native resize / Aero Snap / min-max animations / DWM shadow
   SetWindowLong(hwnd, GWL_STYLE, style);
   ```
   GLFW never reads `GetWindowLong` for its geometry math — it always uses its own (now borderless)
   `getWindowStyle()` — so these bits are invisible to GLFW and only affect Windows' native handling.
3. **Keep removing the frame visually** with the existing `WM_NCCALCSIZE` / `WM_NCHITTEST` subclass.
4. **Show after setup:** `glfwShowWindow(m_Handle)` at the end of the constructor, then cache the live client
   size into `m_Data` via `glfwGetFramebufferSize`.

Supporting hygiene (already in place, not the root fix): the GLFW ImGui backend owns
`io.DisplaySize`/`io.DisplayFramebufferScale` each frame, and `Application::Initialize()` calls
`SynchronizeRenderingState()` once at boot to set the engine viewport/FBO from the true framebuffer size.

## Verification

- **Capture the numbers** (temporary logging) at startup and after an F11 in/out cycle on the laptop:
  `glfwGetWindowSize`, `glfwGetFramebufferSize`, `glfwGetWindowPos`, `GetClientRect`. After the fix they are
  **identical at startup and post-F11** (nothing settles on F11), the client equals the requested size with
  **no phantom caption inset**, and the relationship holds at both 125% and 100%.
- **Laptop @125%, cold launch (no F11):** custom title bar renders on the first frame; clicks register with
  no offset; title-bar drag (move/snap), edge resize, minimize/maximize/restore animations, and the drop
  shadow all work.
- **PC @100%:** no regression to windowed/fullscreen rendering or Launcher↔Workspace transitions.

## How to not reintroduce it

If you ever set `GLFW_DECORATED` back to `TRUE` (or let GLFW re-derive a decorated style, e.g. via
`glfwSetWindowAttrib(GLFW_DECORATED, …)`) while keeping the `WM_NCCALCSIZE` frame strip, the phantom-frame
offset comes back on HiDPI displays. The window must be borderless **in GLFW's model**; native chrome
behaviors are added on the real `HWND` only.
