# API Reference — Math & Simulation Toolkit

> **STATUS: SKELETON** — to be filled by work order **D15** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/math/Spatial.h`, `math/Integrators.h`,
`math/Filters.h`, `math/LookupTable.h`, `math/Noise.h`, `math/Random.h`.
(`math/Frustum.h` is documented in [rendering-3d.md](rendering-3d.md).)

**Read first:** [`docs/plans/archive/03-simulation-engine-plan.md`](../plans/archive/03-simulation-engine-plan.md)
(E10–E15 acceptance notes are the design record); systems explainer
[math-sim-toolkit](../systems/math-sim-toolkit.md). These are header-only, unit-tested,
GL-free — every entry can cite its doctest.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Spatial.h` — NED world frame vs Y-up render frame conventions, quaternion helpers, frame conversion functions (this is the *conventions contract* — document the frames with a diagram)
- [ ] `Integrators.h` (E11) — RK4, semi-implicit Euler, `FixedSubstepper`; state-type requirements (the concept/duck-typing contract for user state structs)
- [ ] `Filters.h` (E12) — low-pass, derivative, rate limiter, biquad, washout; per-filter: transfer behavior, `Reset`, dt handling
- [ ] `LookupTable.h` (E13) — 1D/2D interp tables, clamping vs extrapolation behavior at edges, typical uses (aero polars, thrust maps)
- [ ] `Noise.h` (E14) — seeded value/Perlin/fBm, `Ridged2D` ridged multifractal (F11); value ranges, determinism guarantee
- [ ] `Random.h` (E15) — PCG32: seeding, distribution helpers, determinism/replay guarantee, thread-safety statement

## Sections to write

1. Entries per checklist, one section per header. <!-- TODO(D15) -->
2. Worked example: a 1-DOF spring integrated with RK4 + filtered with LPF — one compiling snippet reused from tests. <!-- TODO(D15) -->
3. Determinism box: what the engine guarantees for replay (same seed + same dt ⇒ same trajectory). <!-- TODO(D15) -->

---
*Changelog:*
