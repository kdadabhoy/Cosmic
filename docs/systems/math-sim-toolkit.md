# Math & Simulation Toolkit — How It Works

> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine (Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as historical. The current exemplars are the template projects, `Projects/PendulumLab`, `Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and [`../parked-3d/systems/build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D) records what the split was.

> **STATUS: SKELETON** — to be filled by work order **D32** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** the header-only, unit-tested math that simulations are built from —
integrators that don't explode, filters that tame noisy sensors, lookup tables for measured
data, seeded noise and RNG so every run is replayable.
**Source:** `Cosmic/src/math/*` (Spatial, Integrators, Filters, LookupTable, Noise, Random, Frustum)
**API Reference:** [../reference/math.md](../reference/math.md) · **Guide:** [../guide/sim-math-toolkit.md](../guide/sim-math-toolkit.md) (D58) · **Plan record:** [`../plans/archive/03-simulation-engine-plan.md`](../plans/archive/03-simulation-engine-plan.md)

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

> **Don't re-derive the usage material.** [`../guide/sim-math-toolkit.md`](../guide/sim-math-toolkit.md)
> (D58) already covers the when-to-use table, per-header behaviour and failure modes, the shared
> filter contract, and the determinism box — including the honest split between what is bit-exact
> (PCG32's integer stream, pinned against the canonical reference vector), what is deterministic
> under IEEE-754 (every `Noise` sampler — arithmetic only), and what rests on libm
> (`Random::Gaussian`, `LowPassFilter`'s `exp`, `Biquad`'s coefficient setup).
> This explainer owns the *why*: the header-only/GL-free testability split, engine-verbs-not-domain-
> logic, and the shown-not-told RK4-vs-Euler comparison. **Also worth an honest note:**
> `LookupTable1D`/`2D`, `LowPassFilter`, `Derivative`, `MovingAverage`, `Biquad`, `Washout` and
> `IntegrateSemiImplicitEuler` have **no in-tree consumers outside their doctests** — tested, not
> yet battle-worn.
