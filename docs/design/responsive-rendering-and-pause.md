# Design: Responsive Window Rendering (client-toggleable) + First-Class Pause

> **Status:** Proposed (not yet implemented).
> **Targets commit:** `b168754` — all file:line references are live as of this commit; re-check before
> implementing.
> **Summary:** Two related controls over the engine's update/render heartbeat. **(A)** Keep rendering while
> the user drags/resizes the OS window (today it freezes), exposed as a client-toggleable option that
> defaults **on**. **(B)** A first-class **pause** the end-user can trigger that freezes simulation and
> animation while the window stays fully rendered and the UI interactive.
>
> These are deliberately specced together: both gate `Application`'s per-frame work, both rely on the same
> refactor (`RenderSingleFrame()`), and they must compose (you can drag a paused window and it still paints).

---

## Background: the frame loop today

`Application::Run()` (Cosmic/src/core/Application.cpp:80-223) is a single loop:

```
while (running && !shouldClose):
    PollEvents()                         # glfwPollEvents — dispatches OS messages
    dt = now - lastFrameTime             # timing
    PASS 1A  fixed-step OnFixedUpdate()  # physics / serial / deterministic logic (60 Hz accumulator)
    PASS 1B  UpdateLayerTime(); OnUpdate()   # variable step — ALSO ISSUES DRAW CALLS
    PASS 2   ImGuiLayer::Begin(); OnImGuiRender(); ImGuiLayer::End()
    SwapBuffers()
    --- SAFE ZONE ---                    # deferred DLL load/unload + layer push/pop (no live iterators)
```

**Critical engine constraint:** in Cosmic, `Layer::OnUpdate(dt)` is where world rendering happens, not just
logic — e.g. `LauncherLayer::OnUpdate` calls `RenderBackground` which submits `Renderer2D` draw calls
(Cosmic/src/layers/LauncherLayer.cpp:168-231). Any feature that wants "keep rendering but freeze the sim"
must therefore **still call `OnUpdate`, just with `dt = 0`** — skipping `OnUpdate` would stop drawing and the
scene would go black. This shapes the pause design below.

---

## Feature A — Responsive rendering during window drag/resize

### Goal / objective
Dragging the custom title bar or a resize border must not freeze the app. The animated background, FPS
counter, ImGui, fixed-step logic, and serial polling all keep running during the drag. Clients that prefer
the old behavior (e.g. a minimal/low-power tool) can turn it off. **Default: on.**

### Root cause
A pure Win32 issue: the **modal move/size loop**. When the user presses the caption or a resize border,
Windows' `DefWindowProc` handling of `WM_NCLBUTTONDOWN` (after our `WM_NCHITTEST` returns `HTCAPTION` /
`HTLEFT` / … — Cosmic/src/core/Window.cpp:73-104) enters its **own internal `GetMessage`/`DispatchMessage`
loop** and does not return until the drag ends. Because that blocks inside `glfwPollEvents()`
(called from `Run()` at Cosmic/src/core/Application.cpp:88), the `Run()` body never executes — no update, no
render — until release.

GLFW only toggles cursor mode on `WM_ENTERSIZEMOVE` / `WM_EXITSIZEMOVE`
(dependencies/glfw/src/win32_window.c:988-1018) and does **not** handle `WM_TIMER`, so a timer-driven frame
pump is conflict-free.

### Design — pump frames from a `WM_TIMER` during the modal loop
The only code that can run during the modal loop is a window-procedure handler responding to a message
Windows pumps inside it. Set a timer when the loop begins; render one frame per `WM_TIMER`; kill the timer
when the loop ends.

**1. Factor the per-frame body out of `Run()`** into `bool Application::RenderSingleFrame()`
(Cosmic/src/core/Application.cpp). It does everything in the loop body **except** `PollEvents()` and the
post-swap **Safe Zone**: compute dt, advance `m_AbsoluteTime`, PASS 1A fixed, PASS 1B variable, ImGui
`Begin`/render/`End`, `SwapBuffers`. Move the loop-locals `lastFrameTime` / `accumulator`
(Application.cpp:82-83) to members `m_LastFrameTime` / `m_Accumulator` so the main loop and the modal pump
share one coherent clock. Return `false` on the `m_Minimized && m_PauseOnMinimize` early-out so `Run()` can
keep its existing "skip the Safe Zone while minimized" behavior. Add a `bool m_InFrameTick` re-entrancy
guard (cheap insurance — modal `WM_TIMER` dispatch is sequential, but `SendMessage` edge cases are ruled out
for free).

`Run()` becomes:
```cpp
m_LastFrameTime = (float)glfwGetTime();
while (m_Running && !m_Window->ShouldClose())
{
    m_Window->PollEvents();
    if (!RenderSingleFrame())   // false == minimized-skip (matches today's `continue`)
        continue;
    // ... existing Safe Zone (DLL load/unload, layer transitions) unchanged ...
}
```

