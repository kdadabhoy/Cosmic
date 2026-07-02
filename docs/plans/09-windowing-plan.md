# Windowing Plan — Fullscreen, DPI, and Capture-Overlay Correctness

> **New 2026-07-01.** The borderless-chrome + DPI work (see
> [`docs/engineering-notes/borderless-window-dpi.md`](../engineering-notes/borderless-window-dpi.md))
> fixed the 125%-scaling launch bugs, but the window layer still has rough edges the user hits
> daily: **black screening when toggling fullscreen**, and **glitching when the Windows
> screen-snip overlay (Win+Shift+S / Snipping Tool) is invoked over the fullscreen app**. This doc
> is (a) a map of how the window layer works today, (b) ranked root-cause hypotheses with the
> evidence each needs, and (c) W-series work orders with acceptance criteria.
>
> Scaling constraint that must never regress: the app works at 100% (desktop), 125% (laptop), and
> everything between/beyond — the engineering note's "how to not reintroduce it" section is binding.

## 1. How it works today (verified against source 2026-07-01)

| Mechanism | Where | Behavior |
| --- | --- | --- |
| Borderless model | `Window.cpp` ctor (`GLFW_DECORATED=FALSE`, hidden create) + `EnableCustomChromeWin32()` re-adds `WS_OVERLAPPEDWINDOW` on the real HWND | GLFW's DPI math is frame-free; native snap/resize/shadow preserved; frame stripped visually in `WM_NCCALCSIZE` |
| Fullscreen enter | `ApplyFullscreenWin32(true)` | saves window rect → strips `WS_OVERLAPPEDWINDOW` → `SetWindowPos(HWND_TOP, monitor origin, mode w×h, SWP_FRAMECHANGED\|SWP_SHOWWINDOW)` — **exactly covers the monitor** (that is what makes the shell hide the taskbar) |
| Fullscreen exit | `ApplyFullscreenWin32(false)` | restores style bits + saved rect |
| Resize plumbing | GLFW size callback → `WindowResizeEvent` → `Application::OnWindowResize` → `m_Framebuffer->Resize` + `Renderer::OnWindowResize` (synchronous) | FBO tracks the client size within the same message dispatch |
| Boot sync | `Application::Initialize` → `SynchronizeRenderingState()` | first-frame viewport/FBO from the true framebuffer size |
| Hotkey | F11 in `Window::HandleFullscreenHotkey` (+ per-app override delegate) | |
| Known gap (by design, documented) | `docs/design/responsive-rendering-and-pause.md` (Status: Proposed) | rendering **freezes during OS modal loops** (title-bar drag / resize) because `glfwPollEvents` blocks inside the Win32 modal loop |

## 2. Symptoms & ranked hypotheses

### Symptom A — black screen/flash when entering or leaving fullscreen

- **H-A1 (likely): transition frames are presented before the engine paints at the new size.**
  `SetWindowPos` recomputes the frame and shows the window at monitor size immediately; the last
  swapped buffer was windowed-sized. Until the *next* `SwapBuffers` (up to a full frame later —
  more if the toggle happens mid-frame), DWM shows uninitialized/stretched content, and GDI may
  clear newly exposed regions to the class background (black) on `WM_ERASEBKGND`.
- **H-A2: DWM composition mode switch.** A borderless window that *exactly* covers a monitor gets
  promoted by DWM to "fullscreen optimizations" (independent flip / MPO). The promote/demote at
  enter/exit can black-flash on some driver/monitor combos — same class of issue the file-header
  comment already avoids for `glfwSetWindowMonitor`, just milder.
- **H-A3: stale-bits blit.** `SetWindowPos` without `SWP_NOCOPYBITS` may blit old client pixels
  into the new rect before the first proper present (visible as a stretched/garbage flash rather
  than pure black).

### Symptom B — glitching when the snip overlay is invoked over fullscreen

- **H-B1 (likely): iFlip demotion under the capture overlay.** The snip UI is a topmost layered
  window; overlaying an iFlip-promoted fullscreen window forces DWM to demote it back to composed
  mode mid-swapchain → one-to-several black/torn frames, occasionally a stuck black until the next
  resize/present on some drivers. This is a well-known borderless-fullscreen + capture interaction
  and is consistent with "works windowed, glitches fullscreen."
- **H-B2: focus-loss handling.** The overlay steals foreground; `GLFW_AUTO_ICONIFY` is already
  `FALSE` (good — GLFW only auto-iconifies *monitor-owned* windows anyway), but our frame loop
  keeps swapping while occluded; if the driver throttles an occluded fullscreen GL swapchain,
  frames stall. Needs measurement, not assumption.
- **H-B3: the frozen-render modal-loop gap.** If any part of the snip flow enters a modal loop in
  our thread's message pump, rendering freezes (design-doc gap) and the last frame shown is
  whatever DWM had — indistinguishable from a "glitch" to the user.

## 3. W-series work orders

### W1 — Instrumentation + repro matrix (do first; no behavior change)
Add a `CS_WINDOW_TRACE` compile-time (or log-category) switch that logs, with timestamps:
`WM_WINDOWPOSCHANGED` (rect+flags), `WM_SIZE`/`WM_DPICHANGED`, `WM_ACTIVATE`/`WM_KILLFOCUS`,
`WM_SYSCOMMAND`, style-bit changes, every `ApplyFullscreenWin32` step, `SwapBuffers` duration
(detect throttling), and current `glfwGetFramebufferSize` at each event.
Run the matrix and record findings **in this doc**: {100%, 125%} × {windowed, maximized,
fullscreen} × {F11 toggle, snip overlay, Alt+Tab, monitor sleep/wake}. Optional: PresentMon
capture to confirm/deny iFlip promotion (H-A2/H-B1) — note the presentation mode observed.
**Acceptance:** findings table appended here; each hypothesis marked confirmed/denied with evidence.

