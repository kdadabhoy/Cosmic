# Flight test card — Gate G2: Scripted VTOL → cruise → VTOL

*Playbook §5.4 template; sim gate per doc 04 §4. Run via Transition screen → "Run gate G2".*

| Field | Value |
| --- | --- |
| Config rev | viper.toml `meta.config_rev` at run time |
| CG offset | `aero.cg_offset_x_m` (sweep ±10 mm across runs for the envelope) |
| Cruise speed | `fc.cruise_airspeed` (sweep 16–24 m/s across runs) |

## Commanded
1. Arm, hover-climb to 30 m AGL.
2. Forward transition: HOVER → ACCEL (35°/s pitch-over) → BLEND (8→14 m/s) → CRUISE.
3. Cruise 10 s.
4. Back transition: CRUISE → DECEL (45°/s pitch-up) → FLARE → HOVER, 4 s hold.

## Expected (gate criteria)
- Both directions complete without abort/timeout.
- Altitude stays above 12 m AGL through both transitions (sag budget ~18 m).
- Stays under the 400 ft geofence.
- Blend/phase traces recorded (fc entity: `blend`, `phase`).
- Repeatable across the CG/airspeed envelope (re-run per sweep row above).

## Observed
- [ ] Run date: ____  ·  CG: ____ mm  ·  V_cruise: ____ m/s  ·  PASS/FAIL: ____
- Min AGL: ____ m  ·  forward blend time: ____ s  ·  back capture time: ____ s
- Recording committed: `recordings/regression/g2_transition/`
- Notes:
