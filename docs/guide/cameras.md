# Cameras & Viewport Navigation — Guide

**What this covers:** the `Camera` interface and its two concrete cameras (`OrthographicCamera`,
`PerspectiveCamera`), all four controllers — `Camera2DController`, `OrthographicCameraController`,
`OrbitCameraController` (`NavStyle`, `ViewPreset`, frame-and-snap), `FlyCameraController` — and then
CAD navigation end to end: the `NavigationCube` orientation widget, click-to-select entity picking
through `ScenePicker`'s ID buffer and its viewport-pixel coordinate contract, and `Gizmo` /
ImGuizmo transform manipulation with the editor's hotkeys. Plus rendering from a scene
`CameraComponent`.
**Source of truth:** `Cosmic/src/camera/Camera.h`, `camera/OrthographicCamera.{h,cpp}`,
`camera/PerspectiveCamera.{h,cpp}`, `camera/OrthographicCameraController.{h,cpp}`,
`camera/Camera2DController.{h,cpp}`, `camera/OrbitCameraController.{h,cpp}`,
`camera/FlyCameraController.{h,cpp}`, `camera/NavigationCube.{h,cpp}`,
`scene/ScenePicker.{h,cpp}`, `graphics/Gizmo.{h,cpp}`, `scene/Components.h` (`CameraComponent`),
`layers/PlayerLayer.cpp`, `Cosmic/CMakeLists.txt`, `Projects/Engine3DDemo/src/Engine3DDemo.cpp`,
`Projects/Starforge/src/{ViewportController,EditorCameraRig}.{h,cpp}`
**API Reference:** [../reference/cameras.md](../reference/cameras.md) *(skeleton — D14)*. Note
`camera/Camera2DController.h` has **no row** in the reference manifest (D52's finding — it is the
only controller missing) and `scene/ScenePicker.h`'s row points at
[../reference/ecs.md](../reference/ecs.md), not at the camera chapter.
**How it works:** [../systems/cameras-navigation.md](../systems/cameras-navigation.md)
*(skeleton — D27)*
**Configuration:** **mostly both.** Every camera and every controller ships in the 2D engine build —
`Cosmic/CMakeLists.txt:192-198` deliberately keeps `Camera`, `PerspectiveCamera`,
`OrthographicCamera`, `Camera2DController`, `OrbitCameraController` and `FlyCameraController`, and
`Gizmo` survives too. **`NavigationCube` and `ScenePicker` do not** — both are filtered out of the
2D build (`:198`, `:202`) because they draw through `Renderer3D`. See
[Two things a 2D build does not have](#two-things-a-2d-build-does-not-have) and
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md).

> The old root README §16 covered `OrthographicCameraController` and `OrthographicCamera` and
> nothing else — a grep of all 4,875 lines returned **zero** hits for `PerspectiveCamera`,
> `OrbitCamera`, `FlyCamera`, `Camera2DController`, `NavigationCube`, `ScenePicker` or `Gizmo`. Five
> of the seven had never been documented for clients at all.

---

## Quick start

Every renderer entry point takes `const Camera&`. Pick a controller, tick it, hand its camera over.

**2D — pan and zoom:**

```cpp
#include "Cosmic.h"                       // Cosmic.h DOES aggregate every camera header

Cosmic::Camera2DController m_Cam { 16.0f / 9.0f };

void OnUpdate(float ts) override
{
    m_Cam.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());   // screen px
    m_Cam.OnUpdate(ts);                                                  // MMB pan
    Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());
    // … draw …
    Cosmic::Renderer2D::EndScene();
}
void OnEvent(Cosmic::Event& e) override { m_Cam.OnEvent(e); }            // scroll = zoom
```

**3D — orbit around a model:**

```cpp
Cosmic::OrbitCameraController m_Cam { 16.0f / 9.0f };

void OnUpdate(float ts) override
{
    m_Cam.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());
    m_Cam.SetNavigationStyle(Cosmic::NavStyle::CAD);     // MMB orbit, LMB free for picking
    m_Cam.OnUpdate(ts);
    m_Scene->OnRender3D(m_Cam.GetCamera());
}
void OnEvent(Cosmic::Event& e) override { m_Cam.OnEvent(e); }
```

Three rules that apply to **every** controller:

- **`OnUpdate(ts)` polls, `OnEvent(e)` dispatches.** Continuous input (drags, held keys) is polled
  from `Input` each frame; discrete input (scroll, and for three of the four, window resize) arrives
  as events. Miss either call and half the controls go dead.
- **No controller ever consumes an event.** Every `OnMouseScrolled` and `OnWindowResized` returns
  `false` by explicit contract, so your layer and the framebuffers still see them.
- **Mouse positions are `Input::GetMouseScreenPosition()` — ImGui screen space**, the same space as
  `WorkspaceLayer::GetViewportPos()`. Not `Input::GetMousePosition()` (window-client pixels). Drag
  *deltas* are the same in either space; absolute positions are not, and zoom-to-cursor,
  orbit-about-cursor and picking are all absolute.

---

## The camera / controller split

