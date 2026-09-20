# PendulumLab

The Cosmic App Platform showcase (contract §8 sample row; catalog F02, Y01–Y03). It demonstrates
the platform's one idea end to end: **the logic is a C++ service, the visuals are scenes authored
in the editor, and the DataBus is the only thing between them.**

## What it shows

- **`PendulumService`** (`src/services/`) integrates a damped pendulum with the classic RK4 of
  `math/Integrators.h` at the project's fixed step (240 Hz). It reads its parameters from bus
  channels (`settings.length`, `settings.gravity`, `settings.damping`, `settings.theta0_deg`,
  `settings.small_angle`), publishes `pendulum.angle_deg`, `pendulum.omega`, `pendulum.energy`,
  `pendulum.period_est` (an upward-zero-crossing estimate) and `pendulum.running`, and handles the
  signals `pendulum.start` / `stop` / `reset` / `nudge`. Its `CS_PANEL("PhasePlot")` draws an ImPlot
  theta–omega scatter from the bus history inside the Lab screen's `UiHostedPanel` element.
- **Screens** (`scenes/*.cscene`, `src/screens/*Screen.h`): `Home` (an animated title, Start /
  Settings / Quit), `Lab` (the rig — pivot, rod, bob sprites positioned from the bus every frame by
  `LabScreen`; readouts, a gauge, a `UiPlot` of angle and omega, the hosted phase plot, controls),
  `Settings` (sliders and a toggle that write `settings.*`; the service re-reads them every step) and
  the `Stopped` overlay.
- **The flow** (`flows/Main.cflow`): `Home →(start_clicked) Lab →(settings_clicked) Settings
  →(back_clicked) Lab`, `key:Escape` back, `@quit` from Home, and a **channel-guarded `when`
  transition** — `Lab` pushes the `Stopped` overlay when `pendulum.energy < 0.01`; `resume_clicked`
  pops it (the overlay's script also emits `pendulum.reset`, so the guard does not re-arm).
- **Small-angle toggle.** By default the service integrates the full `sin(theta)` model. With
  `settings.small_angle = true` it integrates the linearised model, which is what the analytic
  F-PENDULUM reference (`tests/fixtures/ap04/pendulum_reference.csv`) solves; `tests/test_pendulumlab.cpp`
  compares the two within 1e-4 rad over 10 s at 5 degrees, checks monotone energy decay under damping,
  the period estimate against `2 pi sqrt(L/g)`, and bit-identical repeat runs.

## Build (standalone, from a clean SDK path — Y01)

```
cmake -S Projects/PendulumLab -B <fresh dir outside the tree> -A x64 -DCOSMIC_SDK_DIR=<SDK checkout> -DGAME_OUTPUT_DIR=<fresh dir>
cmake --build <fresh dir> --config Release --parallel
```

`tests/acceptance/fixtures/Run-AP04Sample.ps1` records the exact commands. The root build skips this
folder (`COSMIC_SKIP_PROJECTS`); its CMakeLists refuses an in-tree `add_subdirectory`.

## Run

Open the folder in Starforge (Open… ▸ `Projects/PendulumLab`), Build Scripts, Play — or package it
(File ▸ Package) and run `PendulumLab.exe`. With `kind = "app"` the editor auto-builds on save and
resumes Play on the same screen with the bus intact.

## Self-test (Y02)

`Y02SelfTestService` (`src/Y02SelfTest.cpp`) is registered alongside the physics and is inert unless
`COSMIC_Y02_SELFTEST=<result.json>` is set. Armed, it navigates the real flow by feeding the signals
the buttons emit (Home → Lab → Settings → Lab → Escape → Home), samples `pendulum.angle_deg`, checks
the PhasePlot hosted panel was drawn and reads the `UiPlot` rect back from the framebuffer, then
writes the verdict JSON (PASS exits 0, FAIL exits 1). AP-Q1 runs it against the packaged exe.
