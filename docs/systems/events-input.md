# Events & Input — How It Works

> **STATUS: SKELETON** — to be filled by work order **D27** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** hardware signals become typed `Event` objects that fall top-down through the
layer stack until someone marks them handled; `Input` is the opposite — layers ask "what's
pressed *right now*?" on demand.
**Source:** `Cosmic/src/events/*`, `core/Input.*`, `codes/*`
**API Reference:** [../reference/events-input.md](../reference/events-input.md) · **Guide:** root README §5, §6

## Section plan

1. **Overview** — push (events) vs pull (polling), with the doorbell-vs-glancing-out-the-window analogy (or better). <!-- TODO(D27) -->
2. **Mental model** — diagram **DG-4** (propagation flow incl. ImGuiLayer blocking + viewport-hover pass-through). <!-- TODO(D27) -->
3. **Step-by-step** — a keypress from GLFW callback → `Event` on the stack → `WorkspaceLayer` forward → project DLL handler → `Handled` short-circuit. <!-- TODO(D27) -->
4. **Technical implementation** — event type/category bitmask machinery (mine README §41), `EventDispatcher` dispatch mechanics, why blocking is conditional (`BlockEvents(false)` while viewport hovered), gamepad polling internals (E7), key-repeat semantics. <!-- TODO(D27) -->
5. **Design decisions** — immediate dispatch (no event queue) and its trade-offs. <!-- TODO(D27) -->
6. **Limits & future work.** <!-- TODO(D27) -->

**Truth sources:** README §5/§41 (migrating here), `events/Event.h`, `layers/ImGuiLayer.cpp`
(block logic), `core/Input.cpp`.