`Camera` is a pure interface with four getters and no data (`Camera.h:46`): `GetViewMatrix`,
`GetProjectionMatrix`, `GetViewProjectionMatrix`, `GetPosition`. `Renderer2D::BeginScene`,
`Renderer3D::BeginScene` and `RenderPass` all take `const Camera&` and call only those four, so any
camera works anywhere — including one you write yourself. `PlayerLayer` and Starforge both define a
private `Camera` subclass that just holds four matrices and is fed from a scene `CameraComponent`.

A **camera** stores matrices. A **controller** owns a camera plus input handling and interaction
state. Reach the camera through `GetCamera()`.

| Class | Camera | Ships in 2D build | Input | Use it for |
| --- | --- | --- | --- | --- |
| `OrthographicCamera` | — | ✅ | none | Direct matrix control; the base of both 2D rigs |
| `PerspectiveCamera` | — | ✅ | none | Direct matrix control; the base of both 3D rigs |
| `Camera2DController` | `OrthographicCamera` | ✅ | MMB pan, scroll zoom-to-cursor | Editor-style 2D navigation, tile painting, sprite games |
| `OrthographicCameraController` | `OrthographicCamera` | ✅ | WASD pan, scroll zoom, optional Q/E roll | Keyboard-driven 2D; the original Phase-1 rig |
| `OrbitCameraController` | `PerspectiveCamera` | ✅ | LMB/MMB orbit, pan, dolly, scroll zoom | Inspecting a model; the editor camera |
| `FlyCameraController` | `PerspectiveCamera` | ✅ | RMB look, WASD/QE move, Shift boost | Exploring a world; first-person feel |

### `OrthographicCamera`

`SetProjection(left, right, bottom, top)` keeps a **-1…1** depth range — the Phase-1 behaviour. A
six-argument overload takes explicit `nearZ`/`farZ` for ortho views that must see real world depth,
which is what `Camera2DController` uses (±1000). `SetPosition` / `SetRotation` (Z-axis, degrees)
rebuild the view. The view is built as `transpose(R) * T(-pos)` rather than `glm::inverse` — valid
because a rotation matrix's transpose is its inverse, and roughly 8× cheaper on a path that runs on
every mutation.

### `PerspectiveCamera`

`SetProjection(fovYDegrees, aspect, nearClip, farClip)`; defaults are 45° / 16:9 / 0.1 / 1000.
Position plus a quaternion orientation; `LookAt(eye, target, up = +Y)` is the usual way to pose it.
`GetForward()` is the **look direction** (local −Z rotated to world), not +Z. `GetRight()` and
`GetUp()` complete the basis.

`SetViewportSize(w, h)` updates the aspect only and **ignores a zero width or height** — the guard
exists because docking and minimising both produce transient 0×0 viewports
(`PerspectiveCamera.cpp:33`).

Frame convention throughout: right-handed, **Y-up**, camera looks down its local −Z (see
`math/Spatial.h`). Simulation code working in NED converts through `Math::NedToRender`.

---

## Set up a 2D pan/zoom view

`Camera2DController` is the modern 2D rig: an `OrthographicCamera` on the XY plane, +Y up, looking
down −Z.

Two conventions do all the work:

- **`Focus`** is the world XY point at the centre of the view.
- **`Zoom`** is the visible **half-height in world units**. Smaller is closer in. Default 5.0,
  clamped to [0.01, 10 000].

The camera sits at `(Focus, 0)` with a **±1000** Z clip range, so sprites spread across Z / `ZOrder`
and modest 2.5D props all render. Larger world Z is nearer the viewer.

```cpp
Cosmic::Camera2DController cam { 16.0f / 9.0f };
cam.SetFocus({ 0.0f, 0.0f });
cam.SetZoom(6.0f);                                  // 12 world units of visible height

// Per frame, in this order:
cam.SetViewportRect(vpPos, vpSize);                 // superset of OnResize — also sets aspect
cam.SetControlEnabled(hovered || cam.IsDragging()); // the standard host gate
cam.OnUpdate(ts);
```

Controls are **MMB drag = pan** and **scroll = zoom about the cursor** (exponential, ×1.15 per
notch, keeping the world point under the pointer pinned). There are no keyboard bindings.

Queries and helpers:

| Call | Does |
| --- | --- |
| `VisibleRect(outMin, outMax)` | The world XY rect currently on screen — for grids and culling |
| `FrameBounds(worldMin, worldMax)` | Recentre and zoom to fit a box, padded ~10% |
| `ScreenToWorld(screenPx, vpPos, vpSize, focus, zoom)` | **static** — screen pixel → world XY |
| `PanBy(focus, deltaPx, zoom, viewportHeightPx)` | **static** — new focus for a pixel drag |
| `ZoomAboutPoint(focus, worldAnchor, zoomBefore, zoomAfter)` | **static** — new focus that pins an anchor |

The three statics are pure and headless-testable (`tests/test_camera2d.cpp`), and `ScreenToWorld` is
what you want for "which tile did the user click?" — it needs no controller instance, just the
numbers.

**`OnEvent` dispatches scroll only.** Unlike the orbit, fly and ortho controllers, this one does
**not** handle `WindowResizeEvent`; keeping the aspect current is the host's job via `OnResize` or
`SetViewportRect`. That is deliberate — a 2D rig usually lives in a docked panel whose size has
nothing to do with the window's.

Full 2D authoring — sprites, tilemaps, 2D lights and how this rig fits the editor — is
[`sprites-and-tilemaps.md`](sprites-and-tilemaps.md).

