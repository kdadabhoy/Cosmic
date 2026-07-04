# 2D Renderer — How It Works

> **STATUS: SKELETON** — to be filled by work order **D28** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** every `DrawQuad` appends vertices to a CPU-side batch; `EndScene` uploads the
batch and draws thousands of sprites in a handful of GPU calls.
**Source:** `Cosmic/src/renderer/Renderer2D.*`, `graphics/SubTexture2D.*`, `graphics/Font.*`
**API Reference:** [../reference/rendering-2d.md](../reference/rendering-2d.md) · **Guide:** root README §8, §11–§14

## Section plan

1. **Overview** — why batching exists (draw calls are expensive; the postal-truck analogy: one truck, many letters). <!-- TODO(D28) -->
2. **Mental model** — vertex buffer as a growing array; texture-slot table; when the truck departs early (batch flush triggers). <!-- TODO(D28) -->
3. **Step-by-step** — one `DrawQuad(pos, size, texture)` from call to pixels. <!-- TODO(D28) -->
4. **Technical implementation** — mine README §36 (batch deep dive): buffer sizes, texture-slot limit, SDF circle path, line path, the instanced path and when it wins, text/atlas rendering (§27 internals), `RenderPass` interaction, stats counters. <!-- TODO(D28) -->
5. **Design decisions** — single shader + slot array vs sort-by-texture; z-ordering approach. <!-- TODO(D28) -->
6. **Limits & future work.** <!-- TODO(D28) -->

**Truth sources:** README §36 (migrating here), `Renderer2D.cpp` (constants at top),
`assets/shaders/` 2D shaders.
