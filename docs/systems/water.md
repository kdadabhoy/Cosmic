# Water — How It Works

> **STATUS: SKELETON** — to be filled by work order **D30** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a flat grid displaced by summed Gerstner waves (identical math on CPU and
GPU, so boats float correctly), shaded with a mirrored render of the world above and a
grabbed copy of the world below — plus a full underwater mode when the camera dives.
**Source:** `Cosmic/src/water/GerstnerWave.h`, `water/Water.*`, `Cosmic/assets/shaders/Water*.glsl`
**API Reference:** [../reference/world-systems.md](../reference/world-systems.md) · **Design record:** [`../design/water-rendering-notes.md`](../design/water-rendering-notes.md)

## Section plan

1. **Overview** — why water is hard (it's a mirror, a lens, and a moving surface at once). <!-- TODO(D30) -->
2. **Mental model** — Gerstner in one picture: points move in circles, crests sharpen; reflection = "render the world upside-down into a texture". <!-- TODO(D30) -->
3. **Step-by-step** — one water frame: `BeginReflection` (mirrored camera + oblique clip) → refraction grab (`BlitCopy`) → surface draw (depth fade, foam, Fresnel, glint) → underwater branch when submerged. <!-- TODO(D30) -->
4. **Technical implementation** — `GerstnerWave.h` shader==CPU contract (why it's a header-only pure struct), Lengyel oblique near-plane clip (what it solves), per-frame primary-reflection handoff (F12a: two waters, one reflection budget), water v2 (8 waves, whitecaps, `WaterFlow` rivers/waterfalls), buoyancy queries, underwater stack: depth-graded fog, screen-space seafloor caustics, god-ray tint, surface-from-below (Tonemap/PostProcessStack/Water interplay), Layer-0 shimmer fix (mipmapped procedural texture + distance fade). <!-- TODO(D30) -->
5. **Design decisions** — Gerstner over FFT (tier decision, FFT parked), planar reflection over SSR/raytrace, mirror-quality budget (MirrorLake amp ≤ 0.02 @ 2048 reflection). <!-- TODO(D30) -->
6. **Limits & future work** — FFT ocean (S9.3), user tuning pass on from-below surface + caustics. <!-- TODO(D30) -->

**Truth sources:** `water-rendering-notes.md` (design/rationale — summarize + link),
`Water.cpp`, `GerstnerWave.h` + its doctest, doc 05 §8 banner, Frontier worlds F12a/F15/F16.
