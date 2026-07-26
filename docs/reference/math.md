# API Reference — Math & Simulation Toolkit

> **STATUS: SKELETON** — to be filled by work order **D15** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/math/Spatial.h`, `math/Integrators.h`,
`math/Filters.h`, `math/LookupTable.h`, `math/Noise.h`, `math/Random.h`.
(`math/Frustum.h` is documented in [rendering-3d.md](rendering-3d.md).)

**Read first:** the guide chapter [`../guide/sim-math-toolkit.md`](../guide/sim-math-toolkit.md)
(D58 — written from source, doctests cited per header; it is the client-facing source for this
material until this chapter lands);
[`docs/plans/archive/03-simulation-engine-plan.md`](../plans/archive/03-simulation-engine-plan.md)
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
   **Be precise (D58):** the PCG32 *integer* stream is bit-exact and pinned against the algorithm's
   canonical reference vector; every `Noise` sampler is arithmetic-only and therefore deterministic
   under IEEE-754; but `Random::Gaussian` (`log`/`sin`/`cos`), `LowPassFilter` (`exp`) and
   `Biquad`'s coefficient setup depend on libm and are **not** bit-identical across C runtimes.
   `Random.h`'s "the same sensor noise everywhere, forever" over-promises for the Gaussian
   specifically.

---
*Changelog:*
