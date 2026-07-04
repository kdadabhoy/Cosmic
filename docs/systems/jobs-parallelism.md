# Jobs & Parallelism — How It Works

> **STATUS: SKELETON** — to be filled by work order **D33** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a worker-thread pool runs simulation work in parallel while the main thread
keeps exclusive ownership of the GPU; parallel ECS systems declare what they read and write
so the scheduler can overlap them safely, and double buffers let readers see last frame's
data while writers build the next.
**Source:** `Cosmic/src/jobs/*` (JobSystem, ParallelSystem, ParallelFor, SystemQuery, ComponentArray, DoubleBuffer)
**API Reference:** [../reference/jobs.md](../reference/jobs.md) · **Guide:** root README §22, §39

## Section plan

1. **Overview** — why multithreading a game loop is dangerous and how declared access (read vs write) makes it safe. <!-- TODO(D33) -->
2. **Mental model** — diagram **DG-12** (main thread + worker lanes + sync points within the frame). <!-- TODO(D33) -->
3. **Step-by-step** — a `ParallelFor` over 100k particles CPU-side; a background terrain build with `IsLoading()` polling + swap-in (the Frontier loading-screen pattern). <!-- TODO(D33) -->
4. **Technical implementation** — pool sizing, work stealing/queueing model (verify in `JobSystem.cpp`), `SystemQuery` conflict detection, `ComponentArray` SoA views, `DoubleBuffer` flip timing, **the GL rule: no GPU calls off the main thread — ever** (single GL context; how async loads split CPU prep from main-thread upload). <!-- TODO(D33) -->
5. **Design decisions** — own pool vs std::async; where the 20-agent telemetry template fits as a stress exemplar. <!-- TODO(D33) -->
6. **Limits & future work.** <!-- TODO(D33) -->

**Truth sources:** README §22/§39 (migrating here), `JobSystem.cpp`, Frontier
`World::IsLoading()` + `common/LoadingScreen.h` usage.
