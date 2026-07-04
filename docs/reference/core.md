# API Reference — Core Runtime

> **STATUS: SKELETON** — to be filled by work order **D6** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth — enumerate every public symbol in them):**
`Cosmic/src/core/Core.h`, `core/Application.h`, `core/Layer.h`, `core/Timestep.h`,
`core/Log.h`, `core/Window.h` *(client-reachable via `Application::GetWindow()`)*,
`Cosmic/src/Cosmic.h` *(plugin exports: `CreatePluginLayer`, `InitializePluginContexts`,
`HostContext`)*.

**Read first:** root README §1 (getting started), §3 (application lifecycle), §4 (layers),
§7 (time) — this chapter is the formal lookup behind those guides.

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
2. A short "lifecycle map" callout linking the frame-loop diagram in the root README. <!-- TODO(D6) -->
3. Failure behaviors pinned per call (e.g. `Application::Get()` before construction = null deref — documented singleton-ordering note in README §3). <!-- TODO(D6) -->

---
*Changelog: (append `YYYY-MM-DD — what changed` lines here as the API evolves).*
