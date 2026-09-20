# Lighting & Post-Processing in 2D — Guide

> **History (2026-09-20, App Platform AP-D1).** This chapter was written for the two-configuration engine (Phase 29) and cites `Projects/Frontier`, `Projects/Engine3DDemo`, `Projects/ForgeIsle`, `Projects/ViperSim` or `#ifndef COSMIC_2D_ONLY` fences as worked examples. `main` is now the 2D-only trunk (D-PURGE): those projects, the fences and the `engine-2d` branch are gone from it and survive only on `engine-3d` (`0e8894b`, tag `cosmic-pre-2d-2026-09-16`), so read such mentions and their `file:line` references as historical. The current exemplars are the template projects, `Projects/PendulumLab`, `Projects/AnalysisSample` and `Projects/SF_Telem`; the trunk policy is in the root README 1.6 and [`../parked-3d/systems/build-2d-3d-split.md`](../parked-3d/systems/build-2d-3d-split.md) (parked 3D) records what the split was.

**What this covers:** what the engine-owned frame orchestrator `SceneRenderer` does for a 2D app on the
trunk: the spine `BeginHDR → sprites + 2D lights via DrawTransparent → post chain → DrawOverlay2D`, the
post-chain toggles on `SceneRenderDesc::Settings` (bloom, FXAA, tonemap/exposure/gamma, vignette, and the
depth-based effects that still compile), `ApplyEnvironment` from an `EnvironmentComponent`, and
`RenderToTexture`. The 2D *lights themselves* (`Light2DComponent`, `Ambient2D`, `Light2DRenderer`) are
documented where they are used: [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) and
[`rendering-2d.md`](rendering-2d.md).
**Source of truth:** `Cosmic/src/renderer/SceneRenderer.{h,cpp}`, `renderer/PostProcessStack.{h,cpp}`,
`renderer/Light2DRenderer.{h,cpp}`, `scene/Components.h` (`EnvironmentComponent`, `Light2DComponent`),
`Cosmic/assets/shaders/Tonemap.glsl`.
**API Reference:** [../reference/rendering-pipeline.md](../reference/rendering-pipeline.md) *(skeleton)* ·
**How it works:** [../systems/rendering-pipeline.md](../systems/rendering-pipeline.md) *(skeleton)*

> **History (2026-09-20, App Platform AP-D1).** This chapter was split out of `lighting-and-environment.md`,
> which described the full 3D frame (sun and point lights, PBR + IBL, the four sky modes, time of day, shadows,
> coverage capture). That code was deleted from `main` by AP-05 (D-PURGE) and the original chapter is kept
> verbatim at [`../parked-3d/guide/lighting-and-environment.md`](../parked-3d/guide/lighting-and-environment.md) (parked 3D)
> (parked 3D). The sections below are the parts that apply to the 2D trunk, copied from it unchanged except
> for this note; AP-D2 owns the rewrite. Rows and fields that name shadow maps, the sun, terrain, water or
> emitters exist in the headers as 3D-era toggles kept for source compatibility and do nothing on `main`
> (`SceneRenderer.h:106-108`); the god-rays pass was removed outright (AP-05 [B1]).

## The 2D spine

`SceneRenderer::Render(desc)` on the trunk runs, in order: clear to `Settings.ClearColor` → bind the HDR
target (`PassOpaqueHDR`) → your `desc.DrawTransparent` callback, which is where `Scene::OnRenderSprites` draws
sprites and tilemaps and `Scene::OnRender2DLights` multiplies the 2D light buffer over them →
`PassPostAndComposite` (the table below) into the bound LDR viewport target → your `desc.DrawOverlay2D`
callback for canvas UI, which post never touches (**UI is LDR**). The editor viewport and `PlayerLayer` both
call `ApplyEnvironment(env, desc)` first so the scene’s single `Environment` entity drives exposure and the
post toggles.

```cpp
SceneRenderDesc desc;
desc.Camera     = &camera;              // any Camera; 2D apps pass the orthographic one
desc.EcsScene   = m_Scene.get();
if (auto* env = m_Scene->FindEnvironment()) m_Renderer.ApplyEnvironment(*env, desc);
desc.DrawTransparent = [&](const SceneDrawContext& c) { m_Scene->OnRenderSprites(c); };
desc.DrawOverlay2D   = [&] { m_Scene->OnRenderUi(); };
m_Renderer.Render(desc);
```