**2. Timer pump in the WndProc subclass** (`CosmicWndProc`, Cosmic/src/core/Window.cpp:107), handled for
`self != nullptr` *outside* the `HasCustomChrome()` switch (the freeze is chrome-independent), then chained
through to the original proc so GLFW's cursor handling still runs:
```cpp
case WM_ENTERSIZEMOVE:
case WM_ENTERMENULOOP:  self->BeginModalFramePump(); break;   // SetTimer, then fall through to GLFW
case WM_EXITSIZEMOVE:
case WM_EXITMENULOOP:   self->EndModalFramePump();   break;   // KillTimer, then fall through to GLFW
case WM_TIMER:
    if (wParam == Window::kModalFrameTimerId) { self->ModalFrameTick(); return 0; }
    break;
```

**3. New `Window` surface** (mirrors the existing "public for the file-local WndProc" pattern,
Window.h:175-181):
- `static constexpr UINT_PTR kModalFrameTimerId = 0xC05;`
- `bool m_InModalLoop = false;`
- `bool m_ModalRenderingEnabled = true;` — the client gate (see API below)
- `std::function<void()> m_ModalFrameCallback;` + `SetModalFrameCallback(const std::function<void()>&)`
- `BeginModalFramePump()` → `if (m_ModalRenderingEnabled && !m_InModalLoop) { m_InModalLoop = true; SetTimer(hwnd, kModalFrameTimerId, USER_TIMER_MINIMUM, nullptr); }`
- `EndModalFramePump()` → `if (m_InModalLoop) { m_InModalLoop = false; KillTimer(hwnd, kModalFrameTimerId); }`
- `ModalFrameTick()` → `if (m_ModalFrameCallback) m_ModalFrameCallback();`
- `void SetModalRenderingEnabled(bool e) { m_ModalRenderingEnabled = e; if (!e) EndModalFramePump(); }`

Defensive `EndModalFramePump()` (KillTimer) in `DisableCustomChromeWin32()` and `~Window` so no timer
outlives the HWND.

`USER_TIMER_MINIMUM` (~10 ms; WM_TIMER floors near ~15 ms) is plenty — VSync caps the effective rate anyway.

**4. Wire it up** in `Application::Initialize()` after the window + ImGui layer exist (Application.cpp:408-413):
```cpp
m_Window->SetModalFrameCallback([this]{ RenderSingleFrame(); });
```

### Client toggle API (default on)
Exposed on `Application` for discoverability (clients reach everything via `Application::Get()`), forwarding
to `Window`:
- `void Application::SetRenderWhileDragging(bool enabled);`  → `m_Window->SetModalRenderingEnabled(enabled)`
- `bool Application::IsRenderWhileDragging() const;`

Example (client plugin):
```cpp
void MyProject::OnAttach()
{
    Cosmic::Application::Get().SetRenderWhileDragging(false); // opt out; window freezes while dragged
}
```

### Why this is safe (doesn't break anything else)
- **Native behavior untouched:** we only *add* `SetTimer`/`KillTimer` and chain the size/move messages to
  GLFW — the modal loop, Aero Snap, resize, and double-click-maximize all keep working.
- **No re-entrancy into transitions:** `RenderSingleFrame()` excludes the Safe Zone, so no DLL load/unload or
  layer push/pop runs mid-drag; transition flags stay queued for the normal loop. `m_InFrameTick` blocks
  nested ticks.
- **Coherent timing:** shared `m_LastFrameTime` / `m_Accumulator` keep fixed-step physics/serial draining
  correctly; the spiral-of-death clamp (Application.cpp:110-113) still applies.
- **Live resize:** during a border drag, `WM_SIZE` → GLFW size callback → `OnWindowResize` (FBO +
  `glViewport`) runs as messages dispatch; the next `WM_TIMER` tick redraws at the new size, and ImGui reads
  the live size in `Begin()`.
- **Single-threaded:** the GL context stays current on the main thread throughout — no context migration.

### Rejected alternative
A dedicated **render thread** (move the GL context off the main thread so it keeps drawing while the main
thread sits in the modal loop) is how some AAA engines avoid this entirely. It is also a large, risky
refactor of a single-threaded, main-thread-GL engine (context migration, cross-thread resource sync, ImGui
threading) — exactly the "don't break anything" risk to avoid. The timer pump achieves the same user-visible
result with a contained change.

---

## Feature B — First-class pause (sim frozen, UI + render live)

### Goal / objective
A pause the **end-user** can trigger (the client binds a key or a button) that freezes simulation and
animation while the window stays fully rendered and the UI interactive — so the client can show a pause menu
over a still, live scene.

### Semantics (the key design point)
Pause is a boolean engine state, **orthogonal to `TimeScale`** (so `Resume()` restores the user's scale and
it never collides with rewind, `TimeScale < 0`). When paused, for each frame inside `RenderSingleFrame()`:

