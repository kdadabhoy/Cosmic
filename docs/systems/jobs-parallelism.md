# Jobs & Parallelism — How It Works

> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine (Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as historical. The current exemplars are the template projects, `Projects/PendulumLab`, `Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and [`../parked-3d/systems/build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D) records what the split was.

> **STATUS: SKELETON** — to be filled by work order **D33** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a worker-thread pool runs simulation work in parallel while the main thread
keeps exclusive ownership of the GPU; parallel ECS systems declare what they read and write
so the scheduler can overlap them safely, and double buffers let readers see last frame's
data while writers build the next.
**Source:** `Cosmic/src/jobs/*` (JobSystem, ParallelSystem, ParallelFor, SystemQuery, ComponentArray, DoubleBuffer)
**API Reference:** [../reference/jobs.md](../reference/jobs.md) ·
**Guide:** [`../guide/jobs-and-parallelism.md`](../guide/jobs-and-parallelism.md) (root README §22
is now an overview pointing there; §39 is still live Part II material)

> **Don't re-derive the client surface.** The guide chapter (D59) already documents, from source,
> the pool and its lifecycle, `ParallelFor`'s sync/async split and capture rules, the four-pass
> `ParallelSystem` tick, `SystemQuery` staging, `ComponentArray`'s page-0 limit, `DoubleBuffer`, the
> T18 counters — and the full main-thread-only table. This explainer covers *why*: own pool vs
> `std::async`, the barrier-per-frame choice, pool sizing. **DG-12 is built** in
> [that chapter](../guide/jobs-and-parallelism.md#dg-12--where-the-work-actually-runs); reuse it
> rather than authoring a second one.

## Section plan

1. **Overview** — why multithreading a game loop is dangerous and how declared access (read vs write) makes it safe. <!-- TODO(D33) -->
2. **Mental model** — diagram **DG-12** (main thread + worker lanes + sync points within the frame). <!-- TODO(D33) -->
3. **Step-by-step** — a `ParallelFor` over 100k particles CPU-side; a background terrain build with `IsLoading()` polling + swap-in (the Frontier loading-screen pattern). <!-- TODO(D33) -->
4. **Technical implementation** — pool sizing, work stealing/queueing model (verify in `JobSystem.cpp`), `SystemQuery` conflict detection, `ComponentArray` SoA views, `DoubleBuffer` flip timing, **the GL rule: no GPU calls off the main thread — ever** (single GL context; how async loads split CPU prep from main-thread upload). <!-- TODO(D33) -->
5. **Design decisions** — own pool vs std::async; where the 20-agent telemetry template fits as a stress exemplar. <!-- TODO(D33) -->
6. **Limits & future work.** <!-- TODO(D33) -->

**Truth sources:** [`../guide/jobs-and-parallelism.md`](../guide/jobs-and-parallelism.md) (the
source-verified client surface), README §39 (implementation notes still to migrate here),
`JobSystem.cpp`, `Scene.cpp` (the four-pass tick), Frontier `IslandWorld::IsLoading()` +
`common/LoadingScreen.h` usage.