## The post chain

Everything here lives on `desc.Settings` and is applied to the owned `PostProcessStack` at the top
of pass 8 (`SceneRenderer.cpp:695-745`). Two stages: `RenderEffects` runs the screen-space passes
into their own buffers, then `Composite` folds them all into one tonemap draw against your LDR
target.

| Effect | Flag (default) | Parameters | Stage | Notes |
| --- | --- | --- | --- | --- |
| SSAO | `SSAO` (**off**) | `SsaoRadius` 0.5, `SsaoBias` 0.025 | `RenderEffects` → half-res, then a blur | Reconstructs view-space position from scene depth; needs `desc.Projection`, which `Render` passes for you |
| Bloom | `Bloom` (**off**) | `BloomThreshold` 1.0, `BloomKnee` 0.6, `BloomIntensity` 0.6 | `RenderEffects` → half-res | Soft-knee threshold then **10** separable Gaussian ping-pong passes |
| God rays | `GodRays` (**off**) | `GodRaysIntensity` 0.6, `GodRaysDensity` 0.04 | `RenderEffects` → half-res | Raymarches the **shadow map** — see the gate below |
| Height fog | `Fog` (**off**) | `FogColor`, `FogDensity` 0.02, `FogHeightFalloff` 0.12, `FogBaseHeight` 0 | folded into the tonemap | Reconstructs world position from depth |
| Underwater | `Underwater` (**off**) | `UnderwaterY`, `UnderwaterColor`, `UnderwaterDensity`, `UnderwaterTint`, `UnderwaterDeepColor`, `UnderwaterDepthReference`, `UnderwaterCaustic{Strength,Scale}` | folded into the tonemap | Gated *shader-side* against `UnderwaterY` — see below |
| Heat haze | `HeatHaze` (**off**) | `HeatHazeStrength` 0.02 | distortion field + tonemap fetch offset | Needs `DistortionEmitters` — see below |
| Tonemap | always | `desc.Exposure` 1.0, `Settings.Gamma` 2.2 | `Composite` | ACES; `Gamma` 2.2 reproduces the previously hardcoded curve |
| Vignette | `Vignette` (**off**) | `VignetteAmount` 0.35, `VignetteRadius` 0.9, `VignetteFeather` 0.4, `VignetteColor` black | folded into the tonemap | Post-tonemap edge darkening; amount 0 makes the shader skip the block |
| Lens flare | `LensFlare` (**off**) | `LensFlareIntensity` 0.35 | after tonemap, **before** FXAA | Tinted by `Lights.SunColor`; sun screen position derived from the camera |
| FXAA | `FXAA` (**on**) | — | last | Adds a full-res LDR intermediate; off means the tonemap writes straight to your FBO |
| Wireframe | `Wireframe` (**off**) | — | rasterization mode | Geometry passes draw as lines and the skybox is skipped; `Fill` restored before post |
| Selection outline | `OutlineEnabled` (**off**) | `OutlineColor`, `OutlineWidthPx` 2.0 | after composite | Requires `EcsScene` + `SelectedEntities`; editor-facing but generic |

Three of these have preconditions that are easy to trip:

**God rays silently require shadows.** `SceneRenderer` computes
`godRays = s.GodRays && s.Shadows` (`:728`) — shafts raymarch the shadow map, so with `Shadows` off
they are unconditionally disabled. Then `RenderEffects` adds a second gate,
`m_GodRaysEnabled && m_ShaftShadowMapID != 0` (`PostProcessStack.cpp:196`). No warning is logged for
either.

**Heat haze requires distortion emitters.** The tonemap only samples the offset field if something
wrote it: `s.HeatHaze && !desc.DistortionEmitters.empty()` (`:747`), and then
`m_HeatHazeEnabled && m_DistortionWritten` in `Composite` (`PostProcessStack.cpp:454`). Setting
`HeatHaze = true` with an empty `DistortionEmitters` list is a no-op. Populate it with the same
emitters you want writing the field:

```cpp
desc.Settings.HeatHaze         = true;
desc.Settings.HeatHazeStrength = 0.015f;
desc.DistortionEmitters.push_back(m_HazeEmitter.get());   // usually also in desc.Emitters
```

**Underwater is a per-frame decision, not a mode.** The tonemap tests the camera against
`UnderwaterY` shader-side, but the flag itself is yours to drive. The idiom (Frontier
`IslandWorld.cpp:538`) uses a small margin so crossing the surface is not an instant pop:

