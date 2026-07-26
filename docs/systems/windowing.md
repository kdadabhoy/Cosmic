# Windowing & Platform — How It Works

> **STATUS: SKELETON** — to be filled by work order **D26** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a GLFW window wrapped in custom Win32 borderless chrome, with
DPI awareness, borderless-fullscreen toggle (F11), and rendering that keeps painting during
drag/resize/modal loops.
**Source:** `Cosmic/src/core/Window.*`, `platform/OpenGL/OpenGLContext.*`
**API Reference:** [../reference/core.md](../reference/core.md) (Window surface) · **Guide:**
[../guide/windowing-and-viewport.md](../guide/windowing-and-viewport.md)

> **Don't re-derive the client-facing material.** D60 wrote the `Window` surface, borderless custom
> chrome and the title-bar hit test, the DPI story, borderless fullscreen + compat mode +
> `SetFullscreenHotkeyOverride`, the modal frame pump, `SetIcon`/drop-a-file branding and the
> viewport/screen-pixel mouse contract from source. This explainer covers *why* and *how it works
> inside*; link the guide for usage.

## Section plan

1. **Overview** — what "borderless chrome" means and why the engine draws its own title bar. <!-- TODO(D26) -->
2. **Mental model** — the three nested rectangles (OS window → framebuffer → ImGui viewport panel) and which coordinate space each API uses (`GetViewportPos/Size` contract). <!-- TODO(D26) -->
3. **Step-by-step** — window creation, event callbacks → engine events, F11 fullscreen round-trip, what happens on minimize. <!-- TODO(D26) -->
4. **Technical implementation** — Win32 hit-testing for the custom chrome (WS6), DPI handling (manifest + content scale), paint-through-transition (W2), modal frame pump (W4), maximize/restore state hardening (W5), `SetFullscreenHotkeyOverride` plumbing. <!-- TODO(D26) -->
5. **Design decisions** — link `../design/responsive-rendering-and-pause.md`; W3 DWM compat-mode mechanism (**shipped default-ON since 2026-07-02** — `OversizeByOne` is the default, `Window.h:336`; the skeleton's "default-off" is stale) and the pending user repro matrix (doc 09 §3.5). <!-- TODO(D26) -->
6. **Limits & future work** — known DPI edges, snip-overlay findings table pointer, and **the GLFW single-window constraint** (carried here from the retired README §24, D60): `glfwTerminate()` is called from `~Window` (`Window.cpp:565-570`), which is a *global* teardown. That is safe only because the engine is single-window; a second `Window` would have its handles invalidated by the first one's destructor and crash on the next `glfwPollEvents`. The fix, if a second window is ever needed, is to move `glfwTerminate()` to `Application::Shutdown()` balanced against the `glfwInit()` in the `Window` constructor. Contributor-facing, so it lives here rather than in the guide. <!-- TODO(D26) -->

**Truth sources:** `docs/plans/archive/09-windowing-plan.md` (W-series banners = the record),
`Window.cpp`, `Application.cpp` (the modal-frame callback + `SynchronizeRenderingState`),
`Runtime/CosmicApp.manifest`, and the guide chapter
[../guide/windowing-and-viewport.md](../guide/windowing-and-viewport.md). README §24 is **retired** —
its body is now an overview pointing at that chapter.
