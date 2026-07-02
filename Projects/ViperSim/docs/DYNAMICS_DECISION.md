# Viper Dynamics Decision Record

> **Scope:** which 6DOF dynamics engine backs `IDynamics` for ViperSim.
> **Plan reference:** doc 04 §2.1 (P1a JSBSim spike / P1b composable fallback);
> the Software & Tools Decision Record marked JSBSim **LEANING**, to be closed by
> prototyping inside a 1-week timebox.

## Decision (2026-07-02): ship **ComposableDynamics**; keep JSBSim behind `IDynamics`

The simulator ships the hand-rolled **`ComposableDynamics`** (6DOF rigid body,
composable forces, integrated with the engine's E11 RK4 substepper) as the
default backing for `IDynamics`. The JSBSim path stays a first-class option: the
`IDynamics` interface fully isolates the choice, so a `JsbsimDynamics` can be
dropped in later as a factory change with **zero** downstream edits.

This is recorded as **provisional-closed**: it is the working decision the rest
of the plan builds on, made on the evidence below. It flips only if a JSBSim
build spike surfaces a decisive advantage (criteria at the end).

## What was delivered in P1

- **`IDynamics`** (`src/sim/IDynamics.h`) — `Step / GetTruth / SetWind / Reset`.
  Everything downstream (SimHal sensor sampling, screens, DataRecorder) sees only
  this interface, so the dynamics choice never ripples.
- **`ComposableDynamics`** (`src/sim/ComposableDynamics.{h,cpp}`) — gravity +
  ground-contact spring-damper for the drop test, with stubbed composition
  points for motor thrust (P2), aero panels + wind (P4). Translation via
  `Cosmic::IntegrateRK4` (E11) inside a `FixedSubstepper`; attitude via
  `Spatial.h` quaternion integration (E3).
- **Drop test** wired through `SimHub` → `DataRecorder` → replayable session,
  surfaced on the Flight and Replay screens.

### Drop-test verification (numeric, `scratchpad/drop_check` harness)

6 m drop, design-point mass 1.49 kg, ground k=4000 N/m, c=350 N·s/m:

| Quantity | Simulated | Analytic | ✓ |
| --- | --- | --- | --- |
| Time to ground contact | 1.10 s | 1.09 s (√(2·5.82/g)) | ✓ |
| Peak fall speed | 10.62 m/s | 10.68 m/s (√(2g·5.82)) | ✓ |
| Rest spring compression | 3.7 mm | 3.65 mm (mg/k) | ✓ |
| Rest velocity | 0.000 m/s | 0 | ✓ |
| Max ground penetration | 3.8 cm (springs back) | — (no tunneling) | ✓ |

The contact is overdamped (ζ = c/2√(km) ≈ 2.3), so the box settles without
bouncing — the expected drop-test behavior.

## Rationale

1. **The tailsitter is the risk JSBSim fights hardest.** The plan itself flags
   it: prop-wash-over-elevon control authority at *zero airspeed* (α = 90° in
   hover) "is likely a custom external force/moment — if this fights JSBSim's
   structure, that's the strongest fall-back signal." Our core deliverable —
   the transition state machine — lives exactly in the regime where JSBSim's
   built-in aero tables help least and custom external forces do the work, which
   is precisely what a composable force model expresses natively.
2. **The engine already supplies the hard parts.** E11 RK4 + substepper, E13
   thrust/polar tables, E15 seeded RNG for sensor noise, `Spatial.h` attitude
   integration, `DataRecorder`/`DataPlayer` for replay. `ComposableDynamics` is
   composition over primitives we've already unit-tested, not a new stack.
3. **No external build/licensing surface.** JSBSim would be a static lib built
   by the ViperSim CMake, XML airframe authoring, and a C++/table-function
   binding for the prop-wash model. That is real integration cost for a model we
   would still have to hand-extend at the tailsitter corners.
4. **Determinism for replay is trivial to guarantee** when we own every force
   term and every RNG draw (E15) — critical for the regression-replay discipline
   the playbook demands.

## What would flip this to JSBSim (spike criteria, not yet run)

Run the P1a spike (timeboxed) and adopt JSBSim if **all** hold:
- It builds clean into the ViperSim DLL (MSVC) with acceptable build time.
- It steps deterministically at 480+ Hz substeps.
- Full-envelope aero tables (α through ±90°) load from the blended XFLR5 +
  flat-plate polars **without** fighting its table structure.
- Prop-wash-over-elevon at zero airspeed is expressible as an external
  force/moment **without** contorting the model — the make-or-break test.

Because `IDynamics` isolates the choice, running this spike later costs nothing
already built here. Until it is run and clears every bullet, ComposableDynamics
is the shipping path.
