# API Reference — Core Runtime

> **STATUS: SKELETON** — to be filled by work order **D6** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth — enumerate every public symbol in them):**
`Cosmic/src/core/Core.h`, `core/Application.h`, `core/Layer.h`, `core/Timestep.h`,
`core/Log.h`, `core/Window.h` *(client-reachable via `Application::GetWindow()`)*,
`Cosmic/src/Cosmic.h` *(plugin exports: `CreatePluginLayer`, `InitializePluginContexts`,
`HostContext`)*.

**Read first:** [`../guide/getting-started.md`](../guide/getting-started.md), then
[`../guide/project-anatomy.md`](../guide/project-anatomy.md) (application lifecycle, layers,
ownership) and [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md) (the timeline, pause,
fixed vs variable) — this chapter is the formal lookup behind those guides. For everything under
`core/Window.h`, plus `Application`'s window-facing members (`GetViewportPos`/`GetViewportSize`,
`SetRenderWhileDragging`, `SetPauseOnMinimize`), the client-facing source is
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md) (D60) — it replaced root
README §24, so don't re-derive it here.

> **Two `Application.h` doc-comment corrections D60 verified against source**, worth folding into
> the entries: `GetViewportPos`/`GetViewportSize` are documented at `Application.h:93` as *"GLFW
> window-space pixels"*, but the value comes from `ImGui::GetCursorScreenPos()` and is in **ImGui
> screen (desktop) pixels** — `WorkspaceLayer.h:271-278` has it right, and this is the space
> `Input::GetMouseScreenPosition()` lives in. And `SetPauseOnMinimize` defaults to **`false`**
> (D47's finding), which the header states correctly and the old README §3 did not.

## Coverage checklist *(starting point — the headers are authoritative, not this list)*

- [ ] `Core.h` — `COSMIC_API`, `Ref<T>`/`Scope<T>` + `CreateRef`/`CreateScope`, `CS_BIND_EVENT_FN`, assertion macros
- [ ] `Application` — `Get`, `Close`, `PushLayer`/`PushOverlay`, `GetWindow`, `GetFrameBuffer`, `GetWorkspaceLayer`, `GetViewportPos`/`GetViewportSize`, time control (`SetTimeScale`/`GetTimeScale`/`GetAbsoluteTime`/`UseFixedTimeStep`), `Pause`/`Resume`/`TogglePause`/`IsPaused`, `SetPauseOnMinimize`/`GetPauseOnMinimize`, `SetRenderWhileDragging`/`IsRenderWhileDragging`, `TransitionFromLauncherToWorkspace`/`TransitionToLauncher`
- [ ] `Layer` — all virtual hooks (`OnAttach`/`OnDetach`/`OnUpdate`/`OnFixedUpdate`/`OnRender`/`OnImGuiRender`/`OnEvent`), local-time API (`GetLocalTime`/`SetLocalTime`/`GetTimeScale`/`SetTimeScale`), `GetName`
- [ ] `Window` client surface — `GetWidth`/`GetHeight`, fullscreen + hotkey override (`SetFullscreenHotkeyOverride`, …), VSync — enumerate from `Window.h`
- [ ] `Log` — client/core logger macros (`CS_TRACE`…`CS_ERROR` and core variants), init behavior, sink locations
- [ ] `Timestep` — the type and its conversions
- [ ] Plugin boundary — `CreatePluginLayer`, `InitializePluginContexts`, `HostContext`, ownership rules across the DLL boundary

## Sections to write

1. Class intro blocks + entries per the checklist. <!-- TODO(D6) -->
2. A short "lifecycle map" callout linking the frame-loop diagram **DG-3** in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#dg-3--the-frame-sequence). <!-- TODO(D6) -->
3. Failure behaviors pinned per call (e.g. `Application::Get()` before construction = null deref — documented singleton-ordering note in [`../guide/project-anatomy.md`](../guide/project-anatomy.md#construction-order)). <!-- TODO(D6) -->

---
*Changelog: (append `YYYY-MM-DD — what changed` lines here as the API evolves).*