```cpp
desc.Settings.Underwater  = m_UnderwaterEnabled && (camPos.y < kOceanY + 1.0f);
desc.Settings.UnderwaterY = kOceanY;
```

Depth grading (`UnderwaterDeepColor` + `UnderwaterDepthReference`) darkens and blue-shifts as the
camera descends; `UnderwaterCausticStrength` (0 = off) dances light webs over submerged geometry and
needs `desc.TimeSeconds` to advance.

### The composite order, precisely

Inside one tonemap draw (`PostProcessStack::Composite`): scene fetch — displaced by the heat-haze
field if present — then height fog, then the underwater medium, then AO modulation, then additive
bloom, then additive sun shafts, then the ACES curve with exposure, then gamma, then vignette. Lens
flare is a second additive draw on top of that. FXAA, if on, is a third pass reading the LDR
intermediate. Anything you draw in `DrawOverlay2D` happens after all of it and is never touched by
post — that is the standing contract: **UI is LDR**.

### Driving the post chain from a scene

`SceneRenderer::ApplyEnvironment(env, desc)` maps an `EnvironmentComponent` onto the desc. It is
what the editor viewport and `PlayerLayer` call each frame for the scene's single `Environment`
entity. What it **does** map:

`Exposure` · `Skybox` · `IBL` · `Fog` + all four fog params · `Bloom`/`BloomThreshold`/
`BloomIntensity` · `SSAO`/`SsaoRadius` · `FXAA` · `LensFlare`/`LensFlareIntensity` · all five
vignette fields · `AmbientIntensity` · `Gamma` · the sun (direction/colour/intensity) · the
environment's sun direction, sky intensity, HDRI and physical-sky state.

What it does **not** map, and therefore stays at whatever you set on the desc: `Shadows`,
`ShadowCenter`, `ShadowRadius`, `ShadowBias`, `WaterReflections`, `TerrainCastsShadows`,
`ClearColor`, `BloomKnee`, `SsaoBias`, every god-ray / heat-haze / underwater / wireframe / outline
field, `Lights.Ambient`, and `Lights.Points`.

> **A scene with no `Environment` entity renders differently under `PlayerLayer`.** With
> `FindEnvironment()` returning null, `PlayerLayer` explicitly sets `Skybox`, `IBL` and `Shadows` to
> **false** (`PlayerLayer.cpp:385-390`) — the opposite of the `SceneRendererSettings` defaults. A
> packaged app whose scene lacks the entity gets a flat, shadowless, sky-less frame. Add an
> `Environment` entity to any scene you intend to ship.

---

## Rendering into a texture

`RenderToTexture(desc, target)` runs a complete frame — env, sky, shadows, post — into an offscreen
framebuffer instead of the bound viewport, then re-binds whatever was bound on entry. It is the
stable verb behind minimaps, security cameras, portals and thumbnails.

```cpp
m_Minimap.RenderToTexture(desc, m_MinimapTarget);   // a DEDICATED SceneRenderer
```

Use a **dedicated** `SceneRenderer` sized to the target. `RenderToTexture` calls `SetViewportSize`
when the sizes differ, so sharing your main renderer resizes the whole post stack twice per frame.
A headless, uninitialized or null target is a safe no-op. The minimap *logic* — what to draw, fog of
war, orientation — stays app-side; the engine ships only the generic verb.

> `SceneRenderer::RenderToTexture` produces a `Ref<FrameBuffer>`, while
> `UiImageComponent::RuntimeTexture` wants a `Ref<Texture2D>`, and **no engine call bridges them**.
> See [`game-ui.md`](game-ui.md) for the client-side adapter over the colour-attachment handle.

---

## See also

- [`rendering-2d.md`](rendering-2d.md) — `Renderer2D`, `RenderPass`, and how sprites reach the HDR target
- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — `Light2DComponent`, `Ambient2D`, sorting layers
- [`materials-and-shaders.md`](materials-and-shaders.md) — the shader contract and `BindingPoints`
- [`entities-and-components.md`](entities-and-components.md) — `EnvironmentComponent` field-by-field
- [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md) — the resource and render-state contract
- [`../parked-3d/guide/lighting-and-environment.md`](../parked-3d/guide/lighting-and-environment.md) (parked 3D) — the full original chapter
