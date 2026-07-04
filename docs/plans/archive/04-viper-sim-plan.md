# ViperSim — UAV Simulator & Flight-Computer Workbench (App Plan)

> **Rewritten 2026-07-01** against the current Viper document set (Proposal v0.3, Project Playbook
> v1.0, Software & Tools Decision Record, Cost & Weight Tracker — all 2026-07-01). Was
> `04-uav-sim-app-plan.md`; the architecture survives, the dynamics strategy and numbers are updated.
>
> **Status 2026-07-02 — P2–P7 CODE COMPLETE** (roadmap Phases 4–6). `viper-fc/` ships inside the
> project at `Projects/ViperSim/viper-fc/` (header-only portable lib + doctest suite
> `ViperFcTests` + PlatformIO Teensy firmware);
> ViperSim grew the full hover/transition/orbit/failsafe stack, all five screens, SITL↔HIL
> dropdown, rig output, and scripted gate scenarios with auto PASS/FAIL reports + auto-flushed
> regression recordings. **Gates G1–G3 remain a user run** (compile, click "Run G1/G2/G3", commit
> the recordings + fill the test cards in `Projects/ViperSim/docs/test-cards/`); P6/P7 physical
> acceptance needs the Teensy + rig hardware. Deviations from the letter of the plan: the fc-side
> telemetry schema moved into `viperfc/TelemetrySchema.h` (the plan predicted the move);
> `fc_glue/FcBackend.h` names the SITL seam `SitlBackend` with `using SimHal = SitlBackend`.
>
> **Goal:** a Cosmic project (`Projects/ViperSim/`) that does for **Viper** what Gazebo+SITL does
> for ArduPilot: fly the vehicle in software, develop the **same C++ control code that will run on
> the Teensy 4.1**, replay every flight, then drive the real board (HIL) and a physical rig.
> Engine prerequisites: [doc 03](03-simulation-engine-plan.md) (E-series) and
> [doc 05](05-3d-engine-plan.md) (S1–S3). This doc is the **app** (and the portable `viper-fc` library).

## 0. Locked Viper decisions this plan builds on (from the doc set)

| Decision | Value |
| --- | --- |
| Configuration | **Dual-motor tailsitter** (2 lift/cruise motors + elevons; quad-tailsitter fallback; quadplane shelved) |
| Control surfaces | Flying-wing 2-elevon assumed; conventional 4-surface still OPEN (Phase-2 aero closes it) — keep servo count data-driven |
| Design point | AUW 1.5 kg (NE 2.0 kg) · wing 0.30 m², AR 6 · cruise 20 m/s · stall ~8.1 m/s · Vmax ~25 m/s @ 200 W |
| Power | ~100 Wh Li-ion ~500 g, 4S nominal 14.8 V · hover ~230 W (~15.5 A) · cruise ~106 W (~7.2 A) |
| Endurance targets | ≥45 min economical cruise · ~35 min orbit @ 45 mph · **hover capped 3–5 min cumulative (enforced in software)** |
| Motors | ~1,490 gf max thrust each (selection in progress — thrust maps arrive as bench data) |
| Flight computer | Teensy 4.1, PlatformIO + Arduino framework |
| Dynamics engine | **ComposableDynamics (hand-rolled 6DOF)** shipped 2026-07-02 behind `IDynamics`; JSBSim kept as a drop-in option (provisional-closed, `Projects/ViperSim/docs/DYNAMICS_DECISION.md`). ~~JSBSim wrap — LEANING~~ |
| Camera/orbit v1 | Fixed camera + aircraft-pointing (bank/crab holds ROI in frame); gimbal deferred |
| Regulatory | 400 ft AGL geofence, Remote ID, failsafes — sim enforces the same envelope the firmware will |

## 1. The architecture that makes "write once, fly on Teensy" work *(unchanged — still the plan)*