---

## Keyboard-driven 2D

`OrthographicCameraController` is the original Phase-1 rig and still the simplest thing that works
for a keyboard-driven 2D app.

```cpp
Cosmic::OrthographicCameraController m_Cam { 1280.0f / 720.0f };         // no rotation
Cosmic::OrthographicCameraController m_Cam { 1280.0f / 720.0f, true };   // enable Q/E roll

void OnUpdate(float ts) override { m_Cam.OnUpdate(ts); Renderer2D::BeginScene(m_Cam.GetCamera()); }
void OnEvent(Cosmic::Event& e) override { m_Cam.OnEvent(e); }            // scroll + resize
```

WASD pans; **pan speed scales with the zoom level** (`speed × zoom`) so it feels the same at any
magnification. Q/E roll about Z only when `rotation = true` was passed to the constructor.

Zoom has two setters and the difference matters:

```cpp
m_Cam.SetZoomLevel(2.0f);          // HARD SNAP — sets current and target, no interpolation
m_Cam.SetTargetZoomLevel(2.0f);    // sets target only — OnUpdate's asymptotic blend animates there
m_Cam.SetZoomLimits(0.25f, 10.0f); // the defaults
m_Cam.SetZoomSpeed(0.25f);         // the default
```

Everything else is tuning: `SetTranslationSpeed` (5.0), `SetRotationSpeed` (180 °/s),
`SetPositionLimits(minX, maxX, minY, maxY)` (defaults ±1000), `SetManualMovementEnabled(false)` to
hand the camera to a cutscene, and `SetKeyBindings` for a custom `CameraKeyBindings` struct
(`MoveLeft`/`MoveRight`/`MoveUp`/`MoveDown`/`RotateQ`/`RotateE`; a binding of `0` disables that
action).

Three sharp edges, all verified:

- **Zoom is LINEAR here and exponential everywhere else.** `OnMouseScrolled` does
  `target -= yOffset × zoomSpeed` (`OrthographicCameraController.cpp:113`), while the 2D, orbit and
  fly rigs all multiply by `1.15^-notch`. Scrolling out from a wide view feels sluggish; scrolling
  in near the minimum feels violent.
- **`SetPositionLimits` is only enforced by keyboard panning.** `SetPosition` writes the position
  through unclamped.
- **`OnWindowResized` has no zero-height guard.** It calls `OnResize(w, h)` unconditionally and
  `OnResize` divides — so a minimise (a 0×0 `WindowResizeEvent`, which `Application::OnWindowResize`
  does **not** consume) leaves the aspect `inf` or `NaN` until the next real resize.
  `OrbitCameraController` and `FlyCameraController` both guard with `if (e.GetHeight() > 0)`; this
  one does not. **Looks like a real bug and a one-line Phase 30 fix.** In the meantime, prefer
  `Camera2DController` for new 2D work.

---

## Orbit around a model

`OrbitCameraController` rides a spherical mount: a `Target` point, a `Distance`, and yaw/pitch in
degrees. Yaw 0 / pitch 0 puts the camera on the target's **+Z** side looking −Z; positive pitch
raises it. Pitch clamps to ±89° so the up vector never degenerates.

### Two binding schemes

```cpp
m_Orbit.SetNavigationStyle(Cosmic::NavStyle::CAD);   // default is Classic
```

| Gesture | `NavStyle::Classic` (default) | `NavStyle::CAD` |
| --- | --- | --- |
| Orbit | LMB drag | **MMB** drag |
| Pan | RMB drag | **Ctrl + MMB** drag |
| Dolly | — | **Shift + MMB** drag (vertical; up = in) |
| Zoom | scroll, toward the **centre**, smoothly blended | scroll, toward the **cursor**, snapped |
| Orbit pivot | the current `Target` | the world point **under the cursor** |
| LMB | orbits | **free** — click-to-select |

CAD is the SolidWorks feel and the reason `LMB` stays free: an editor needs it for picking.
Starforge defaults its viewport to CAD.

The gesture is **latched on the frame the drag starts**, so changing modifiers mid-drag does not
switch modes. The first frame of a drag produces no motion at all (the position is latched, the
delta is zero) — that is what stops the camera jumping by however far the cursor travelled since the
last drag ended.

### Orbiting about a point, not a target

`OrbitBy(dYaw, dPitch)` rigidly rotates the **whole rig** — eye and target together — about a
latched pivot, so the pivot's projected pixel is invariant and nothing snaps when you press the
button. `BeginOrbitAbout(pivot)` latches it; `OnUpdate` calls that itself on the first frame of a
drag (Classic: the target; CAD: the point under the cursor). Both are public, so a scripted camera
move or a headless test can drive the same primitive.

Where does "the point under the cursor" come from? A `PivotProbe` you supply, falling back to the
cursor ray intersected with the plane through the target:

```cpp
m_Orbit.SetPivotProbe([this](const glm::vec2& screenMouse, glm::vec3& out) -> bool
{
    const glm::vec2 vpPos = Cosmic::Application::Get().GetViewportPos();
    const int px = (int)(screenMouse.x - vpPos.x);      // viewport-local pixels
    const int py = (int)(screenMouse.y - vpPos.y);
    m_Picker->RenderIdPass(*m_Scene, m_Orbit.GetCamera(), vpW, vpH);
    return m_Picker->WorldPoint(m_Orbit.GetCamera(), px, py, out);
});
```

