# API Reference — Graphics Resources

> **STATUS: SKELETON** — to be filled by work order **D8** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/graphics/Shader.h`, `graphics/Material.h`,
`graphics/Texture.h`, `graphics/TextureCube.h`, `graphics/FrameBuffer.h`, `graphics/Buffer.h`,
`graphics/VertexArray.h`, `graphics/UniformBuffer.h`, `graphics/StorageBuffer.h`,
`renderer/RenderCommand.h`, `renderer/BindingPoints.h`, `renderer/Renderer.h`.

**Read first:** root README §9 (materials & shaders), §10 (shader contract), §18
(framebuffer); systems explainer [rendering-3d](../systems/rendering-3d.md) for how these
resources flow through a frame.

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `Shader` — `Create` (**returns `nullptr` on compile/link failure** — pin this), uniform setters, `Bind`, compute dispatch entry points if present, `#type` block preprocessing contract
- [ ] `Material` — `Create`, `Set(...)` overloads, texture slots, **`Clone` + deferred-flush semantics (S12.2 breaking change: values read at flush — every entry that mutates state must repeat this)**, `SetTransparent`, `SetInstancingShader`
- [ ] `Texture2D` — `Create` file/spec/from-memory forms (**returns degraded non-null on failure** — pin), `SetSampling`, `SetData`, `Bind`, mip policy
- [ ] `TextureCube` — creation, IBL usage, `Bind`
- [ ] `FrameBuffer` — spec struct (attachments, MRT), `Create`, `Bind`/`Unbind`, `Resize`, `ReadPixel` (entity ID), `ReadDepth`, `BlitCopy`/blit verbs, color-renderable format guidance (**RGBA16F default for cube targets — RGB16F is not color-renderable off NVIDIA**)
- [ ] `VertexBuffer`/`IndexBuffer`/`BufferLayout`/`VertexArray` — layouts, dynamic vs static, draw prerequisites
- [ ] `UniformBuffer` — `Create(size, binding)`, `SetData`; which bindings the engine owns
- [ ] `StorageBuffer` — SSBO create/bind/set, compute pairing, `GpuMemoryBarrier`
- [ ] `RenderCommand` — every verb: clear/viewport, `SetCullMode`, `SetBlendMode(Alpha|Additive|Off)`, depth verbs, `BindTextureSlot`, draw/dispatch, timer-query GPU-profiler verbs (F3)
- [ ] `BindingPoints.h` — the full reserved registry (UBO bindings, SSBO bindings incl. particles @8 / instances @9, reserved sampler units `TexUnitIbl*`, `TexUnitShadowMap`, `TexUnitSnowMask` = 12) as a table with "who binds it, when"
- [ ] `Renderer` — `Init`, `OnWindowResize`, scene begin/end if public

## Sections to write

1. Resource ownership rules up front: `Ref<>` factories, **GPU-owning classes are non-copyable**, same-`Cosmic.dll` requirement (README §2). <!-- TODO(D8) -->
2. Entries per checklist. <!-- TODO(D8) -->
3. `BindingPoints` table — this is the *binding contract* other chapters link to. <!-- TODO(D8) -->

---
*Changelog:*
