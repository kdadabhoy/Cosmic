# API Reference — 2D Rendering

> **STATUS: SKELETON** — to be filled by work order **D9** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/renderer/Renderer2D.h`,
`renderer/RenderPass.h`, `graphics/SubTexture2D.h`, `graphics/Font.h`.

**Read first:** the client guide chapter [`../guide/rendering-2d.md`](../guide/rendering-2d.md) —
the draw API, sprite sheets, SDF circles, text, instancing, `RenderPass`, the stats counters and
**every batch limit with its flush behaviour**. It **replaces root README §8 and §11–§14** (whose
bodies are now overviews pointing there), and it also owns the **world-space SDF text** half of the
retired README §27 — the ImGui-font half went to
[`../guide/editor-ui-and-theming.md`](../guide/editor-ui-and-theming.md). This chapter is their
formal per-call lookup.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Renderer2D` scene control — `BeginScene` overloads, `EndScene`, `Flush` semantics
- [ ] `DrawQuad` — every overload (vec2/vec3 × color/texture/tiling/tint/material/subtexture)
- [ ] `DrawRotatedQuad` — every overload (radians!)
- [ ] Circles — SDF circle draws (fade/thickness params), instanced circle path
- [ ] Lines & debug — `DrawLine`, `DrawRect`, line width behavior
- [ ] Instanced draws — the instancing entry points and when the batcher auto-switches
- [ ] Text — `DrawString`/text draw calls, `Font::Create`/default font, atlas notes
- [ ] `SubTexture2D` — `CreateFromCoords`, UV accessors, sprite-sheet workflow
- [ ] `RenderPass` — multi-camera/multi-target pass API ([guide](../guide/rendering-2d.md#render-more-than-one-camera)), interaction with the engine framebuffer
- [ ] `Statistics` — `GetStats`/`ResetStats`/`SetStatsStatus`, what counts as what

## Sections to write

1. Batching contract callout: what breaks a batch (texture slots, material switch, pass change) — link systems/rendering-2d.md for internals. <!-- TODO(D9) -->
2. Entries per checklist, grouped: scene control → quads → circles → lines → text → sprite sheets → passes → stats. <!-- TODO(D9) -->
3. Z-ordering + transparency note (higher z in front; alpha within a batch). <!-- TODO(D9) -->

---
*Changelog:*
