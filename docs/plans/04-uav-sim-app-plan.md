# ViperSim — UAV Simulator & Flight-Computer Workbench (App Plan)

> **Goal:** a Cosmic project (`Projects/ViperSim/`) that does for **Viper** (VTOL tailsitter FPV
> aircraft, 2026-06-27 design doc) what Gazebo+SITL does for ArduPilot: fly the vehicle in software,
> develop the **same C++ control code that will run on the Teensy**, replay every flight, and
> eventually drive the real board (HIL) and a physical attitude rig.
>
> Engine prerequisites are in [`03-uav-sim-engine-features.md`](03-uav-sim-engine-features.md) (E1–E7)
> and [`05-3d-viewport-plan.md`](05-3d-viewport-plan.md). This doc is the **app**.

---

## 1. The architecture that makes "write once, fly on Teensy" work

The single most important decision: **the flight computer code is a separate, dependency-free C++
library** — call it **`viper-fc`** (its own folder/repo, not inside the Cosmic tree) — and *everything
else plugs into it through a small HAL (hardware abstraction layer)*.

```
                 ┌────────────────────── viper-fc (portable C++17, no OS, no Arduino.h) ─┐
                 │  estimator (complementary → EKF)                                      │
                 │  mode machine (HOVER / TRANSITION / CRUISE / ORBIT / RTL / FAILSAFE)  │
                 │  cascaded controllers + gain schedule      mixers (hover & cruise)    │
                 └───────────────▲───────────────────────────────▲───────────────────────┘
                        IHal interface (sensors in, actuators out, clock, log)
        ┌────────────────────────┴─────────────┬─────────────────┴───────────────────────┐
        │ SimHal (inside ViperSim DLL)         │ TeensyHal (PlatformIO project)          │
        │ reads simulated sensors,             │ reads real IMU/baro/GPS/pitot drivers,  │
        │ writes to simulated motors/servos    │ writes PWM/DShot + servo outputs        │
        └──────────────────────────────────────┴──────────────────────────────────────────┘
```

**`IHal` sketch** (keep it boring and C-like; this is the contract everything hangs off):

```cpp
struct SensorFrame  { uint64_t t_us; Vec3 gyro_rads, accel_mss, mag_uT;
                      float baro_pa, airspeed_pa; GpsFix gps; float vbat_V, ibat_A; };
struct ActuatorFrame{ float motor[4]; float servo[4]; };  // normalized 0..1 / -1..1

class IHal {
public:
    virtual bool     ReadSensors(SensorFrame& out) = 0;   // true if a new frame is ready
    virtual void     WriteActuators(const ActuatorFrame&) = 0;
    virtual uint64_t NowMicros() = 0;
    virtual void     Log(const char* key, float value) = 0; // fanned out to DataRecorder / SD card
};
```

- **No `Arduino.h`, no `std::thread`, no heap after init** inside `viper-fc` — then the identical
  `.cpp` files compile in the ViperSim DLL (MSVC) and in a **PlatformIO Teensy 4.x project**
  (`platformio.ini` with `framework = arduino`, `viper-fc` referenced via `lib_deps = symlink://…`).
  PlatformIO is the honest answer to "write it in C++ and have it translate to what I can upload":
  it drives the same toolchain as the Arduino IDE but treats your code as a normal C++ library with
  proper builds. (Fallback: `arduino-cli` with the library vendored — same idea, fewer features.)
- The Teensy `main.cpp` is ~50 lines: init drivers → loop `{ ReadSensors → fc.Step() → WriteActuators }`.
- **Determinism bonus:** because `viper-fc` only sees `SensorFrame`s and a microsecond clock, you can
  replay a recorded sensor log through it *offline* and diff controller outputs between code versions
  — regression tests for control changes, no simulator needed.

### Integration modes (all four, staged — this is the "pitch more ideas" menu)

| Mode | What runs where | What it's for | When |
| --- | --- | --- | --- |
| **SITL (in-process)** | `viper-fc` compiled into ViperSim, `SimHal` | Daily control development; single-step debugging of the mode machine in Visual Studio | P2 |
| **Replay-through-FC (offline)** | recorded `SensorFrame` log → `viper-fc` on desktop | Regression-test controller changes against real/sim flight logs | P2+ (nearly free) |
| **HIL (hardware-in-loop)** | sim physics on PC; **real Teensy** runs `viper-fc`; sensor frames down / actuator frames up over USB-serial (COBS+CRC frames, engine E5) | Validates timing, sensor drivers, the serial protocol, and the exact binary that will fly | P6 |
| **Rig output ("iron bird lite")** | sim (or HIL) attitude → 3-axis servo gimbal holding a foam model on a rod; sim commands the rig over the same serial link while the screen shows FPV | Physical, intuitive demo of roll/pitch/yaw + servo-direction sanity checks before the airframe exists | P7 |
| **GCS/MAVLink (optional)** | ViperSim speaks MAVLink over UDP (engine E4) to QGroundControl / Mission Planner | Free professional ground-station UI, mission upload, plus the door to comparing against ArduPilot SITL itself | P8 |