`screenMouse` arrives in ImGui screen pixels — the same space as `GetViewportPos()` — so the
subtraction is a straight remap. The two in-tree probes differ only in *when* the ID pass runs:
Starforge renders one inside the probe (`ViewportController.cpp:504-522`, invoked only on the frame
an orbit drag begins), while Engine3DDemo omits it because its editor mode already renders the
picker FBO every frame (`Engine3DDemo.cpp:258`). Either is fine; a stale ID pass gives a stale
pivot.

**The probe is optional** — with none set, CAD navigation still works off the ray/plane fallback,
which is good enough when geometry sits near the target and needs no `ScenePicker` at all (so it
works in a 2D build too).

`SetViewportRect(posPx, sizePx)` is **required** for zoom-to-cursor and the ray fallback — they need
to know where the viewport is on screen. It also updates the projection aspect, making it a superset
of `OnResize`. An app that never uses CAD nav can keep calling `OnResize`.

### Snap views and framing

```cpp
m_Orbit.SnapView(Cosmic::ViewPreset::Top);              // animated by default
m_Orbit.SnapView(Cosmic::ViewPreset::Iso, false);       // instant cut
m_Orbit.FrameBounds(worldMin, worldMax);                // fit an AABB
m_Orbit.FrameSphere(center, radius);                    // the primitive FrameBounds builds on
if (m_Orbit.IsAnimating()) { /* a blend is in progress */ }
```

| `ViewPreset` | Yaw | Pitch |
| --- | --- | --- |
| `Front` | 0° | 0° |
| `Back` | 180° | 0° |
| `Right` | 90° | 0° |
| `Left` | −90° | 0° |
| `Top` | 0° | **89°** (shy of the pole) |
| `Bottom` | 0° | **−89°** |
| `Iso` | 45° | 30° |

Snaps keep the current target and distance and blend over a few frames (~63% closed per 1/12 s,
frame-rate independent, yaw along the short arc). `FrameSphere` fits the sphere to about 70% of the
viewport half-height at any aspect; `FrameBounds` uses the box's bounding **sphere**, so a long thin
box frames conservatively. Degenerate (zero-radius) boxes are ignored.

**Any drag or scroll cancels an in-progress blend**, as do the explicit hard-setters `SetDistance`
and `SetYawPitch`. `SetTargetDistance` blends instead.

### Tuning and gating

`SetDistanceLimits` (0.5–500), `SetPitchLimits` (±89°), `SetOrbitSpeed` (0.25 °/px), `SetPanSpeed`,
`SetZoomSpeed`, and `SetInertiaEnabled(true)` for optional exponential drift after an orbit release
(off by default, purely cosmetic — it does not change end poses).

The host gate every editor uses:

```cpp
m_Orbit.SetControlEnabled((vpHovered || m_Orbit.IsDragging()) && !gizmoActive && !gizmoOver);
```

`IsDragging()` is what lets a drag that *started* inside the viewport continue after the cursor
leaves it. Disabling control mid-drag ends the drag.

---

## Fly through a world

`FlyCameraController` moves the camera itself instead of orbiting a point. It is the rig behind the
Frontier showcase and Starforge's fly mode.

| Input | Action |
| --- | --- |
| RMB held | mouse-look (cursor right yaws right, cursor up pitches up) |
| W / S | forward / back along the **full look vector** (including pitch) |
| A / D | strafe along the camera right axis |
| E or Space | ascend (world +Y) |
| Q or LCtrl | descend (world −Y) |
| LShift | speed boost while held (×4 by default) |
| Scroll | change the base move speed, ×1.15 per notch |

**Movement does not require RMB.** The mouse-look block and the movement block are independent in
`OnUpdate` — WASD flies whenever `IsControlEnabled()` is true. If you want Unreal's "hold RMB to
fly", gate control on the button yourself; that is what Starforge's temporary-fly mode does.

```cpp
Cosmic::FlyCameraController m_Fly { 16.0f / 9.0f };
m_Fly.SetPose({ 0.0f, 30.0f, 60.0f }, /*yaw*/ 0.0f, /*pitch*/ -20.0f);
m_Fly.SetMoveSpeed(25.0f);          // clamped to [0.5, 500] m/s
m_Fly.SetBoostMultiplier(4.0f);
m_Fly.SetSmoothing(12.0f);          // exponential velocity smoothing per second; 0 = raw
m_Fly.SetLookSpeed(0.15f);          // degrees per pixel

// Optional: never fall through the terrain.
m_Fly.SetGroundProbe([this](float x, float z) { return m_Terrain->SampleHeight(x, z); }, 1.5f);
```

Yaw 0 looks down −Z, matching the orbit rig; positive pitch looks up; pitch clamps to ±89°.
`SetPose` zeroes residual velocity. Disabling control coasts the velocity to rest rather than
stopping dead. `IsLooking()` reports an active RMB drag, the fly equivalent of
`OrbitCameraController::IsDragging()`.

The movement maths is exposed as four pure statics — `DirectionFromYawPitch`, `ComputeWishVelocity`,
`IntegrateMotion`, `ClampAboveGround` — so it is unit-testable without an `Input` backend
(`tests/test_flycamera.cpp`).

