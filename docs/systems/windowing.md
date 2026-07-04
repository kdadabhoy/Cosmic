# Windowing & Platform — How It Works

> **STATUS: SKELETON** — to be filled by work order **D26** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a GLFW window wrapped in custom Win32 borderless chrome, with
DPI awareness, borderless-fullscreen toggle (F11), and rendering that keeps painting during
drag/resize/modal loops.
**Source:** `Cosmic/src/core/Window.*`, `platform/OpenGL/OpenGLContext.*`
**API Reference:** [../reference/core.md](../reference/core.md) (Window surface) · **Guide:** root README §24

## Section plan

1. **Overview** — what "borderless chrome" means and why the engine draws its own title bar. <!-- TODO(D26) -->
2. **Mental model** — the three nested rectangles (OS window → framebuffer → ImGui viewport panel) and which coordinate space each API uses (`GetViewportPos/Size` contract). <!-- TODO(D26) -->
3. **Step-by-step** — window creation, event callbacks → engine events, F11 fullscreen round-trip, what happens on minimize. <!-- TODO(D26) -->
4. **Technical implementation** — Win32 hit-testing for the custom chrome (WS6), DPI handling (manifest + content scale), paint-through-transition (W2), modal frame pump (W4), maximize/restore state hardening (W5), `SetFullscreenHotkeyOverride` plumbing. <!-- TODO(D26) -->
5. **Design decisions** — link `../design/responsive-rendering-and-pause.md`; W3 DWM compat-mode mechanism (shipped default-off) and the pending user repro matrix (doc 09 §3.5). <!-- TODO(D26) -->
6. **Limits & future work** — known DPI edges, snip-overlay findings table pointer. <!-- TODO(D26) -->

**Truth sources:** `docs/plans/archive/09-windowing-plan.md` (W-series banners = the record),
`Window.cpp`, README §24.
