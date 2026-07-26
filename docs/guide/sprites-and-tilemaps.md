# Sprites & Tilemaps — Guide

**What this covers:** authoring a 2D game out of **components** rather than draw calls —
`SpriteRendererComponent` and its three draw modes, the painter list that decides sort order,
`SpriteAnimationComponent` flipbooks, `TilemapComponent` (atlas layout, the cell array, the
`kMaxGrid = 1024` cap, the culled draw), `Light2DComponent` with the 2D light composite and
`Ambient2D`, and the `Camera2DController` pan/zoom rig. Throughout: **which parts the editor authors
for you and which parts only exist in code.**
**Source of truth:** `Cosmic/src/scene/Components.h`, `scene/Scene.cpp`
(`UpdateSpriteAnimations`, `BuildSpriteDrawList`, `OnRenderSprites`, `OnRender2DLights`),
`renderer/Light2DRenderer.{h,cpp}`, `Cosmic/assets/shaders/Light2D.glsl`,
`camera/Camera2DController.{h,cpp}`, `reflect/TypeRegistry.cpp`, `scene/SceneSerializer.cpp`
**API Reference:** [../reference/ecs.md](../reference/ecs.md) *(skeleton — D13)* ·
[../reference/rendering-2d.md](../reference/rendering-2d.md) *(skeleton — D9)*.
`camera/Camera2DController.h` and `renderer/Light2DRenderer.h` have **no row in the reference
manifest at all**, so this chapter is the client-facing source for both.
**How it works:** [../systems/rendering-2d.md](../systems/rendering-2d.md) *(skeleton — D28)*
**Configuration:** **both.** Every component and every call below is shared source that compiles
unfenced in the 3D and 2D engine builds ([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)).

This is the *component* half of 2D. Its sibling, [`rendering-2d.md`](rendering-2d.md), is the
*immediate-mode* half — `Renderer2D::DrawQuad` and friends, called from a layer. You can use either,
or both in one project. The difference in one line: **add a `SpriteRendererComponent` and the scene
draws it for you, in a sorted order, forever; call `DrawQuad` and you own the ordering and the call
site.** Everything on this page ends up going through `Renderer2D` — the components are the
authoring layer on top.

The worked example is **ForgePong**, the 2D flagship sample. It is generated in code by
`StarforgeApp::BuildForgePong` (`Projects/Starforge/src/StarforgeApp.cpp:3027`) and reachable from
the Starforge homescreen's *Pong Sample* button; on a 2D engine build it is also the first-run
offer. Every snippet below that says "ForgePong" is quoting it.

---

## Quick start

A complete playable 2D scene: an orthographic camera, three sprites, and a script that moves one of
them.

```cpp
#include "Cosmic.h"

Cosmic::Ref<Cosmic::Scene> BuildCourt()
{
    using namespace Cosmic;
    Ref<Scene> scene = Scene::Create();

    // The camera. OrthoSize is the visible HALF-HEIGHT in world units, so 5.0
    // at 16:9 shows a 16 x 9 world-unit court.
    {
        Entity cam = scene->CreateEntity("Camera");
        auto& c = cam.AddComponent<CameraComponent>();
        c.ProjectionType = CameraComponent::Projection::Orthographic;
        c.OrthoSize      = 5.0f;
        cam.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 10.0f };
    }

    // A flat-colour sprite: no texture, so Transform.Scale IS the world size.
    {
        Entity e = scene->CreateEntity("PaddleL");
        auto& t = e.GetComponent<TransformComponent>();
        t.Position = { -7.4f, 0.0f, 0.0f };
        t.Scale    = { 0.3f, 1.6f, 1.0f };
        auto& s = e.AddComponent<SpriteRendererComponent>();
        s.Color  = { 0.95f, 0.97f, 1.0f, 1.0f };
        s.ZOrder = 1;
        e.AddComponent<NativeScriptComponent>("PaddleController");
    }

    // A textured sprite: world size comes from the image, not from Scale.
    {
        Entity e = scene->CreateEntity("Ball");
        e.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.1f };
        auto& s = e.AddComponent<SpriteRendererComponent>();
        s.TexturePath   = "project://textures/ball.png";
        s.PixelsPerUnit = 32.0f;    // a 32 px image is 1 world unit across
        s.ZOrder        = 2;
    }

    return scene;
}
```

Save that with `SceneSerializer::Save(*scene, FileSystem::Resolve("project://scenes/Game.cscene"))`
and it opens in Starforge — the components round-trip through reflection, so everything above is
also editable in the Inspector. See [`scenes-and-serialization.md`](scenes-and-serialization.md).

Three things this is quietly asserting:

- **Nothing calls a draw function.** `Scene::OnRenderSprites` walks the registry each frame; you
  never submit a sprite by hand.
- **The camera Z matters.** `CameraComponent::Near` defaults to `0.1`, so a camera sitting at
  `z = 0` clips away everything at `z ≥ -0.1`. Put the ortho camera at a positive Z (ForgePong uses
  `10.0`) and lay the art out near `z = 0`.