### Switching between orbit and fly without a jump

The two rigs' angle conventions mirror each other exactly: `fly(yaw, pitch) == (−orbitYaw,
−orbitPitch)` produces the same look direction. So *orbit → fly* seeds the fly pose at the orbit
**eye**, and *fly → orbit* re-targets the orbit pivot `distance` metres along the fly look
direction. `Starforge::EditorCameraRig` (`EditorCameraRig.h`) is the reference implementation of
that pair, plus a read-only **Possess** mode that renders from a scene `CameraComponent`'s pose.

---

## Two things a 2D build does not have

`NavigationCube` and `ScenePicker` are excluded from the 2D engine build:

```cmake
list(FILTER COSMIC_SOURCES EXCLUDE REGEX "/src/camera/NavigationCube\\.")
list(FILTER COSMIC_SOURCES EXCLUDE REGEX "/src/scene/(Scene3D|Components3D|SceneNav|ScenePicker|WorldSystemRecipes)\\.")
```

Both draw through `Renderer3D`, which is itself filtered out. The cube renders a shaded box and an
axis tripod; the picker renders the scene's meshes into an entity-ID attachment. Neither has a
meaning in a 2D viewport, and Starforge's own `ViewportController` fences every call site.

**The two fail differently, and one of them fails late.** `Cosmic.h` guards
`#include "scene/ScenePicker.h"` with `#ifndef COSMIC_2D_ONLY`, so naming `ScenePicker` in a 2D
project is a clean compile error. It does **not** guard `#include "camera/NavigationCube.h"` — the
header's dependencies (`FrameBuffer.h`, `Mesh.h`, `OrbitCameraController.h` for `ViewPreset`) are
all present in the 2D build, so `NavigationCube::Create(120)` **compiles and then fails to link**
with an unresolved external. That asymmetry is a one-line fix (fence the include) and a **Phase 30
candidate**.

What a 2D viewport does instead of an ID pass: rect-pick the topmost sprite directly. Starforge
walks every `SpriteRendererComponent`, tests the cursor's world XY against each sprite's rect, and
sorts by `(Z, ZOrder, handle)` (`ViewportController.cpp:430-460`) — `Camera2DController::
ScreenToWorld` is the only camera call it needs. The gizmo works in both builds.

---

## Add the navigation cube *(3D only)*

A self-contained orientation widget: it renders a cube into its **own** framebuffer, oriented to
match a camera's view, and turns a click on a face into a `ViewPreset`.

```cpp
m_NavCube = Cosmic::NavigationCube::Create(140);        // square FBO edge in pixels

// 1) Pre-pass, OUTSIDE any BeginScene/EndScene — it leaves its FBO unbound,
//    so re-bind your own render target afterwards.
m_NavCube->Render(m_Orbit.GetCamera().GetViewMatrix());

// 2) Inside the ImGui frame, in the viewport overlay window:
ImGui::SetCursorScreenPos({ vpPos.x + vpSize.x - sz - 12.0f, vpPos.y + 12.0f });
ImGui::Image((ImTextureID)(intptr_t)m_NavCube->GetTextureID(),
             { sz, sz }, ImVec2(0, 1), ImVec2(1, 0));   // flip V — GL is bottom-left
if (ImGui::IsItemHovered())
{
    m_NavCubeHovered = true;                            // the camera must yield this corner
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 imgMin = ImGui::GetItemRectMin();
        const ImVec2 m      = ImGui::GetMousePos();
        Cosmic::ViewPreset preset;
        if (m_NavCube->PickFace((m.x - imgMin.x) / sz, (m.y - imgMin.y) / sz, preset))
            m_Orbit.SnapView(preset);
    }
}
```

That is `Engine3DDemo.cpp:1436-1460` condensed.

- **Only the camera view's rotation is used.** `Render` strips the translation, so the cube reads
  orientation and nothing else. The projection is orthographic (fixed) so the cube shows no
  perspective skew, and the background clears to **transparent** so it floats over the viewport.
- **`PickFace(u, v, out)` takes panel coordinates in [0, 1], `v` measured DOWN from the top-left** —
  the natural ImGui convention. It reuses the exact view-projection of the last `Render`, so pixels
  and picks always agree. Returns `false` on a background miss.
- `PickFaceFromViewProjection` is the same ray/AABB test exposed statically, so the mapping is
  unit-testable with no GL context (`tests/test_s5_navigation.cpp`).
- **Feed the hover state back into the camera gate.** The cube owns its corner: while it is hovered,
  camera drags and click-to-select must both stand down, or clicking a face also starts an orbit.

---

## Click to select an entity *(3D only)*

`ScenePicker` turns a mouse position into the `Entity` under it, using the entity-ID MRT attachment
that `Scene::OnRender3D` already writes. It owns a private `{RGBA8, RED_INTEGER, DEPTH24STENCIL8}`
framebuffer that resizes itself to whatever you ask for.

