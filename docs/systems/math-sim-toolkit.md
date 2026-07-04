# Math & Simulation Toolkit — How It Works

> **STATUS: SKELETON** — to be filled by work order **D32** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** the header-only, unit-tested math that simulations are built from —
integrators that don't explode, filters that tame noisy sensors, lookup tables for measured
data, seeded noise and RNG so every run is replayable.
**Source:** `Cosmic/src/math/*` (Spatial, Integrators, Filters, LookupTable, Noise, Random, Frustum)
**API Reference:** [../reference/math.md](../reference/math.md) · **Plan record:** [`../plans/03-simulation-engine-plan.md`](../plans/03-simulation-engine-plan.md)

## Section plan

1. **Overview** — why a game engine ships flight-sim math (Cosmic's sim-first identity: ViperSim, telemetry apps). <!-- TODO(D32) -->
2. **Mental model** — the coordinate-frames picture: NED world frame vs Y-up render frame, and where the conversion happens (`Spatial.h` is the treaty). <!-- TODO(D32) -->
3. **Step-by-step** — a falling body through RK4 at fixed dt; the same body with Euler at big dt (why RK4 exists, shown not told). <!-- TODO(D32) -->
4. **Technical implementation** — per header: integrator templates + state-type requirements + `FixedSubstepper`, each filter's behavior + reset semantics, LUT edge behavior, noise family incl. `Ridged2D` multifractal (F11 — the ridge trick: `1−|noise|` sharpened), PCG32 determinism guarantees; everything cites its doctest. <!-- TODO(D32) -->
5. **Design decisions** — header-only/GL-free rule (testability split), engine-verbs-not-domain-logic (aero polars are app data in LUTs, not engine code). <!-- TODO(D32) -->
6. **Limits & future work.** <!-- TODO(D32) -->

**Truth sources:** the headers themselves + `tests/` doctests (doc 03 lists acceptance per
E-item). Gotcha to preserve: `doctest::Approx.epsilon` is RELATIVE — worked examples in docs
should quote absolute-tolerance comparisons for world coordinates.
