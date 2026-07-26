# API Reference — Events & Input

> **STATUS: SKELETON** — to be filled by work order **D7** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/events/Event.h`, `events/ApplicationEvent.h`,
`events/KeyEvent.h`, `events/MouseEvent.h`, `core/Input.h`, `codes/KeyCodes.h`,
`codes/MouseButtonCodes.h`, `codes/GamepadCodes.h`.

**Read first:** [`../guide/events-and-input.md`](../guide/events-and-input.md) — reactive events vs.
on-demand polling is the organizing distinction of this chapter, and the guide already carries the
full `CS_KEY_*` / `CS_MOUSE_BUTTON_*` / `CS_GAMEPAD_*` tables and diagram **DG-4**.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Event` base — `Handled`, `GetEventType`/`GetName`, `IsInCategory`, category flags table
- [ ] `EventDispatcher` — `Dispatch<T>` (lambda + `CS_BIND_EVENT_FN` forms), return-bool consumption contract
- [ ] Every concrete event type + its accessors: `WindowResizeEvent`, `WindowCloseEvent`, app tick/update/render events if public, `KeyPressedEvent` (repeat count), `KeyReleasedEvent`, `KeyTypedEvent`, `MouseButtonPressed/ReleasedEvent`, `MouseMovedEvent`, `MouseScrolledEvent`
- [ ] `Input` — `IsKeyPressed`, `IsMouseButtonPressed`, `GetMousePosition`/`GetMouseX`/`GetMouseY`; gamepad polling (enumerate from `Input.h` — axes/buttons/deadzone, E7)
- [ ] Code tables — `CS_KEY_*`, `CS_MOUSE_BUTTON_*`, `CS_GAMEPAD_*` (full tables, values, GLFW correspondence)

## Sections to write

1. Event class hierarchy mini-diagram (Mermaid `classDiagram`). <!-- TODO(D7) -->
2. Entries per checklist; each event entry states *when the engine fires it* and *whether it propagates after Application handles it* (resize does, close doesn't — see [the guide's propagation section](../guide/events-and-input.md#how-an-event-reaches-your-code)). <!-- TODO(D7) -->
3. Gamepad section: polling model, connection handling, deadzone defaults. <!-- TODO(D7) -->

---
*Changelog:*