```cpp
m_Picker = Cosmic::ScenePicker::Create();

// On the CLICK FRAME only — an ID pre-pass is not free.
const glm::vec2 mouse = Cosmic::Input::GetMouseScreenPosition();
const int px = (int)(mouse.x - vpPos.x);          // x from the LEFT of the viewport
const int py = (int)(mouse.y - vpPos.y);          // y from the TOP of the viewport
if (px >= 0 && py >= 0 && px < (int)vpSize.x && py < (int)vpSize.y)
{
    m_Picker->RenderIdPass(*m_Scene, renderCam, (uint32_t)vpSize.x, (uint32_t)vpSize.y);
    if (Cosmic::Entity hit = m_Picker->Pick(*m_Scene, px, py))
        Select(hit);
    else
        ClearSelection();                          // click-away deselects
}
```

### The coordinate contract

**`Pick` and `WorldPoint` take viewport-local pixels: x from the LEFT, y from the TOP.** That is the
ImGui/mouse convention, and the GL bottom-left flip (`glY = height - 1 - yFromTop`) happens inside.
Subtract the viewport origin from the screen mouse position; do **not** flip y yourself.

`ScenePicker` is one of two places in the engine with a deliberate double convention, and the other
half is `FrameBuffer`: `ReadPixel`/`ReadDepth` take **GL bottom-left** coordinates while `ReadPixels`
returns a **top-left-origin** buffer. `ScenePicker` is the one that speaks mouse space.

### What the ID pass can and cannot see

`RenderIdPass` clears the ID attachment to **−1** (a plain `glClear` does not touch integer
attachments), then calls `Scene::OnRender3D`. So it sees exactly what the 3D pass draws: meshes,
LOD-group levels, skinned meshes, terrain and voxel chunks. It does **not** see sprites, tilemaps,
2D lights, UI entities, water or particles — clicking any of those falls through as a miss. That is
why Starforge rect-picks sprites and UI *before* falling back to the ID pass.

The optional `only` parameter restricts the pass to a list of entities, turning the ID attachment
into a selection mask — that is how `SceneRenderer`'s outline pass works, not something client code
usually needs.

### Failure modes

`Pick` returns an **invalid `Entity`** (`operator bool` is false) for a miss, out-of-range
coordinates, or an id that no longer maps to a live registry slot — never a crash and never a stale
handle. `WorldPoint` returns `false` at the far plane (nothing drawn) and otherwise reconstructs the
world position from the depth buffer; the `camera` you pass **must be the one `RenderIdPass`
used**, or the unprojection is silently wrong.

The read-back is a synchronous `glReadPixels` — a small pipeline stall. Fine for click-to-select;
if you ever want per-frame hover picking on a large scene, an async PBO round-robin is the noted
future direction.

---

## Move, rotate and scale with the gizmo

