# Cosmic showcase kit (DOC05)

Status: AP-Q1 deliverable, 2026-09-20, captured on `main` at the qualified SHA recorded in
[`../plans/app-platform-2026-09-18/evidence/AP-Q1/release-report.md`](../plans/app-platform-2026-09-18/evidence/AP-Q1/release-report.md).
Every PNG is 1920x1080 or the window's own size and at most 1 MB. Whole-window captures are Windows
screenshots of the real editor / app window; viewport-only captures are the engine's PNG readback.

## What Cosmic is (website blurb, 150 words)

Cosmic is a small, self-contained 2D application engine for Windows, written in C++20, with an editor
(Starforge) built on the same runtime. It turns instrument panels, telemetry dashboards, lab simulations
and small interactive tools into shipped desktop applications without a web stack. An app is a set of
screens (UI scenes whose widgets are bound to a live data bus), a flow graph that decides which screen is
shown, and a few C++ services that publish the numbers. The editor arranges screens with a rect gizmo,
links every widget, signal and channel back to its source line, rebuilds the app module while it runs and
resumes on the same screen, and packages the result as one folder: a renamed exe, two DLLs, the assets
and a boot file. Every claim is backed by an acceptance catalog run through a runner that refuses phantom
passes, and by two shipped apps: SF_Telem, a serial telemetry recorder, and PendulumLab.

## One line per feature

- **DataBus** — host-owned, GL-free channel store (number / bool / string, ring history, producers,
  subscriptions) that survives a module reload.
- **App services** — `CS_SERVICE` classes with a fixed frame order, scene rebinding and `CS_PANEL`
  ImGui/ImPlot hosted panels drawn inside a UI element's rect.
- **Bound widgets** — value text, bar/arc gauge, indicator, plot, slider, toggle and hosted panel,
  reflected, serialized and rendered by the 2D UI system with a preview mode for edit time.
- **Screens and flows** — every screen is a UI scene; a flow graph with channel guards, `when`
  transitions, key signals and a start state decides which one is live.
- **Screens panel** — new screen, set-as-start, rename, create-and-link script; refuses a module
  without the `CS_SCREENS` markers.
- **Rect gizmo** — move and eight resize handles with 1/8 px and 16 px snapping, one undo entry per gesture.
- **Source links** — Open / Reveal for a screen script, a service, a panel, a bound channel or a
  button signal from the Inspector, the Screens panel, the DataBus panel and the viewport menu.
- **Live loop** — a debounced `src/` watcher rebuilds the app module during Play and resumes on the
  same flow state with the bus kept; a compile error stops Play and shows the error in the Console.
- **Template picker** — App / Game / Blank kinds plus FlowDemo and ForgePong samples, scaffolded from
  trees on disk.
- **One package layout** — `package.bat`, the editor's File > Package and CI stage the same tree;
  packaged apps write only under `user://`.
- **PendulumLab** — the damped-pendulum sample: RK4 at 240 Hz checked against an analytic reference,
  Home / Lab / Settings screens, a hosted ImPlot phase plot, an in-app self-test.
- **SF_Telem** — the qualified serial telemetry app (record, autosave, export, replay) that runs
  unchanged on the 2D trunk.
- **Acceptance runner** — manifests, capability gating, per-case deadlines, hashed goldens; a missing
  GPU or COM port is `ENVIRONMENT_BLOCKED`, never a pass.

## The twelve captures

| # | File | Caption |
| --- | --- | --- |
| 1 | `01-homescreen-template-picker.png` | Starforge homescreen with the New Project modal: the template combo lists App, Game and Blank; the Samples buttons open FlowDemo and ForgePong from the trees on disk. |
| 2 | `02-rect-gizmo-arranging-screen.png` | A PendulumLab screen being arranged in the viewport with the UI rect gizmo (move surface, eight resize handles, snap chips). |
| 3 | `03-screens-panel-linked-script.png` | The Screens panel: the flow's screens with the start marker, each linked to its scene and its screen script. |
| 4 | `04-inspector-open-source.png` | The Inspector on a screen entity: the NativeScript row with its Open source / Reveal items. |
| 5 | `05-pendulumlab-flow-graph.png` | PendulumLab's flow graph in the flow editor: Home, Lab, Settings and the Stopped overlay with the `when pendulum.energy < 0.01` guard. |
| 6 | `06-databus-panel-live.png` | The DataBus panel during Play: `pendulum.*` and `settings.*` channels with values, age and producer. |
| 7 | `07-lab-screen-live.png` | The Lab screen live in editor Play: the angle plot, the energy gauge and the damping slider bound to the bus. |
| 8 | `08-hosted-implot-phase-plot.png` | The hosted ImPlot phase plot (`CS_PANEL("PhasePlot")`) drawn inside its UI element during editor Play (crop of #7; #10 shows the same panel in the packaged player). |
| 9 | `09-live-loop-chip-after-rebuild.png` | After an automatic rebuild of the app module while Play was running: the Console's `[build] SUCCESS` / `[Live] Resumed Play on 'Lab'` lines and the status bar back to `PLAYING · module ok · Live`. |
| 10 | `10-packaged-pendulumlab.png` | The packaged `PendulumLab.exe` (File > Package output) running from its own folder. |
| 11 | `11-sf-telem-main-screen.png` | SF_Telem's main screen. |
| 12 | `12-acceptance-run-summary.png` | The acceptance runner (`Run-Acceptance.ps1`) in a terminal, followed by the per-manifest summary of the AP-Q1 qualification chain. |

All twelve were taken (2026-09-20, `main` at `fa1223a`); the capture method and per-file details are in the release report §7.1.

## SF_Telem engineering screens (added 2026-09-26)

Four more whole-window captures of the real `SF_Telem` app (`CosmicApp.exe --project SF_Telem`, Release, `main` at
`9ecc118`), 1280x720, taken on the campaign's VMware VM (software OpenGL 4.5, llvmpipe). No hardware was attached,
so the bench screens show their "waiting for telemetry" state.

| # | File | Caption |
| --- | --- | --- |
| 13 | `13-sf-telem-drivetrain-analysis.png` | The Analysis workspace — the drivetrain spin-up calculator: chassis/wheel diagram (rear 3.5 in, front 2.5 in, level chassis), speed and acceleration curves vs time, and the KPIs (top speed, peak acceleration, launch force vs traction cap: "launch is traction-limited"). |
| 14 | `14-sf-telem-drive-esc-bench.png` | The Testing workspace, single drive-ESC bench: the robot's drivetrain render with live RPM / speed / current / voltage callouts, the serial-link panel and the wiring instructions for the ESP32 test firmware. |
| 15 | `15-sf-telem-weapon-esc-bench.png` | The single weapon-ESC bench: the weapon render with weapon RPM, tip speed, current, voltage and temperature callouts. |
| 16 | `16-sf-telem-homescreen.png` | SF_Telem's homescreen: Main Telemetry, Testing, Analysis and Replay workspaces. |
