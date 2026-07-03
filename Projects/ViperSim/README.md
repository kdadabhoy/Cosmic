# ViperSim

UAV simulator & flight-computer workbench for **Viper** (dual-motor tailsitter).
The Cosmic-side counterpart to what Gazebo+SITL is for ArduPilot: fly the vehicle
in software, develop the same C++ control code that will run on the Teensy 4.1
(the [`viper-fc`](viper-fc/) library), replay every flight, then drive the
real board (HIL) and a physical rig. Plan:
[`docs/plans/04-viper-sim-plan.md`](../../docs/plans/04-viper-sim-plan.md).

## Status

| Phase | State |
| --- | --- |
| **P0** skeleton (SimHub, config, telemetry schema, screens) | ✅ 2026-07-02 |
| **P1** `IDynamics` + drop test + dynamics decision | ✅ 2026-07-02 |
| **P2** viper-fc + SimHal + attitude loop + Tuning screen | ✅ code 2026-07-02 — run the 20° step + replay-through-FC |
| **P3** full hover: pos hold, sensors, energy, gamepad, alerts | ✅ code 2026-07-02 — **Gate G1**: click "Run G1" |
| **P4** full-envelope aero, cruise, transition machine | ✅ code 2026-07-02 — **Gate G2**: click "Run G2" (+ CG/speed sweeps) |
| **P5** orbit-on-ROI, failsafes, fault injection, FPV inset | ✅ code 2026-07-02 — **Gate G3**: click "Run G3" + fault checklist |
| **P6** HIL (Teensy over serial, latency on screen) | software ✅ 2026-07-02 — needs the physical Teensy 4.1 |
| **P7** gimbal rig output (rate-clamped) | software ✅ 2026-07-02 — needs the physical rig |
| P8 MAVLink → QGroundControl (optional) | unstarted |

Gate acceptance: each "Run G#" scenario reports PASS/FAIL per check and
auto-flushes a recording to `user://recordings/regression/…`; commit accepted
runs under [`recordings/regression/`](recordings/regression/) and fill the
matching card in [`docs/test-cards/`](docs/test-cards/).

## Running

Build the engine + projects (`build_all.bat` or the normal CMake build), then
launch straight into it:

```
CosmicApp.exe --project ViperSim
```

or pick **ViperSim** from the Launcher. All five tiles are live:

- **Flight** — arm/takeoff/mode buttons, gamepad flying (E7), wind + gusts,
  fault injection, SITL↔HIL dropdown, rig output, gate runners, FPV inset.
- **Tuning** — live viper-fc gains, attitude step commands + response plots,
  offline replay-through-FC regression.
- **Energy** — pack state, hover-budget bar, measured-vs-model power table
  (230 W hover / 106 W cruise), mission endurance calculator.
- **Transition** — state machine visualizer (HOVER→ACCEL→BLEND→CRUISE and
  back), blend/airspeed/altitude traces, the G2 runner.
- **Replay** — scrub any recording; the airframe re-flies from recorded truth.

### Drop test (P1 regression)

Flight screen → "Drop test" node → **Drop**. Falls, contacts (spring-damper),
settles, records to `user://recordings/viper_drop`; scrub it in Replay.

### HIL (P6)

Flash [`viper-fc/firmware/`](viper-fc/firmware/) onto a Teensy 4.1
(`pio run -t upload`), pick **HIL** in the Flight screen's backend section,
connect the COM port. The sim streams sensors; the board answers with actuator
frames; the round-trip latency shows on screen.

## Layout

```
config/viper.toml            airframe + gains (E10) — edit, relaunch, no recompile
src/
  ViperSim.{h,cpp}           root layer: homescreen tiles -> screens
  SimHub.{h,cpp}             sim backbone: dynamics, sensors, battery, FC backend,
                             scenarios/gates, faults, gamepad, alerts, recording
  sim/
    IDynamics.h              dynamics abstraction (Step/GetTruth/SetWind/Reset)
    ComposableDynamics.*     6DOF full-envelope tailsitter (motors, aero, wash, wind)
    Sensors.h                noise models (E15), GPS latency, pitot low-speed truth
    Battery.h                pack + electrical power model (230 W / 106 W numbers)
    Wind.h                   steady + E14 gusts (deterministic)
  fc_glue/
    telemetry_schema.h       sim-side schema (truth/sensors entities) + viperfc re-exports
    FcBackend.h              SITL backend (= the plan's SimHal) behind IFcBackend
    HilBridge.h              HIL backend: E5 framed serial to the Teensy + latency
    RigOutput.h              P7 gimbal rig: rate-clamped RIG,r,p,y @ 50 Hz
  screens/                   Flight / Tuning / Energy / Transition / Replay
viper-fc/                    the portable flight computer (see below)
docs/
  DYNAMICS_DECISION.md       JSBSim-vs-composable record (P1)
  test-cards/                G1/G2/G3 flight test cards (playbook §5.4)
recordings/regression/       committed gate baselines (see its README)
```

The portable flight computer itself lives at [`viper-fc/`](viper-fc/)
— estimator, mode machine, controllers, mixers, failsafe supervisor, HIL wire
protocol, unit tests (`ViperFcTests`), and the Teensy firmware. It is
engine-free C++17 so the identical headers compile on the Teensy.

## Design rule

Engine ships **generic verbs** (RK4, tables, RNG, recorder, 3D renderer, config,
audio, serial write); this app owns **domain logic** (tailsitter dynamics,
mixers, aero, the mode machine). See
[`docs/plans/00-MASTER-ROADMAP.md`](../../docs/plans/00-MASTER-ROADMAP.md).
