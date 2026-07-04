# API Reference — Frame Pipeline (SceneRenderer, Post-Processing, Environment)

> **STATUS: SKELETON** — to be filled by work order **D11** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/renderer/SceneRenderer.h`,
`renderer/PostProcessStack.h`, `renderer/EnvironmentMap.h`, `renderer/ShadowMap.h`,
`renderer/CoverageCapture.h`.

**Read first:** [`docs/design/frame-lifecycle.md`](../design/frame-lifecycle.md) — the pass
graph this chapter's API drives; systems explainer
[rendering-pipeline](../systems/rendering-pipeline.md). Real-world usage exemplars:
`Projects/Frontier` worlds (F2/F12–F16) and `Projects/Engine3DDemo`.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `SceneRenderer` — construction/configuration desc struct, the per-frame entry point(s), pass enable flags, per-pass GPU-profiler zones (F3), water reflection handoff, how a client injects draw callbacks per pass — enumerate exactly from the header
- [ ] `PostProcessStack` — HDR target creation/resize-in-place, tonemap (ACES/exposure/gamma), bloom, SSAO, FXAA, god rays, heat-haze distortion field, lens flare (F7), underwater mode (Tonemap), enable/parameter setters per effect
- [ ] `EnvironmentMap` — procedural-sky IBL bake (**bake-FBO completeness check; RGBA16F cube default**), skybox draw, `DrawSkyboxDetailed`/`SkyDetailDesc` (sun disc/stars/phased moon), `SetNightSky`/`SetMoon`, time-of-day drive
- [ ] `ShadowMap` — 2k directional map, PCF, `Renderer3D::SetShadow` pairing, camera-follow shadow pattern (F12a), texel-snapping notes if present
- [ ] `CoverageCapture` — top-down accumulation mask (F8: snow), capture pass API, `SetSnow`/`TexUnitSnowMask` = 12 pairing
- [ ] State-restore contract — every pass that changes GL state restores depth ON/ON, cull None, blend Alpha, rebinds the replaced framebuffer (doc 10 note 5) — a shared "Notes" block entries link to

## Sections to write

1. The pass graph (Mermaid flowchart: shadow → coverage → reflection → refraction → main → water → particles → post chain → present) — one diagram, reused by the systems doc. <!-- TODO(D11) -->
2. Entries per checklist. <!-- TODO(D11) -->
3. "Rolling your own vs SceneRenderer" — when to drive passes manually (Engine3DDemo pre-F2 style) vs let `SceneRenderer` orchestrate. <!-- TODO(D11) -->

---
*Changelog:*