`Gizmo` is a thin static wrapper over vendored ImGuizmo that exposes only engine enums, so no
third-party type reaches a public header. It ships in **both** configurations.

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
if (ws->BeginViewportOverlay())                       // append to the Viewport window
{
    Cosmic::Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);   // ImGui screen px
    if (selected && selected.HasComponent<Cosmic::TransformComponent>())
    {
        auto& t = selected.GetComponent<Cosmic::TransformComponent>();
        Cosmic::Gizmo::Manipulate(cam, t, m_Op, m_Space, snap);
    }
}
ws->EndViewportOverlay();                             // always pair with Begin
```

**`Manipulate` must be called between `Begin`/`End` of the window showing the rendered scene.** It
draws into — and, critically, hit-tests hover against — the *current* ImGui window. Called outside
one (say, into a foreground draw list) it renders a gizmo that can never be grabbed, because
ImGuizmo decides the mouse is "over some other window" whenever your viewport is hovered. This is
the single most common way to get a gizmo that looks right and does nothing.

The engine calls `ImGuizmo::BeginFrame()` once per frame in `ImGuiLayer::Begin`
(`ImGuiLayer.cpp:104`), so clients have no per-frame bookkeeping.

| Enum | Values |
| --- | --- |
| `Gizmo::Operation` | `Translate`, `Rotate`, `Scale`, `Universal` |
| `Gizmo::Space` | `Local`, `World` |

`Universal` combines translate arrows, rotate rings and universal scale handles in one gizmo.
**ImGuizmo takes a single snap value per call**, so under `Universal` your snap applies to the
**move** handles and rotate/scale drag unsnapped — pass the move increment there.

`snap > 0` snaps to that increment (world units for translate and scale, **degrees** for rotate);
`0` disables snapping. Orthographic vs perspective is detected from the camera's projection matrix
(`[3][3] > 0.5`), so a 2D ortho viewport needs no special handling.

Two overloads:

- `Manipulate(camera, glm::mat4& model, …)` — edits a raw matrix in place, returns `true` if it
  changed this frame.
- `Manipulate(camera, TransformComponent&, …)` — decomposes the result back into
  `Position` / `Scale` / `RotationQuat` and **sets `UseQuatRotation = true`**. The Euler `Rotation`
  field is left stale afterwards; the component's two rotation representations are independent by
  design. (`ScenePhysics::WriteBackWorldPose` and `Scene::SetParent(..., keepWorldPose=true)` do the
  same thing — it is the engine's normal behaviour, not a gizmo quirk.)

The decomposition is done by hand — translation from the 4th column, per-axis scale from the basis
column lengths, rotation from the normalised columns — rather than `glm::decompose`, to dodge a
quaternion-sign pitfall some glm versions have.

**The component overload edits the LOCAL transform.** It reads `TransformComponent::GetTransform()`,
which composes only that entity's own position/rotation/scale. For a parented entity the gizmo
therefore sits at the local pose, not the world pose — Starforge passes the component directly
(`ViewportController.cpp:1165`) and inherits that behaviour. If you need world-space manipulation,
use the `glm::mat4` overload with `Scene::GetWorldTransform(e)` and re-derive the local transform
yourself.

### Interaction etiquette

Two static queries drive everything:

- **`Gizmo::IsUsing()`** — a drag is in progress. The camera controller must yield, or grabbing a
  handle also orbits.
- **`Gizmo::IsOver()`** — the cursor is on a handle. Click-to-select must skip, or every grab also
  reselects.

`Gizmo::SetEnabled(false)` greys the gizmo out without removing it.

Undo is the host's job. Starforge's pattern is worth copying: snapshot the transform on the frame
`IsUsing()` goes true, and commit one coalesced command on the frame it goes false
(`ViewportController.cpp:1167-1175`) — one undo entry per drag, not per frame.

### The editor's hotkeys

Starforge's viewport bindings, for reference and for matching in your own tools
(`ViewportController.cpp:197-216`):

| Key | Action |
| --- | --- |
| `W` | Translate |
| `E` | Rotate |
| `R` | Scale |
| `Q` | Universal |
| `F` | Frame the selection |
| `G` | Toggle the grid |
| `1`–`9` | Recall camera bookmark |
| `Ctrl` + `1`–`9` | Save camera bookmark |

They are gated on `vpHover && !io.WantTextInput && !io.WantCaptureKeyboard && !rig.IsFlying()` —
the last condition matters, because while flying W/A/S/D/Q/E are **movement** and must not flip the
gizmo mid-flight.

---

## Render from a scene camera

For a shipped game the camera usually lives in the scene as a `CameraComponent`, not as a
controller. The component holds only the **projection**; position and orientation come from the
entity's `TransformComponent`.

```cpp
auto& cc = camEntity.AddComponent<Cosmic::CameraComponent>();
cc.Primary        = true;                                        // default
cc.ProjectionType = Cosmic::CameraComponent::Projection::Perspective;
cc.FovDeg         = 60.0f;                                       // vertical FOV
cc.Near           = 0.1f;
cc.Far            = 1000.0f;
cc.OrthoSize      = 10.0f;                                       // half-height, ortho only
```

The rule every host follows: **the first `Primary` camera wins**, and `view = inverse(world
transform)` (`PlayerLayer.cpp:314-323`). With no primary camera, `PlayerLayer` warns **once** and
falls back to a fixed 3/4 view from `(0, 4, 10)`.

To drive a renderer from one, wrap the matrices in a tiny `Camera` subclass — that is exactly what
`PlayerLayer::PlayerCamera` and `Starforge::PossessCamera` are:

```cpp
class SceneCamera : public Cosmic::Camera
{
public:
    void Set(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& pos)
    { m_View = view; m_Proj = proj; m_ViewProj = proj * view; m_Pos = pos; }

    const glm::mat4& GetViewMatrix() const override           { return m_View; }
    const glm::mat4& GetProjectionMatrix() const override     { return m_Proj; }
    const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProj; }
    const glm::vec3& GetPosition() const override             { return m_Pos; }