The flight computer code is a separate, dependency-free C++ library — **`viper-fc`** (own
folder/repo, not inside the Cosmic tree) — and everything else plugs into it through a small HAL:

```
                 ┌────────────────────── viper-fc (portable C++17, no OS, no Arduino.h) ─┐
                 │  estimator (complementary → EKF)                                      │
                 │  mode machine (HOVER / TRANSITION / CRUISE / ORBIT / RTL / FAILSAFE)  │
                 │  cascaded controllers + gain schedule      mixers (hover & cruise)    │
                 │  failsafe supervisor (energy accounting, geofence, link, envelope)    │
                 └───────────────▲───────────────────────────────▲───────────────────────┘
                        IHal interface (sensors in, actuators out, clock, log)
        ┌────────────────────────┴─────────────┬─────────────────┴───────────────────────┐
        │ SimHal (inside ViperSim DLL)         │ TeensyHal (PlatformIO project)          │
        └──────────────────────────────────────┴──────────────────────────────────────────┘
```

```cpp
struct SensorFrame  { uint64_t t_us; Vec3 gyro_rads, accel_mss, mag_uT;
                      float baro_pa, airspeed_pa; GpsFix gps; float vbat_V, ibat_A; };
struct ActuatorFrame{ float motor[4]; float servo[4]; };  // normalized; dual tailsitter uses 2+2
```

- No `Arduino.h`, no `std::thread`, no heap after init inside `viper-fc` → identical `.cpp` files
  compile in the ViperSim DLL (MSVC) and in the PlatformIO Teensy project (`lib_deps = symlink://…`).
- The playbook's firmware sketch (§6.1: drivers behind interfaces, estimator, per-mode controllers,
  mode manager, failsafe supervisor above it, logger) **is** this library's module list.