- **`ZOrder`, not `z`, is the primary sort key.** `z` is only the tie-break. See
  [Control what draws in front](#control-what-draws-in-front).

---

## Put a sprite on screen

```cpp
struct SpriteRendererComponent    // scene/Components.h:151
{
    Ref<Material> ActiveMaterial;
    glm::vec4     Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    bool          FlipX = false;
    bool          FlipY = false;

    glm::vec4     SourceRect{ 0.0f, 0.0f, 1.0f, 1.0f };   // normalised UV {u0,v0,u1,v1}, V top-left
    float         PixelsPerUnit = 100.0f;
    int32_t       ZOrder = 0;

    std::string   TexturePath;                            // AssetPath("texture")
    bool          YSort   = false;
    bool          Enabled = true;

    Ref<Texture2D> Resolved;       // runtime-only, not reflected
    std::string    ResolvedPath;   // runtime-only, not reflected
};
```

### The three draw modes, in priority order

`Scene::OnRenderSprites` picks exactly one branch per sprite (`Scene.cpp:703-730`):

| Condition | What draws | Sizing |
| --- | --- | --- |
| `Resolved` texture with `GetWidth() > 0` | `DrawRotatedQuad(pos, size, rot, subTexture, Color)` — the `SourceRect` window of the texture, tinted by `Color` | **from the image**: `SourceRect` texels ÷ `PixelsPerUnit` × `Transform.Scale.xy` |
| else `ActiveMaterial` is set | `DrawRotatedQuad(pos, size, rot, ActiveMaterial)` — your own shader | `Transform.Scale.xy` |
| else | `DrawRotatedQuad(pos, size, rot, Color)` — a flat quad | `Transform.Scale.xy` |

`Color` is **ignored** on the material path (the material's own `u_Color` applies instead — see
[`rendering-2d.md`](rendering-2d.md#draw-a-quad-with-your-own-shader)).

### The sizing rule, exactly

The rule is a public static so picking, outlines and the render pass cannot disagree
(`Components.h:199`):

```cpp
static glm::vec2 WorldSize(const SpriteRendererComponent& s, const glm::vec2& scale,
                           int texW, int texH);
```

```
textured:    { (u1-u0) * texW / PixelsPerUnit * scale.x,
               (v1-v0) * texH / PixelsPerUnit * scale.y }
untextured:  scale
```

So a 16 × 16 sprite at `PixelsPerUnit = 13` is ≈ 1.23 world units wide at `Scale = 1` — the number
ForgePong uses to make one flipbook frame about 1.2 units (`StarforgeApp.cpp:3123`). Raising
`PixelsPerUnit` makes the sprite *smaller*; it is "how many image pixels fit in one world unit".
`PixelsPerUnit <= 0` is treated as `1.0`.

**The untextured case is the one that surprises people.** With no texture, `Transform.Scale.xy` is
the sprite's world size in units, not a multiplier — a `Scale` of `{0.3, 1.6}` is a 0.3 × 1.6
rectangle. That is the legacy quad behaviour and every flat-colour sprite in ForgePong relies on it.

### Flips are negative scale

`FlipX`/`FlipY` negate the corresponding component of the computed size (`Scene.cpp:710-711`,
`:721-722`, `:727-728`). `WorldSize` itself returns unsigned values; the flip is applied by the
draw. This means a flip mirrors about the sprite's centre and costs nothing.

### `SourceRect` is normalised UV with a top-left origin

`{u0, v0, u1, v1}` in the `0..1` range, `v` measured **downward from the top of the image** — the
image-editor convention, not GL's. `OnRenderSprites` converts for you when it builds the
`SubTexture2D` (`Scene.cpp:714-716`):

```cpp
auto sub = CreateRef<SubTexture2D>(s.Resolved,
    glm::vec2{ src.x, 1.0f - src.w },   // uvMin (bottom-left)
    glm::vec2{ src.z, 1.0f - src.y });  // uvMax (top-right)
```

Default `{0, 0, 1, 1}` is the whole image. A `SpriteAnimationComponent` overwrites this field every
frame — do not hand-edit `SourceRect` on an animated sprite.

### Texture resolution is lazy, main-thread, and keyed on the path

`TexturePath` is resolved on first draw and cached in `Resolved`; the resolve repeats only when
`TexturePath` changes (`Scene.cpp:694-699`). It goes through `AssetLibrary::GetTexture`, so VFS
prefixes work (`project://`, `engine://`, `user://`) and the texture is shared with every other
consumer of the same path. Assigning `Resolved` directly (leaving `TexturePath` empty) is legal and
skips the resolve entirely — that is how the render tests feed procedural textures in
(`tests/render/render_2d.cpp:16-19`).

For **pixel art**, set the process-wide sampling preset once, before content loads:

```cpp
Cosmic::AssetLibrary::SetDefaultTextureSampling(Cosmic::TextureFilter::Nearest,
                                                Cosmic::TextureWrap::ClampToEdge);
```

It only affects textures loaded *after* the call; already-cached textures keep their sampling
(`AssetLibrary.h:72-79`).

### `Enabled` and `Active`

Two independent gates, both checked in `BuildSpriteDrawList` (`Scene.cpp:562-565`):

- `SpriteRendererComponent::Enabled` — per-sprite. Hidden from the Inspector and omitted from the
  scene file while `true` (`TypeRegistry.cpp:54`), so it costs nothing in an unchanged scene.
- `TagComponent::Active` via `Scene::IsActiveInHierarchy` — per-entity, inherited from ancestors.

Both are honoured by the sprite pass, the tilemap pass and the 2D light pass. See
[`entities-and-components.md`](entities-and-components.md) for the full picture of where `Active` is
and is not respected.

---

## Control what draws in front

Every sprite and every tilemap in the scene goes into one **painter list**, rebuilt each frame by
`Scene::BuildSpriteDrawList` (`Scene.cpp:552`) and drawn front-to-back-last by `OnRenderSprites`.
The sort is three keys deep:

| Key | Source | Notes |
| --- | --- | --- |
| 1. `ZOrder` | `SpriteRendererComponent::ZOrder` / `TilemapComponent::ZOrder` | ascending — lower draws first, i.e. **further back** |
| 2. per-item key | `YSort ? -Position.y : Position.z` (sprites) · `Position.z` (tilemaps) | ascending |
| 3. entity handle | `entt::entity` value | a stable tie-break so the order never flickers |

`ZOrder` is a signed `int32_t`, so negative values push things behind the default layer —
ForgePong's centre line uses `ZOrder = -1` (`StarforgeApp.cpp:3102`).

**Tilemaps and sprites interleave in the same list.** A tilemap with `ZOrder = 0` and a sprite with
`ZOrder = 0` are ordered by their `Position.z`; there is no separate "tile layer" concept.

### Y-sort for top-down games

Set `YSort = true` and the item's sort key becomes `-Position.y` instead of `Position.z`, so a
sprite lower on the screen draws **in front** — the top-down convention that lets a character walk
behind a tree. It is per-sprite: mix Y-sorted characters and Z-sorted background props in the same
`ZOrder` band freely, remembering that they then compete on different scales (a `-y` of `-3.0` and
a `z` of `0.5` sort against each other numerically).

### Depth state during the sprite pass

The pass runs with **depth test on, depth write off**, straight alpha (`Scene.cpp:618-622`):

- 3D geometry drawn earlier in the frame **can occlude** a sprite. That is deliberate — it is what
  makes a 2.5D scene work.
- Sprites **never occlude each other by depth**, because nothing writes depth. Ordering is purely
  the painter list.

Depth write is restored to `true` on exit; depth test was already on.

### Two things the painter list does *not* do

**Parenting does not move a sprite.** `OnRenderSprites`, the tilemap draw and `OnRender2DLights` all
read the **raw `TransformComponent`** — they never compose through `Scene::GetWorldTransform`. Only
the 3D submit path, physics and voxels honour the hierarchy. Parent a sprite to another entity for
organisation, `Active` inheritance, or prefab structure; do not expect the child to follow the
parent's position. (This is a deliberate 2D design decision, not an oversight; if you need
following, write the position in a script.)

**Sprites are not frustum-culled.** Every enabled, active sprite is submitted every frame regardless
of where the camera is looking. Only the *tilemap cell walk* is culled. At ForgePong scale this is
irrelevant; at ten thousand sprites it is the first thing to fix in your own code.

### Draw order inside one flush is still fixed

Everything on this page becomes `Renderer2D` quads, and `Renderer2D::Flush()` always emits quads →
lines → circles → text irrespective of submission order. The painter list orders sprites relative to
*each other* — it cannot put a sprite over a line. See
[`rendering-2d.md`](rendering-2d.md#draw-order-between-primitive-types-is-fixed).

---

## Animate a sprite sheet

```cpp
struct SpriteAnimationComponent   // scene/Components.h:221
{
    std::string SheetPath;        // AssetPath("texture")
    int32_t     FrameW  = 16;     // cell width in texels
    int32_t     FrameH  = 16;     // cell height in texels
    int32_t     Frames  = 1;      // cells to play, along Row
    int32_t     Row     = 0;      // 0-based, row 0 is the TOP of the sheet
    float       FPS     = 8.0f;
    bool        Playing = true;
    bool        Loop    = true;

    float       Elapsed = 0.0f;   // runtime-only, not reflected
};
```

A flipbook drives the **sibling `SpriteRendererComponent::SourceRect`** — it draws nothing itself.
An animated sprite therefore needs both components, and the sprite's own texture should be the same
sheet:

```cpp
Entity e = scene->CreateEntity("HitFx");
auto& s = e.AddComponent<SpriteRendererComponent>();
s.TexturePath   = "project://textures/hit.png";
s.PixelsPerUnit = 13.0f;
s.ZOrder        = 5;

auto& a = e.AddComponent<SpriteAnimationComponent>();
a.SheetPath = "project://textures/hit.png";
a.FrameW = 16; a.FrameH = 16; a.Frames = 8; a.FPS = 24.0f;
a.Loop = false; a.Playing = false;      // a one-shot, parked until something fires it
```

That is ForgePong's impact burst, verbatim (`StarforgeApp.cpp:3117-3128`). The sheet itself is
generated at project-creation time: eight 16 × 16 frames laid out in **one row of 128 × 16 pixels**.

### Who ticks it — and who does not

`Scene::UpdateSpriteAnimations(float dt)` is **owner-ticked**. Nothing in the engine calls it for
you:

| Host | Call site | When |
| --- | --- | --- |
| `PlayerLayer` (packaged app) | `PlayerLayer.cpp:259` | every frame while not paused |
| Starforge | `StarforgeApp.cpp:807` | **only in Play mode** |
| your own layer | — | you call it |

So flipbooks are frozen in the Starforge viewport until you hit Play — that is expected, not a bug.
And `Scene::OnUpdate` is *not* the hook: it has no callers anywhere in the engine, the sample
projects or the tests.

### The frame maths (both functions are pure and unit-tested)

```cpp
static int SelectFrame(float elapsed, float fps, int frames, bool loop);
static glm::vec4 FrameUV(int texW, int texH, int frameW, int frameH, int row, int frame);
```

`SelectFrame` returns `0` when `frames <= 1` or `fps <= 0`; a looping clip wraps (with a modulo that
is correct for negative input), a one-shot clamps to the last frame and **stays there**. `FrameUV`
returns the whole image `{0,0,1,1}` on any non-positive dimension, and lays frames out **left to
right along `Row`**, with row 0 at the top of the image.

Per tick, `UpdateSpriteAnimations` advances `Elapsed` only if `Playing`, then **always** recomputes
`SourceRect` from the current `Elapsed`. Two consequences:

- Setting `Playing = false` freezes the *current* frame; it does not reset to frame 0.
- The sheet is resolved through `AssetLibrary::GetTexture` on **every tick, for every animated
  sprite** (`Scene.cpp:537-538`). That is a hash lookup, not a load, but it is not free — and if the
  sheet is unavailable (headless, or not yet on disk) the update `continue`s and the sprite keeps
  its previous `SourceRect`.

### Play a one-shot

There is no "play once and stop" flag beyond `Loop = false`, and no completion callback. The
in-tree pattern is a script-side cooldown (`PongBall.h:104-126`):

```cpp
void MoveFx(const glm::vec3& pos, bool play)
{
    using namespace Cosmic;
    auto& reg = GetScene().GetRegistry();
    for (auto e : reg.view<TagComponent, SpriteAnimationComponent, TransformComponent>())
    {
        if (reg.get<TagComponent>(e).Tag != "HitFx")
            continue;
        reg.get<TransformComponent>(e).Position = pos;
        auto& anim = reg.get<SpriteAnimationComponent>(e);
        anim.Elapsed = 0.0f;          // restart the clip
        anim.Playing = play;
        if (play && anim.FPS > 0.0f)
            m_FxCooldown = (float)anim.Frames / anim.FPS;   // seconds until it has played out
        break;
    }
}
```

…and in `OnUpdate`, once the cooldown expires, the effect is parked far offscreen and stopped. Note
`Elapsed = 0.0f` is what restarts a clip — it is a public field precisely so this works.

---

## Paint a tilemap

```cpp
struct TilemapComponent            // scene/Components.h:279
{
    static constexpr int32_t kMaxGrid = 1024;

    std::string TilesetPath;       // AssetPath("texture") — the atlas
    int32_t     TileW   = 16;      // texels per tile in the atlas
    int32_t     TileH   = 16;
    int32_t     Columns = 0;       // atlas columns; 0 => texture width / TileW
    int32_t     GridW   = 32;      // map size in cells, 1..kMaxGrid
    int32_t     GridH   = 32;
    int32_t     ZOrder  = 0;
    std::vector<uint16_t> Cells;   // row-major [y * GridW + x]; y = 0 is the BOTTOM row

    Ref<Texture2D> Resolved;       // runtime-only
    std::string    ResolvedPath;   // runtime-only
};
```

### Cell values and the atlas

`Cells` holds **tile index + 1**. `0` means empty (nothing is drawn); `v > 0` selects atlas tile
`v - 1`, counted row-major from the atlas's **top-left**:

```
col = (v - 1) % Columns
row = (v - 1) / Columns
```

`Columns == 0` derives it as `textureWidth / TileW`, clamped to at least 1 (`Scene.cpp:647-648`).
The atlas's row 0 is the top of the image; the draw flips V for you when it builds each
`SubTexture2D` (`Scene.cpp:678-680`).

### World mapping

**One cell is exactly one world unit**, and the entity's `Position` is the map's **bottom-left
corner**. Cell `(cx, cy)` is drawn centred at `Position.xy + (cx + 0.5, cy + 0.5)`, at
`Position.z`. Cells grow +X to the right and +Y upward — so `Cells` row 0 is the bottom row of the
map, the opposite of the atlas's row order. (Yes, this catches everyone once.)

**The entity's rotation and scale are ignored.** The tilemap draw is axis-aligned and unscaled in
v1. If you need a 32-pixel tile to be one world unit, that is what `TileW`/`TileH` and the atlas
resolution decide, not `Transform.Scale`.

### Sizing the grid

```cpp
void EnsureCells();               // clamp GridW/GridH to 1..1024, resize Cells, new cells = 0
bool InBounds(int x, int y) const;
uint16_t At(int x, int y) const;  // 0 for out-of-bounds or a short buffer — never throws
```

`EnsureCells` is the only thing that keeps `Cells.size()` consistent with `GridW * GridH`, and it is
**not automatic**: the draw calls it (`Scene.cpp:643`), the editor calls it, `SceneSerializer` calls
it on load. If you resize a grid in code, call it yourself before touching `Cells`. Note that it
resizes **linearly** — growing `GridW` shifts every existing row, it does not re-flow the map.

`kMaxGrid = 1024` is a hard clamp in `EnsureCells`, so the largest map is 1024 × 1024 = 1 048 576
cells (2 MB of `uint16_t`). It is also mirrored as the reflected `Range(1, 1024)` on `GridW`/`GridH`
so the Inspector cannot exceed it. Nothing warns when a value is clamped.

### The culled draw

`OnRenderSprites` computes the camera's world-space XY bounds **once per call**, by pushing the
eight NDC cube corners through `inverse(viewProjection)` (`Scene.cpp:600-616`) — exact for an
orthographic camera, conservative for a perspective one. Each tilemap then walks only the cells
inside that rect:

```
x0 = max(0,         floor(cullMin.x - Position.x))
x1 = min(GridW - 1, ceil (cullMax.x - Position.x))
```

…and the same for Y. Empty cells are skipped, and one `SubTexture2D` is built per **distinct tile
id used in this draw** and reused for every cell with that id — so a 1024 × 1024 map costs work
proportional to what is on screen, not to the map. All of it lands in the normal quad batch, so a
screenful of tiles from one atlas is typically one draw call.

The bounds are only computed when the scene contains at least one tilemap; a scene with no 2D
content at all makes **no GL calls** in this pass (`Scene.cpp:592-593`).

### Editing cells

In code, write `Cells` directly (respecting `EnsureCells`), or use the flood fill, which is a pure
static shared by the editor and the tests:

```cpp
static std::vector<uint32_t> FloodFill(std::vector<uint16_t>& cells, int gridW, int gridH,
                                       int x, int y, uint16_t value);
```

It is 4-connected, fills the connected region of whatever value sits at `(x, y)`, and returns the
**indices it changed** — empty when the start is out of bounds or the region already holds `value`.
It grows `cells` to `gridW * gridH` if it is short.

In the editor, tilemaps are authored with the **Tile Palette** panel
(`Projects/Starforge/src/panels/TilePalettePanel.cpp`), and this is the part with no code
equivalent:

1. *Entity ▸ 2D ▸ Tilemap* creates the entity, calls `EnsureCells()` and opens the panel.
2. Set `TilesetPath` in the Inspector (drag a texture from the Content Browser).
3. The panel shows the atlas as a clickable grid; clicking a tile selects the value LMB paints.
4. Tick **Paint in viewport**, turn on **2D mode** on the toolbar, then paint in the viewport — LMB
   applies the tool, RMB always erases.
5. Tools are **Paint** (a drag coalesces into one undo step), **Flood** (one step per click) and
   **Rect** (press-drag-release, one step).

The cell under the cursor is `floor(worldXY - Position.xy)`, computed with
`Camera2DController::ScreenToWorld` (`ViewportController.cpp:291-295`) — the same maths you would
write in a custom tool.

### How cells persist

`Cells` is **not a reflected field**. `SceneSerializer` gives it a hand-written block: a plain JSON
integer array under `"Cells"` inside the `Tilemap` component object (`SceneSerializer.cpp:171-182`,
`:269-276`), which diffs and compresses well in git. On load the array is read back and
`EnsureCells()` clamps the grid and sizes the buffer, so a hand-edited file with a mismatched array
length is corrected rather than rejected. Everything else on the component is ordinary reflected
data.

---

## Light a 2D scene

```cpp
struct Light2DComponent           // scene/Components.h:367
{
    glm::vec3 Color{ 1.0f, 0.85f, 0.6f };   // warm default (campfire)
    float     Radius    = 4.0f;             // world units
    float     Intensity = 1.5f;             // HDR brightness at the centre
    float     Falloff   = 2.0f;             // radial exponent — higher is tighter
    bool      Enabled   = true;
};
```

2D lighting is a **darkening composite**, not a lighting model. The sprites draw fully lit; then
`Scene::OnRender2DLights` accumulates every light additively into a **half-resolution RGBA16F**
buffer that starts cleared to the scene's ambient colour, and **multiplies** that buffer over the
scene. Regions no light reaches fall to the ambient level.

The falloff, from `Light2D.glsl`, is a VBO-free six-vertex quad of side `2 × Radius` centred on the
light, with:

```glsl
float d = length(v_Local);                        // 0 at centre, 1 at the inscribed circle
float f = pow(clamp(1.0 - d, 0.0, 1.0), u_Falloff);
color = vec4(u_Color * (u_Intensity * f), 1.0);   // additive HDR
```

So `Radius` is a hard cut — the contribution is exactly zero at and beyond it — and `Falloff` shapes
the curve inside. The light sits at its entity's **raw `TransformComponent` XY**; Z is ignored, and
the hierarchy is not composed (same rule as sprites).

### Ambient is a scene property

The clear colour is `EnvironmentComponent::Ambient2D`, a `glm::vec3` defaulting to **white**
(`Components.h:457`). `Scene::FindEnvironment()` returns the first `EnvironmentComponent` in the
scene; with none, ambient is white. In the editor it is the *Environment* entity's inspector row,
labelled "2D lighting ambient".

- **White ambient = no darkening**, because the multiply is by 1.
- For a night scene, drop it to something like `{0.12, 0.13, 0.18}` and let lights carve out the
  visible area. ForgeIsle's design doc uses ~0.12 for its tent scene.

### The compat gate — and what it means for you

```cpp
if (lights.empty() && ambient == glm::vec3(1.0f))
    return;                      // Scene.cpp:763-764 — no GL calls at all
```

With no active lights **and** a white ambient the pass is skipped entirely, so a 2D scene that never
opts in is byte-identical to one from before 2D lighting existed. The corollary is the thing to
remember: **darkening the ambient is what arms the pass.** Adding one light to a scene whose ambient
is still white gives you a bright spot on an already-fully-lit scene — visible, but not what you
wanted. Turn the ambient down first.

`Enabled == false` and an inactive entity both remove a light from the gather (`Scene.cpp:753-756`),
so a scene of nothing but disabled lights and white ambient still short-circuits.

### Cost and state

The light buffer is `(targetW + 1) / 2 × (targetH + 1) / 2`, created once and resized when the
viewport changes. Each light is one 6-vertex `glDrawArrays` — cheap, and there is **no cap on the
light count**, unlike the 3D point-light path. The composite is one full-screen triangle with
`BlendMode::Multiply`. On exit, `Light2DRenderer::Composite` restores depth test on, depth write on
and `BlendMode::Alpha`, so the mode never leaks (`Light2DRenderer.cpp:107-111`).

If shader creation fails — headless, or a missing `Light2D.glsl`/`BlitCopy.glsl` — the whole pass
returns without touching the scene. There is no error beyond whatever `Shader::Create` logged.

### Where it sits in the frame

```cpp
desc.DrawTransparent = [scenePtr, vw, vh](const Cosmic::SceneDrawContext& c)
{
    scenePtr->OnRenderSprites (c.ViewProjection, vw, vh);
    scenePtr->OnRender2DLights(c.ViewProjection, vw, vh);   // immediately after, same target
};
```

That is `PlayerLayer.cpp:374-379`, and Starforge wires the identical pair for its viewport
(`StarforgeApp.cpp:1385-1405`) — which is why the editor and a packaged build match. Both run inside
the HDR phase, so the multiply happens **before** tonemapping; canvas UI composites afterwards and
is therefore never darkened by 2D lights. See [`game-ui.md`](game-ui.md).

---

## Drive the 2D camera

There are two different cameras in play, and conflating them is the single most common 2D confusion.

| | `Camera2DController` | `CameraComponent` (Orthographic) |
| --- | --- | --- |
| What it is | an **input-driven rig** owning an `OrthographicCamera` | scene data on an entity |
| Who uses it | the editor viewport in 2D mode; any app that wants pan/zoom navigation | Play mode and packaged builds — `PlayerLayer` renders from the first `Primary` one |
| Serialized | no | yes |
| Zoom control | `SetZoom(halfHeight)`, scroll wheel | `OrthoSize` (also a half-height) |
| Position | `SetFocus(xy)` | the entity's `TransformComponent` |

A shipped game uses `CameraComponent`. `Camera2DController` is what you reach for when *you* are
building a tool, a map view, or an in-game free-look camera.

### The rig

```cpp
Camera2DController cam(16.0f / 9.0f);

// per frame, before OnUpdate:
cam.SetViewportRect(viewportPosPx, viewportSizePx);   // SCREEN pixels; also updates aspect
cam.SetControlEnabled(hoveringViewport || cam.IsDragging());
cam.OnUpdate(ts);                                     // polls the MMB pan drag

// in the layer's event hook:
cam.OnEvent(e);                                       // dispatches MouseScrolled -> zoom
```

Conventions, straight from the header:

- **`Focus`** is the world XY at the view centre. **`Zoom`** is the visible **half-height in world
  units** — smaller is closer in. Clamped to `[0.01, 10000]`.
- The camera sits at `(Focus, 0)` with a **±1000 Z clip range**, so sprites spread across `z`/`ZOrder`
  and modest 3D props in a 2.5D scene all stay visible. Larger world Z is nearer the viewer.
- **MMB drag pans, scroll zooms about the cursor.** Zoom is multiplicative: `1.15^-scroll`.
- `SetViewportRect` takes **screen** pixels (`WorkspaceLayer::GetViewportPos/GetViewportSize` — the
  ImGui coordinate space), not viewport-local ones, because zoom-to-cursor needs to map the raw
  mouse position. `Input::GetMouseScreenPosition()` is the matching input call.
- `SetControlEnabled(false)` ends any in-progress drag. Keep it enabled while `IsDragging()` so a
  drag that leaves the viewport still tracks.
- `OnMouseScrolled` **never consumes the event** (returns `false`), matching the other controllers —
  other observers still see the scroll.

### The pure maths

Three statics, all headless-testable, all usable without owning a controller:

```cpp
static glm::vec2 ScreenToWorld(const glm::vec2& screenPx,
                               const glm::vec2& vpPosPx, const glm::vec2& vpSizePx,
                               const glm::vec2& focus, float zoomHalfHeight);
static glm::vec2 PanBy(const glm::vec2& focus, const glm::vec2& deltaPx,
                       float zoomHalfHeight, float viewportHeightPx);
static glm::vec2 ZoomAboutPoint(const glm::vec2& focus, const glm::vec2& worldAnchor,
                                float zoomBefore, float zoomAfter);
```

`ScreenToWorld` is the one you will actually call — it turns a mouse position into the world point
under the cursor, flipping Y (screen +y is down, world +y is up):

```cpp
const glm::vec2 world = Cosmic::Camera2DController::ScreenToWorld(
    Cosmic::Input::GetMouseScreenPosition(), vpPos, vpSize, cam.GetFocus(), cam.GetZoom());

const glm::ivec2 cell{ (int)std::floor(world.x - mapPos.x),
                       (int)std::floor(world.y - mapPos.y) };   // which tilemap cell
```

That is exactly how the editor's tile brush and 2D sprite picking find their target
(`ViewportController.cpp:291-295`, `:424-427`). It returns `focus` unchanged if the viewport height
is zero, so it never divides by zero.

### Framing and visibility

```cpp
void VisibleRect(glm::vec2& outMin, glm::vec2& outMax) const;   // the world rect on screen
void FrameBounds(const glm::vec2& worldMin, const glm::vec2& worldMax);
```

`FrameBounds` centres on the box and picks the zoom that fits it with **~10 % padding**, respecting
aspect; a degenerate (zero-size) box just recentres without changing zoom. Starforge calls it when
you switch into 2D mode, over the union of every sprite's `WorldSize` box, so the toolbar toggle
frames your art (`StarforgeApp.cpp:1944-1968`) — a nice pattern to copy for a "frame all" hotkey:

```cpp
glm::vec2 mn(-8.0f), mx(8.0f);
bool any = false;
auto view = scene->GetRegistry().view<TransformComponent, SpriteRendererComponent>();
for (auto e : view)
{
    const auto& t = view.get<TransformComponent>(e);
    const auto& s = view.get<SpriteRendererComponent>(e);
    const glm::vec2 half = SpriteRendererComponent::WorldSize(
        s, { t.Scale.x, t.Scale.y },
        s.Resolved ? (int)s.Resolved->GetWidth()  : 0,
        s.Resolved ? (int)s.Resolved->GetHeight() : 0) * 0.5f;
    const glm::vec2 p{ t.Position.x, t.Position.y };
    if (!any) { mn = p - half; mx = p + half; any = true; }
    else      { mn = glm::min(mn, p - half); mx = glm::max(mx, p + half); }
}
cam.FrameBounds(mn, mx);
```

---

## Editor-authored vs code-driven

| Thing | Editor | Code |
| --- | --- | --- |
| Create a sprite / tilemap / 2D light entity | *Entity ▸ 2D ▸ Sprite · Tilemap · Light · Camera (Ortho)*; on a 2D build also the Hierarchy's **+ Create** | `AddComponent<…>()` |
| Every reflected field on those components | Inspector, with undo | plain member assignment |
| **Tilemap `Cells`** | **Tile Palette panel only** — pick a tile, paint/flood/rect in the viewport, undoable | write `Cells` / `FloodFill` yourself |
| `Ambient2D` | Environment entity's Inspector row | `FindEnvironment()->Ambient2D` |
| Sprite selection / picking | click in the viewport (rect test, topmost by sort key) | `SpriteRendererComponent::WorldSize` + your own test |
| Camera framing | 2D-mode toolbar toggle auto-frames all sprites | `Camera2DController::FrameBounds` |
| Flipbook playback | advances **only in Play** | `Scene::UpdateSpriteAnimations(dt)` from your tick |
| `SpriteRendererComponent::Enabled` | **not shown** (`HideInInspector`) — use the entity's Active checkbox | set the field |
| `SpriteAnimationComponent::Elapsed`, all `Resolved*` handles | not shown, not saved | public members, set freely |

Anything not reflected is code-only, and anything reflected is available in both. The one genuinely
editor-only workflow is tile painting.

---

## Common patterns

**Author in code, tune in the editor.** Building a scene programmatically and calling
`SceneSerializer::Save` is how every in-tree sample ships — it gives you a `.cscene` that opens in
Starforge and is then edited by hand. `BuildForgePong` is 190 lines of exactly this.

**Give each visual layer a `ZOrder` band.** Background `-10`, terrain tilemap `0`, characters `10`,
effects `20`, and sort within a band by `z` or `YSort`. Signed values mean you never have to
renumber to insert a layer underneath.

**One atlas per band.** A batch holds 31 distinct textures, and every texture switch beyond that is
a flush. A tile atlas + a character sheet + an effects sheet is three slots for an entire game.

**Set the pixel-art sampling preset at project open**, before any content loads, and pick a
`PixelsPerUnit` that makes your tile size a whole number of world units. Then integer zoom levels
stay crisp.

**Park one-shot effects offscreen rather than destroying them.** Creating and destroying entities
churns the registry and invalidates iteration; ForgePong moves its `HitFx` to `y = 1000` and sets
`Playing = false`.

**Reach for `Renderer2D` directly for debug overlays.** Grids, hit boxes and gizmos do not want to
be entities. Draw them from a layer with an explicit `RenderPass` —
[`rendering-2d.md`](rendering-2d.md#render-more-than-one-camera).

---

## Pitfalls

**"My sprite is invisible and there is no error."** Walk the gates in order: is `Enabled` true; is
the entity (and every ancestor) `Active`; is the camera `Primary` and orthographic; is the camera's
Z far enough from the sprite that `Near = 0.1` does not clip it; and is anything ticking the scene at
all? A missing `Primary` camera logs `"PlayerLayer: no Primary CameraComponent"` **once** and falls
back to a 3/4 perspective view, which looks like "my 2D game rendered in 3D".

**"My sprite is enormous / a speck."** Textured sprites are sized by the image and `PixelsPerUnit`,
**not** by `Transform.Scale` alone — `Scale` multiplies that result. Untextured sprites are the
opposite: `Scale` *is* the size. Adding a `TexturePath` to a flat-colour sprite silently changes
which rule applies.

**"Parenting a sprite to another entity does nothing."** Correct — the 2D passes read the raw
transform and ignore the hierarchy. Move the child in a script.

**"The tilemap draws upside down."** `Cells` row 0 is the map's **bottom** row, while the atlas's
row 0 is the image's **top** row. The two conventions are deliberate and opposite.

**"Half my tilemap disappeared when I made the grid wider."** `EnsureCells` resizes `Cells`
linearly; it does not re-flow rows. Changing `GridW` on a painted map shifts everything. Rebuild the
map, or re-index it yourself before changing the width.

**"Grid size 4000 became 1024."** `kMaxGrid` clamps silently in `EnsureCells`, with no log line.

**"I added a 2D light and nothing got darker."** `Ambient2D` is white by default, and white ambient
with lights means "fully lit, plus a bright spot". Lower the ambient on the Environment entity.

**"The animation is frozen in the editor."** Flipbooks advance only in Play. In a custom host,
nothing advances them until you call `Scene::UpdateSpriteAnimations(dt)` — and `Scene::OnUpdate` is
not a substitute; it has no callers anywhere.

**"My one-shot animation restarts every time I set `Playing = true`."** It does not — `Playing`
gates the `Elapsed` advance only. Set `Elapsed = 0.0f` to restart a clip.

**"Editing `SourceRect` on an animated sprite has no effect."** `UpdateSpriteAnimations` overwrites
it every tick from `Elapsed`.

**"My 2D scene is bloomed / washed out."** Sprites composite in the HDR phase, before tonemapping,
so `EnvironmentComponent`'s exposure and post chain apply to them. A `Color` or light `Intensity`
above 1.0 blooms rather than clipping.

**"Ten thousand sprites tank the frame rate even though most are offscreen."** Sprites are never
frustum-culled — only the tilemap cell walk is. Cull in your own code, or move the content into a
tilemap.

---

## See also

- [`rendering-2d.md`](rendering-2d.md) — the `Renderer2D` immediate-mode API everything here calls,
  including batching, limits and the fixed inter-type draw order
- [`game-ui.md`](game-ui.md) — screen-space canvas UI, which composites **after** the tonemap and is
  a different system from both of these
- [`entities-and-components.md`](entities-and-components.md) — the full component catalogue, the
  `Active`/`Enabled` gates, hierarchy semantics
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — `.cscene` format, prefabs, undo
- [`scripting.md`](scripting.md) — `ScriptableEntity`, `Signals()`, and the `PongBall`/
  `PaddleController` scripts quoted above
- [`materials-and-shaders.md`](materials-and-shaders.md) — the `ActiveMaterial` path
- [`../reference/ecs.md`](../reference/ecs.md) — component field tables *(skeleton, D13)*
- [`../reference/rendering-2d.md`](../reference/rendering-2d.md) — per-call signatures *(skeleton, D9)*
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what each engine
  configuration ships
