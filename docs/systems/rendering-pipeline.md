# Frame Pipeline & Post-Processing — How It Works

> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine (Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as historical. The current exemplars are the template projects, `Projects/PendulumLab`, `Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and [`../parked-3d/systems/build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D) records what the split was.

> **2D trunk (2026-09-20):** `SceneRenderer` and `PostProcessStack` are the live 2D spine (`BeginHDR → sprites + 2D lights → post → DrawOverlay2D`). The shadow, reflection, environment/IBL and sky passes this skeleton planned are parked at [`../parked-3d/systems/rendering-pipeline-3d.md`](../parked-3d/systems/rendering-pipeline-3d.md) (parked 3D).

> **STATUS: SKELETON** — to be filled by work order **D29** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md). Biggest
> explainer — the work order allows two sessions (pipeline/passes, then lighting theory).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** a frame is a *pipeline of passes* — the main HDR render (sprites, tilemaps and 2D
lights), then a post chain (bloom, FXAA, tonemap, vignette) that turns raw HDR light into the final
image.
**Source:** `Cosmic/src/renderer/SceneRenderer.*`, `PostProcessStack.*`, `Light2DRenderer.*` + `Cosmic/assets/shaders/*`
**API Reference:** [../reference/rendering-pipeline.md](../reference/rendering-pipeline.md) · **Guide:** [`../guide/lighting-and-environment.md`](../guide/lighting-2d.md) · **Design spec:** [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md)

> **The guide chapter landed first (D55, split 2026-09-20).**
> [`../guide/lighting-2d.md`](../guide/lighting-2d.md) carries the 2D spine, every post toggle with
> its preconditions, the composite order and `ApplyEnvironment` — all from source, with line
> references. D29 should *summarise and link* those and spend its own words on §4's implementation
> and §5's rationale, which the guide deliberately does not cover.

> **Build note (Phase 29):** the configuration story here is **per class**, not per document.
> `SceneRenderer` and `PostProcessStack` ship in **both** engine builds — a 2D frame runs the same
> compositor spine (`BeginHDR` → sprites via `DrawTransparent` → tonemap/FXAA/bloom/vignette →
> `DrawOverlay2D`), which is what preserves `frame-lifecycle.md` §5 verbatim on both engines.
> `EnvironmentMap`, `ShadowMap` and `CoverageCapture` are excluded outright, and inside
> `SceneRenderer.h` the fence runs *through* `SceneRenderDesc`. See
> [`build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D).

## Section plan

1. **Overview** — "a photograph is developed in stages"; what HDR means and why the engine renders in it (real-light math first, display conversion last). <!-- TODO(D29) -->
2. **Mental model** — diagram **DG-8** (the full pass graph with read/write targets per pass). <!-- TODO(D29) -->
3. **Step-by-step** — one PendulumLab frame narrated pass by pass, with the GPU-profiler zone names as the section beats. <!-- TODO(D29) -->
4. **Technical implementation** — HDR target formats (S6.1), the 2D light buffer multiply, tonemap (ACES, exposure, gamma), bloom pyramid, FXAA, vignette; the RGBA16F-not-RGB16F portability rule. <!-- TODO(D29) -->
5. **Design decisions** — pass order rationale, why UI is drawn LDR after post, Gaussian vs progressive bloom. <!-- TODO(D29) -->
6. **Limits & future work** — the 3D-era toggles that still compile but do nothing (`SceneRenderer.h:106-108`); 2D-native particles (roadmap v5 deferred list). <!-- TODO(D29) -->

**Truth sources:** `frame-lifecycle.md` (state contract — summarize, don't fork),
`SceneRenderer.cpp` (the real pass sequence), `Light2DRenderer.cpp`, the shaders themselves
(uniform names are the contract).