- **Define the telemetry packet format + log schema in P0** (playbook §2.3: "cheap early, painful
  to retrofit"): one header `viperfc/telemetry_schema.h` shared by SimHal (→ `DataRecorder`),
  TeensyHal (→ SD + downlink), and the ground-station decode.
- Determinism bonus: replay a recorded `SensorFrame` log through `viper-fc` offline and diff
  controller outputs between code versions — regression tests with no simulator in the loop.

### Integration modes (staged)

| Mode | What runs where | For | Phase |
| --- | --- | --- | --- |
| SITL (in-process) | `viper-fc` in ViperSim + `SimHal` | daily control dev, VS debugging | P2 |
| Replay-through-FC | recorded `SensorFrame` log → `viper-fc` desktop | controller regression vs logs | P2+ |
| HIL | sim physics on PC; **real Teensy** runs `viper-fc`; frames over USB-serial (E5 COBS+CRC, ≥2 Mbaud, sim-clock timestamps, latency displayed) | timing, drivers, the exact flying binary | P6 |
| Rig ("iron bird lite") | sim/HIL attitude → 2–3-axis servo gimbal holding a foam model; rate-clamped in firmware | physical demo + servo-direction sanity | P7 |
| GCS/MAVLink (opt) | ViperSim ⇄ QGroundControl over UDP (E4); MAVLink vendored app-side | free pro GCS UI; ArduPilot SITL cross-check | P8 |

SimHal and the HIL bridge implement the same internal interface → SITL↔HIL is a dropdown, not a rebuild.

## 2. Simulation core (inside ViperSim)

### 2.1 Dynamics — JSBSim first (1-week timebox), composable-forces fallback

Per the Decision Record (LEANING → close it by prototyping):

- **`IDynamics` interface** owned by the app:
  `Step(const ActuatorFrame&, float dt)` · `GetTruth() → {pos NED, vel, quat, omega, alpha/beta, airspeed}`
  · `SetWind(vec3 steady, gustModel)` · `Reset(initialState)`. Everything downstream (SimHal sensor
  sampling, screens, recorder) sees only `IDynamics`, so the JSBSim-vs-hand-rolled outcome never
  ripples.
- **P1a — `JsbsimDynamics` spike (the timeboxed week):** vendor JSBSim as a static lib built by the
  ViperSim CMake (app-side dependency — the engine tree is untouched); author `viper.xml` from the
  design-point table + XFLR5 polars as they arrive; motors as JSBSim electric-engine/prop tables or
  an external force. **Evaluation criteria (from the decision record + tailsitter reality):**
  builds clean into the DLL; steps deterministically at 480+ Hz substeps; accepts full-envelope
  aero tables (α through ±90° — tailsitters pass α=90° in hover; JSBSim takes arbitrary table
  functions, so feed it the blended XFLR5+flat-plate polars); prop-wash-over-elevon control
  authority at zero airspeed can be represented (likely a custom external force/moment — if this
  fights JSBSim's structure, that's the strongest fall-back signal).
- **P1b — `ComposableDynamics` fallback** (previous plan's §2.1, kept ready): 6DOF rigid body
  integrated with **E11 RK4** at fixed substeps (e.g. 60 Hz engine × 8 = 480 Hz; FC stepped at its
  real rate); forces as composable components — gravity; per-motor first-order lag + thrust/torque
  `k·ω²` maps (**E13 tables** from the thrust stand); per-panel aero from full-envelope polars
  (XFLR5 blended into flat-plate `Cl = 2 sinα cosα`, `Cd = 2 sin²α`) with elevon incidence shift +
  prop-wash dynamic-pressure term; fuselage drag; ground contact = spring-damper on leg points
  (later: `Terrain::SampleHeight`, doc 05 S8.3); wind = steady + **E14 noise** gusts (Dryden-ish).
- Either way: **truth first, estimation second, control third** — never tune a controller against
  an estimator bug.

### 2.2 Config-driven everything (E10)

`Projects/ViperSim/config/viper.toml` — the app-owned airframe file; numbers flow from the tracker
and sizing calculator, no recompiles:

```toml
[airframe]   auw_kg = 1.49, wing_area_m2 = 0.30, aspect_ratio = 6.0, inertia = [...]
[motors]     count = 2, max_thrust_gf = 1490, tau_s = 0.05, kf = ..., kq = ...   # E13 map file when bench data lands
[battery]    capacity_wh = 100, mass_g = 500, cells_s = 4, r_int = ..., usable_frac = 0.85
[aero]       polar_csv = "polars/viper_blend.csv", cd0 = 0.035, clmax = 1.2      # replaced by XFLR5 output
[sensors.gyro]  noise = ..., bias_walk = ...        # every sensor: noise params + enable
[sim]        perfect_sensors = false, substeps = 8
[limits]     geofence_agl_m = 121.9, hover_budget_s = 300                        # FAA 400 ft; 5 min cap
```

### 2.3 Sensor models (feeding `SensorFrame`)

Sampled from truth at each FC step, all parameters from `viper.toml`, all randomness via **E15
seeded RNG** (replays must reproduce): gyro/accel (bias random-walk + white noise + mount
misalignment), baro (noise + drift), GPS (5–10 Hz, 100–200 ms latency, position noise), mag
(declination + noise), **pitot unreliable below ~5 m/s** (model it — it gates transition logic),
battery (§2.5). `perfect_sensors = true` toggle: develop against perfect, then harden the estimator.

### 2.4 Flight modes to develop in `viper-fc` (mirrors Proposal §3.1)

1. **Hover/attitude:** quaternion attitude error → rate PIDs → hover mixer (differential thrust +
   elevons in prop wash). Position hold on top (GPS/baro).
2. **Transition state machine** — *the* deliverable and the project's top risk. Explicit states
   `HOVER → ACCEL (pitch-over schedule) → BLEND (mixer crossfade on airspeed) → CRUISE` and reverse
   (`DECEL/flare`). Gains/weights scheduled on airspeed + pitch (**E13** schedules). Instrument
   everything (scheduled gains, blend factor, per-effector commands → DataRecorder).
3. **Cruise:** TECS-style energy controller + L1/PD lateral track.
4. **Orbit / loiter-on-ROI** (signature mode): circular path around a ground point with wind-drift
   compensation; **v1 camera pointing = fixed camera + aircraft pointing** (bank/crab holds ROI in
   frame — per the playbook decision); HUD shows ROI-in-frame error + live power draw vs hover
   (the thesis, quantified: ~106 W orbit vs ~230 W hover).
5. **RTL + vertical land; failsafe supervisor:** link loss → RTL; low battery → land; **geofence
   400 ft AGL** + envelope protection; **hover-budget energy accounting** (3–5 min cap with
   warnings — this failsafe is unique to Viper's energy story and gets its own tests). UI gets
   fault-injection buttons (kill link, drop GPS, freeze pitot, motor-out).

### 2.5 Energy/endurance model — EARLY (the first genuinely useful output)

Battery: capacity/internal-resistance/sag from `viper.toml`; per-motor electrical power from
ω/torque + efficiency (E13 map). Live Wh counter + **Energy screen**: hover vs cruise vs orbit
power, projected endurance for a mission profile (X min hover + Y cruise + Z orbit), compared
against the proposal's model numbers (230 W / 106 W / ~36 min / ~35 min). Diverging from the
spreadsheet is a *finding*, not a bug — surface both. Available by P3 with only the hover model.

## 3. App structure (SF_Telem patterns — reuse, don't reinvent)

```
Projects/ViperSim/
├── CMakeLists.txt              (template-generated; also builds viper-fc/ + vendored JSBSim)
├── config/viper.toml           (+ polars/, motor maps as data files)
├── src/
│   ├── ViperSim.h/.cpp         root layer: homescreen tiles → screens (SF_Telem pattern, README §21.5)
│   ├── SimHub.h/.cpp           owns IDynamics, FC instance/bridge, DataRecorder/Player,
│   │                           SerialLink (HIL/rig), mode + fault injection, energy accounting
│   ├── sim/                    JsbsimDynamics / ComposableDynamics, Sensors, Battery, Wind
│   ├── fc_glue/                SimHal, HilBridge (E5 framed serial), RigOutput
│   ├── screens/                Flight (3D viewport + FPV inset + PFD) · Tuning (live gains,
│   │                           step-response plots) · Energy (§2.5) · Transition (state machine
│   │                           visualizer + blend traces) · Replay (DataPlayer + TelemetryPanel)
│   └── ui/                     ADI, HSI, battery bar (ImGui/Renderer2D)
└── viper-fc/                   (engine-free portable subfolder — unit-tested with plain doctest)
    ├── include/viperfc/*.h     estimator, modes, control, mixers, failsafe, IHal, telemetry_schema.h
    └── firmware/               PlatformIO project (Teensy 4.1) consuming viper-fc + TeensyHal
```

Telemetry: register FC internals (`fc.attitude_est`, `fc.mode`, `fc.mix[i]`, `fc.energy_wh`) *and*
sim truth (`truth.attitude`, …) as separate DataRecorder entities → estimator-vs-truth overlay in
replay is the estimator's report card.

## 4. Phases with acceptance demos *(record every demo)*

| Phase | Build | Demo that proves it |
| --- | --- | --- |
| **P0** ✅ 2026-07-02 | Skeleton (`Projects/ViperSim`); SimHub; **E10 config loading** (`viper.toml`); **telemetry schema defined** (`fc_glue/telemetry_schema.h`); homescreen tiles → Flight/Replay live + Tuning/Energy/Transition stubs | Boots via `--project ViperSim`; tiles navigate; viper.toml values shown (verified: config resolves to `assets/projects/ViperSim/config/viper.toml`, no crash) |
| **P1** ✅ 2026-07-02 | `IDynamics` abstraction; **`ComposableDynamics`** (6DOF, E11 RK4) drop-test scene (gravity + ground spring-damper) in the 3D viewport; DataRecorder → replayable session; **dynamics decision recorded** (`Projects/ViperSim/docs/DYNAMICS_DECISION.md`) | Airframe drops, settles at ground, replayable in ReplayScreen. Numeric drop check PASSES (impact 1.10 s vs 1.09 s analytic; peak 10.62 vs 10.68 m/s; rest compression 3.7 mm = mg/k; no tunneling). Decision: ship ComposableDynamics, keep JSBSim behind `IDynamics` (provisional-closed — see record) |
| **P2** ✅ code 2026-07-02 | `viper-fc` skeleton + IHal + SimHal; rate→attitude hover loop, perfect sensors; TuningScreen | 20° roll step: clean response, no divergence; offline replay-through-FC runs — *Tuning screen: step button + replay-through-FC runner; **user acceptance run pending*** |
| **P3** ✅ code 2026-07-02 | Full hover: position hold, battery + energy accounting, sensor noise + complementary filter, gamepad (E7), EnergyScreen; audio alerts (doc 08 A1/A2) | **Gate G1** (playbook S1) — scripted as the "Run G1" scenario (noise + alternating 5 m/s gusts, auto PASS/FAIL vs max-dev/altitude/230 W ±15%), recording auto-flushed to `regression/g1_hover`; ***user gate run pending*** |
| **P4** ✅ code 2026-07-02 | Full-envelope aero tables; cruise (TECS-lite); transition state machine both directions; S3 viewport conveniences as needed | **Gate G2** (playbook S2) — scripted round trip in the "Run G2" scenario; CG (`aero.cg_offset_x_m`) + `fc.cruise_airspeed` sweeps re-run it across the envelope; blend/phase traces in the fc entity; ***user gate runs pending*** |
| **P5** ✅ code 2026-07-02 | Orbit-on-ROI + aircraft-pointing camera; full failsafe set + fault injection; FPV inset (S3.1) | **Gate G3** (playbook S3) — "Run G3" scripts orbit-in-gusts metrics + link-kill → RTL → land hands-off; remaining failsafe paths via fault buttons, tracked by the session checklist; ***user gate run pending*** |
| **P6** ✅ code 2026-07-02 | HIL bridge (E5): Teensy runs `viper-fc`, sim streams sensors | Same P4 transition flown by the physical Teensy; latency figure on screen — *software complete (HilBackend + `viper-fc/firmware/`); **needs the physical Teensy 4.1*** |
| **P7** ✅ code 2026-07-02 | Gimbal rig output + rate clamps | Rig mirrors sim attitude; servo directions verified — *software complete (`RigOutput`, ASCII `RIG,r,p,y` @ 50 Hz, E12 rate clamps); **needs the physical rig*** |
| **P8** (opt) | UDP (E4) + MAVLink heartbeat/attitude subset | QGroundControl shows live attitude/map |

**Gates G1–G3 are the playbook's simulation gates (§5.2)** — they are the *contract between this
sim and the flight-test program*: no real hover before G1, no real transition before G2, no free
flight before G3. That is why they are phase acceptance criteria and not nice-to-haves.

## 5. Validation habits (rehearse the flight-test discipline now)

- Every phase demo = a recorded session committed under `recordings/regression/`, replayed after
  changes (engine replay makes this nearly free).
- `viper-fc` unit tests (plain doctest, no engine): quaternion integration, mixer saturation,
  mode-machine transition table, **energy-accounting failsafe thresholds**, estimator convergence
  on canned data.
- One markdown "flight test card" per milestone (commanded / expected / observed) — same template
  the playbook's real test cards use (§5.4).
- Versioned gains: PID/config in `viper.toml` under git — know exactly what "flew" in every
  recording (playbook §7's firmware-config rule, rehearsed in sim).
