# ViperSim / viper-fc — Debugging Notes

Running log of **Viper-specific** debugging: what was tested, what passed/failed,
data trails, and open items. Engine-level defects do NOT belong here (they get
fixed in `Cosmic/` directly); this file is for FC control logic, sim models,
gains, and app wiring.

---

## Session 2026-07-02 — Phases 4–6 acceptance pass (build + tests + G1)

First compile-and-run of the Phases 4–6 code drop (audio A1/A2, viper-fc P2–P7,
five screens, scripted gates). Debug build, SITL backend, stock `viper.toml`.

### Result summary

| Check | Result |
| --- | --- |
| Full build (`build_all` equivalent, Debug) | ✅ after 2 engine-side fixes (below) |
| `CosmicTests` (incl. new audio suite) | ✅ all pass |
| `ViperFcTests` (math/mixer/transition/failsafe/estimator) | ✅ 18/18 after 1 fc fix (below); now 19 cases |
| **Gate G1** (hover vs noise + 5 m/s gusts) | ❌ **FAILED** — twice; see analysis |
| Gate G2 (transition) | ⬜ not run (G1 blocks a meaningful pass; session ended) |
| Gate G3 (orbit + failsafe) | ⬜ not run |
| P6 HIL / P7 rig | ⬜ needs hardware |

Fixes landed during the session (all compiled + unit-tested):

- **Engine** (not Viper, listed for context): `Sound::Impl` needed a public
  forward declaration (C2248 in `Audio.cpp`); `FrameBuffer` needed `COSMIC_API`
  export for the FPV inset's out-of-DLL `FrameBuffer::Create` (LNK2019).
- **viper-fc**: forward transition left the machine latched `Active()` forever —
  `Blend` completion now reports `done` and goes inactive in the same step,
  mirroring `Flare` (caught by `test_transition.cpp`; telemetry `phase` would
  have shown a stale value for the rest of the flight).
- **viper-fc**: battery voltage failsafes are now **time-qualified**
  (`batt_v_qualify_s`, default 2.0 s, `viper.toml [limits]`) — see G1 run 1.
- **Layout**: `viper-fc/` moved from repo root into `Projects/ViperSim/viper-fc/`
  (portable/engine-free as before; firmware + tests moved with it).

### G1 run 1 — false BatteryCritical latched a forced land (FIXED)

Timeline (console + `fc.csv`): takeoff clean, 10 m reached at t≈4.0 s; gusts
start; FC fights, power spikes 350 W+; IR sag (r_int 0.08 Ω) pulls the pack from
15.6 V through 14.0 V (3.5 V/cell → `BatteryLow`) to below 13.2 V (3.3 V/cell →
`BatteryCritical`) **within ~5 s of arming**. Battery flags latch by design →
`Demand::Land` forever → the aircraft lands itself through the gusts (that run's
"max dev 13.83 m" was mostly the unpowered drift). Alerts in the log at
t+5.0/t+5.2 s confirm.

Root cause: the voltage thresholds compared the **instantaneous** sagged voltage.
A punch-out/gust fight is not a flat pack. Fix: cell voltage must stay below the
threshold for `batt_v_qualify_s` continuously (ArduPilot `BATT_LOW_TIMER` / PX4
filtered-vbat equivalent). New unit test: 6 cycles of 1 s deep sag + 2 s recovery
must NOT latch anything; sustained undervoltage must still land.

Note the **energy** (Wh) reserve failsafe is untouched — it was always the
primary mechanism; the voltage pair is the backstop.

### G1 run 2 (after fix) — hover position hold departs in the first gust cycle — **OPEN**

Report: `[FAIL] max dev 53.31 m (< 4.0 m)` · `[FAIL] min AGL 0.1 m (> 3 m)` ·
`[FAIL] avg power 398 W (230 W ±15%)`. Recording:
`build/Runtime/Debug/recordings/regression/g1_hover/session/` (truth/fc/sensors
CSVs, 2170 rows @ 60 Hz).

What the data shows (fc.csv vs truth.csv):

```
  t(s)   truth r/p (ZYX°)   est r/p (°)     att_err   AGL     mR/mL       sR/sL
  4.35    -5.4 /  16.3      -7.9 / 42.0      82.6°   10.35   0.38/0.47   0.89/0.85
  4.95    -4.2 / -40.3      -5.0 / 13.0     109.4°    9.04   0.72/0.84   1.00/0.95
  5.35  -147.3 / -80.7      -7.8 /-41.2     155.7°    4.90   0.97/1.00   1.00/0.95
  5.55  -168.0 / -57.8     -16.5 /-65.0     179.8°    1.34   0.96/1.00  -1.00/-0.98
  6.15+ (on ground, motors saturated, att_err oscillating 130–180°)
```

- The quaternion attitude error is already **~83° during the (visually clean)
  climb**, before the gust fight — while motors sit at a calm 0.4 and the
  vehicle hovers fine. An 83° "error" with a stable hover means the *reported*
  error, the estimator, or the target frame is inconsistent — not that the
  vehicle was actually 83° off.
- At the first 5 m/s gust step the vehicle pitches over hard (truth pitch −40°
  by 4.95 s → tumbles, truth roll −147°), motors + elevons saturate, ground
  contact by t≈5.6 s at full throttle (hence avg 398 W).
- Prime suspects, in order:
  1. **Estimator**: complementary filter's accel correction under sustained
     lateral acceleration (gust fight) corrupts the attitude estimate; and/or a
     convention mismatch near the tailsitter's 90°-pitch singularity (est vs
     truth Euler disagree wildly during a hover that truth shows as near-vertical
     — compare quaternions, not ZYX Euler, when digging).
  2. **Hover attitude/tilt authority**: `tilt_max` 0.61 rad vs ~32° needed for a
     5 m/s broadside — little margin before the position loop demands more tilt
     than allowed and windup/saturation behavior takes over.
  3. Plain gain weakness (first-cut analytic values — the plan expected this).
- Healthy reference points from the same recording: stabilized pre-gust hover at
  10 m draws **217–245 W** (230 W model ✓); pack sits at 15.5–15.7 V under
  hover load (sag model sane).

How to iterate (all in-app, no rebuild):

1. Replay screen → load `recordings/regression/g1_hover/session` and overlay
   estimator vs truth attitude channels — confirms/kills suspect 1 fast.
2. Tuning screen → live gains + 20° step response; `[fc]` in `viper.toml`
   versions anything that works.
3. Tuning screen → replay-through-FC regression against the recorded `sensors`
   entity for offline controller iteration.
4. Re-run G1 (auto PASS/FAIL + fresh recording every run).

### Not bugs / for-later notes

- **FPV inset renders a solid red/brown frame** on the pad and in flight —
  looks like the belly camera sits inside the fuselage mesh (near-plane
  clipping the vehicle's own body), not an engine problem: the S3.1 second
  render pass (own FBO) works, resizes, and never crashed. Check the camera
  offset in `FlightScreen` when next in there.
- Engine 3D otherwise clean all session: grid, pad, vehicle, trail polyline,
  geofence box, docking/viewport resize — no artifacts observed.
- Audio: engine init (48 kHz), mode-change chime + warning/critical tones all
  audible at the right moments; headless CosmicTests audio suite passes.
- `DataPlayer::Load: './recordings/viper_drop/session' is not a .bin file or
  directory` logs an error at startup before any drop recording exists —
  harmless but noisy; consider a quiet existence probe in the Replay screen.