| Stage | Paused behavior | Why |
| ----- | --------------- | --- |
| PASS 1A — fixed `OnFixedUpdate` | **Skipped** (accumulator not advanced) | Pure logic/physics; nothing to draw, safe to freeze |
| PASS 1B — `UpdateLayerTime` + `OnUpdate` | **Called with effective dt = 0** | `OnUpdate` issues draw calls; dt=0 freezes motion but keeps the scene rendering |
| PASS 2 — ImGui + `SwapBuffers` | **Runs normally** | Pause menu animates and stays clickable; frame is presented |
| `m_AbsoluteTime` (wall-clock uptime) | **Keeps advancing** | It's scale-independent uptime (README §7); used for profiling/session time |
| `GetLocalTime()` (scaled, per-layer) | **Frozen** (advances by 0) | Shader `u_Time`/animation driven by it freeze — the visual "pause" |

Net effect: motion, physics, and `GetLocalTime()`-driven shaders freeze; the scene is still drawn (frozen);
ImGui is fully interactive. Document the shader nuance explicitly: effects reading `u_Time` / `GetLocalTime()`
freeze, while anything reading `GetAbsoluteTime()` keeps moving (intended — e.g. a live "PAUSED" clock).

### API
On `Application`:
- `void Pause();`  `void Resume();`  `void TogglePause();`  `bool IsPaused() const;`

Implemented as a gate in `RenderSingleFrame()`: when `m_Paused`, skip the fixed-step block and force the
PASS 1B scaled delta to `0.0f`. No engine default hotkey — the engine exposes the state; the **client** binds
a key/menu (consistent with "defined by the client"). `IsPaused()` lets layers/clients branch (draw a
"PAUSED" overlay, gate gameplay input, etc.).

Example (client plugin):
```cpp
void MyProject::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher d(e);
    d.Dispatch<Cosmic::KeyPressedEvent>([](Cosmic::KeyPressedEvent& k){
        if (k.GetKeyCode() == CS_KEY_ESCAPE && k.GetRepeatCount() == 0) {
            Cosmic::Application::Get().TogglePause();
            return true;
        }
        return false;
    });
}
```

### Interaction with Feature A
They compose through the same `RenderSingleFrame()` path: a **paused** window that the user **drags** still
paints the frozen-but-live UI each `WM_TIMER` tick. No special-casing needed.

### Relationship to existing `SetTimeScale(0)`
`SetTimeScale(0.0f)` remains the low-level soft-pause (layers still get `OnUpdate` with 0 dt). The new
`Pause()` is the ergonomic, queryable, **scale-preserving** first-class version intended for end-user pause:
it doesn't overwrite the user's `TimeScale`, it short-circuits the fixed step, and it advertises state via
`IsPaused()`. Both can coexist; `Pause()` wins (forces dt=0) regardless of the current scale.

---

## README updates (to apply when this ships)
- **§3 Application Lifecycle → Application Control API** (README.md ~336): document
  `SetRenderWhileDragging`/`IsRenderWhileDragging` and `Pause`/`Resume`/`TogglePause`/`IsPaused` with the
  short examples above.
- **§7 Time & Timeline** (README.md ~696): a "Pause vs. `TimeScale(0)`" subsection — what freezes, what
  keeps moving (`GetAbsoluteTime()` vs `GetLocalTime()`), and that `Pause()` preserves the user's scale.
- **§24 Window System** (README.md ~2202): a "Responsive drag/resize" note (default on, client-toggleable)
  pointing back to this doc.
- **§43 Known Limitations & Roadmap**: a dated changelog entry when shipped.

## Verification (for the eventual implementation)
- Drag the title bar / a resize border → animated background + FPS keep updating; release → normal operation.
- Double-click-maximize, Aero Snap (drag to a screen edge), minimize/restore still work; resize renders live
  at the new size with the title bar + hit regions aligned.
- `SetRenderWhileDragging(false)` → window freezes while dragged (old behavior) and no timer leaks.
- `Pause()` → motion/animation/physics freeze, the ImGui pause menu still animates and is clickable, the
  scene renders frozen; `Resume()` restores the prior `TimeScale`; `TogglePause()` round-trips.
- Drag a **paused** window → still paints.
- No regression to windowed/fullscreen rendering, Launcher↔Workspace transitions, or shutdown.

## Open questions
- **`OnFixedUpdate` during pause:** skip entirely vs. call with `dt = 0`. *Recommendation: skip* (it's pure
  logic; skipping avoids any 0-dt edge cases in client physics).
- **Pause + minimized:** when minimized with `m_PauseOnMinimize`, keep the existing skip behavior (don't
  force-render). Pause does not override minimize handling.
- **API home for `SetRenderWhileDragging`:** `Application` (forwarding to `Window`) vs. directly on `Window`.
  *Recommendation: `Application`* for client discoverability.
- **Timer cadence:** `USER_TIMER_MINIMUM` vs. a fixed ~8–16 ms. VSync dominates either way; revisit only if
  drag feels choppy on slow hardware.