### W2 — Paint-through-transition (fixes H-A1/H-A3 regardless of W1's DWM findings)
1. Factor the render body of `Application::Run` into `RenderSingleFrame()` (this refactor is
   **shared with the responsive-rendering design doc** — implement it once, per that spec).
2. `Window::SetFullscreen` gains a post-transition hook: after `SetWindowPos`, immediately fire the
   resize event (already synchronous) **and request one immediate `RenderSingleFrame()`** so a
   correctly-sized frame is presented within the same toggle, not a frame later.
3. Add `SWP_NOCOPYBITS` to both fullscreen-enter and -exit `SetWindowPos` calls (kills stale-bit blits).
4. Verify (don't assume) the GLFW window-class background brush situation: if newly exposed areas
   flash black before first paint, handle `WM_ERASEBKGND` → return 1 while custom chrome is active.
**Acceptance:** frame-by-frame capture (phone slow-mo or OBS) of F11 both directions at 100% and
125% shows no black/garbage frame; log shows present-at-new-size within the toggle's message dispatch.

### W3 — Fullscreen/DWM strategy experiment (addresses H-A2/H-B1; gated on W1 evidence)
Behind a debug toggle (`Window::SetFullscreenCompatMode(...)`), implement and A/B test:
- **(a) Oversize-by-one:** size the fullscreen rect to `monitor height + 1 px` (still covers the
  taskbar, still borderless) so DWM never classifies the window as fullscreen → no iFlip
  promotion → capture overlays composite normally. Cost: loses independent-flip latency, which a
  tools engine does not care about.
- **(b) Exact cover (today's behavior)** as control.
- Measure with the W1 instrumentation + snip overlay: black frames? recovery? capture correctness?
**Decision rule:** if (a) eliminates Symptom B with no regressions (taskbar stays hidden, Alt+Tab
fine, screenshots capture the app), make it the default and document why in the engineering note.
If W1 showed no iFlip promotion at all, close this order as N/A with the evidence.
**Acceptance:** decision recorded here + default set + engineering note updated.

### W4 — Responsive rendering during drag/resize + first-class Pause
Implement `docs/design/responsive-rendering-and-pause.md` as specced (Feature A default-on:
`WM_TIMER`-pumped `RenderSingleFrame()` during the modal move/size loop; Feature B: engine
`Pause()/Resume()` that zeroes sim `dt` while UI/render stay live). It shares W2's refactor,
closes H-B3, and fixes the long-standing "window freezes while dragging" wart.
**Acceptance:** per that design doc's own verification section (drag paints live at both DPIs;
pause freezes sim but UI stays interactive).

### W5 — Window-state machine hardening (small, independent fixes)
1. **Maximized → fullscreen → exit** currently restores the *maximized rect* as a normal window
   (saved via `GetWindowRect` while zoomed). Save `IsZoomed` alongside the rect; on exit, restore
   maximize state properly (`ShowWindow(SW_MAXIMIZE)`), not a pseudo-maximized floating window.
2. **Saved-rect validity:** on exit, clamp the restore rect to the nearest monitor work area
   (monitor unplugged / resolution changed while fullscreen).
3. **DPI change while fullscreen** (drag-to-other-monitor is impossible, but resolution/scale can
   change under us): verify `WM_DPICHANGED` path keeps exact cover; re-issue the fullscreen
   `SetWindowPos` on `WM_DISPLAYCHANGE`.
4. **Multi-monitor enter:** `FindCurrentMonitor` uses the window *center* — correct; add a test
   note. Ensure the saved rect restores to the same monitor.
**Acceptance:** manual checklist run at 100% + 125%, single + dual monitor, all four cases logged.

### W6 — Documentation & regression guard
Update README §24 (fullscreen behavior, compat mode if W3 changed the default, pause API from W4)
and extend the borderless-DPI engineering note with the W1 findings + W3 decision. Add the W1
trace switch usage to the note so the next investigation starts warm.
**Acceptance:** docs updated; `docs/design/responsive-rendering-and-pause.md` status flipped to
Implemented (or updated where reality diverged).

## 4. Non-goals

- **No exclusive fullscreen** and **no `glfwSetWindowMonitor`** path — the borderless-windowed
  strategy stays (mode switches cause worse flashes; the file header documents why).
- **No presentation-layer rewrite** (DXGI-interop/ANGLE) — if W1–W3 prove GL presentation itself
  is the problem on target hardware, that escalates to the doc-05 S13 gate, not this plan.

## 5. Order & size

| Order | Size | Depends on |
| --- | --- | --- |
| W1 instrumentation | S | — |
| W2 paint-through-transition | M | shares refactor with W4 |
| W3 DWM experiment | S–M | W1 evidence |
| W4 responsive render + pause | M–L | design doc (already written) |
| W5 state hardening | S | — (parallel-safe) |
| W6 docs | S | W1–W5 |

W1/W5 are safe lower-tier-AI tasks; W2/W4 touch the frame loop — stronger model + your review.