private:
    glm::mat4 m_View{ 1.0f }, m_Proj{ 1.0f }, m_ViewProj{ 1.0f };
    glm::vec3 m_Pos{ 0.0f };
};
```

**An orthographic `CameraComponent` at `z = 0` clips the whole scene.** `GetProjection` feeds
`Near` (0.1) and `Far` (1000) straight into `glm::ortho`, so anything within 0.1 units of the camera
plane is clipped — and in a 2D scene everything is at z ≈ 0. ForgePong parks its camera at
`z = 10`. `Camera2DController` has no such trap because it pins the camera at `(Focus, 0)` with a
±1000 range.

---

## Common patterns

**The host gate.** Every controller in an editor-style app gets the same three lines each frame:
`SetViewportRect(...)`, `SetControlEnabled(hovered || dragging)`, `OnUpdate(ts)`. The
`|| dragging` half is what makes a drag survive the cursor leaving the panel.

**Yield to the tools.** Camera control off while `Gizmo::IsUsing() || Gizmo::IsOver()` or while the
nav cube is hovered; click-to-select skipped under the same conditions. Get this wrong and every
gizmo grab also orbits.

**One pick pass per click, never per frame.** `RenderIdPass` is a full scene draw. Gate it on the
click edge. The CAD pivot probe follows the same rule — Starforge invokes it only on the frame an
orbit drag begins (`ViewportController.cpp:518`).

**Frame the selection.** `F` → compute the selection's world AABB →
`OrbitCameraController::FrameBounds` in 3D, `Camera2DController::FrameBounds` in 2D. Both animate
(or recentre) sensibly on a degenerate box.

**Camera bookmarks.** The orbit rig's whole pose is four values — `GetYaw()`, `GetPitch()`,
`GetDistance()`, `GetTarget()`. Store them; restore with `SetYawPitch` + `SetDistance` +
`SetTarget`. That is all Starforge's Ctrl+1..9 does.

**Headless camera tests.** `OrbitBy`, `FlyCameraController`'s four statics,
`Camera2DController`'s three statics and `NavigationCube::PickFaceFromViewProjection` are all pure
and GL-free. `tests/test_camera2d.cpp`, `test_flycamera.cpp` and `test_s5_navigation.cpp` are the
worked examples.

---

## Pitfalls

**"The gizmo draws but I can't grab it."** `Manipulate` was called outside the ImGui window that
shows the scene. Wrap it in `BeginViewportOverlay()` / `EndViewportOverlay()`.

**"Grabbing the gizmo also orbits the camera."** Gate `SetControlEnabled` on `!Gizmo::IsUsing() &&
!Gizmo::IsOver()`.

**"Rotate snapping is enormous / translate snapping is invisible."** One `snap` argument, three
meanings: world units for translate and scale, **degrees** for rotate. Under `Universal` it applies
to the move handles only.

**"The gizmo is nowhere near my parented entity."** The `TransformComponent` overload edits the
**local** transform. Use the `glm::mat4` overload with `Scene::GetWorldTransform` for world-space
manipulation.

**"My Euler rotation values stopped updating."** The gizmo writes the quaternion and sets
`UseQuatRotation = true`. The Euler field is stale from then on, by design.

**"Zoom-to-cursor zooms to the centre."** `SetViewportRect` was never called, so the controller has
no viewport rectangle and `CursorRay` bails. Same cause when the CAD pivot fallback does nothing.

**"The camera jumps when I press the mouse button."** You are deriving deltas from a position that
was last sampled before the drag. Both engine rigs latch on the first frame and emit no delta; if
you write your own, do the same.

**"Absolute mouse maths is off by the panel position."** Use
`Input::GetMouseScreenPosition()` (ImGui screen space, matching `GetViewportPos()`), not
`Input::GetMousePosition()` (window-client pixels). D48 documented that split.

**"Picking selects the wrong thing near the edges / is vertically mirrored."** `Pick` wants y from
the **top**. The GL flip is internal — do not pre-flip.

**"Clicking a sprite, a UI element or the water selects nothing."** The ID pass only draws what
`Scene::OnRender3D` draws. Rect-pick those separately, as Starforge does.

**"`WorldPoint` returns garbage."** The `camera` must be the one the last `RenderIdPass` used. It
also returns `false` — not a point — where nothing was drawn.

**"My render target changed after the pick / nav-cube pass."** Both are self-contained: they bind
their own FBO, set their own viewport, and leave it **unbound**. Re-bind your target and reset the
viewport afterwards.

**"`NavigationCube::Create` fails to link in my 2D build."** It is filtered out of the 2D engine
build, and `Cosmic.h` does not fence its include, so the failure arrives at link time. Guard your
call site with `#ifndef COSMIC_2D_ONLY`.

**"The aspect ratio goes wrong after minimising."** `OrthographicCameraController::OnWindowResized`
has no zero-height guard and divides by it. The orbit and fly rigs guard; `PerspectiveCamera::
SetViewportSize` and `Camera2DController::OnResize` guard too.

**"`Camera2DController` stretches when the panel resizes."** It handles scroll only — it does not
listen for `WindowResizeEvent`. Call `SetViewportRect` (or `OnResize`) yourself each frame.

**"My 2D scene renders black through a `CameraComponent`."** An orthographic camera at `z = 0`
clips everything within `Near` (0.1) of itself. Move the camera to `z = 10`.

**"Nothing renders and there's a warning about a Primary camera."** No `CameraComponent` in the
scene has `Primary = true`; the fallback 3/4 view is looking at the origin.

**"Zoom feels wrong in my old 2D app."** `OrthographicCameraController` is the only rig with linear
zoom. Every other controller is exponential.

**"Scroll reaches my layer even though the camera handled it."** By design — controllers never
consume events. Check `e.Handled` if you need to know, and see the consumption contract on every
controller header.

**"Q/E don't rotate my 2D camera."** `OrthographicCameraController`'s rotation is opt-in: pass
`true` as the constructor's second argument.

---

## See also

- [`sprites-and-tilemaps.md`](sprites-and-tilemaps.md) — the 2D rig in context: sprites, tilemaps,
  lights, and `ScreenToWorld` for tile picking
- [`rendering-2d.md`](rendering-2d.md) — `Renderer2D::BeginScene`, `RenderPass` and the viewport
  contract `BeginScene` quietly enforces
- [`entities-and-components.md`](entities-and-components.md) — `CameraComponent`,
  `TransformComponent`, and the `UseQuatRotation` split
- [`scenes-and-serialization.md`](scenes-and-serialization.md) — `CommandStack`, the undo pattern a
  gizmo drag commits into
- [`events-and-input.md`](events-and-input.md) — `Input` polling, the two mouse coordinate spaces,
  and `EventDispatcher`
- [`project-anatomy.md`](project-anatomy.md) — `WorkspaceLayer`, `BeginViewportOverlay`, and the
  viewport rect the gizmo needs
- [`../reference/cameras.md`](../reference/cameras.md) — per-call signatures *(skeleton — D14)*
- [`../systems/cameras-navigation.md`](../systems/cameras-navigation.md) — internals and rationale
  *(skeleton — D27)*
- [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) — what each engine
  configuration ships
