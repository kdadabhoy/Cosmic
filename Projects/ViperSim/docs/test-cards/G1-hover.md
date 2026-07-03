# Flight test card — Gate G1: Tuned hover vs noise + gusts

*Playbook §5.4 template; sim gate per doc 04 §4. Run via Flight screen → "Run G1 (hover vs gusts)".*

| Field | Value |
| --- | --- |
| Config rev | viper.toml `meta.config_rev` at run time |
| Backend | SITL (scenario-enforced) |
| Sensors | NOISY (scenario forces `perfect_sensors = false`) |

## Commanded
1. Arm, hover-climb to 10 m AGL.
2. 30 s station-hold under gust sigma 2 m/s + steady wind stepping through
   5 m/s N → calm → 5 m/s E → 3.5 m/s SW every 5 s.
3. Calm, 3 s settle, evaluate.

## Expected (gate criteria)
- Max horizontal deviation from the hold point < 4.0 m; no divergence.
- Altitude never sags below 3 m AGL.
- Mean hover power within ±15% of the proposal's 230 W model.

## Observed
- [ ] Run date: ____  ·  PASS/FAIL: ____
- Max deviation: ____ m  ·  min AGL: ____ m  ·  mean power: ____ W
- Recording committed: `recordings/regression/g1_hover/`
- Notes:
