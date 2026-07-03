# Flight test card — Gate G3: Orbit-on-ROI + full failsafe set

*Playbook §5.4 template; sim gate per doc 04 §4. Run via Flight screen → "Run G3 (orbit+failsafe)".*

| Field | Value |
| --- | --- |
| Config rev | viper.toml `meta.config_rev` at run time |
| ROI | (100, 0) m NED, orbit radius `fc.orbit_radius_m` |
| Sensors | NOISY + gust sigma 2 m/s |

## Commanded
1. Arm, hover-climb to 25 m; set ROI; request ORBIT (transitions automatically).
2. 30 s of orbit in gusts — radial + ROI-in-frame metrics accumulate.
3. Link kill. **Hands off from here**: expect RTL → cruise home → back
   transition → hover to home → vertical land → auto-disarm.

## Expected (gate criteria)
- Mean radial error < 20 m; mean ROI-in-frame error < 35° (v1 fixed camera +
  aircraft pointing).
- LinkLost alert raised; RTL engaged without pilot input.
- Touchdown < 20 m from home, disarmed, zero further input.

## Remaining failsafe paths (fault buttons, tracked by the session checklist)
- [ ] Battery low (voltage warn) · [ ] Battery critical → forced land
- [ ] Battery reserve → RTL (Battery → 20% button)
- [ ] Geofence altitude · [ ] Geofence radius
- [ ] GPS drop · [ ] Pitot freeze (transition abort path) · [ ] Motor-out
- [ ] Hover budget warn (80%) · [ ] Hover budget hit → forced exit

## Observed
- [ ] Run date: ____  ·  PASS/FAIL: ____
- Mean radial: ____ m  ·  mean ROI err: ____°  ·  touchdown dist: ____ m
- Recording committed: `recordings/regression/g3_orbit_failsafe/`
- Notes:
