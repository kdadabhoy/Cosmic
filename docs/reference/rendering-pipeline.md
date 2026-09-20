# API Reference — Frame Pipeline (SceneRenderer, Post-Processing)

> **STATUS: SKELETON** — to be filled by work order **D11** in
> [`docs/plans/archive/12-documentation-plan.md`](../plans/archive/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/renderer/SceneRenderer.h`,
`renderer/PostProcessStack.h`. The 3D half of this chapter (`EnvironmentMap`, `ShadowMap`,
`CoverageCapture`; deleted from `main` by AP-05) is parked at
[`../parked-3d/reference/rendering-pipeline-3d.md`](../parked-3d/reference/rendering-pipeline-3d.md) (parked 3D).

**Read first:** the guide chapter [`../guide/lighting-2d.md`](../guide/lighting-2d.md) — until this
skeleton is filled it is the client-facing source of truth for the scope above: the 2D spine, every
post toggle with its preconditions, `ApplyEnvironment` and `RenderToTexture`. Then
[`docs/design/frame-lifecycle.md`](../design/frame-lifecycle.md) — the render-state contract this
chapter's API drives — and the systems explainer [rendering-pipeline](../systems/rendering-pipeline.md).
Real-world usage exemplars: `Projects/PendulumLab`, `Projects/Starforge` (editor viewport) and
`Cosmic/src/layers/PlayerLayer.cpp`.

**D11 must not re-derive the guide's material.** Link it for usage and worked examples; this tier
carries signatures, parameters, return/failure behaviour and per-entry notes. History (2026-09-20): until AP-05 `EnvironmentMap.h`, `ShadowMap.h` and
`CoverageCapture.h` were the 3D-only part of this scope; they are deleted from `main` and their
skeleton is parked. `SceneRendererSettings` still carries `Skybox`/`IBL`/`Shadows`/`WaterReflections`
as do-nothing compatibility toggles (`SceneRenderer.h:106-108`); entries for them should say so.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `SceneRenderer` — construction/configuration desc struct, the per-frame entry point(s), pass enable flags, per-pass GPU-profiler zones (F3), how a client injects draw callbacks per pass — enumerate exactly from the header
- [ ] `PostProcessStack` — HDR target creation/resize-in-place, tonemap (ACES/exposure/gamma), bloom, SSAO, FXAA, heat-haze distortion field, lens flare (F7), underwater mode (Tonemap), enable/parameter setters per effect
- [ ] State-restore contract — every pass that changes GL state restores depth ON/ON, cull None, blend Alpha, rebinds the replaced framebuffer (doc 10 note 5) — a shared "Notes" block entries link to

## Sections to write

2. Entries per checklist. <!-- TODO(D11) -->
3. "Rolling your own vs SceneRenderer" — when to drive `Renderer2D` + `RenderPass` by hand vs let `SceneRenderer` orchestrate. <!-- TODO(D11) -->

---
*Changelog:*
