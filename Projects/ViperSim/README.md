# ViperSim

UAV simulator & flight-computer workbench for **Viper** (dual-motor tailsitter).
The Cosmic-side counterpart to what Gazebo+SITL is for ArduPilot: fly the vehicle
in software, develop the same C++ control code that will run on the Teensy 4.1,
and replay every flight. Plan: [`docs/plans/04-viper-sim-plan.md`](../../docs/plans/04-viper-sim-plan.md).

## Status

| Phase | State |
| --- | --- |
| **P0** skeleton (SimHub, config, telemetry schema, screens) | ✅ 2026-07-02 |
| **P1** `IDynamics` + drop test + dynamics decision | ✅ 2026-07-02 |
| P2+ viper-fc, tuning, energy, transition, orbit, HIL | planned |

## Running

Build the engine + projects (`build_all.bat` or the normal CMake build), then
launch straight into it:

```
CosmicApp.exe --project ViperSim
```

or pick **ViperSim** from the Launcher. The homescreen tiles route to the
screens; **Flight** and **Replay** are live (P1), the rest are stubs for their
phases.

### Drop test (P1)

Flight screen → set a drop height → **Drop**. The airframe falls under gravity,
contacts the ground (spring-damper), and settles; the run records to
`user://recordings/viper_drop`. **Flush → Replay**, switch to the Replay screen,
**Load latest drop**, and scrub — the airframe re-falls from recorded truth.

## Layout

```
config/viper.toml            airframe params (E10) — edit, relaunch, no recompile
                             (synced to assets/projects/ViperSim/config at build)
src/
  ViperSim.{h,cpp}           root layer: homescreen tiles -> screens
  SimHub.{h,cpp}             owns IDynamics, DataRecorder/Player, config, mode
  sim/
    IDynamics.h              dynamics abstraction (Step/GetTruth/SetWind/Reset)
    ComposableDynamics.*     6DOF rigid body, E11 RK4; drop-test physics
  fc_glue/
    telemetry_schema.h       SensorFrame/ActuatorFrame/RigidState + channel schema
                             (the ONE contract shared with future viper-fc + GCS)
  screens/
    FlightScreen.*           3D drop-test viewport (orbit cam, live truth)
    ReplayScreen.*           DataPlayer scrub, airframe driven by recorded truth
docs/
  DYNAMICS_DECISION.md       JSBSim-vs-composable record (P1)
```

## Design rule

Engine ships **generic verbs** (RK4, tables, RNG, recorder, 3D renderer, config);
this app owns **domain logic** (tailsitter dynamics, mixers, aero, the mode
machine). See [`docs/plans/00-MASTER-ROADMAP.md`](../../docs/plans/00-MASTER-ROADMAP.md).
