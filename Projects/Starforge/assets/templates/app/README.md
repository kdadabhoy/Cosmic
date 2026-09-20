# @PROJECT_NAME@

An **app** project scaffolded from Starforge's `app` template (Cosmic App Platform). The shape:

```
project.cproj              kind = "app", startup_flow = "flows/Main.cflow", fixed_dt_hz = 60
flows/Main.cflow           Home -> Dashboard -> Settings; Escape steps back; Quit from Home
scenes/Home.cscene         canvas + ortho camera + the widgets each screen shows
scenes/Dashboard.cscene
scenes/Settings.cscene
src/Module.cpp             CS_SERVICE(AppService) + one CS_SCRIPT per screen (between the CS_SCREENS markers)
src/services/AppService.*  the app's logic: publishes app.uptime / app.sine / app.counter every tick,
                           handles counter.increment / counter.reset / settings.defaults,
                           draws the "Diagnostics" hosted ImGui panel
src/screens/*Screen.h      per-display touches only (a pulse, a tint) — no logic
assets/ui/*.png            placeholder art referenced by UiImage elements
CMakeLists.txt             the module DLL build (same as the game template)
```

## How it fits together

- **Logic lives in the service.** `AppService` (an `AppService` subclass registered with
  `CS_SERVICE`) is constructed once per run by the host, ticked before the UI and the flow, and
  torn down before the module unloads. It talks to the world only through the **DataBus**
  (`Bus().Set("app.sine", v)`) and through **signals** (`OnSignal("counter.increment", …)`).
- **Visuals live in the scenes.** The screens are ordinary 2D scenes: a `Canvas` entity, an
  orthographic `Camera`, and bound widgets — `UiValueText` (a readout), `UiGauge`, `UiPlot`
  (history of one or more channels), `UiIndicator`, `UiSlider` / `UiToggle` (write a channel,
  optionally emit a signal), `UiButton` (emit a signal), `UiImage`, and `UiHostedPanel` (a rect
  the service fills with ImGui through `CS_PANEL`). Edit them in Starforge; nothing here needs
  a rebuild to move a widget or change a channel name.
- **Navigation lives in the flow.** `flows/Main.cflow` maps button signals and `key:Escape`
  to screen changes; `@quit` closes the app.
- **Screen scripts** (`src/screens/<Name>Screen.h`, attached to each screen's `Canvas` entity as a
  `NativeScript`) read the bus through `Data()` for per-display touches. Starforge ▸ Screens ▸
  New Screen generates one from the stub and inserts its `CS_SCRIPT` between the markers in
  `Module.cpp`.

## Live loop

With `kind = "app"` the editor's auto-build is on: save a file under `src/`, the module rebuilds
and Play resumes on the same screen with the bus intact — so readouts and plots continue. Keep
state you care about on the bus (see `AppService::OnAttach`, which recovers `app.uptime` and
`app.counter`), not in members.

## Build outside the editor

```
cmake -S . -B build -A x64 -DCOSMIC_SDK_DIR=<path to the Cosmic SDK checkout> -DGAME_OUTPUT_DIR=<this folder>/build
cmake --build build --config Release --parallel
```

The packaged app (File ▸ Package in Starforge) is the player exe + this module DLL + `project.cproj`,
`flows/`, `scenes/` and `assets/`.