**HIL notes (worth deciding early):** run the serial link at 2 Mbaud+ (Teensy USB CDC is fast);
timestamp every frame with the *sim* clock; make `SimHal` and the HIL bridge implement the same
interface inside ViperSim so switching SITL↔HIL is a dropdown, not a rebuild. Expect and measure
latency — display round-trip time in the UI; the control loop should tolerate one frame of delay
(it will see similar on real hardware).

**Rig notes:** 2 servos (pitch+roll) on a rod is already hugely useful; yaw needs a slip ring or
just accept wire wrap for short demos. Drive it with the same `ActuatorFrame`-style framed protocol;
clamp rates in firmware so a sim glitch can't slam the servos.

---

## 2. Simulation core (inside ViperSim)

### 2.1 Vehicle dynamics — 6DOF rigid body, written in the app
State: `pos (NED), vel, quat, omega_body`; integrate with RK4 at fixed substeps (engine E1/E3;
e.g. 60 Hz engine tick × 8 substeps = 480 Hz physics; FC stepped at its real rate, e.g. 500 Hz→ match).
Forces/moments as **composable components** (a tailsitter is "just" a sum of these):

1. Gravity (constant NED).
2. **Rotor/propulsion**: per motor — first-order motor lag, thrust `k_f·ω²`, torque `k_q·ω²`,
   simple inflow reduction with axial airspeed. Coefficients from static thrust-stand numbers when
   you have them; catalog data until then.
3. **Aero surfaces**: per-panel lift/drag from α using **full-envelope polars** — this is the
   tailsitter-critical piece: the wing passes through α = 90° in hover. Standard trick: blend XFLR5
   polars (attached flow, |α| < ~15°) into flat-plate theory (`Cl = 2·sinα·cosα`, `Cd = 2·sin²α`)
   for post-stall. Elevon deflection shifts the local incidence. Prop wash over elevons = extra
   local dynamic pressure term in hover (crude but essential — it's the only control authority at zero
   airspeed).
4. Fuselage drag; ground plane (spring-damper on landing-leg points); wind + gusts (constant + optional
   Dryden turbulence later).

**Config-driven**: one `viper.toml`/`json` the app owns — masses, inertia tensor, geometry, motor/prop
coefficients, polar tables — so airframe iterations don't recompile. Numbers flow straight from the
Viper doc's mass/propulsion spreadsheet as it firms up.

### 2.2 Sensor models (feeding `SensorFrame`)
Sampled from truth at each FC step: gyro/accel (bias random-walk + white noise + optional mount
misalignment), baro (noise + slow drift), GPS (5–10 Hz, latency ~100–200 ms, position noise),
magnetometer (declination + noise), pitot (unreliable below ~5 m/s — model that, it drives transition
logic), battery (see 2.4). All noise parameters in the config file with a "perfect sensors" toggle —
develop against perfect, then harden the estimator against realistic.

### 2.3 Flight modes to develop in `viper-fc` (mirrors the Viper doc §3.1)
1. **Hover/attitude**: quaternion attitude error → rate PIDs → hover mixer (differential thrust +
   elevons-in-prop-wash). Position hold on top (GPS/baro).
2. **Transition state machine** — *the* deliverable. Explicit states
   `HOVER → ACCEL (pitch-over schedule) → BLEND (mixers crossfade on airspeed) → CRUISE` and reverse
   (`DECEL/flare`). Gains and mixer weights scheduled on airspeed + pitch. The sim exists so this can
   fail a thousand times safely; instrument everything (record scheduled-gain values, blend factor,
   per-effector commands to DataRecorder).
3. **Cruise**: TECS-style energy controller (throttle ↔ total energy, pitch ↔ energy balance) + L1
   or simple PD lateral track.
4. **Orbit / loiter-on-ROI** (signature feature): circular path around a ground point with wind-drift
   compensation + camera-pointing solution (gimbal angles or fuselage-yaw pointing); HUD shows
   ROI-in-frame error. Power draw comparison vs hover displayed live (see 2.4 — this quantifies the
   whole thesis of the project).
5. **RTL + vertical land**, failsafe triggers (link loss → RTL, low battery → land, geofence/altitude
   ceiling per FAA table, envelope protection). The sim UI gets fault-injection buttons (kill link,
   drop GPS, freeze pitot, motor-out).

