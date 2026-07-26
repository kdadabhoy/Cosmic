# In-Game UI — Guide

**What this covers:** the **entity-based** UI system — `CanvasComponent`, `RectTransformComponent`
and the anchor/pivot model, `UiImageComponent` / `UiTextComponent` / `UiButtonComponent`, the
hit-test and the button state machine, canvas scaling, `UiWorldAnchorComponent` for nameplates and
interaction prompts, and feeding a live render target into an image through
`UiImageComponent::RuntimeTexture` + `SceneRenderer::RenderToTexture`.
**Source of truth:** `Cosmic/src/scene/ui/UiComponents.h`, `scene/ui/UiSystem.{h,cpp}`,
`scene/EventBus.h`, `renderer/SceneRenderer.{h,cpp}` (`RenderToTexture`),
`reflect/TypeRegistry.cpp`, `layers/PlayerLayer.cpp`
**API Reference:** none. `scene/ui/UiComponents.h` and `scene/ui/UiSystem.h` have **no row in the
reference manifest** (the same gap D50 found for `SceneSerializer`, `EventBus`, `FlowMachine` and
the whole `scripting/` tier), so **this chapter is the client-facing source for both headers.**
**How it works:** [../systems/rendering-2d.md](../systems/rendering-2d.md) *(skeleton — D28)*
**Configuration:** **both.** `scene/ui/` is shared source, unfenced in the 3D and 2D engine builds
([`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)). This is UI for a **3D** game
every bit as much as a 2D one.

> ### This is not the editor's UI system
>
> Cosmic has **two** unrelated UI stacks and they are easy to confuse:
>
> | | **In-game UI** — this chapter | **Editor chrome** — [`editor-ui-and-theming.md`](editor-ui-and-theming.md) |
> | --- | --- | --- |
> | Built from | scene **entities** + components | **ImGui** immediate-mode calls |
> | Lives in | `scene/ui/` | `ui/` (`ImGuiLayer`, `ThemeManager`, `Fonts`, `Overlay`, `Widgets`) |
> | Authored by | the Inspector, the serializer, prefabs, undo, scripts | C++ calls in `OnImGuiRender` |
> | Drawn through | `Renderer2D`, into the game's render target | ImGui's own draw lists, over the window |
> | Survives in a packaged build | **yes** — it *is* the game's HUD and menus | yes, but it looks like a tool |
> | Themed by `ThemeManager` | **no** | yes |
>
> Rule of thumb: **if a player sees it, it belongs here; if a developer sees it, it belongs in
> ImGui.** A pause menu, a health bar, a dialogue box → entities. A profiler window, a debug slider,
> an asset browser → ImGui.
>
> The ImGui side has its own chapter: [`editor-ui-and-theming.md`](editor-ui-and-theming.md).

Everything below is engine-generic: a scene with **no `CanvasComponent` does nothing at all** —
`UiSystem::Render` returns before touching GL. Adding UI to an existing project cannot change how it
already renders.

---

## Quick start

A title screen: a canvas, a label, and a button that emits a signal.

```cpp
#include "Cosmic.h"
#include "scene/ui/UiComponents.h"

void BuildMenu(Cosmic::Ref<Cosmic::Scene>& scene)
{
    using namespace Cosmic;

    Entity canvas = scene->CreateEntity("Canvas");
    canvas.AddComponent<CanvasComponent>();          // defaults: ScaleWithHeight, ref 1080 px

    // A centred title.
    {
        Entity e = scene->CreateEntity("Title");
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = rt.AnchorMax = { 0.5f, 0.26f };   // a POINT anchor
        rt.OffsetMin = { -420.0f, -50.0f };              // box around that point, in px
        rt.OffsetMax = {  420.0f,  50.0f };
        auto& txt = e.AddComponent<UiTextComponent>();
        txt.Text   = "FORGEPONG";
        txt.SizePx = 84.0f;
        txt.Color  = { 0.95f, 0.98f, 1.0f, 1.0f };
        scene->SetParent(e, canvas, /*keepWorldPose=*/false);
    }

    // A button: an image + a button + a label, all on one entity.
    {
        Entity e = scene->CreateEntity("PlayButton");
        auto& rt = e.AddComponent<RectTransformComponent>();
        rt.AnchorMin = rt.AnchorMax = { 0.5f, 0.56f };
        rt.OffsetMin = { -130.0f, -28.0f };
        rt.OffsetMax = {  130.0f,  28.0f };
        e.AddComponent<UiImageComponent>().Tint  = { 0.16f, 0.19f, 0.25f, 0.92f };
        e.AddComponent<UiButtonComponent>().Signal = "play_clicked";
        auto& txt = e.AddComponent<UiTextComponent>();
        txt.Text   = "Play";
        txt.SizePx = 30.0f;
        scene->SetParent(e, canvas, /*keepWorldPose=*/false);
    }
}
```

That is ForgePong's menu, condensed from `StarforgeApp.cpp:2791-2827` and `:3078-3085`. Catch the
click anywhere that can reach the scene's `EventBus`:

```cpp
// In a ScriptableEntity:
void OnSignal(const std::string& signal, Cosmic::Entity source) override
{
    if (signal == "play_clicked")
        Signals().Emit("start_match");
}
```

…or let a `.cflow` screen graph route it straight to another scene, which is what ForgePong does and
what makes it a **zero-code** menu.

Five things this is quietly asserting:

- **`Cosmic.h` does not pull in the UI headers.** `scene/ui/UiComponents.h` and
  `scene/ui/UiSystem.h` are **not** in the umbrella header (unlike `camera/Camera2DController.h`,
  which is), so every file touching UI components must include `UiComponents.h` explicitly — as the
  stock `PongBall.h` script does. The stock `StoryUiBinding.h` script does *not*, and gets away with
  it only because nothing currently compiles it; add it to your module after `PongBall.h` or add the
  include.
- **UI entities are ordinary entities.** They serialize, undo, prefab, and are scriptable. That is
  the whole point of the design.
- **`RectTransformComponent` replaces `TransformComponent` under a canvas.** The sibling
  `TransformComponent` that `CreateEntity` always adds is **ignored**.
- **Parenting is the layout tree.** `SetParent` with `keepWorldPose = false` is how you attach an
  element; the E3 hierarchy is what `UiSystem` walks.
- **Nothing draws until a host calls `UiSystem::Render`.** `PlayerLayer` and Starforge do it for you
  — see [Wire it up in your own host](#wire-it-up-in-your-own-host).

---

## Canvas space

One coordinate system, used by every rect, every pointer position and every projection:

> **Pixels. Origin TOP-LEFT. +x right, +y DOWN.**

That is the screen convention, not the world convention — so anchor `y = 1.0` is the **bottom** edge
of the canvas, and a `ScreenOffset` of `{0, -40}` moves an element *up*. The render tests pin this
deliberately (`tests/render/render_2d.cpp:461-467`).

The canvas root's rect is the viewport (or the letterbox band — see
[the letterboxed variant](#letterboxing-a-game-view)). Every descendant's rect is a **pure function**
of its parent's rect plus its own anchors and offsets. `UiRect` itself is a tiny header-only value:

```cpp
struct UiRect
{
    glm::vec2 Min;   // top-left
    glm::vec2 Max;   // bottom-right
    glm::vec2 Size() const;  glm::vec2 Center() const;
    float Width() const;     float Height() const;
    bool  Contains(const glm::vec2& p) const;
};
```

---

## Anchor an element

```cpp
struct RectTransformComponent      // scene/ui/UiComponents.h:94
{
    glm::vec2 AnchorMin{ 0.0f, 0.0f };      // fraction of the parent rect
    glm::vec2 AnchorMax{ 0.0f, 0.0f };
    glm::vec2 OffsetMin{ 0.0f, 0.0f };      // pixels from the min anchor point
    glm::vec2 OffsetMax{ 100.0f, 40.0f };   // pixels from the max anchor point
    glm::vec2 Pivot{ 0.5f, 0.5f };
    int32_t   ZOrder = 0;
};
```

The resolution is four lines, and `UiSystem::ResolveRect` is a public static so you can compute it
yourself (`UiSystem.cpp:25-35`):

```
anchorMinPt = parent.Min + parent.Size() * AnchorMin
anchorMaxPt = parent.Min + parent.Size() * AnchorMax
rect.Min    = anchorMinPt + OffsetMin * scale
rect.Max    = anchorMaxPt + OffsetMax * scale
```

Two idioms come out of that, and they cover nearly everything:

**Point anchor (`AnchorMin == AnchorMax`) — a fixed-size box.** The two anchor points collapse to
one, so the offsets become a box *around* it and the element keeps its pixel size at any viewport
size. Use for buttons, badges, HUD readouts.

```cpp
rt.AnchorMin = rt.AnchorMax = { 0.5f, 0.5f };    // dead centre
rt.OffsetMin = { -130.0f, -28.0f };              // 260 x 56 px, centred
rt.OffsetMax = {  130.0f,  28.0f };
```

**Stretched anchor (`AnchorMin != AnchorMax`) — insets from the parent's edges.** The offsets become
margins and the element grows with its parent. Use for panels, backgrounds, bars.

```cpp
rt.AnchorMin = { 0.0f, 0.0f };  rt.AnchorMax = { 1.0f, 1.0f };
rt.OffsetMin = {  16.0f,  16.0f };               // 16 px in from top-left
rt.OffsetMax = { -16.0f, -16.0f };               // 16 px in from bottom-right (NEGATIVE)
```

Mixing them per axis is legal and useful — a bottom status strip stretches in X and is pinned in Y:

```cpp
rt.AnchorMin = { 0.0f, 1.0f };  rt.AnchorMax = { 1.0f, 1.0f };   // bottom edge, full width
rt.OffsetMin = { 0.0f, -48.0f };  rt.OffsetMax = { 0.0f, 0.0f }; // 48 px tall
```

### What `Pivot` does (nothing, yet)

`Pivot` is the rotation/scale reference point in `0..1` of the rect, and **it does not affect the
resolved rect in v1** — there is no rotation in this system, so nothing reads it during layout.
`UiSystem::PivotPoint(rect, pivot)` computes the point for you if your own code wants it. Changing
`Pivot` will never move an element; change the offsets.

### Elements without a `RectTransform`

An element that carries UI content but no `RectTransformComponent` **inherits its parent's rect
exactly** (`UiSystem.cpp:135-138`). Occasionally handy for a full-bleed background; usually a
mistake. The editor's *Entity ▸ UI ▸ Image/Text/Button* always adds one.

The canvas root itself is also drawable: put a `UiImageComponent` on the entity carrying
`CanvasComponent` and you get a quad covering the whole canvas rect.

---

## Order the layers

`UiSystem::CollectElements` produces one **flat, back-to-front list** for the whole scene, sorted by:

1. `CanvasComponent::SortOrder` (ascending — lower draws first, further back)
2. `RectTransformComponent::ZOrder` (ascending)
3. DFS sequence — the order elements were visited, a stable tie-break

> **`ZOrder` is flat within a canvas — it is *not* nested.** A child does not inherit or sit above
> its parent's `ZOrder`. A panel with `ZOrder = 3` whose children are all `ZOrder = 0` draws **over
> its own children**. You must raise `ZOrder` with depth by hand.

The render golden does exactly that, and it is the pattern to copy: panel `0`, title bar `1`,
buttons `2`, badge `3`, the dot inside the badge `4` (`tests/render/render_2d.cpp:437-458`).

Multiple canvases are the coarse control: a HUD canvas at `SortOrder = 5` always draws over a menu
canvas at `0`, whatever their children's `ZOrder`s are. An **inactive** canvas — or one whose
ancestor is inactive — is skipped entirely (`UiSystem.cpp:180-181`).

---

## Scale a layout to any window

```cpp
struct CanvasComponent             // scene/ui/UiComponents.h:68
{
    UiScaleMode ScaleMode       = UiScaleMode::ScaleWithHeight;
    float       ReferenceHeight = 1080.0f;
    int32_t     SortOrder       = 0;
};
```

| Mode | Scale factor | Use when |
| --- | --- | --- |
| `ConstantPixel` | `1.0` | crisp fixed-size chrome; pixel art that must not resample |
| `ScaleWithHeight` *(default)* | `viewportHeight / ReferenceHeight` | a layout authored at one height that should fill any window |

The factor multiplies **`OffsetMin`/`OffsetMax` and text `SizePx` only** — anchors are fractions and
already scale, and `ReferenceHeight` is clamped to at least `1.0`. So a layout authored against
1080 px looks proportionally identical at 720 p and 1440 p, and `UiSystem::CanvasScale(canvas,
viewport)` returns the factor if you need it.

Nothing scales with *width*: a much wider window gives more horizontal room rather than bigger
elements. That is usually what you want; anchor to edges rather than assuming a width.

---

## Draw an image

```cpp
struct UiImageComponent            // scene/ui/UiComponents.h:113
{
    std::string    TexturePath;                      // AssetPath("texture"); empty => solid Tint
    glm::vec4      Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4      NineSlice{ 0.0f };                // l, t, r, b border in TEXELS
    bool           PreserveAspect = false;

    Ref<Texture2D> Resolved;         // runtime-only
    std::string    ResolvedPath;     // runtime-only
    Ref<Texture2D> RuntimeTexture;   // runtime-only — wins over Resolved
};
```

- **Empty `TexturePath` ⇒ a flat `Tint` quad.** This is the workhorse: panels, dim overlays and
  button backgrounds in every in-tree sample are untextured tinted rects.
- **`Tint` multiplies** the texture sample, and is multiplied *again* by the button state tint when
  the entity also has a `UiButtonComponent`.
- Resolution is lazy, keyed on the path, through `AssetLibrary::GetTexture` — so VFS prefixes work
  and the texture is shared with the rest of the project.

### Nine-slice

`NineSlice` is `{ left, top, right, bottom }` **in texture pixels**. All-zero (the default) means a
plain stretched quad. Non-zero splits the image into a 3 × 3 grid: corners keep their size (scaled
by the canvas factor), edges stretch along one axis, the centre stretches both ways — the standard
resizable-panel trick, and it costs **nine quads** instead of one.

```cpp
auto& img = e.AddComponent<UiImageComponent>();
img.TexturePath = "project://ui/panel.png";     // e.g. 48 x 48 with 12 px rounded corners
img.NineSlice   = { 12.0f, 12.0f, 12.0f, 12.0f };
```

If the borders would overlap in a small rect they are scaled down proportionally so the result never
inverts (`UiSystem.cpp:325-326`). Degenerate cells are skipped. A texture reporting 0 × 0 falls
through to the plain path.

### Preserve aspect

`PreserveAspect = true` shrinks the drawn quad **inside** the resolved rect to the texture's aspect
ratio — letterboxing vertically or pillarboxing horizontally. The rect itself is unchanged, so
layout and hit-testing still use the full box; only the pixels shrink. It is ignored when nine-slice
is active (nine-slice is checked first) and when the texture has no size.

---

## Draw text

```cpp
struct UiTextComponent             // scene/ui/UiComponents.h:144
{
    std::string Text = "Text";
    std::string FontPath;                     // font stem or VFS path; empty => default face
    float       SizePx = 32.0f;               // cap height in canvas px, BEFORE canvas scaling
    glm::vec4   Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    UiHAlign    HAlign = UiHAlign::Center;    // Left | Center | Right
    UiVAlign    VAlign = UiVAlign::Middle;    // Top  | Middle | Bottom
    bool        Wrap   = false;
};
```

Text renders through `Renderer2D::DrawString` against an **SDF font atlas**, so it stays crisp at
any canvas scale. Layout inside the element's rect:

- The block is split on `\n` (a trailing `\r` is stripped), measured in em units from the font's
  glyph advances, then aligned horizontally per line and vertically as a block.
- Block height is `lineCount × font.LineHeight() × SizePx × canvasScale`.
- Text is drawn with a **negative Y scale** to flip the font's y-up baseline space into y-down canvas
  space — the idiom is quoted in [`rendering-2d.md`](rendering-2d.md#draw-world-space-text).

> **`Wrap` is not implemented.** The field exists, is reflected, and is read by nothing — word wrap
> is a v2 follow-up, as `TypeRegistry.cpp:230`'s own tooltip admits. **Explicit `\n` works; long
> lines overflow the rect silently.** Insert your own line breaks.

`FontPath` is a font **stem** (`"Roboto-Bold"`) or a VFS path; empty uses `Font::Default()`, and a
failed `Font::Get` also falls back to the default rather than drawing nothing. Text is batched by
font atlas, so a screen using two faces costs an extra draw call per switch.

Text is written like any other field, which is how ForgePong keeps score (`PongBall.h:92-102`):

```cpp
auto& reg = GetScene().GetRegistry();
for (auto e : reg.view<TagComponent, UiTextComponent>())
{
    const std::string& tag = reg.get<TagComponent>(e).Tag;
    if (tag == "ScoreL") reg.get<UiTextComponent>(e).Text = std::to_string(m_ScoreL);
    if (tag == "ScoreR") reg.get<UiTextComponent>(e).Text = std::to_string(m_ScoreR);
}
```

---

## Make it clickable

```cpp
struct UiButtonComponent           // scene/ui/UiComponents.h:193
{
    std::string Signal = "clicked";                  // emitted on release-inside
    glm::vec4   NormalTint  { 1.0f,  1.0f,  1.0f,  1.0f };
    glm::vec4   HoverTint   { 1.15f, 1.15f, 1.15f, 1.0f };
    glm::vec4   PressedTint { 0.8f,  0.8f,  0.8f,  1.0f };
    glm::vec4   DisabledTint{ 0.5f,  0.5f,  0.5f,  0.6f };
    bool        Interactable = true;

    UiButtonState State = UiButtonState::Normal;     // runtime-only
    bool          Armed = false;                     // runtime-only
};
```

**A button is an image plus this component.** The state tint is multiplied into a sibling
`UiImageComponent`'s `Tint`; a button with no image has working logic and no visible feedback.

### The state machine

`UiSystem::StepButtonState` is pure and unit-tested (`UiSystem.cpp:62-90`):

| Input | Result |
| --- | --- |
| `!Interactable` | `Disabled`, `Armed = false`, never emits — checked first, before anything else |
| `PressedEdge && hovered` | `Armed = true` |
| `ReleasedEdge` | emits **iff** `Armed && hovered`; then `Armed = false` either way |
| `!Down` | `Armed = false` (covers a release that happened off-window) |
| `Armed && hovered && Down` | `Pressed` |
| `hovered` | `Hover` |
| otherwise | `Normal` |

The semantics that matter: **a click is a press *and* a release on the same button.** Press on a
button, drag off, drag back, release → it fires. Press on a button, drag off, release → it does not.
The previous `State` is ignored entirely (the parameter exists for symmetry) — `Armed` carries all
the memory.

### The hit test

`UiSystem::Update` finds the **topmost interactable button** under the pointer by walking the
back-to-front element list in reverse, then steps *every* button in the scene with `hovered = (e ==
topHit)`. So buttons never both light up, and a button that scrolled out from under the cursor
correctly falls back to `Normal`.

Two consequences worth internalising:

- **Only buttons block buttons.** The topmost search skips anything without an interactable
  `UiButtonComponent`, so an image or a text label drawn *over* a button does **not** stop the click
  reaching it. If you need a modal shield, put a transparent, non-interactable... *button* on top —
  an image will not do it.
- **`Interactable = false` removes it from hit-testing entirely**, so the button underneath becomes
  clickable.

### The signal

On a release-inside, `Update` calls `scene.Events().Emit(btn->Signal, Entity(e, &scene))` — the
scene's `EventBus`, which is the one channel that reaches everything (`EventBus.h:8-17`):

| Listener | How |
| --- | --- |
| a script on any entity | override `OnSignal(signal, source)`, or `Signals().Connect("name", fn)` |
| a `.cflow` screen graph | a transition whose trigger is the signal name |
| your own C++ | `scene->Events().Connect("play_clicked", [](Entity src){ … })` |

Signals are plain strings, deliberately — they are authored in the Inspector and stored in the scene
and flow files. Emission is **synchronous**: named handlers fire first in subscription order, then
any-handlers, all on the main thread inside `UiSystem::Update`. It is safe to connect or disconnect
from inside a handler.

`PlayerLayer` runs `UpdateUI` **before** the flow's `OnUpdate`, so a button pressed this frame is
routed by the flow in the same frame (`PlayerLayer.cpp:229-241`).

### The editor's viewport is live in Play

Starforge runs the same `UiSystem::Update` in Play mode, so hover and press tints work and buttons
emit for real. While **editing**, a viewport click on a UI element **selects** it instead — the
3D ID-buffer picker renders meshes and cannot see rect-based UI, so `UiSystem::HitTest` fills the
gap (`ViewportController.cpp:377-418`). That is what `HitTest` exists for; it returns *any* drawable
element, not just buttons.

---

## Pin UI to a world position

```cpp
struct UiWorldAnchorComponent      // scene/ui/UiComponents.h:173
{
    uint64_t  TargetEntity = 0;       // UUID of the tracked entity; 0 = use WorldOffset absolutely
    glm::vec3 WorldOffset{ 0.0f };    // added to the target's world position
    glm::vec2 ScreenOffset{ 0.0f };   // canvas-pixel nudge AFTER projection
    bool      HideWhenOffscreen = true;
};
```

Add this to a UI element and it stops laying out against its parent. Instead, before layout,
`UiSystem` projects `targetWorldPosition + WorldOffset` through the active camera into canvas space,
adds `ScreenOffset`, and uses that point as a **zero-size parent rect** — so the element's normal
`OffsetMin`/`OffsetMax` size the box around it (`UiSystem.cpp:109-128`).

Nameplates, health bars over enemies, "Press E" prompts. It works for **both** 2D orthographic and
3D perspective cameras, because it only needs a view-projection matrix.

```cpp
Entity plate = scene->CreateEntity("Nameplate");
auto& rt = plate.AddComponent<RectTransformComponent>();
rt.OffsetMin = { -80.0f, -24.0f };     // 160 x 24 px box centred on the projected point
rt.OffsetMax = {  80.0f,   0.0f };
plate.AddComponent<UiTextComponent>().Text = "Hermit";

auto& anchor = plate.AddComponent<UiWorldAnchorComponent>();
anchor.TargetEntity = npc.GetComponent<IDComponent>().ID.Value();   // the UUID, not the handle
anchor.WorldOffset  = { 0.0f, 2.1f, 0.0f };   // above the head, in WORLD units
anchor.ScreenOffset = { 0.0f, -8.0f };        // 8 px higher, in CANVAS px (+y is DOWN)
scene->SetParent(plate, canvas, false);
```

`TargetEntity` is a **UUID**, not an `entt` handle — reflection deduces `uint64_t` as
`FieldKind::EntityRef`, so the Inspector shows an entity picker. Resolution is
`Scene::FindByUUID`; an unknown UUID silently leaves the world point at `WorldOffset` alone.
Unlike parenting, this **does** compose the hierarchy: the target's world transform comes from
`Scene::GetWorldTransform`.

Visibility rules, both of which hide **the element and its entire subtree**:

- **Behind the camera** (`clip.w <= 1e-6`) → hidden, always.
- **Outside the canvas rect** and `HideWhenOffscreen` → hidden. With `HideWhenOffscreen = false` an
  off-screen anchor keeps drawing at its clamped-off projected position, which is what you want for
  an edge-of-screen objective marker only if you also clamp it yourself.

The projector is public and pure, so you can use it for your own overlays:

```cpp
static bool ProjectToCanvas(const glm::vec3& worldPos, const glm::mat4& viewProj,
                            const UiRect& canvasRect, glm::vec2& outPoint);
```

> **Two limitations to plan around.**
>
> **1. The anchor only applies when the host passes a camera view-projection.** It is an *optional*
> trailing parameter on `CollectElements`/`Update`/`HitTest`/`Render`, defaulting to `nullptr`, and
> **both shipped hosts pass it to `Render` but not to `Update`/`HitTest`** (`PlayerLayer.cpp:291` and
> `:369`; `ViewportController.cpp:399`, `:409` and `StarforgeApp.cpp:1418`). So a world-anchored
> element **draws at its projected position but hit-tests at its un-projected, parent-relative one.**
> Keep world-anchored UI non-interactive — nameplates, bars, prompts — or drive `UiSystem::Update`
> yourself with the camera VP.
>
> **2. `EntityRef` fields are not remapped by `InstantiatePrefab`** (D50). A nameplate prefab's
> `TargetEntity` is loaded verbatim, so every instance points at whatever the prefab was authored
> against. Set `TargetEntity` in code after spawning.

---

## Show a live render target in an image

`UiImageComponent::RuntimeTexture` is a runtime-only `Ref<Texture2D>` that, when set, **wins over
the path-loaded image** (`UiSystem.cpp:478-480`). It is not serialized and not in the Inspector —
a script or the app sets it. The intended source is the offscreen render verb:

```cpp
void SceneRenderer::RenderToTexture(const SceneRenderDesc& desc, const Ref<FrameBuffer>& target);
```

`RenderToTexture` binds `target`, resizes **this renderer's** post stack to it, runs the normal
`Render()` — so sky, shadows, tonemap and the whole post chain apply — and then **re-binds whatever
framebuffer was bound on entry**, leaving the main viewport untouched. An uninitialized renderer, a
null target, or a zero-sized target is a safe no-op (`SceneRenderer.cpp:381-403`).

Because it resizes the post stack, **use a dedicated `SceneRenderer` instance sized to the target**
rather than the one drawing your main view — otherwise you thrash the main post stack every frame.

### The gap you have to bridge yourself

**`RenderToTexture` writes into a `Ref<FrameBuffer>`; `RuntimeTexture` wants a `Ref<Texture2D>`. The
engine ships no call that converts one to the other, and nothing in the tree sets `RuntimeTexture`.**
`FrameBuffer` exposes only a raw handle (`GetColorAttachmentRendererID`) and `Texture2D`'s factories
all allocate their own storage. So the minimap is a documented *pattern*, not a shipped feature, and
you supply the adapter. Two options, both public API only:

**A. Wrap the attachment handle** — zero-copy, and what `Renderer2D` actually needs (it calls
`GetRendererID()` for batch slot de-duplication and `Bind(slot)` at flush):

```cpp
// Client-side adapter: a Texture2D view over a FrameBuffer colour attachment.
class FboTexture : public Cosmic::Texture2D
{
public:
    explicit FboTexture(const Cosmic::Ref<Cosmic::FrameBuffer>& fbo) : m_Fbo(fbo) {}

    uint32_t GetWidth()      const override { return m_Fbo->GetWidth(); }
    uint32_t GetHeight()     const override { return m_Fbo->GetHeight(); }
    uint64_t GetGpuBytes()   const override { return 0; }              // accounting only
    uint32_t GetRendererID() const override { return m_Fbo->GetColorAttachmentRendererID(0); }

    void Bind(uint32_t slot = 0) const override
    { Cosmic::RenderCommand::BindTextureSlot(slot, GetRendererID()); }

    void SetData(void*, uint32_t) override {}                          // not writable
    void SetSampling(Cosmic::TextureFilter, Cosmic::TextureWrap) override {}
    bool operator==(const Cosmic::Texture& o) const override
    { return GetRendererID() == o.GetRendererID(); }

private:
    Cosmic::Ref<Cosmic::FrameBuffer> m_Fbo;
};
```

The handle is re-read every call, so it survives a `FrameBuffer::Resize` (which reallocates the
attachment).

> **Clear `RuntimeTexture` in `OnDestroy`.** The adapter is a *client-defined* type: its vtable and
> destructor live in your game DLL, while the `Ref` is held by a component the engine owns. If a
> script hot reload unloads the DLL with the slot still set, the eventual release calls into
> unmapped code. Same class of hazard as the `Layer*` ownership rule in
> [`project-anatomy.md`](project-anatomy.md#ref-scope-and-the-shared-allocator-rule) — and the same
> reason GPU handles are released in `OnDetach`. Null the slot when your owner goes away.

**B. Read back and re-upload** — `FrameBuffer::ReadPixels` into `Texture2D::SetData`. Simple, but it
is a full GPU→CPU→GPU round trip per update, and `ReadPixels` returns a **top-left-origin** buffer
while the quad samples bottom-left, so the image arrives vertically flipped unless you flip it.
Fine for a one-off thumbnail, wrong for a per-frame minimap.

### The minimap shape

Whichever adapter you use, the loop is the same, and the logic stays app-side by design — the engine
ships the generic verb and the slot, not fog-of-war:

```cpp
// Once, at attach:
FramebufferSpecification spec;
spec.Width = spec.Height = 256;
m_MiniFbo = FrameBuffer::Create(spec);
m_MiniRenderer.Init(256, 256);
m_MiniTex = CreateRef<FboTexture>(m_MiniFbo);

// Each frame (or every other frame — 256^2 is cheap but not free):
SceneRenderDesc desc;
m_Scene->BuildRenderDesc(m_TopDownCamera, dt, desc);
desc.Settings.Shadows = false;              // a minimap does not need them
m_MiniRenderer.RenderToTexture(desc, m_MiniFbo);

if (Entity e = m_Scene->FindByUUID(m_MinimapImageId))
    e.GetComponent<UiImageComponent>().RuntimeTexture = m_MiniTex;
```

The same shape covers a security-camera feed, a portal, and an RTT asset thumbnail — the difference
is only which camera builds the desc.

---

## Wire it up in your own host

Three engine calls, in this order, per frame. `PlayerLayer` is the reference implementation.

```cpp
// 1) Interaction — canvas-space pointer, edges derived by the caller.
const UiRect viewport{ { 0.0f, 0.0f }, { (float)fb->GetWidth(), (float)fb->GetHeight() } };

const glm::vec2 mouse = Input::GetMousePosition();          // window-CLIENT pixels
const bool down = Input::IsMouseButtonPressed(CS_MOUSE_BUTTON_LEFT);

UiPointer p;
p.Position     = mouse;
p.Down         = down;
p.PressedEdge  = down && !m_PrevMouseDown;
p.ReleasedEdge = !down && m_PrevMouseDown;
m_PrevMouseDown = down;

const bool overUi = UiSystem::Update(*scene, viewport, p);  // true = pointer over a live button

// 2) Draw — from the SceneRenderer overlay hook, after post, with the LDR target bound.
const glm::mat4 camVP = desc.Projection * desc.View;
desc.DrawOverlay2D = [scene, vw, vh, camVP]()
{
    UiSystem::Render(*scene, UiRect{ { 0.0f, 0.0f }, { (float)vw, (float)vh } }, &camVP);
};
```

Contract notes, all load-bearing:

- **The pointer must be in canvas space** — screen mouse minus the viewport's top-left. In a
  full-window player that is just `Input::GetMousePosition()`; in a docked editor viewport it is
  `Input::GetMouseScreenPosition() - viewportPos`.
- **You derive the edges.** `UiPointer` has no history; `PressedEdge`/`ReleasedEdge` are this
  frame's transitions and you track the previous state. Call `Update` **once** per frame — calling it
  twice with the same edge flags fires the button twice.
- **Park the pointer when the cursor leaves the viewport** (Starforge uses `{-1e6, -1e6}`) so hover
  clears and an armed press cancels on release.
- **Use `Update`'s return value to suppress world picking** so a click on a button never also selects
  or shoots something behind it.
- **`Render` requires the destination FBO bound and its GL viewport set to the full target.** It
  pushes its own `Renderer2D` pass with a screen-space ortho projection, disables depth test and
  write, forces straight alpha, and restores depth on / write on / alpha on exit
  (`UiSystem.cpp:450-494`). No canvases ⇒ it returns before any of that.
- **Draw UI in `DrawOverlay2D`, not `DrawTransparent`.** The overlay hook runs after the composite
  with the **LDR** target bound, so UI colours are literal — never exposed, tonemapped or bloomed.
  Sprites and 2D lights, by contrast, run in the HDR phase; see
  [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md).

### Letterboxing a game view

```cpp
static void Render(Scene& scene, const UiRect& canvasRect,
                   uint32_t targetW, uint32_t targetH,
                   const glm::mat4* cameraViewProj = nullptr);
```

The four-argument form lays the canvases out inside `canvasRect` — an aspect-locked band inside a
larger target — while projecting over the **full** `targetW × targetH`, so elements land at their
absolute positions inside the band and authored anchors stay truthful. `canvasRect` equal to the
whole target behaves exactly like the two-argument form, which is itself just a forward
(`UiSystem.cpp:424-431`). Starforge uses it for its aspect-locked game view
(`StarforgeApp.cpp:1412-1419`).

---

## Common patterns

**One canvas per screen, one HUD canvas over them all.** Menu, pause and dialogue canvases at
`SortOrder = 0`, HUD at `5`. Toggle whole screens with the entity's `Active` flag — an inactive
canvas is skipped, children and all.

**Raise `ZOrder` with depth, in steps of one, as you build.** It is the only way nested elements
layer correctly, and getting it wrong looks like "my button vanished behind its own panel".

**Point-anchor anything with a fixed size; stretch-anchor anything that should breathe.** Almost
every layout is one of the two, and mixing per axis covers bars and strips.

**Name UI entities you intend to drive, and look them up by tag.** `PongBall` finds `"ScoreL"` /
`"ScoreR"` / `"HitFx"` by `TagComponent`. For anything long-lived, store the UUID instead — tags are
not unique and the lookup is a linear scan.

**Let a `.cflow` route button signals when the button just changes screens.** ForgePong's entire
menu → game → win navigation is data; no script observes those buttons at all.

**Build a menu in code, save it, then tune it in the Inspector.** The `MakeUiLabel`/`MakeUiButton`
helpers in `StarforgeApp.cpp:2791-2827` are ~15 lines each and worth copying into your project.

**Keep world-anchored UI non-interactive** until the hit-test gap above is closed.

---

## Pitfalls

**"Nothing appears."** In order: is there a `CanvasComponent` anywhere; is the element a descendant
of it via `SetParent`; is it and every ancestor `Active`; does it carry a `UiImage`, `UiText` or
`UiButton` (an element with none is walked for its children but never drawn); and is a host calling
`UiSystem::Render`?

**"My element ignores its `TransformComponent`."** By design. Under a canvas, `RectTransformComponent`
is authoritative and the sibling `Transform` is dead weight.

**"My child draws behind its parent."** `ZOrder` is flat within a canvas, not nested. Give the child
a higher value.

**"The element is at the wrong end of the screen."** Canvas space is **+y DOWN** with the origin at
the top-left, so anchor `y = 1` is the bottom edge and negative `ScreenOffset.y` moves *up*.

**"My stretched panel is inside out."** For a stretch anchor, `OffsetMax` is measured from the
**max** anchor point, so an inset from the right/bottom edges is **negative**: `OffsetMin =
{16,16}`, `OffsetMax = {-16,-16}`.

**"A missing UI image draws a black box, not my tint."** `AssetLibrary::GetTexture` returns a
degraded, non-null 0 × 0 texture for a bad path and caches it, so `Resolved` is set and the flat-tint
fallback (which tests the `Ref`, not the size) is never taken. The quad samples handle 0 and reads
black. Check the path; an empty `TexturePath` is the correct way to ask for a solid colour.

**"My long line runs out of the box."** `Wrap` is reflected but unimplemented. Insert `\n`.

**"An image on top of my button doesn't block clicks."** Only interactable buttons participate in the
hover search. Use a transparent, interactable button as a shield.

**"The button fires twice."** Something is calling `UiSystem::Update` more than once per frame with
the same edge flags, or two hosts are both driving the same scene.

**"Nothing happens on click in the editor."** Buttons are only live in **Play**. While editing, a
click selects the element instead.

**"Hover sticks when the mouse leaves the window."** Park the pointer far outside the canvas on the
frames the cursor is elsewhere; `Update` has no window-focus knowledge of its own.

**"My world-anchored nameplate is unclickable / clicks land somewhere else."** Both shipped hosts
pass the camera view-projection to `Render` but not to `Update`, so it draws projected and hit-tests
un-projected. See [Pin UI to a world position](#pin-ui-to-a-world-position).

**"Setting `Pivot` doesn't move anything."** It does not affect layout in v1; there is no rotation.

**"My prefabbed nameplates all track the same entity."** `EntityRef` fields are not remapped on
prefab instantiation. Assign `TargetEntity` after spawning.

**"`RuntimeTexture` won't take my `RenderToTexture` result."** The types do not meet — the engine
ships no `FrameBuffer` → `Texture2D` bridge. Write the small adapter above.

**"My main view got blurry / resized after the minimap render."** `RenderToTexture` resizes the
calling renderer's post stack to the target. Use a second, dedicated `SceneRenderer`.

---

## See also

- [`editor-ui-and-theming.md`](editor-ui-and-theming.md) — **the other UI system**: `ImGuiLayer`,
  docking, `ThemeManager`, fonts and Lucide icons, `Widgets`
- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — world-space 2D, which composites **before**
  the tonemap
- [`rendering-2d.md`](rendering-2d.md) — `Renderer2D`, batching, `DrawString`, `BlendMode`
- [`flow-and-story.md`](flow-and-story.md) — `.cflow` screen graphs and `.cstory` dialogue, the
  usual consumers of button signals *(D53)*
- [`scripting.md`](scripting.md) — `ScriptableEntity`, `OnSignal`, the `Signals()` proxy
- [`entities-and-components.md`](entities-and-components.md) — the component catalogue and the
  `Active` gate
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — how UI entities persist, and the
  prefab `EntityRef` caveat
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what each engine
  configuration ships
