# API Reference — Jobs & Parallelism

> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine (Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as historical. The current exemplars are the template projects, `Projects/PendulumLab`, `Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and [`../parked-3d/systems/build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D) records what the split was.

> **STATUS: SKELETON** — to be filled by work order **D17** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/jobs/JobSystem.h`, `jobs/ParallelSystem.h`,
`jobs/ParallelFor.h`, `jobs/SystemQuery.h`, `jobs/ComponentArray.h`, `jobs/DoubleBuffer.h`.

**Read first:** the guide chapter
[`../guide/jobs-and-parallelism.md`](../guide/jobs-and-parallelism.md) (D59) — written from source,
covering every header in this scope plus the threading contract. Root README §22 is now an overview
that points at it; §39 (parallel pipeline architecture) is still live Part II material. Systems
explainer: [jobs-parallelism](../systems/jobs-parallelism.md) (skeleton — D33).

> **D59 corrections to carry into the entries.** `ComponentArray<T>::From` maps **page 0 only** and
> returns an **empty view** (logging an error) when the pool spans more than one EnTT page — that
> guard now runs in every configuration, so the failure mode is "the loop silently does nothing".
> `ReadWriteQuery::Commit`'s structural-change guard is `CS_CORE_ASSERT` on a member behind
> `#ifdef CS_ENABLE_ASSERTS`, and **neither exists in any build** — the rule is real, nothing
> enforces it. And **nothing in the engine calls `Scene::OnUpdate` or `Scene::OnFixedUpdate`**, so a
> registered `System`/`ParallelSystem` never runs unless the scene's owner ticks the scene.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `JobSystem` — submit/wait API, worker count, **the async-load pattern** (Frontier `World::IsLoading()` + loading screen rides on this), main-thread-only constraints (GL calls stay on the main thread — pin this)
- [ ] `ParallelFor` — range splitting, grain size, capture rules
- [ ] `ParallelSystem` / `SystemQuery` — parallel ECS system base, query construction, read/write component declarations, scheduling guarantees
- [ ] `ComponentArray` — the SoA view handed to parallel systems
- [ ] `DoubleBuffer` — read/write flip semantics, when the flip happens in the frame

## Sections to write

1. Threading contract box FIRST: what may touch GL (main thread only), what may run on workers, where the sync points sit in the frame loop. <!-- TODO(D17) -->
2. Entries per checklist. <!-- TODO(D17) -->
3. Example: background terrain build → `IsLoading()` poll → swap-in on completion (the F-series loading-screen pattern, generalized). <!-- TODO(D17) -->

---
*Changelog:*