### 2.4 Energy/endurance model — do this EARLY (Viper doc §3.2 "reality check")
Battery: capacity, internal resistance, sag; per-motor electrical power from `ω`,`torque` + ESC/motor
efficiency. Live Wh counter + a dedicated **Energy screen**: hover-power vs cruise-power, projected
endurance for a mission profile (X min hover + Y min cruise + Z min orbit). This single screen answers
"is 60 min cruise / 30 min hover on one pack even possible" *before* buying parts — arguably the
first genuinely useful output of the whole simulator, available by P3 with only the hover model.

---

## 3. App structure (follows the SF_Telem patterns — reuse, don't reinvent)

```
Projects/ViperSim/
├── CMakeLists.txt              (template-generated; also builds ../viper-fc sources into the DLL)
├── config/viper.toml           (airframe + sensors + battery)
└── src/
    ├── ViperSim.h/.cpp         root layer: homescreen tiles → screens (SF_Telem pattern)
    ├── SimHub.h/.cpp           the app's TelemHub-equivalent: owns World, FC instance/bridge,
    │                           DataRecorder/Player, SerialLink (HIL/rig), mode + fault injection
    ├── sim/                    dynamics: RigidBody6DOF, RotorModel, AeroPanel, Battery, Wind, Sensors
    ├── fc_glue/                SimHal, HilBridge (serial framed), RigOutput
    ├── screens/
    │   ├── FlightScreen        3D viewport (orbit cam) + FPV inset + PFD (ADI/alt/airspeed/power HUD)
    │   ├── TuningScreen        gain sliders (live), step-response plots, per-axis PID traces
    │   ├── EnergyScreen        §2.4
    │   ├── TransitionScreen    state-machine visualizer + blend/schedule traces
    │   └── ReplayScreen        DataPlayer + TelemetryPanel transport (exists in engine)
    └── ui/                     instruments (ADI widget, HSI, battery bar) drawn with ImGui/Renderer2D

viper-fc/                       (SEPARATE repo/folder — portable)
├── include/viperfc/*.h  src/*.cpp     estimator, modes, control, mixers, IHal
└── firmware/ (PlatformIO project for Teensy 4.x consuming viper-fc + TeensyHal)
```

Telemetry: register the FC's internals (`fc.attitude_est`, `fc.mode`, `fc.mix[i]`, …) *and* sim truth
(`truth.attitude`, …) as separate DataRecorder entities → the estimator-vs-truth overlay in replay is
the estimator's report card.

---

## 4. Phases with acceptance demos

| Phase | Build | Demo that proves it (record each one) |
| --- | --- | --- |
| **P0** | Project skeleton from launcher template; SimHub; config loading; screens stubbed | App opens from launcher, tiles navigate |
| **P1** | 6DOF core + gravity + ground; 3D viewport (E2-S1/S2) shows falling/settling box; DataRecorder wired | Drop test replayable in ReplayScreen |
| **P2** | `viper-fc` skeleton + `IHal` + SimHal; rate-then-attitude hover loop w/ perfect sensors; TuningScreen | Commanded 20° roll step: clean response curve, no divergence; offline replay-through-FC runs |
| **P3** | Full hover: position hold, battery model, sensor noise + complementary filter; gamepad (E7); EnergyScreen | 5-min manual+assisted hover under gusts; hover-power number vs Viper doc estimate |
| **P4** | Aero panels w/ full-envelope polars; cruise mode (TECS-lite); transition state machine both directions | Scripted VTOL→cruise→VTOL with altitude excursion < target; blend traces recorded |
| **P5** | Orbit-on-ROI + camera pointing; failsafes + fault injection; FPV inset (E2-S3) | Orbit holds ROI centered in gusty wind; link-kill → RTL → vertical land, hands off |
| **P6** | HIL bridge (E5 framing): Teensy runs `viper-fc`, sim streams sensors | Same P4 transition flown by the physical Teensy; latency figure on screen |
| **P7** | Gimbal rig output + safety clamps | Rig mirrors sim attitude live; servo directions verified against sim |
| **P8** (opt) | UDP (E4) + MAVLink heartbeat/attitude/mission subset → QGroundControl | QGC shows live attitude/map; optional cross-check vs ArduPilot SITL quadplane |

Rule of thumb for sequencing inside each phase: **truth first, estimation second, control third** —
never tune a controller against an estimator bug.

## 5. Validation habits (industry practice, cheap to adopt now)

- Every phase demo = a recorded session committed under `recordings/regression/` and replayed after
  changes (the engine's replay makes this nearly free).
- Unit tests in `viper-fc` (plain doctest, no engine): quaternion integration, mixer saturation
  behavior, mode-machine transition table, estimator convergence on canned data.
- One markdown "flight test card" per milestone (what was commanded, expected, observed) — the same
  discipline the real flight program will need, rehearsed in sim.
