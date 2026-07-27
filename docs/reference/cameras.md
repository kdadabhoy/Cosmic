# API Reference — Cameras & Navigation

> **STATUS: WRITTEN** — work order **D14** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/camera/Camera.h`, `camera/OrthographicCamera.h`,
`camera/PerspectiveCamera.h`, `camera/Camera2DController.h`,
`camera/OrthographicCameraController.h`, `camera/OrbitCameraController.h` (+ `NavStyle` /
`ViewPreset`), `camera/FlyCameraController.h`, `camera/NavigationCube.h`, `scene/ScenePicker.h`,
`graphics/Gizmo.h`.

**Read first:** the guide chapter [`../guide/cameras.md`](../guide/cameras.md) — it owns the task
half (pick a rig, wire it up, the editor patterns, the pitfall list) and covers every class in
scope here. **This chapter does not repeat it**: it is the per-call lookup behind it — signature,
exact behaviour, clamp, failure mode. Systems explainer:
[cameras-navigation](../systems/cameras-navigation.md) *(skeleton — D27)*. For `CameraComponent`
(the scene-authored camera, which is a *component*, not a camera object) see
[ecs.md](ecs.md) and [`../guide/cameras.md`](../guide/cameras.md#render-from-a-scene-camera).

---

## Configuration — read this before you call anything

Every **camera** and every **controller** in this chapter ships in **both** engine builds, and so
does `Gizmo` (`Cosmic/CMakeLists.txt:192-198` keeps `Camera`, `PerspectiveCamera`,
`OrthographicCamera`, `Camera2DController`, `OrbitCameraController` and `FlyCameraController` on
purpose; `Gizmo` is "generic GPU infrastructure" and stays).

Two classes are **3D-only**, and — the trap — **they fail differently**:

| Class | Excluded from the 2D build | `Cosmic.h` include | What a 2D project sees |
| --- | --- | --- | --- |
| [`ScenePicker`](#scenepicker-3d-only) | `CMakeLists.txt:202` | **fenced** — `#ifndef COSMIC_2D_ONLY` at `Cosmic.h:117-119` | clean **compile** error: `ScenePicker` is not declared |
| [`NavigationCube`](#navigationcube-3d-only) | `CMakeLists.txt:198` | **NOT fenced** — `Cosmic.h:91` | it **compiles**, then fails at **LINK** with an unresolved external |

`NavigationCube.h`'s own dependencies (`FrameBuffer.h`, `Mesh.h`, and `OrbitCameraController.h` for
`ViewPreset`) all survive the 2D build, so nothing stops the header from being parsed — only
`NavigationCube.cpp` is missing. Guard 2D call sites yourself:

```cpp
#ifndef COSMIC_2D_ONLY
    m_NavCube = Cosmic::NavigationCube::Create(140);
#endif
```

Fencing the `Cosmic.h` include is a one-line fix and is logged as a Phase 30 candidate. The manifest
in [reference/README.md](README.md#coverage-manifest--every-public-header-maps-to-a-chapter) marks
this "compiles but does not link" case **³ᴰ⁺** to distinguish it from a plain fenced ³ᴰ header.

> **No header pre-condition in this chapter is enforced at runtime by an assertion.**
> `CS_ASSERT` / `CS_CORE_ASSERT` are compiled out in *every* configuration, and in fact no file in
> this chapter's scope uses them at all. Where a header docstring writes "Pre: eye != target" or
> "Pre: fovY in (0, 180)", that is a *documented expectation*, not a check. The guards that do
> exist are ordinary runtime `if`s and each one is named in the entry that owns it.

---

## Contents

- [Choosing a camera and a controller](#choosing-a-camera-and-a-controller)
- [The controller contract](#the-controller-contract) — the four rules every rig shares
- [`Camera`](#camera) — the interface both renderers accept
- [`OrthographicCamera`](#orthographiccamera) · [`PerspectiveCamera`](#perspectivecamera)
- [`Camera2DController`](#camera2dcontroller) — 2D pan/zoom
- [`OrthographicCameraController`](#orthographiccameracontroller) — keyboard 2D
- [`NavStyle` / `ViewPreset`](#navstyle) · [`OrbitCameraController`](#orbitcameracontroller) — CAD orbit
- [`FlyCameraController`](#flycameracontroller) — WASD + mouse-look
- [`NavigationCube`](#navigationcube-3d-only) *(3D only)* · [`ScenePicker`](#scenepicker-3d-only) *(3D only)*
- [`Gizmo`](#gizmo) — ImGuizmo transform manipulators
- [Binding tables](#binding-tables) — orbit gestures, `ViewPreset` angles, the Engine3DDemo hotkeys
- [Manifest & coverage notes](#manifest--coverage-notes)

---

## Choosing a camera and a controller

A **camera** stores matrices and nothing else. A **controller** owns a camera plus input handling
and interaction state, and hands the camera out through `GetCamera()`.

| You want | Use | Camera it owns | Both builds? |
| --- | --- | --- | --- |
| Direct matrix control, no input | [`OrthographicCamera`](#orthographiccamera) / [`PerspectiveCamera`](#perspectivecamera) | — | ✅ |
| Editor-style 2D: MMB pan, zoom-to-cursor | [`Camera2DController`](#camera2dcontroller) | `OrthographicCamera` | ✅ |
| Keyboard-driven 2D: WASD pan, optional roll | [`OrthographicCameraController`](#orthographiccameracontroller) | `OrthographicCamera` | ✅ |
| Inspect a model / an editor viewport | [`OrbitCameraController`](#orbitcameracontroller) | `PerspectiveCamera` | ✅ |
| Explore a world, first-person feel | [`FlyCameraController`](#flycameracontroller) | `PerspectiveCamera` | ✅ |
| A camera authored **in the scene** | `CameraComponent` + your own `Camera` subclass ([ecs.md](ecs.md)) | — | ✅ |

---

## The controller contract

Four rules hold for all four controllers. They are stated once here and referenced from the
entries rather than repeated.

**1 — `OnUpdate(ts)` polls, `OnEvent(e)` dispatches.** Continuous input (mouse drags, held keys) is
read from `Input` inside `OnUpdate`. Discrete input (scroll, and for three of the four, window
resize) arrives through `OnEvent`. Call both or half the controls are dead.

**2 — no controller ever consumes an event.** Every `OnMouseScrolled` and `OnWindowResized` in this
chapter ends with `return false`, by explicit contract in the headers. Your layer still sees the
scroll; framebuffers still see the resize.

**3 — mouse math is in ImGui SCREEN pixels.** All four rigs poll
`Input::GetMouseScreenPosition()` (OS virtual-desktop coordinates), which is the same space as
`WorkspaceLayer::GetViewportPos()` / `Application::GetViewportPos()`. It is **not**
`Input::GetMousePosition()`, which is window-client relative. Drag *deltas* agree in either space;
absolute positions do not, and zoom-to-cursor, orbit-about-cursor and
[picking](#scenepickerpick) are all absolute.

> **A stale doc comment to ignore.** `Application.h:93` describes `GetViewportPos`/`GetViewportSize`
> as "GLFW window-space pixels". That is wrong — the value is `ImGui::GetCursorScreenPos()` recorded
> by `WorkspaceLayer`, i.e. screen space. `WorkspaceLayer.h:271-278` states it correctly. Writing a
> pick or a pivot probe against the comment instead of the code is a bug the engine has already
> shipped once.

**4 — the host gate.** Each frame, in this order:

```cpp
rig.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());   // or OnResize(w, h)
rig.SetControlEnabled(vpHovered || rig.IsDragging());               // || IsLooking() for fly
rig.OnUpdate(ts);
```

`|| IsDragging()` is what lets a drag that started inside the viewport survive the cursor leaving
it. Disabling control mid-drag ends the drag.

**Which events each controller handles**

| Controller | `MouseScrolledEvent` | `WindowResizeEvent` | `SetViewportRect` |
| --- | --- | --- | --- |
| `Camera2DController` | ✅ zoom about cursor | **✖ — not handled** | ✅ required for pan + zoom-to-cursor |
| `OrthographicCameraController` | ✅ linear zoom | ✅ (no zero guard — see below) | ✖ (no such method) |
| `OrbitCameraController` | ✅ exponential zoom | ✅ guarded `> 0` | ✅ required for CAD nav |
| `FlyCameraController` | ✅ move-speed change | ✅ guarded `> 0` | optional (aspect only) |

---

## `Camera`

*Declared in `Cosmic/src/camera/Camera.h`. Both configurations.*

A pure interface: four getters, no data members, no stored matrices. It exists so renderer entry
points accept **any** camera — `Renderer2D::BeginScene(const Camera&)`,
`Renderer3D::BeginScene(const Camera&)` (`Renderer3D.h:118`), `RenderPass(const Camera&, ...)`
(`RenderPass.h:90`) and `Scene::OnRender3D(const Camera&)` (`Scene.h:238`) all call only these four
and nothing else. Both concrete cameras already cache the matrices; the getters expose the cache.

### `Camera::GetViewMatrix` / `GetProjectionMatrix` / `GetViewProjectionMatrix` / `GetPosition`

```cpp
virtual const glm::mat4& GetViewMatrix() const           = 0;
virtual const glm::mat4& GetProjectionMatrix() const     = 0;
virtual const glm::mat4& GetViewProjectionMatrix() const = 0;
virtual const glm::vec3& GetPosition() const             = 0;
```

**What it does** — returns, by `const&`, the derived camera's cached world→view matrix, projection
matrix, pre-multiplied `Projection * View` (the value shaders consume), and world-space position.
No call recomputes anything.

**Why you'd use it** — you implement them, you rarely call them. Deriving from `Camera` is the
supported way to drive a renderer from matrices you produced elsewhere: a scene `CameraComponent`,
a replay file, a physics-attached head. Both in-tree examples do exactly this
(`PlayerLayer::PlayerCamera`, `Starforge::PossessCamera`).

**Example**

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

**Notes & pitfalls**
- **Return `const&` to storage that outlives the call.** Returning a reference to a temporary is
  undefined behaviour and the interface gives you no chance to notice.
- **`GetViewProjectionMatrix()` must equal `Projection * View`.** Nothing verifies it; a subclass
  that forgets to refresh the cached product renders with a stale matrix and no diagnostic.
- The base is deliberately data-free: adding members would be an ABI change for every exported
  camera class.

---

## `OrthographicCamera`

*Declared in `Cosmic/src/camera/OrthographicCamera.h`. Both configurations. Derives from
[`Camera`](#camera).*

The 2D camera: a box frustum, no perspective divide. It is also the camera inside both 2D
controllers. State is `position` + `rotation` (Z axis, degrees); every mutation rebuilds the view
and the view-projection immediately.

The destructor is user-declared and empty — the class owns no resources.

### `OrthographicCamera::OrthographicCamera`

```cpp
OrthographicCamera(float left, float right, float bottom, float top);
```

**What it does** — builds `glm::ortho(left, right, bottom, top, -1.0f, 1.0f)` and starts the view
at identity (position `(0,0,0)`, rotation `0`).

**Why you'd use it** — direct construction is for code that owns its own projection math. If you
want pan/zoom behaviour, construct a [`Camera2DController`](#camera2dcontroller) instead and never
touch the camera directly.

**Example**

```cpp
Cosmic::OrthographicCamera cam(-1.6f, 1.6f, -0.9f, 0.9f);   // 16:9, 1.8 world units tall
Cosmic::Renderer2D::BeginScene(cam);
```

**Notes & pitfalls**
- **Nothing validates the bounds.** `left == right` produces a degenerate projection full of
  infinities and nothing warns.
- The default depth range is **−1…1**, which is why an unmodified `OrthographicCamera` clips
  anything more than one unit away in Z. Use the
  [depth-ranged overload](#orthographiccamerasetprojection-depth-ranged-overload) when Z matters.

### `OrthographicCamera::SetProjection`

```cpp
void SetProjection(float left, float right, float bottom, float top);
```

**What it does** — replaces the projection with `glm::ortho(left, right, bottom, top, -1.0f, 1.0f)`
and refreshes the cached view-projection. The view matrix is untouched.

**Why you'd use it** — resize handling for a hand-rolled 2D camera: recompute the half-extents from
the new aspect and call this. Both 2D controllers call it internally, so if you use one, you do not.

**Example**

```cpp
const float halfH = 1.0f, halfW = halfH * (width / height);
cam.SetProjection(-halfW, halfW, -halfH, halfH);
```

**Notes & pitfalls**
- **This overload always resets the depth range to −1…1**, even if you previously called the
  six-argument form. Mixing the two on one camera silently re-clips your scene.

### `OrthographicCamera::SetProjection` *(depth-ranged overload)*

```cpp
void SetProjection(float left, float right, float bottom, float top,
                   float nearZ, float farZ);
```

**What it does** — the same, with an explicit clip range: `glm::ortho(left, right, bottom, top,
nearZ, farZ)`.

**Why you'd use it** — when an orthographic view must see real world depth: sprites spread across
Z / `ZOrder`, or modest 2.5D props. This is the overload
[`Camera2DController`](#camera2dcontroller) uses, with `nearZ = -1000`, `farZ = 1000`.

**Example**

```cpp
cam.SetProjection(-halfW, halfW, -halfH, halfH, -1000.0f, 1000.0f);
cam.SetPosition({ focus.x, focus.y, 0.0f });   // camera sits in the middle of the Z band
```

**Notes & pitfalls**
- A **negative** `nearZ` is normal here and is what puts the camera in the middle of the depth
  band rather than at its front face.
- Larger world Z is nearer the viewer under this setup — the standard 2D convention.

### `OrthographicCamera::SetPosition` / `SetRotation`

```cpp
void SetPosition(const glm::vec3& position) { m_Position = position; UpdateViewMatrix(); }
void SetRotation(float rotation)            { m_Rotation = rotation; UpdateViewMatrix(); }
```

**What it does** — writes the value and immediately rebuilds the view and the view-projection.
`rotation` is degrees about the Z axis.

**Why you'd use it** — moving a hand-rolled 2D camera. Note that a controller's own position state
is authoritative: calling this on `controller.GetCamera()` is overwritten on the controller's next
`OnUpdate`.

**Notes & pitfalls**
- **Both are eager.** Setting position then rotation rebuilds the matrices twice. It is cheap —
  the view is built as `transpose(R) * T(-pos)`, valid because a rotation matrix's transpose is its
  inverse, about 8× cheaper than `glm::inverse` — but it is not free in a tight loop.
- No clamping of any kind. `SetPosition` on a controller's camera bypasses that controller's limits
  (see [`OrthographicCameraController::SetPositionLimits`](#orthographiccameracontrollersetpositionlimits)).

### `OrthographicCamera` — getters

```cpp
const glm::vec3& GetPosition() const override             { return m_Position; }
float            GetRotation() const                      { return m_Rotation; }
const glm::mat4& GetProjectionMatrix() const override      { return m_ProjectionMatrix; }
const glm::mat4& GetViewMatrix() const override            { return m_ViewMatrix; }
const glm::mat4& GetViewProjectionMatrix() const override  { return m_ViewProjectionMatrix; }
```

**What it does** — returns cached state. `GetRotation` is degrees about Z. The three matrix getters
satisfy the [`Camera`](#camera) interface.

**Notes & pitfalls** — all five are `O(1)` reads of members; there is no lazy recompute anywhere in
this class, so a getter never has a side effect.

---

## `PerspectiveCamera`

*Declared in `Cosmic/src/camera/PerspectiveCamera.h`. Both configurations. Derives from
[`Camera`](#camera).*

The 3D camera: pinhole projection (vertical FOV / aspect / near / far) plus a rigid transform stored
as position + quaternion. Frame convention throughout the engine is right-handed, **Y-up**, camera
looking down its local **−Z** (see `math/Spatial.h`; simulation code in NED converts through
`Math::NedToRender`).

### `PerspectiveCamera::PerspectiveCamera`

```cpp
PerspectiveCamera(float fovYDegrees = 45.0f, float aspect = 16.0f / 9.0f,
                  float nearClip = 0.1f, float farClip = 1000.0f);
```

**What it does** — stores the four projection parameters, builds the projection and the (identity)
view, and caches their product.

**Why you'd use it** — a camera you pose yourself with [`LookAt`](#perspectivecameralookat) each
frame, e.g. a chase camera or a fixed cinematic. For interactive navigation reach for
[`OrbitCameraController`](#orbitcameracontroller) or
[`FlyCameraController`](#flycameracontroller) — note that they choose **different far planes**
(orbit 1000, fly **5000**), so a hand-built camera is also where you decide that yourself.

**Notes & pitfalls**
- **No validation.** `aspect = 0`, `near = far` or a 180° FOV all produce a garbage projection
  silently. The only zero guard in the class is inside
  [`SetViewportSize`](#perspectivecamerasetviewportsize).

### `PerspectiveCamera::SetProjection`

```cpp
void SetProjection(float fovYDegrees, float aspect, float nearClip, float farClip);
```

**What it does** — replaces all four projection parameters, rebuilds
`glm::perspective(radians(fovY), aspect, near, far)` and refreshes the view-projection. The pose is
untouched.

**Why you'd use it** — a zoom (FOV) change, or pushing the far plane out for a large world. Prefer
[`SetViewportSize`](#perspectivecamerasetviewportsize) for aspect-only updates: it keeps the other
three values and guards against a zero-sized viewport.

**Notes & pitfalls**
- Unlike `SetViewportSize`, this call has **no zero guard**. Passing `aspect = 0` writes a
  degenerate projection.

### `PerspectiveCamera::SetViewportSize`

```cpp
void SetViewportSize(float width, float height);   // updates aspect only
```

**What it does** — sets `aspect = width / height` and rebuilds the projection. **Returns without
doing anything if either argument is `<= 0`** (`PerspectiveCamera.cpp:35`).

**Why you'd use it** — every viewport resize. The guard is the point: docking, minimising and tab
switches all produce transient 0×0 viewports, and this call keeps the previous aspect through them
instead of writing `inf` / `NaN` into the projection.

**Example**

```cpp
const glm::vec2 vp = Cosmic::Application::Get().GetViewportSize();
cam.SetViewportSize(vp.x, vp.y);   // safe even while the panel is collapsed
```

**Notes & pitfalls**
- **Failure is silent by design** — a rejected call logs nothing. That is correct here (it happens
  many times a second while docking), but it means "my aspect never updates" is diagnosed by
  checking the size you passed, not by looking for a warning.
- `OrbitCameraController::OnResize`, `FlyCameraController::OnResize` and both `SetViewportRect`
  implementations all funnel into this call, which is why they inherit the same guard.

### `PerspectiveCamera::LookAt`

```cpp
void LookAt(const glm::vec3& eye, const glm::vec3& target,
            const glm::vec3& up = { 0.0f, 1.0f, 0.0f });
```

**What it does** — places the camera at `eye` looking at `target`, derives the orientation
quaternion as the conjugate of `glm::quat_cast(glm::lookAt(...))`, and stores the `glm::lookAt`
result **directly** as the view matrix (reusing it rather than rebuilding from the quaternion, to
avoid a round of float error between the two representations).

**Why you'd use it** — the natural way to pose a 3D camera. Both 3D controllers rebuild their
camera through this call every frame.

**Example**

```cpp
Cosmic::PerspectiveCamera cam(60.0f, 16.0f / 9.0f, 0.1f, 2000.0f);
cam.LookAt({ 0.0f, 30.0f, 60.0f }, { 0.0f, 0.0f, 0.0f });
Cosmic::Renderer3D::BeginScene(cam);
```

**Notes & pitfalls**
- **Degenerate input is handled, loudly.** If `eye == target` (squared distance `< 1e-12`) the call
  logs `CS_CORE_WARN("PerspectiveCamera::LookAt: eye == target — orientation unchanged.")`, sets the
  position, refreshes the view from the *existing* orientation, and returns. It does not throw and
  does not produce `NaN`.
- **`up` parallel to `target - eye` is NOT handled.** `glm::lookAt` degenerates and you get a
  `NaN` view with no warning. This is why both controllers clamp pitch to ±89°.
- `up` is a hint, not a stored value: only the resulting orientation is kept.

### `PerspectiveCamera::SetPosition` / `SetOrientation`

```cpp
void SetPosition(const glm::vec3& position)    { m_Position = position; UpdateViewMatrix(); }
void SetOrientation(const glm::quat& orientation) { m_Orientation = orientation; UpdateViewMatrix(); }
```

**What it does** — writes the value and rebuilds the view as `mat4_cast(conjugate(orientation)) *
translate(-position)` — the closed-form inverse of a rigid transform, not a general 4×4 inverse.

**Why you'd use it** — driving the camera from something that already produces a quaternion (a
physics body, an IMU, an animation). `LookAt` is easier when you have a target point.

**Notes & pitfalls**
- **The quaternion is not normalised for you.** The header says "Pre: Orientation must be (close
  to) unit length" and nothing enforces it; a drifting quaternion scales the view matrix and shears
  the render. Normalise before you set.
- Storage order is `glm::quat{w, x, y, z}`, matching `TransformComponent::RotationQuat`.

### `PerspectiveCamera::GetForward` / `GetRight` / `GetUp`

```cpp
glm::vec3 GetForward() const;
glm::vec3 GetRight() const;
glm::vec3 GetUp() const;
```

**What it does** — the camera's world-space basis, unit length: `orientation * (0,0,-1)`,
`orientation * (1,0,0)`, `orientation * (0,1,0)`.

**Why you'd use it** — camera-relative movement and panning. `OrbitCameraController`'s pan uses
`GetRight()` / `GetUp()`; a cursor ray uses `GetForward()` as the plane normal.

**Notes & pitfalls**
- **`GetForward()` is the LOOK direction (local −Z), not the +Z axis.** This is the single most
  common sign error when porting camera code from a +Z-forward engine.
- These are computed per call (a quaternion-vector rotation each), not cached. Hoist them out of
  inner loops.

### `PerspectiveCamera` — projection and pose getters

```cpp
float            GetFovY() const                           { return m_FovYDegrees; }
float            GetAspect() const                         { return m_Aspect; }
float            GetNearClip() const                       { return m_NearClip; }
float            GetFarClip() const                        { return m_FarClip; }
const glm::vec3& GetPosition() const override              { return m_Position; }
const glm::quat& GetOrientation() const                    { return m_Orientation; }
const glm::mat4& GetProjectionMatrix() const override      { return m_ProjectionMatrix; }
const glm::mat4& GetViewMatrix() const override            { return m_ViewMatrix; }
const glm::mat4& GetViewProjectionMatrix() const override  { return m_ViewProjectionMatrix; }
```

**What it does** — cached reads. `GetFovY()` is **degrees** (it is what
[`FrameSphere`](#orbitcameracontrollerframesphere) reads to compute a fit distance).

---

## `Camera2DController`

*Declared in `Cosmic/src/camera/Camera2DController.h`. Both configurations. Owns an
[`OrthographicCamera`](#orthographiccamera).*

The modern 2D rig: MMB drag pans, scroll zooms about the cursor, no keyboard bindings at all. Two
conventions do all the work:

- **`Focus`** — the world XY point at the centre of the view.
- **`Zoom`** — the visible **half-height in world units**. Smaller is closer in. Default `5.0`,
  clamped to **[0.01, 10000]**.

Every recalculation writes `SetProjection(-zoom*aspect, +zoom*aspect, -zoom, +zoom, -1000, +1000)`
and parks the camera at `(Focus.x, Focus.y, 0)`.

> **The zoom limits and the scroll speed are private with no setters** (`m_MinZoom = 0.01f`,
> `m_MaxZoom = 10000.0f`, `m_ZoomSpeed = 1.0f`). Unlike
> [`OrthographicCameraController`](#orthographiccameracontroller), this rig is not tunable. If you
> need different limits, clamp `SetZoom` yourself on the way in.

### `Camera2DController::Camera2DController`

```cpp
Camera2DController(float aspectRatio);
```

**What it does** — stores the aspect (**substituting `1.0f` if `aspectRatio <= 0`**) and builds the
initial projection at focus `(0,0)`, zoom `5.0`.

**Example**

```cpp
Cosmic::Camera2DController cam { 16.0f / 9.0f };
```

### `Camera2DController::OnUpdate`

```cpp
void OnUpdate(float ts);
```

**What it does** — polls the middle mouse button. On the frame the drag starts it latches the
cursor position and produces **no** motion; on subsequent frames it applies
[`PanBy`](#camera2dcontrollerpanby) with the per-frame delta and rebuilds the projection. The `ts`
argument is **ignored** (`(void)ts;` — panning is delta-driven, not rate-driven).

**Why you'd use it** — once per frame, after `SetViewportRect` and `SetControlEnabled`. See
[the controller contract](#the-controller-contract).

**Notes & pitfalls**
- **Panning silently does nothing until you call
  [`SetViewportRect`](#camera2dcontrollersetviewportrect).** `PanBy` needs the viewport *height in
  pixels*, which lives only in the rect; `OnResize` does not set it. With the rect unset the height
  is `0`, `PanBy` returns the focus unchanged, and MMB drag appears dead while zoom still works
  (centred, not about the cursor). This is the most common "the 2D camera won't pan" report.
- The latch is what stops the camera jumping by however far the cursor travelled since the previous
  drag ended.

### `Camera2DController::OnEvent`

```cpp
void OnEvent(Event& e);
```

**What it does** — dispatches **`MouseScrolledEvent` only**. Each notch multiplies the zoom by
`1.15^(-yOffset)` and then moves the focus with
[`ZoomAboutPoint`](#camera2dcontrollerzoomaboutpoint) so the world point under the cursor stays
pinned. Returns nothing; the internal handler returns `false` (never consumes).

**Notes & pitfalls**
- **`WindowResizeEvent` is deliberately NOT handled** — the only controller of the four that
  ignores it. A 2D rig usually lives in a docked panel whose size has nothing to do with the
  window's, so keeping the aspect current is the host's job via `OnResize` / `SetViewportRect`.
- Scroll is a no-op while `IsControlEnabled()` is false, and also when the new zoom equals the old
  (i.e. already at a clamp limit) — in that case the focus is not touched either.
- The cursor anchor is applied only when the stored viewport size is non-zero; otherwise the zoom
  still happens, just about the view centre.

### `Camera2DController::OnResize`

```cpp
void OnResize(float width, float height);
```

**What it does** — sets `aspect = width / height` and rebuilds. **Returns without doing anything if
either argument is `<= 0`.**

**Notes & pitfalls**
- It updates the **aspect only**. It does not record the viewport rect, so it does not enable
  panning or zoom-to-cursor. Prefer `SetViewportRect`, which is a strict superset.

### `Camera2DController::SetViewportRect`

```cpp
void SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);
```

**What it does** — stores the viewport origin and size in **ImGui screen pixels**, then calls
`OnResize(sizePx.x, sizePx.y)`.

**Why you'd use it** — every frame. It is the only way to enable pan and zoom-to-cursor, and it
keeps the aspect current at the same time.

**Example**

```cpp
auto& app = Cosmic::Application::Get();
cam.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());
```

**Notes & pitfalls**
- Rule 3 of [the controller contract](#the-controller-contract) applies: this must be the same
  space as `Input::GetMouseScreenPosition()`, which `Application::GetViewportPos()` is (despite its
  doc comment).
- A zero `sizePx` is stored as-is; only the `OnResize` half is guarded. Pan then goes dead until a
  real size arrives, which is the correct behaviour for a collapsed panel.

### `Camera2DController::SetControlEnabled` / `IsControlEnabled` / `IsDragging`

```cpp
void SetControlEnabled(bool enabled);
bool IsControlEnabled() const { return m_ControlEnabled; }
bool IsDragging() const       { return m_Dragging; }
```

**What it does** — master gate for MMB pan and scroll zoom. **Disabling ends any in-progress drag**
immediately. `IsDragging()` is true only while an MMB pan is live.

**Why you'd use it** — `SetControlEnabled(vpHovered || cam.IsDragging())` is the standard host gate;
in an editor also `&& !Gizmo::IsUsing() && !Gizmo::IsOver()`.

### `Camera2DController::SetFocus` / `GetFocus`

```cpp
void             SetFocus(const glm::vec2& xy) { m_Focus = xy; Recalculate(); }
const glm::vec2& GetFocus() const              { return m_Focus; }
```

**What it does** — the view centre in world XY. Setting it rebuilds immediately. **No clamping** —
this rig has no position limits.

### `Camera2DController::SetZoom` / `GetZoom` / `GetAspect`

```cpp
void  SetZoom(float halfHeight);
float GetZoom() const   { return m_Zoom; }
float GetAspect() const { return m_Aspect; }
```

**What it does** — sets the visible half-height in world units, **clamped to [0.01, 10000]**, and
rebuilds. There is no animation: the change is instant.

**Example**

```cpp
cam.SetZoom(6.0f);   // 12 world units of visible height, whatever the aspect
```

**Notes & pitfalls**
- Out-of-range values are clamped, not rejected — `GetZoom()` after `SetZoom(0.0f)` reads `0.01`.

### `Camera2DController::VisibleRect`

```cpp
void VisibleRect(glm::vec2& outMin, glm::vec2& outMax) const;
```

**What it does** — writes the world-space XY rectangle currently on screen:
`focus ± (zoom*aspect, zoom)`.

**Why you'd use it** — grid drawing, culling, "which tiles do I need to stream?". It is a pure
query — no GL, no side effects.

**Example**

```cpp
glm::vec2 mn, mx;
cam.VisibleRect(mn, mx);
for (int y = (int)std::floor(mn.y); y <= (int)std::ceil(mx.y); ++y)
    for (int x = (int)std::floor(mn.x); x <= (int)std::ceil(mx.x); ++x)
        DrawTile(x, y);
```

### `Camera2DController::FrameBounds`

```cpp
void FrameBounds(const glm::vec2& worldMin, const glm::vec2& worldMax);
```

**What it does** — recentres the focus on the box's midpoint and sets the zoom to fit it with about
10 % padding: `zoom = clamp(max(sizeY/2, (sizeX/2)/aspect) * 1.1, 0.01, 10000)`. Instant, not
animated.

**Why you'd use it** — the 2D half of an "F frames the selection" hotkey.
[`OrbitCameraController::FrameBounds`](#orbitcameracontrollerframebounds) is the 3D twin (and *is*
animated).

**Notes & pitfalls**
- **A degenerate box still recentres.** If both extents are `<= 1e-6` the focus moves to the box
  and the zoom is left alone — it does not zoom to a point and it does not fail.

### `Camera2DController::ScreenToWorld`

```cpp
static glm::vec2 ScreenToWorld(const glm::vec2& screenPx,
                               const glm::vec2& vpPosPx, const glm::vec2& vpSizePx,
                               const glm::vec2& focus, float zoomHalfHeight);
```

**What it does** — maps an ImGui screen pixel to world XY for the given focus/zoom/viewport:
`unitsPerPx = 2*zoom / vpSize.y`, then offsets from the viewport centre, **negating Y** because
screen Y grows downward.

**Why you'd use it** — "which tile / sprite did the user click?" in a 2D viewport. It is `static`
and pure: no controller instance needed, no GL, unit-tested in
`tests/test_camera2d.cpp` ("U3: ScreenToWorld — viewport center maps to focus, +y screen is -y
world"). It is also the entire camera surface a 2D rect-pick needs, which is how Starforge picks
sprites in a 2D build where [`ScenePicker`](#scenepicker-3d-only) does not exist.

**Example**

```cpp
const glm::vec2 world = Cosmic::Camera2DController::ScreenToWorld(
    Cosmic::Input::GetMouseScreenPosition(),
    app.GetViewportPos(), app.GetViewportSize(),
    cam.GetFocus(), cam.GetZoom());
```

**Notes & pitfalls**
- **Returns `focus` unchanged when `vpSizePx.y <= 0`** — a plausible-looking answer for a degenerate
  input, so guard the viewport size yourself if the distinction matters.
- Pass `Input::GetMouseScreenPosition()`, never `GetMousePosition()` (rule 3).

### `Camera2DController::PanBy`

```cpp
static glm::vec2 PanBy(const glm::vec2& focus, const glm::vec2& deltaPx,
                       float zoomHalfHeight, float viewportHeightPx);
```

**What it does** — returns the new focus for a pixel drag: `focus.x - dx*unitsPerPx`,
`focus.y + dy*unitsPerPx`. The signs are what make the world follow the cursor (drag right → the
world moves right → the focus moves left).

**Notes & pitfalls**
- **Returns `focus` unchanged when `viewportHeightPx <= 0`.** This is exactly the path that makes
  pan look dead when `SetViewportRect` was never called.

### `Camera2DController::ZoomAboutPoint`

```cpp
static glm::vec2 ZoomAboutPoint(const glm::vec2& focus, const glm::vec2& worldAnchor,
                                float zoomBefore, float zoomAfter);
```

**What it does** — returns the focus that keeps `worldAnchor` at the same screen position across a
zoom change: `anchor + (focus - anchor) * (after / before)`.

**Why you'd use it** — implementing zoom-to-cursor over a different input (a pinch gesture, a zoom
slider, a scripted move). The scroll handler uses it with the anchor from `ScreenToWorld`.

**Notes & pitfalls**
- **Returns `focus` unchanged when `zoomBefore <= 0`.**
- Order matters: compute the anchor with the *old* zoom, then call this with both zooms.

---

## `OrthographicCameraController`

*Declared in `Cosmic/src/camera/OrthographicCameraController.h`. Both configurations. Owns an
[`OrthographicCamera`](#orthographiccamera).*

The original Phase-1 2D rig: WASD pans, scroll zooms, optional Q/E roll. Its zoom is a *scale
factor* (`1.0` = the constructor's frustum), not a half-height — the opposite convention from
[`Camera2DController`](#camera2dcontroller), and the two are not interchangeable.

Defaults: zoom `1.0` in **[0.25, 10]**, zoom speed `0.25`, translation speed `5.0`, rotation speed
`180 °/s`, position limits `±1000` on X and Y, manual movement **on**.

> **`OrthographicCameraController::OnWindowResized` has no zero-height guard.** It calls
> `OnResize(w, h)` unconditionally and `OnResize` divides, so a minimise — a 0×0 `WindowResizeEvent`,
> which `Application::OnWindowResize` does **not** consume — leaves the aspect `inf` or `NaN` until
> the next real resize. [`OrbitCameraController`](#orbitcameracontroller) and
> [`FlyCameraController`](#flycameracontroller) both guard with `if (e.GetHeight() > 0)`; this one
> does not. It is a one-line fix and a **Phase 30 candidate**
> (`OrthographicCameraController.cpp:118-122`). For new 2D work prefer `Camera2DController`.

### `OrthographicCameraController::CameraKeyBindings`

```cpp
struct CameraKeyBindings
{
    uint32_t MoveLeft  = CS_KEY_A;
    uint32_t MoveRight = CS_KEY_D;
    uint32_t MoveUp    = CS_KEY_W;
    uint32_t MoveDown  = CS_KEY_S;
    uint32_t RotateQ   = CS_KEY_Q;
    uint32_t RotateE   = CS_KEY_E;
};
```

**What it does** — the remappable key layout. **A binding of `0` disables that action** (each poll
is guarded by `binding != 0`).

**Why you'd use it** — arrow-key panning, a left-handed layout, or disabling an axis outright. This
is the only controller in the chapter with a rebinding surface; the orbit and fly rigs hard-code
their buttons.

**Example**

```cpp
Cosmic::OrthographicCameraController::CameraKeyBindings b;
b.MoveLeft  = CS_KEY_LEFT;   b.MoveRight = CS_KEY_RIGHT;
b.MoveUp    = CS_KEY_UP;     b.MoveDown  = CS_KEY_DOWN;
b.RotateQ   = 0;             b.RotateE   = 0;      // no roll
m_Cam.SetKeyBindings(b);
```

### `OrthographicCameraController::OrthographicCameraController`

```cpp
OrthographicCameraController(float aspectRatio, bool rotation = false);
```

**What it does** — builds the inner camera at `(-aspect*zoom, +aspect*zoom, -zoom, +zoom)` with
`zoom = 1.0`, and records whether Q/E roll is allowed.

**Notes & pitfalls**
- **Rotation is opt-in.** With the default `rotation = false`, `RotateQ`/`RotateE` are polled but
  never applied — "Q/E don't rotate my camera" is almost always this.
- `aspectRatio` is not validated.

### `OrthographicCameraController::OnUpdate`

```cpp
void OnUpdate(float ts);
```

**What it does**, in order: (1) blends the current zoom toward the target with
`blend = clamp(10 * ts, 0, 1)` — an asymptotic approach, snapped to the target once within `0.001`;
(2) if manual movement is enabled, polls the four movement keys, **scaling speed by the current
zoom level** (`speed * zoom`) so panning feels the same at any magnification, clamps the position
into the configured limits, and applies Q/E roll when rotation was enabled; (3) writes the position
into the camera.

**Notes & pitfalls**
- **Left/right and up/down are `else if` pairs**: holding A and D together moves left, not nowhere.
- The **position clamp lives inside the manual-movement block**, so it only ever applies to keyboard
  panning — see [`SetPositionLimits`](#orthographiccameracontrollersetpositionlimits).
- `CalculateView()` runs only on the frames the zoom actually changes; the camera position is
  written every frame regardless.

### `OrthographicCameraController::OnEvent` / `OnResize`

```cpp
void OnEvent(Event& e);
void OnResize(float width, float height);
```

**What it does** — `OnEvent` dispatches `MouseScrolledEvent` (zoom) and `WindowResizeEvent`
(aspect); both handlers return `false`. `OnResize` sets `aspect = width / height` and rebuilds the
projection.

**Notes & pitfalls**
- **Zoom here is LINEAR and exponential everywhere else.** The handler is
  `target -= yOffset * zoomSpeed` (`OrthographicCameraController.cpp:113`), while the 2D, orbit and
  fly rigs all multiply by `1.15^-notch`. Scrolling out from a wide view feels sluggish; scrolling
  in near the minimum feels violent.
- **`OnResize` divides without a guard** — see the boxed warning above. Do not call it with a zero
  height.
- Scroll is handled **even while manual movement is disabled**; `SetManualMovementEnabled(false)`
  does not gate zoom.

### `OrthographicCameraController::SetZoomLevel`

```cpp
void SetZoomLevel(float level);
```

**What it does** — clamps `level` into the zoom limits, writes it to **both** the current and the
target zoom, and rebuilds the projection immediately. A hard snap with no interpolation.

**Why you'd use it** — restoring a saved view, or jumping to a known magnification. Use
[`SetTargetZoomLevel`](#orthographiccameracontrollersettargetzoomlevel) when you want the blend.

### `OrthographicCameraController::SetTargetZoomLevel`

```cpp
void SetTargetZoomLevel(float level);
```

**What it does** — clamps and writes **only** the target. The current zoom is left alone so
`OnUpdate`'s asymptotic blend animates toward it over the next frames. Does not rebuild the
projection — that happens on the next tick.

**Notes & pitfalls**
- If you never call `OnUpdate` (a paused layer, a headless test), nothing moves. That is by design;
  the header calls it out explicitly.

### `OrthographicCameraController::SetPositionLimits`

```cpp
void SetPositionLimits(float minX, float maxX, float minY, float maxY)
{
    m_MinX = minX; m_MaxX = maxX;
    m_MinY = minY; m_MaxY = maxY;
}
```

**What it does** — sets the box the keyboard pan is clamped into (defaults ±1000 on both axes).

**Notes & pitfalls**
- **Only keyboard panning enforces this.**
  [`SetPosition`](#orthographiccameracontrollersetposition--getposition) writes straight through
  unclamped, and so does `GetCamera().SetPosition(...)`. Setting limits does not retroactively pull
  an out-of-bounds camera back either — the next keyboard frame does that.

### `OrthographicCameraController::SetPosition` / `GetPosition`

```cpp
void             SetPosition(const glm::vec3& position);
const glm::vec3& GetPosition() const { return m_CameraPosition; }
```

**What it does** — writes the controller's position state and pushes it into the camera
immediately. **Not clamped** to the position limits.

**Why you'd use it** — teleporting the view, or restoring a saved camera. For a scripted move, pair
it with `SetManualMovementEnabled(false)` so the player's keys do not fight you.

### `OrthographicCameraController::SetManualMovementEnabled` / `IsManualMovementEnabled`

```cpp
void SetManualMovementEnabled(bool enabled) { m_ManualMovementEnabled = enabled; }
bool IsManualMovementEnabled() const        { return m_ManualMovementEnabled; }
```

**What it does** — gates the keyboard pan/roll block in `OnUpdate`. Zoom (scroll and blend) is
**not** gated.

**Why you'd use it** — cutscenes and scripted camera tracks. This is the closest thing this rig has
to the other controllers' `SetControlEnabled`; note it does **not** stop the mouse wheel.

### `OrthographicCameraController` — tuning and access

```cpp
void  SetTranslationSpeed(float speed) { m_CameraTranslationSpeed = speed; }
float GetTranslationSpeed() const      { return m_CameraTranslationSpeed; }
void  SetRotationSpeed(float speed)    { m_CameraRotationSpeed = speed; }
float GetRotationSpeed() const         { return m_CameraRotationSpeed; }
void  SetZoomSpeed(float speed)        { m_ZoomSpeed = speed; }
float GetZoomSpeed() const             { return m_ZoomSpeed; }
void  SetZoomLimits(float min, float max) { m_MinZoom = min; m_MaxZoom = max; }
float GetZoomLevel() const             { return m_ZoomLevel; }

void                     SetKeyBindings(const CameraKeyBindings& bindings) { m_Bindings = bindings; }
const CameraKeyBindings& GetKeyBindings() const { return m_Bindings; }
CameraKeyBindings&       GetKeyBindings()       { return m_Bindings; }

OrthographicCamera&       GetCamera()       { return m_Camera; }
const OrthographicCamera& GetCamera() const { return m_Camera; }
```

**What it does** — plain accessors. `SetTranslationSpeed` is world units per second **before** the
`× zoom` scaling; `SetRotationSpeed` is degrees per second; `SetZoomSpeed` is zoom units per scroll
notch (linear).

**Notes & pitfalls**
- **`SetZoomLimits` does not re-clamp the current or target zoom.** Narrowing the range below the
  live value leaves it out of range until the next `SetZoomLevel` / scroll.
- The non-const `GetKeyBindings()` lets you mutate one field in place; the header offers both
  overloads deliberately.
- `GetCamera()` is what you hand to `Renderer2D::BeginScene`.

---

## `NavStyle`

```cpp
enum class NavStyle { Classic, CAD };
```

*Declared in `Cosmic/src/camera/OrbitCameraController.h`. Both configurations.*

**What it does** — selects [`OrbitCameraController`](#orbitcameracontroller)'s mouse-binding scheme.
`Classic` (the default) is LMB orbit / RMB pan / scroll zoom to centre. `CAD` is the SolidWorks
feel: MMB orbit / Ctrl+MMB pan / Shift+MMB dolly / scroll zoom toward the cursor, with **LMB left
free** for click-to-select.

**Why you'd use it** — any editor viewport wants `CAD`, because it needs LMB for picking. A viewer
or a game camera can stay `Classic`. The full gesture table is in
[Binding tables](#orbit-gestures-by-navstyle).

**Notes & pitfalls**
- Existing apps are unaffected until they call `SetNavigationStyle` — the default is `Classic` on
  purpose.
- CAD orbit pivots about the point under the cursor, which is what makes the mode feel different;
  it needs [`SetViewportRect`](#orbitcameracontrollersetviewportrect) and, ideally, a
  [`PivotProbe`](#orbitcameracontrollersetpivotprobe).

## `ViewPreset`

```cpp
enum class ViewPreset { Front, Back, Left, Right, Top, Bottom, Iso };
```

*Declared in `Cosmic/src/camera/OrbitCameraController.h`. Both configurations.*

**What it does** — the seven standard orientations shared by
[`OrbitCameraController::SnapView`](#orbitcameracontrollersnapview) and
[`NavigationCube::PickFace`](#navigationcubepickface). Angles are in the
[preset table](#viewpreset-angles); `Iso` is the default 3/4 view.

**Notes & pitfalls**
- `Top` / `Bottom` are **±89°, not ±90°** — shy of the pole, so `glm::lookAt`'s up vector never
  degenerates.
- The enum lives in the orbit controller's header even though `NavigationCube` also returns it;
  that is why `NavigationCube.h` includes `OrbitCameraController.h`, and part of why it still
  compiles in a 2D build.

---

## `OrbitCameraController`

*Declared in `Cosmic/src/camera/OrbitCameraController.h`. Both configurations. Owns a
[`PerspectiveCamera`](#perspectivecamera) built as `(45°, aspect, 0.1, 1000)`.*

The editor camera: the eye rides a spherical mount around a `Target`, parameterised by `Distance`,
`Yaw` and `Pitch` (degrees). Yaw 0 / pitch 0 puts the camera on the target's **+Z** side looking
−Z; positive pitch raises it.

Defaults: target `(0,0,0)`, distance `10` in **[0.5, 500]**, yaw `45°`, pitch `30°` in **[−89, +89]**,
orbit speed `0.25 °/px`, pan speed `1.0`, zoom speed `1.0`, control **enabled**, inertia **off**,
nav style **`Classic`**.

### `OrbitCameraController::PivotProbe`

```cpp
using PivotProbe = std::function<bool(const glm::vec2& screenMouse, glm::vec3& outWorld)>;
```

**What it does** — the app-supplied callback that answers "what world point is under the cursor?".
`screenMouse` arrives in **ImGui screen pixels** (the same space as `GetViewportPos()`); write the
hit to `outWorld` and return `true`, or return `false` on a miss.

**Why you'd use it** — CAD orbit-about-cursor and CAD zoom-to-cursor call it. Without one, the
controller falls back to intersecting the cursor ray with the plane through the target, which is
good enough when geometry sits near the target — and, importantly, needs no
[`ScenePicker`](#scenepicker-3d-only), so **CAD navigation works in a 2D build**.

**Example** *(the Engine3DDemo probe, `Engine3DDemo.cpp:258-267`)*

```cpp
m_Orbit.SetPivotProbe([this](const glm::vec2& screenMouse, glm::vec3& out) -> bool
{
    if (!m_EditorMode || !m_Picker)
        return false;
    auto& app = Cosmic::Application::Get();
    const glm::vec2 vpPos = app.GetViewportPos();
    const int px = static_cast<int>(screenMouse.x - vpPos.x);
    const int py = static_cast<int>(screenMouse.y - vpPos.y);
    return m_Picker->WorldPoint(m_Orbit.GetCamera(), px, py, out);
});
```

**Notes & pitfalls**
- Both spaces are ImGui screen pixels, so the subtraction above is a straight remap — **do not**
  use `Input::GetMousePosition()` here.
- The probe is invoked only on the frame an orbit drag *begins*, and on every CAD scroll. A stale
  ID pass gives a stale pivot; Engine3DDemo renders its picker FBO every frame in editor mode, while
  Starforge renders one *inside* the probe.

### `OrbitCameraController::OrbitCameraController`

```cpp
OrbitCameraController(float aspectRatio);
```

**What it does** — builds the inner `PerspectiveCamera(45.0f, aspectRatio, 0.1f, 1000.0f)` and poses
it from the default mount.

**Notes & pitfalls**
- **The far plane is 1000.** For a larger world either reach for
  [`FlyCameraController`](#flycameracontroller) (far 5000) or call
  `GetCamera().SetProjection(...)` after construction — the controller never rewrites the near/far,
  only the aspect, so your override survives.

### `OrbitCameraController::OnUpdate`

```cpp
void OnUpdate(float ts);
```

**What it does**, in order:

1. Samples `Input::GetMouseScreenPosition()`.
2. If control is enabled, resolves which gesture the current buttons+modifiers mean for the active
   [`NavStyle`](#navstyle).
3. **First frame of a drag:** latches the gesture and the cursor position, zeroes the inertia
   velocity, and — for an orbit — latches the pivot via
   [`BeginOrbitAbout`](#orbitcameracontrollerbeginorbitabout) (CAD: the point under the cursor;
   Classic: the current target). **No motion is produced on this frame**, so the rendered view is
   bit-identical on press: no look-at snap.
4. **Subsequent frames:** orbit applies [`OrbitBy`](#orbitcameracontrollerorbitby)
   (`dYaw = -delta.x * orbitSpeed`, `dPitch = +delta.y * orbitSpeed`); pan translates the target in
   the camera right/up plane by `distance * 0.0015 * panSpeed` per pixel; dolly multiplies the
   distance by `1.01^(delta.y * zoomSpeed)` and applies it **immediately** (no blend).
5. Any live drag cancels an in-progress [`SnapView`](#orbitcameracontrollersnapview) /
   `Frame*` blend.
6. Otherwise: advances a pose blend at `1 - exp(-ts * 12)` (yaw along the short arc), or the plain
   zoom blend at `1 - exp(-ts * 10)`, plus optional inertia drift.
7. Rebuilds the camera with `LookAt`.

**Notes & pitfalls**
- **The gesture is latched on press**, so changing modifiers mid-drag does not switch modes.
- **`Ctrl` beats `Shift`.** The predicates are `orbit = mmb && !ctrl && !shift`,
  `pan = mmb && ctrl` (shift irrelevant), `dolly = mmb && shift && !ctrl`, and the mode is picked
  orbit → pan → dolly, so **Ctrl+Shift+MMB pans**.
- Both Ctrl and both Shift keys count (left or right) — unlike
  [`FlyCameraController`](#flycameracontroller), which reads left Shift only.
- Disabling control clears the drag state; the mouse position is still sampled every frame, so
  re-enabling never produces a jump.
- The blends are frame-rate independent (~63 % of the remaining error closed per 1/12 s and 1/10 s
  respectively).

### `OrbitCameraController::OnEvent` / `OnResize`

```cpp
void OnEvent(Event& e);
void OnResize(float width, float height);
```

**What it does** — `OnEvent` dispatches `MouseScrolledEvent` and `WindowResizeEvent`; both handlers
return `false`. Scroll computes `factor = 1.15^(-yOffset * zoomSpeed)` and then diverges by style:

| Style | Scroll behaviour |
| --- | --- |
| `Classic` | `targetDistance = clamp(targetDistance * factor)` — the smooth blend in `OnUpdate` animates there |
| `CAD` | computes `newDist = clamp(distance * factor)`, moves the target so the world point under the cursor stays pinned (`target = pivot + (target - pivot) * ratio`), and **snaps** both distances so rapid scrolls stay anchored |

`OnResize` forwards to `PerspectiveCamera::SetViewportSize` (aspect only, with its zero guard).

**Notes & pitfalls**
- **Scroll is ignored entirely while `IsControlEnabled()` is false** — including the animation
  cancel.
- **A scroll cancels any in-progress snap/frame blend** (`m_Animating = false`) before anything
  else.
- In CAD mode, if the pivot cannot be computed (no probe hit and the ray fallback degenerates) the
  distance still changes — you get a plain centre zoom rather than nothing.
- `OnWindowResized` is guarded: `if (e.GetHeight() > 0)`. A minimise is safe here.

### `OrbitCameraController::SetViewportRect`

```cpp
void SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);
```

**What it does** — stores the viewport rectangle in **ImGui screen pixels** and, when both extents
are `> 0`, updates the projection aspect. A strict superset of `OnResize`.

**Why you'd use it** — **required** for zoom-to-cursor and for the ray/target-plane pivot fallback:
`CursorRay` returns `false` when the stored size is zero, and `ComputeCursorPivot` then fails, so
CAD navigation silently degrades to centre-zoom and target-pivot. An app that never uses CAD nav can
keep calling `OnResize`.

**Example**

```cpp
auto& app = Cosmic::Application::Get();
m_Orbit.SetViewportRect(app.GetViewportPos(), app.GetViewportSize());
```

### `OrbitCameraController::SetNavigationStyle` / `GetNavigationStyle`

```cpp
void     SetNavigationStyle(NavStyle style) { m_NavStyle = style; }
NavStyle GetNavigationStyle() const         { return m_NavStyle; }
```

**What it does** — switches binding schemes at runtime. See [`NavStyle`](#navstyle) and the
[gesture table](#orbit-gestures-by-navstyle). Existing apps stay `Classic` unless they opt in.

**Notes & pitfalls**
- Switching style mid-drag does not change the in-progress gesture (it is latched), but it does
  change what the *next* press means.

### `OrbitCameraController::SetPivotProbe`

```cpp
void SetPivotProbe(PivotProbe probe) { m_PivotProbe = std::move(probe); }
```

**What it does** — installs the cursor-pivot probe used by CAD orbit-about-cursor and CAD
zoom-to-cursor. Passing a default-constructed `PivotProbe` clears it and restores the ray/plane
fallback.

**Why you'd use it** — to orbit and zoom about the *surface* under the cursor rather than a plane
through the target. See [`PivotProbe`](#orbitcameracontrollerpivotprobe) for the contract and the
worked example.

**Notes & pitfalls**
- The probe is stored by value in a `std::function`; captured pointers must outlive the controller.
- It is invoked only on the frame an orbit drag begins and on every CAD scroll — never per frame.

### `OrbitCameraController::SetTarget` / `GetTarget`

```cpp
void             SetTarget(const glm::vec3& target) { m_Target = target; RecalculateCamera(); }
const glm::vec3& GetTarget() const                  { return m_Target; }
```

**What it does** — moves the point the camera looks at, keeping yaw/pitch/distance, and rebuilds
immediately.

**Notes & pitfalls**
- Unlike [`SetDistance`](#orbitcameracontrollersetdistance--settargetdistance--getdistance) and
  [`SetYawPitch`](#orbitcameracontrollersetyawpitch--getyaw--getpitch), this **does not cancel an
  in-progress pose blend** — an animating `SnapView` will drag the target back toward its own goal
  on the next tick. Call `SnapView(..., false)` or `SetYawPitch` first if you need the write to
  stick.

### `OrbitCameraController::SetDistance` / `SetTargetDistance` / `GetDistance`

```cpp
void  SetDistance(float distance);
void  SetTargetDistance(float distance);
float GetDistance() const { return m_Distance; }
```

**What it does** — `SetDistance` **cancels any pose blend**, clamps into the distance limits, writes
both the current and target distance, and rebuilds: a hard snap. `SetTargetDistance` clamps and
writes only the target, letting `OnUpdate`'s blend animate there (and does **not** cancel a pose
blend — while one is running the blend overwrites the target distance each tick).

**Why you'd use it** — `SetDistance` for restoring a saved view or a camera bookmark;
`SetTargetDistance` for a smooth programmatic dolly.

### `OrbitCameraController::SetYawPitch` / `GetYaw` / `GetPitch`

```cpp
void  SetYawPitch(float yawDeg, float pitchDeg);
float GetYaw() const   { return m_YawDeg; }
float GetPitch() const { return m_PitchDeg; }
```

**What it does** — **cancels any pose blend**, writes the yaw as given (it wraps freely, no
normalisation), clamps the pitch into the pitch limits, and rebuilds.

**Why you'd use it** — camera bookmarks. The rig's entire pose is four values: `GetYaw()`,
`GetPitch()`, `GetDistance()`, `GetTarget()` — store those, restore with `SetYawPitch` +
`SetDistance` + `SetTarget`.

**Notes & pitfalls**
- Because it cancels animation, calling it **every frame** (e.g. an auto-orbit drift) permanently
  suppresses `SnapView`. Engine3DDemo guards its auto-orbit with `!m_Orbit.IsAnimating()`
  (`Engine3DDemo.cpp:884`) for exactly this reason.

### `OrbitCameraController::BeginOrbitAbout`

```cpp
void BeginOrbitAbout(const glm::vec3& pivot) { m_OrbitPivot = pivot; }
```

**What it does** — latches the world point that subsequent [`OrbitBy`](#orbitcameracontrollerorbitby)
calls rotate the whole rig about. **It does not touch the rendered view** — the camera pose is
unchanged until the first `OrbitBy`, which is what makes an MMB press produce zero motion.

**Why you'd use it** — a scripted camera move or a headless test that wants the same primitive the
drag uses. `OnUpdate` calls it for you on the first frame of an orbit drag.

**Notes & pitfalls**
- **The pivot defaults to `(0,0,0)`.** Calling `OrbitBy` without ever latching a pivot rotates the
  rig about the world origin, not about the target — usually not what you meant.

### `OrbitCameraController::OrbitBy`

```cpp
void OrbitBy(float dYawDeg, float dPitchDeg);
```

**What it does** — rigidly rotates the **entire rig** (eye *and* target) about the latched pivot by
the given deltas, so the pivot's projected pixel is invariant. Pitch clamps to the configured
limits; yaw does not. The eye is transformed by the camera-basis rotation between the old and new
poses, then the target is placed on the new view ray at the unchanged distance so a `LookAt` rebuild
reproduces the pose exactly.

**Why you'd use it** — it is the one primitive both the orbit drag and the inertia drift ride, and
it is public so scripted moves and headless tests can use it. Covered by three doctests in
`tests/test_s5_navigation.cpp` ("latching a pivot on press does not move the view", "the pivot
re-projects to the same pixel through a 90° orbit", "a classic orbit about the target leaves the
target fixed").

**Example**

```cpp
m_Orbit.BeginOrbitAbout(surfacePoint);      // latch once
m_Orbit.OrbitBy(-30.0f, 10.0f);             // then rotate; surfacePoint stays put on screen
```

**Notes & pitfalls**
- **No distance clamp on this path** — orbiting about an off-target pivot changes the eye-to-target
  distance implicitly and that is intended; clamping belongs to zoom.
- It rebuilds the camera on every call, so batching several small deltas costs several rebuilds.

### `OrbitCameraController::SnapView`

```cpp
void SnapView(ViewPreset preset, bool animate = true);
```

**What it does** — snaps to a standard orientation about the current target, keeping the current
distance. With `animate = true` it starts a pose blend toward `(yaw, pitch)` from the
[preset table](#viewpreset-angles) using the current **target distance**; with `animate = false` it
calls `SetYawPitch`, which hard-sets and cancels any blend.

**Why you'd use it** — Front/Top/Iso buttons, a Home hotkey, and the click handler for
[`NavigationCube::PickFace`](#navigationcubepickface).

**Example**

```cpp
m_Orbit.SnapView(Cosmic::ViewPreset::Top);          // animated
m_Orbit.SnapView(Cosmic::ViewPreset::Iso, false);   // instant cut
```

**Notes & pitfalls**
- **The non-animated path changes nothing but the angles** — target and distance are left exactly
  as they were.
- The animated path clamps the goal pitch into the pitch limits, so a narrowed
  [`SetPitchLimits`](#orbitcameracontroller--tuning-and-gating) quietly changes where `Top` lands.
- Any drag or scroll cancels the blend mid-flight.

### `OrbitCameraController::FrameSphere`

```cpp
void FrameSphere(const glm::vec3& center, float radius, bool animate = true);
```

**What it does** — recentres the target on `center` and computes the distance that makes the sphere
fill about **70 % of the viewport half-height** at any aspect:
`dist = radius / (0.7 * tan(fovY/2))`, clamped into the distance limits (falling back to
`radius * 3` if the tangent underflows). Yaw and pitch are preserved. **Returns immediately and does
nothing when `radius <= 0`.**

**Why you'd use it** — the primitive behind `FrameBounds`; call it directly when you already have a
bounding sphere. Verified by `tests/test_s5_navigation.cpp` ("FrameSphere: recenters on the sphere
and fits ~70 % of the view height").

**Notes & pitfalls**
- With `animate = false` it cancels any blend, sets the target, then calls `SetDistance` — so the
  distance clamp applies twice, harmlessly.
- A very large radius clamps at the 500-unit distance limit and the object then does **not** fit.
  Widen the limits with `SetDistanceLimits` first if you frame big scenes.

### `OrbitCameraController::FrameBounds`

```cpp
void FrameBounds(const glm::vec3& worldMin, const glm::vec3& worldMax, bool animate = true);
```

**What it does** — takes the box's centre and its **bounding-sphere** radius
(`0.5 * length(max - min)`) and forwards to `FrameSphere`. **Returns without doing anything when the
radius is `<= 1e-6`** (a degenerate box).

**Why you'd use it** — the "F frames the selection" hotkey. Engine3DDemo computes the selected
entity's world AABB, or the whole scene's when nothing is selected
(`Engine3DDemo.cpp:893-901`).

**Example**

```cpp
glm::vec3 mn, mx;
if (ComputeEntityWorldAABB(selected, mn, mx))
    m_Orbit.FrameBounds(mn, mx);
```

**Notes & pitfalls**
- Because it uses the **bounding sphere**, a long thin box frames conservatively — a 100×1×1 wall
  is framed as if it were a 100-unit ball.

### `OrbitCameraController::IsAnimating`

```cpp
bool IsAnimating() const { return m_Animating; }
```

**What it does** — true while a `SnapView` / `Frame*` pose blend is in progress.

**Why you'd use it** — suppress anything that hard-sets the pose while a blend runs. See the
auto-orbit pitfall under [`SetYawPitch`](#orbitcameracontrollersetyawpitch--getyaw--getpitch).

### `OrbitCameraController` — tuning and gating

```cpp
void SetDistanceLimits(float minDist, float maxDist) { m_MinDistance = minDist; m_MaxDistance = maxDist; }
void SetPitchLimits(float minDeg, float maxDeg)      { m_MinPitchDeg = minDeg; m_MaxPitchDeg = maxDeg; }
void SetOrbitSpeed(float degPerPixel)                { m_OrbitSpeed = degPerPixel; }
void SetPanSpeed(float scale)                        { m_PanSpeed = scale; }
void SetZoomSpeed(float scale)                       { m_ZoomSpeed = scale; }
void SetControlEnabled(bool enabled)                 { m_ControlEnabled = enabled; }
bool IsControlEnabled() const                        { return m_ControlEnabled; }
bool IsDragging() const                              { return m_Dragging; }
void SetInertiaEnabled(bool enabled)                 { m_InertiaEnabled = enabled; }
bool IsInertiaEnabled() const                        { return m_InertiaEnabled; }

PerspectiveCamera&       GetCamera()       { return m_Camera; }
const PerspectiveCamera& GetCamera() const { return m_Camera; }
```

**What it does** — the tuning surface. `SetOrbitSpeed` is degrees per pixel of drag (default 0.25);
`SetPanSpeed` scales the distance-proportional pan (so panning feels constant at any zoom);
`SetZoomSpeed` multiplies the scroll and dolly exponents. `SetControlEnabled` is the master gate —
disabling mid-drag ends the drag. `SetInertiaEnabled(true)` adds exponential orbit drift after
release (decay `exp(-ts * 8)`), off by default and **purely cosmetic — it does not change end
poses**.

**Notes & pitfalls**
- **`SetDistanceLimits` and `SetPitchLimits` do not re-clamp the live pose.** They take effect on
  the next mutation.
- `IsDragging()` is what lets a drag survive the cursor leaving the panel; the standard gate is
  `SetControlEnabled((vpHovered || IsDragging()) && !Gizmo::IsUsing() && !Gizmo::IsOver() &&
  !navCubeHovered)` (`Engine3DDemo.cpp:866-867`).
- `GetCamera()` returns a **mutable** reference: this is the supported way to change the near/far
  planes or the FOV that the controller itself never writes.

---

## `FlyCameraController`

*Declared in `Cosmic/src/camera/FlyCameraController.h`. Both configurations. Owns a
[`PerspectiveCamera`](#perspectivecamera) built as `(45°, aspect, 0.1, 5000)` — note the far plane,
five times the orbit rig's.*

Free flight: RMB mouse-look, WASD/QE movement, Shift boost, scroll to change speed. Where the orbit
rig moves a mount around a point, this one moves the camera itself.

Defaults: position `(0, 20, 40)`, yaw `0°`, pitch `−15°`, move speed `25 m/s` clamped to
**[0.5, 500]**, boost `×4`, smoothing `12 /s`, look speed `0.15 °/px`, ground clearance `1.5 m`,
control **enabled**. Yaw 0 looks down −Z (matching the orbit rig), positive pitch looks up, pitch
clamps to **±89°**.

**Bindings are hard-coded** — there is no equivalent of `CameraKeyBindings` here.

| Input | Action |
| --- | --- |
| RMB held | mouse-look — cursor right yaws right, cursor up pitches up |
| `W` / `S` | forward / back along the **full look vector**, pitch included |
| `A` / `D` | strafe along the camera right axis |
| `E` or `Space` | ascend (world +Y) |
| `Q` or **Left** `Ctrl` | descend (world −Y) |
| **Left** `Shift` | boost while held (`× BoostMultiplier`) |
| Scroll | base move speed `× 1.15` per notch |

### `FlyCameraController::GroundProbe` / `Motion`

```cpp
using GroundProbe = std::function<float(float x, float z)>;

struct Motion
{
    glm::vec3 Position{ 0.0f };
    glm::vec3 Velocity{ 0.0f };
};
```

**What it does** — `GroundProbe` returns the world-space ground height at `(x, z)`; the controller
keeps the camera at least `clearance` metres above it each frame. `Motion` is the pure
position/velocity pair advanced by [`IntegrateMotion`](#flycameracontrollerintegratemotion).

**Why you'd use it** — `SetGroundProbe([this](float x, float z){ return m_Terrain->SampleHeight(x, z); }, 1.5f)`
is the "never fly through the terrain" one-liner. `Motion` exists so the movement maths is testable
without an `Input` backend.

### `FlyCameraController::FlyCameraController`

```cpp
FlyCameraController(float aspectRatio);
```

**What it does** — builds `PerspectiveCamera(45.0f, aspectRatio, 0.1f, 5000.0f)` and poses it from
the default position/yaw/pitch.

**Notes & pitfalls**
- **The far plane is 5000, five times the orbit rig's 1000.** Swapping rigs mid-session therefore
  changes what is visible in the distance unless you set the projection yourself.

### `FlyCameraController::OnUpdate`

```cpp
void OnUpdate(float ts);
```

**What it does**, when control is enabled: applies RMB mouse-look (`yaw += dx * lookSpeed`,
`pitch -= dy * lookSpeed`, pitch clamped to ±89°, with the **first look frame producing no delta**);
builds the wish velocity from the movement keys and the current basis; advances position and
velocity through [`IntegrateMotion`](#flycameracontrollerintegratemotion); applies the ground clamp
if a probe is set. Then it stores the mouse position and rebuilds the camera with `LookAt`.

When control is **disabled** it clears the looking flag and decays the stored velocity by
`exp(-ts * 12)` — but **does not integrate the position**. The camera is frozen; the decay only
stops it resuming at full speed when control returns.

**Notes & pitfalls**
- **Movement does not require RMB.** The look block and the movement block are independent — WASD
  flies whenever `IsControlEnabled()` is true. For Unreal's "hold RMB to fly", gate control on the
  button yourself.
- Strafe uses `normalize(cross(forward, worldUp))`, so it stays horizontal regardless of pitch,
  while W/S follow the full look vector (including pitch).
- Diagonal movement is **speed-normalised**: the combined wish direction is normalised before being
  scaled, so no diagonal speed bonus.
- Only **left** Shift boosts and only **left** Ctrl descends — right-hand modifiers do nothing here
  (the orbit rig accepts both).

### `FlyCameraController::OnEvent` / `OnResize`

```cpp
void OnEvent(Event& e);
void OnResize(float width, float height);
```

**What it does** — `OnEvent` dispatches `MouseScrolledEvent` (multiplies the base move speed by
`1.15^yOffset` through `SetMoveSpeed`, so the [0.5, 500] clamp applies) and `WindowResizeEvent`
(guarded `if (e.GetHeight() > 0)`); both return `false`. `OnResize` forwards to
`PerspectiveCamera::SetViewportSize`.

**Notes & pitfalls**
- **Scroll is ignored while control is disabled.**
- There is no zoom-speed setter: the `1.15` factor is fixed and not scaled by any member.

### `FlyCameraController::SetPose` / `GetPosition` / `GetYaw` / `GetPitch`

```cpp
void      SetPose(const glm::vec3& position, float yawDeg, float pitchDeg);
glm::vec3 GetPosition() const { return m_Position; }
float     GetYaw() const      { return m_YawDeg; }    // degrees; yaw 0 looks -Z
float     GetPitch() const    { return m_PitchDeg; }  // clamped to ±89°
```

**What it does** — hard-sets the pose. Yaw is stored as given; pitch clamps to ±89°;
**residual velocity is zeroed** so the camera does not continue drifting from wherever it was.
Rebuilds immediately.

**Why you'd use it** — teleporting, restoring a saved viewpoint, and handing off from another rig.
The two rigs' angle conventions mirror each other: `fly(yaw, pitch) == (-orbitYaw, -orbitPitch)`
produces the same look direction, which is how `Starforge::EditorCameraRig` switches between them
without a jump.

**Notes & pitfalls**
- `GetPosition()` returns **by value** here (the cameras return `const&`) — an easy inconsistency to
  trip over in a `const auto&` binding.

### `FlyCameraController::SetMoveSpeed` / `GetMoveSpeed`

```cpp
void  SetMoveSpeed(float metersPerSec);                 // clamped to [0.5, 500]
float GetMoveSpeed() const { return m_MoveSpeed; }
```

**What it does** — sets the base speed, **clamped to [0.5, 500] m/s**. The scroll handler routes
through this call, so the clamp bounds scrolling too.

**Notes & pitfalls**
- The clamp is silent. `SetMoveSpeed(0.0f)` leaves you at `0.5`, not stopped — use
  `SetControlEnabled(false)` to stop.

### `FlyCameraController::SetBoostMultiplier` / `SetSmoothing` / `SetLookSpeed`

```cpp
void SetBoostMultiplier(float x)     { m_BoostMultiplier = x; }   // LShift (default 4.0)
void SetSmoothing(float perSec)      { m_Smoothing = perSec; }    // 0 = raw velocity
void SetLookSpeed(float degPerPixel) { m_LookSpeed = degPerPixel; }
```

**What it does** — plain tuning writes, no clamping. `SetSmoothing(0)` (or any value `<= 0`) makes
the velocity snap to the wish velocity every frame — instant starts and stops.

**Notes & pitfalls**
- **The boost multiplier is not clamped** and multiplies the *already clamped* base speed, so a
  boosted camera can exceed 500 m/s.

### `FlyCameraController::SetControlEnabled` / `IsControlEnabled` / `IsLooking`

```cpp
void SetControlEnabled(bool enabled);
bool IsControlEnabled() const { return m_ControlEnabled; }
bool IsLooking() const        { return m_Looking; }
```

**What it does** — master gate for keyboard and mouse. **Disabling ends any in-progress mouse-look**
(the orbit contract). `IsLooking()` reports an active RMB drag — the fly equivalent of
`OrbitCameraController::IsDragging()` and the right thing to `||` into your host gate.

### `FlyCameraController::SetViewportRect`

```cpp
void SetViewportRect(const glm::vec2& posPx, const glm::vec2& sizePx);
```

**What it does** — stores the rect and, when both extents are `> 0`, updates the projection aspect.

**Notes & pitfalls**
- **The stored rect is currently unused** by this controller — the header says "stored for future
  cursor math". Today it is an aspect update with a guard, i.e. a safer `OnResize`. Nothing breaks
  if you never call it.

### `FlyCameraController::SetGroundProbe`

```cpp
void SetGroundProbe(GroundProbe probe, float clearance = 1.5f);
```

**What it does** — installs the ground-height callback and the clearance. **Passing a null probe
disables the clamp** (free flight).

**Example**

```cpp
m_Fly.SetGroundProbe([this](float x, float z) { return m_Terrain->SampleHeight(x, z); }, 1.5f);
```

**Notes & pitfalls**
- The probe is called **once per frame** with the camera's current XZ, from inside `OnUpdate`. Keep
  it cheap; `Terrain::SampleHeight` is fine.
- The clamp only raises Y — it never pushes the camera down, and never touches X/Z.

### `FlyCameraController::DirectionFromYawPitch`

```cpp
static glm::vec3 DirectionFromYawPitch(float yawDeg, float pitchDeg);
```

**What it does** — the unit look direction for a (yaw, pitch) in degrees:
`(sin(yaw)·cos(pitch), sin(pitch), −cos(yaw)·cos(pitch))`. Yaw 0 / pitch 0 → `(0, 0, −1)`;
increasing yaw turns toward +X; increasing pitch looks up.

**Why you'd use it** — aiming a projectile down the camera's look axis, or converting a stored
fly pose to a direction without a camera. Pure and GL-free — two doctests in
`tests/test_flycamera.cpp` pin the convention.

### `FlyCameraController::ComputeWishVelocity`

```cpp
static glm::vec3 ComputeWishVelocity(bool forward, bool back, bool left, bool right,
                                     bool up, bool down,
                                     const glm::vec3& forwardDir, const glm::vec3& rightDir,
                                     float speed);
```

**What it does** — sums the requested axes (`up`/`down` always along world +Y/−Y), then **normalises
and scales by `speed`**. Returns the zero vector when nothing is pressed or when opposing keys
cancel exactly.

**Why you'd use it** — reusing the engine's movement feel in a custom rig, or unit-testing input
handling. Covered by `tests/test_flycamera.cpp` ("single key follows its axis, scaled by speed",
"opposing keys cancel to zero; combos stay speed-normalised").

### `FlyCameraController::IntegrateMotion`

```cpp
static Motion IntegrateMotion(const Motion& state, const glm::vec3& wishVelocity,
                              float smoothingPerSec, float ts);
```

**What it does** — one step: exponential velocity approach
`v += (wish - v) * (1 - exp(-smoothing * ts))` (with `smoothing <= 0` meaning "snap to wish"), then
Euler position integration `p += v * ts`. **No ground clamp** — that is
[`ClampAboveGround`](#flycameracontrollerclampaboveground).

**Notes & pitfalls**
- Pure function: it returns a new `Motion` and mutates nothing. Feed it your own state to simulate a
  camera path headlessly.
- The smoothing is frame-rate independent, so the same `smoothingPerSec` feels identical at 30 and
  240 fps.

### `FlyCameraController::ClampAboveGround`

```cpp
static glm::vec3 ClampAboveGround(const glm::vec3& pos, float groundY, float clearance);
```

**What it does** — returns `pos` with `y` raised to at least `groundY + clearance`; `x` and `z` are
untouched. A position already above the clearance is returned unchanged.

### `FlyCameraController::GetCamera`

```cpp
PerspectiveCamera&       GetCamera()       { return m_Camera; }
const PerspectiveCamera& GetCamera() const { return m_Camera; }
```

**What it does** — the camera to hand to `Renderer3D::BeginScene` / `Scene::OnRender3D`. Mutable, so
this is where you override the near/far planes the constructor chose.

---

## `NavigationCube` *(3D only)*

*Declared in `Cosmic/src/camera/NavigationCube.h`. **3D configuration only** — the header is
included **unfenced** by `Cosmic.h:91` but `NavigationCube.cpp` is filtered out of the 2D build
(`Cosmic/CMakeLists.txt:198`), so 2D code **compiles and then fails at LINK**. See
[Configuration](#configuration--read-this-before-you-call-anything).*

A self-contained orientation widget: it renders a shaded cube plus an RGB axis tripod into its
**own** framebuffer, oriented to match a camera's view, and turns a click on a face into a
[`ViewPreset`](#viewpreset).

### `NavigationCube::Create`

```cpp
static Ref<NavigationCube> Create(uint32_t pixelSize = 140);
```

**What it does** — the unified `Ref` factory. `pixelSize` is the square FBO edge in pixels; the
constructor substitutes **140** if you pass `0`. Allocates an `{RGBA8, DEPTH24STENCIL8}` framebuffer
and a unit box mesh, and builds a fixed orthographic projection
(`glm::ortho(-0.95, 0.95, -0.95, 0.95, 1.0, 3.0)`, with the cube pushed back 2.0 units).

**Why you'd use it** — once, at layer attach. Requires a live GL context.

**Example**

```cpp
m_NavCube = Cosmic::NavigationCube::Create(140);
```

**Notes & pitfalls**
- **Never returns null**, but the object degrades if `FrameBuffer::Create` fails: `Render` becomes a
  no-op and `GetTextureID()` returns `0`.
- The ortho half-extent `0.95` exceeds the cube's half-diagonal (`0.5·√3 ≈ 0.866`) so no rotation
  clips it.

### `NavigationCube::Render`

```cpp
void Render(const glm::mat4& cameraView);
```

**What it does** — strips the translation from `cameraView` (**only its rotation is used**), pushes
the cube back, binds its own FBO, sets the viewport to the cube size, **clears to fully transparent
`(0,0,0,0)`**, draws the shaded box + wire edges + axes through `Renderer3D`, and **leaves its FBO
unbound**. Does nothing when the FBO or mesh failed to create.

**Why you'd use it** — once per frame, as a **pre-pass, outside any `BeginScene`/`EndScene`** of
yours. The transparent clear is what lets the cube float over the viewport image.

**Example**

```cpp
if (m_NavCube && m_ShowNavCube)
    m_NavCube->Render(m_Orbit.GetCamera().GetViewMatrix());
// then re-bind your own target:
fb->Bind();
Cosmic::RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());
```

**Notes & pitfalls**
- **It leaves three pieces of GL state changed:** the bound framebuffer (unbound → default), the
  viewport (the cube's size), and the **clear colour** (`glClearColor` is sticky global state and is
  left at transparent black). Restore all three, not just the FBO.
- It opens and closes its own `Renderer3D::BeginScene`/`EndScene`, so calling it inside yours
  corrupts the outer pass.
- Rotating the *view* rather than the cube is deliberate: it keeps the cube axis-aligned in world
  space, which is what makes [`PickFace`](#navigationcubepickface) a plain ray-vs-AABB test.

### `NavigationCube::PickFace`

```cpp
bool PickFace(float u, float v, ViewPreset& outPreset) const;
```

**What it does** — hit-tests a click. `(u, v)` are **panel coordinates in [0, 1] with `v` measured
DOWN from the top-left** — the natural ImGui convention. Reuses the exact view-projection of the
most recent `Render`, so pixels and picks always agree. Writes the entry face's preset and returns
`true`; returns `false` on a background miss.

**Example** *(condensed from `Engine3DDemo.cpp:1436-1460`)*

```cpp
const float sz = static_cast<float>(m_NavCube->GetSize());
ImGui::Image((ImTextureID)(intptr_t)m_NavCube->GetTextureID(), { sz, sz },
             ImVec2(0, 1), ImVec2(1, 0));            // flip V — GL is bottom-left
if (ImGui::IsItemHovered())
{
    m_NavCubeHovered = true;                          // the camera must yield this corner
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

**Notes & pitfalls**
- **Before the first `Render`, the stored view is identity** — picks still resolve, against an
  unrotated cube. Call `Render` first.
- **Feed the hover state back into your camera gate.** The cube owns its corner: while it is
  hovered, camera drags and click-to-select must both stand down, or clicking a face also starts an
  orbit.
- Only the **entry** face is reported: a click near a silhouette edge resolves to whichever face the
  ray enters first, never to an edge or a corner.

### `NavigationCube::PickFaceFromViewProjection`

```cpp
static bool PickFaceFromViewProjection(const glm::mat4& viewProjection,
                                       float u, float v, ViewPreset& outPreset);
```

**What it does** — the same ray/AABB test, exposed statically: unproject `(u, v)` through
`viewProjection`, intersect the unit cube `[-0.5, 0.5]³` with the slab method, and map the entry
face to a preset (`X → Right/Left`, `Y → Top/Bottom`, `Z → Front/Back`).

**Why you'd use it** — unit-testing the mapping with **no GL context**
(`tests/test_s5_navigation.cpp`: "a centered click selects the face nearest the camera", "a click
off the cube misses"), or hit-testing a cube you render yourself.

**Notes & pitfalls**
- Returns `false` for a degenerate unprojection (`|w| < 1e-8`), a ray parallel to and outside a
  slab, a miss, or a box entirely behind the ray.

---

## `ScenePicker` *(3D only)*

*Declared in `Cosmic/src/scene/ScenePicker.h`. **3D configuration only** — filtered out of the 2D
build (`Cosmic/CMakeLists.txt:202`) **and** fenced in `Cosmic.h:117-119`, so a 2D project naming it
gets a clean compile error.*

> **Manifest note:** `scene/ScenePicker.h`'s row in
> [the coverage manifest](README.md#coverage-manifest--every-public-header-maps-to-a-chapter) points
> at [ecs.md](ecs.md), but the picker is a *viewport navigation* tool — the guide chapter that
> covers it is [`../guide/cameras.md`](../guide/cameras.md#click-to-select-an-entity-3d-only), its
> `WorldPoint` exists to feed [`OrbitCameraController::SetPivotProbe`](#orbitcameracontrollersetpivotprobe),
> and it is documented here. The row needs re-pointing at `cameras.md`; `ecs.md` should cross-link.

Turns a mouse position over a 3D viewport into the `Entity` under it, using the entity-ID MRT
attachment that `Scene::OnRender3D` already writes. It owns a private
`{RGBA8, RED_INTEGER, DEPTH24STENCIL8}` framebuffer that resizes itself on demand.

### `ScenePicker::Create`

```cpp
static Ref<ScenePicker> Create();
```

**What it does** — the `Ref` factory. The constructor allocates the MRT framebuffer at 1×1; the
first [`RenderIdPass`](#scenepickerrenderidpass) grows it to the viewport size.

**Notes & pitfalls**
- **Never returns null.** If `FrameBuffer::Create` fails, `RenderIdPass` becomes a no-op, `Pick`
  returns an invalid `Entity` and `WorldPoint` returns `false` — degraded, not crashing.

### `ScenePicker::RenderIdPass`

```cpp
void RenderIdPass(Scene& scene, const Camera& camera, uint32_t width, uint32_t height,
                  const std::vector<entt::entity>* only = nullptr);
```

**What it does** — resizes the FBO to `(width, height)` if needed, binds it, sets the viewport,
clears colour+depth, **clears the ID attachment to `-1`** (a plain `glClear` does not touch integer
attachments), then renders. With `only == nullptr` it calls `Scene::OnRender3D(camera)` — the full
3D pass. With a non-null `only` it draws just those entities' mesh renderers, LOD-group selected
levels and voxel chunks in a flat-colour pass, turning the ID attachment into a **selection mask**.
Finally it **unbinds its FBO**.

**Why you'd use it** — once, on the click frame, immediately before `Pick`. It is a full scene draw;
never run it every frame for hover picking.

**Example**

```cpp
auto fb = Cosmic::Application::Get().GetFrameBuffer();
m_Picker->RenderIdPass(*m_Scene, m_Orbit.GetCamera(), fb->GetWidth(), fb->GetHeight());
fb->Bind();                                                    // RenderIdPass unbound it
Cosmic::RenderCommand::SetViewport(0, 0, fb->GetWidth(), fb->GetHeight());
```

**Notes & pitfalls**
- **Returns immediately when `width == 0` or `height == 0`** (or when the FBO failed to create) —
  which means a `Pick` right after a collapsed-panel frame reads the *previous* pass's contents.
- **It leaves the FBO unbound, the viewport set to `(0,0,width,height)`, and the clear colour black.**
  Restore your own render target, viewport and clear colour.
- **What the ID pass can see is exactly what `Scene::OnRender3D` draws**: meshes, LOD levels, skinned
  meshes, terrain and voxel chunks. It does **not** see sprites, tilemaps, 2D lights, UI entities,
  water or particles — clicking any of those falls through as a miss. Rect-pick those separately.
- The `only` overload is how `SceneRenderer`'s outline pass works; client code rarely needs it.

### `ScenePicker::Pick`

```cpp
Entity Pick(Scene& scene, int xFromLeft, int yFromTop) const;
```

**What it does** — reads one texel of the ID attachment and maps it back to an `Entity`.
**Coordinates are viewport-local pixels: `x` from the LEFT, `y` from the TOP** — the ImGui/mouse
convention. The GL bottom-left flip (`glY = height - 1 - yFromTop`) happens **inside**; do not
pre-flip.

**Why you'd use it** — click-to-select. Feed the result to `EntitySelection::Set` so 2D panels and
3D tools share one selection.

**Example** *(the shape used by `Engine3DDemo::HandleEditorPicking`)*

```cpp
const glm::vec2 vpPos  = app.GetViewportPos();
const glm::vec2 vpSize = app.GetViewportSize();
const glm::vec2 mouse  = Cosmic::Input::GetMouseScreenPosition();   // same space as vpPos
const int px = static_cast<int>(mouse.x - vpPos.x);
const int py = static_cast<int>(mouse.y - vpPos.y);
if (px >= 0 && py >= 0 && px < (int)vpSize.x && py < (int)vpSize.y)
{
    if (Cosmic::Entity hit = m_Picker->Pick(*m_Scene, px, py))
        Cosmic::EntitySelection::Set(hit, "Entity");
    else
        Cosmic::EntitySelection::Clear();                            // click-away deselects
}
```

**Notes & pitfalls**
- **Failure is an invalid `Entity`** (`operator bool` is false) — never a crash, never a stale
  handle. You get one for a background miss (`id < 0`), out-of-range coordinates, and an id that no
  longer maps to a live registry slot.
- **`Input::GetMousePosition()` is the wrong input.** Use `GetMouseScreenPosition()`; the viewport
  origin you subtract lives in that space. A picker that is offset by the window position is this
  bug, and the engine has shipped it before.
- `ScenePicker` is one half of a deliberate double convention. The other half is `FrameBuffer`:
  `ReadPixel`/`ReadDepth` take **GL bottom-left** coordinates while `ReadPixels` returns a
  **top-left-origin** buffer. `ScenePicker` is the one that speaks mouse space.
- The read-back is a synchronous `glReadPixels` — a small pipeline stall. Fine per click; an async
  PBO round-robin is the noted direction if you ever want per-frame hover picking.

### `ScenePicker::WorldPoint`

```cpp
bool WorldPoint(const Camera& camera, int xFromLeft, int yFromTop, glm::vec3& out) const;
```

**What it does** — reads the depth at the same viewport-local pixel and unprojects it to a world
position through `inverse(camera.GetViewProjectionMatrix())` (pixel centres, GL depth range
`[0,1] → [-1,1]`). Returns `false` at the far plane (nothing drawn), for out-of-range coordinates,
for a missing FBO, and for a degenerate unprojection.

**Why you'd use it** — the CAD pivot probe: "orbit about the surface under my cursor". Hand it
straight to [`OrbitCameraController::SetPivotProbe`](#orbitcameracontrollersetpivotprobe).

**Notes & pitfalls**
- **`camera` must be the one the last `RenderIdPass` used**, or the unprojection is silently wrong.
- It reads the depth buffer of the *last* pass — if that pass was skipped (collapsed panel, editor
  mode off) you get a stale point, not a failure.

### `ScenePicker::GetColorTextureID` / `GetIdTextureID` / `GetWidth` / `GetHeight`

```cpp
uint32_t GetColorTextureID() const;
uint32_t GetIdTextureID() const;
uint32_t GetWidth() const;
uint32_t GetHeight() const;
```

**What it does** — the colour attachment handle (for an optional debug `ImGui::Image` view), the
`RED_INTEGER` id attachment handle (which `SceneRenderer`'s outline pass samples), and the current
FBO size. **All four return `0` when the framebuffer failed to create.**

---

## `Gizmo`

*Declared in `Cosmic/src/graphics/Gizmo.h`. **Both configurations** — `Cosmic.h:75` includes it
unfenced and `Gizmo.cpp` is not filtered out of the 2D build. Orthographic viewports are handled
automatically, so the gizmo works in a 2D editor too.*

A thin static wrapper over vendored ImGuizmo that exposes only engine enums, so no third-party type
reaches a public header. Everything is `static`: there is no gizmo object.

> **Frame protocol.** `Manipulate` **must** be called between `ImGui::Begin`/`End` of the window
> that shows the rendered scene — `WorkspaceLayer::BeginViewportOverlay()` /
> `EndViewportOverlay()` does that for the engine viewport. It draws into, and hit-tests hover
> against, the *current* ImGui window. Called outside one (say into a foreground draw list) it
> renders a gizmo that can never be grabbed, because ImGuizmo then treats the mouse as "over some
> other window" whenever your viewport is hovered. This is the single most common way to get a
> gizmo that looks right and does nothing.
>
> The engine calls `ImGuizmo::BeginFrame()` once per frame in `ImGuiLayer::Begin`
> (`ImGuiLayer.cpp:104`), so clients have no per-frame bookkeeping.

### `Gizmo::Operation` / `Gizmo::Space`

```cpp
enum class Operation { Translate, Rotate, Scale, Universal };
enum class Space     { Local, World };
```

**What it does** — which manipulator to draw, and whether its axes are the object's or the world's.
`Universal` combines translate arrows, rotate rings and universal scale handles in one gizmo
(`ImGuizmo TRANSLATE|ROTATE|SCALEU`).

**Notes & pitfalls**
- **ImGuizmo takes a single snap value per call**, so under `Universal` your snap applies to the
  **move** handles and rotate/scale drag unsnapped. Pass the move increment there.

### `Gizmo::SetRect`

```cpp
static void SetRect(float x, float y, float width, float height);
```

**What it does** — sets the screen-space rectangle the gizmo draws and hit-tests within, in **ImGui
screen pixels**.

**Why you'd use it** — every frame, before `Manipulate`, with the viewport rect.

**Example**

```cpp
Cosmic::Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);
```

**Notes & pitfalls**
- Same space as `Application::GetViewportPos()` / `Input::GetMouseScreenPosition()` — rule 3 of
  [the controller contract](#the-controller-contract). A rect in window-client pixels puts the
  gizmo's hit region in the wrong place while the handles still *draw* correctly, which is a
  confusing failure.

### `Gizmo::SetEnabled`

```cpp
static void SetEnabled(bool enabled);
```

**What it does** — greys the gizmo out without removing it (`ImGuizmo::Enable`). A disabled gizmo
still draws but cannot be grabbed.

**Notes & pitfalls**
- It is **global and sticky** across frames — set it back to `true` when your modal state ends, or
  every later gizmo stays dead.

### `Gizmo::IsUsing` / `Gizmo::IsOver`

```cpp
static bool IsUsing();
static bool IsOver();
```

**What it does** — `IsUsing()` is true while a drag is in progress; `IsOver()` is true while the
cursor is over any handle.

**Why you'd use it** — interaction etiquette, and both are needed:
- gate the camera on `!IsUsing() && !IsOver()`, or grabbing a handle also orbits;
- skip click-to-select on `IsOver()`, or every grab also reselects.

**Notes & pitfalls**
- They report ImGuizmo's state from the **last** `Manipulate` call, so an editor that reads them in
  `OnUpdate` (before this frame's ImGui pass) is reading last frame's values. That is what
  Engine3DDemo does deliberately (`m_GizmoActive` / `m_GizmoOver` are cached from the previous ImGui
  pass) and it is fine — one frame of lag on a gate.
- Undo is the host's job. The pattern worth copying: snapshot the transform on the frame `IsUsing()`
  goes true, commit one coalesced command on the frame it goes false — one undo entry per drag, not
  per frame.

### `Gizmo::Manipulate` *(matrix overload)*

```cpp
static bool Manipulate(const Camera& camera, glm::mat4& model,
                       Operation op, Space space, float snap = 0.0f);
```

**What it does** — draws the manipulator for `model` and edits it **in place**. Returns `true` if
the matrix changed this frame. `snap > 0` snaps to that increment — **world units for translate and
scale, degrees for rotate**; `0` disables snapping. Orthographic vs perspective is detected from
`camera.GetProjectionMatrix()[3][3] > 0.5f`, so a 2D ortho viewport needs no special handling.

**Why you'd use it** — world-space manipulation of a parented entity (compose with
`Scene::GetWorldTransform(e)` and re-derive the local transform yourself), or manipulating something
that is not a `TransformComponent` at all — a light gizmo, a spline handle, a probe volume.

**Notes & pitfalls**
- Must be called inside the viewport window (see the frame protocol box).
- `snap` has three meanings for one argument. "Rotate snapping is enormous / translate snapping is
  invisible" is always this.

### `Gizmo::Manipulate` *(TransformComponent overload)*

```cpp
static bool Manipulate(const Camera& camera, TransformComponent& transform,
                       Operation op, Space space, float snap = 0.0f);
```

**What it does** — runs the matrix overload on `transform.GetTransform()`, then decomposes the
result back into `Position` (4th column), `Scale` (basis-column lengths) and `RotationQuat`
(normalised basis columns), and **sets `UseQuatRotation = true`**. Returns `true` if the transform
changed. The decomposition is done by hand rather than with `glm::decompose`, to dodge a
quaternion-sign pitfall some glm versions have.

**Why you'd use it** — the ordinary editor path: one call per selected entity per frame.

**Example**

```cpp
auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
if (ws->BeginViewportOverlay())
{
    Cosmic::Gizmo::SetRect(vpPos.x, vpPos.y, vpSize.x, vpSize.y);
    if (selected && selected.HasComponent<Cosmic::TransformComponent>())
    {
        auto& t = selected.GetComponent<Cosmic::TransformComponent>();
        Cosmic::Gizmo::Manipulate(cam, t, m_Op, m_Space, m_Snap);
    }
}
ws->EndViewportOverlay();                      // always pair with Begin
```

**Notes & pitfalls**
- **It edits the LOCAL transform.** `TransformComponent::GetTransform()` composes only that entity's
  own position/rotation/scale, so for a parented entity the gizmo sits at the *local* pose, not the
  world pose. Use the matrix overload with `Scene::GetWorldTransform` if you need world-space
  manipulation.
- **The Euler `Rotation` field goes stale** from the first drag onward — the component's two
  rotation representations are independent by design, and `ScenePhysics::WriteBackWorldPose` and
  `Scene::SetParent(..., keepWorldPose = true)` behave the same way. This is engine-normal, not a
  gizmo quirk.
- A near-zero scale axis (`< 1e-6`) is replaced by the corresponding identity basis vector rather
  than producing `NaN` — you cannot corrupt a rotation by scaling an object to nothing.

---

## Binding tables

### Orbit gestures by `NavStyle`

Verified against `OrbitCameraController.cpp:36-52` (button resolution) and `:257-292` (scroll).

| Gesture | `NavStyle::Classic` *(default)* | `NavStyle::CAD` |
| --- | --- | --- |
| Orbit | **LMB** drag | **MMB** drag (no Ctrl, no Shift) |
| Pan | **RMB** drag | **Ctrl + MMB** drag *(Shift is ignored — Ctrl wins)* |
| Dolly | — | **Shift + MMB** drag, vertical; drag up = in, applied immediately |
| Zoom | scroll → toward the **centre**, smoothly blended | scroll → toward the **cursor**, snapped |
| Orbit pivot | the current `Target` | the world point **under the cursor** (probe, else ray/plane) |
| LMB | orbits | **free** — click-to-select |

Speeds: orbit `0.25 °/px` (`SetOrbitSpeed`), pan `distance × 0.0015 × panSpeed` per pixel, dolly
`1.01^(Δy × zoomSpeed)`, scroll `1.15^(−notch × zoomSpeed)`.

### `ViewPreset` angles

Verified against `OrbitCameraController.cpp:431-449`.

| `ViewPreset` | Yaw | Pitch | Camera sits on |
| --- | --- | --- | --- |
| `Front` | 0° | 0° | +Z, looking −Z |
| `Back` | 180° | 0° | −Z |
| `Right` | 90° | 0° | +X |
| `Left` | −90° | 0° | −X |
| `Top` | 0° | **89°** | above, shy of the pole |
| `Bottom` | 0° | **−89°** | below, shy of the pole |
| `Iso` | 45° | 30° | the default 3/4 view |

### Engine3DDemo hotkeys

Verified against `Projects/Engine3DDemo/src/Engine3DDemo.cpp:889-917`. All of these are gated on
`!ImGui::GetIO().WantCaptureKeyboard`, so they never fire while a text field has focus.

| Key | Action | Notes |
| --- | --- | --- |
| `F` | Frame the selection, else the whole scene | **Edge-triggered** (`fDown && !m_KeyFWasDown`); calls [`FrameBounds`](#orbitcameracontrollerframebounds) |
| `Home` | `SnapView(ViewPreset::Iso)` | Edge-triggered |
| `W` | Gizmo → `Translate` | **Editor mode only**; level-triggered (idempotent while held) |
| `E` | Gizmo → `Rotate` | Editor mode only |
| `R` | Gizmo → `Scale` | Editor mode only |

Its picking and camera gates, for reference: click-to-select requires
`clicked && vpHovered && !gizmoActive && !gizmoOver && !navCubeHovered` (`:675-676`), and the camera
gate is `SetControlEnabled((vpHovered || IsDragging()) && !gizmoActive && !gizmoOver &&
!navCubeHovered)` (`:866-867`).

Starforge's larger viewport binding set (`Q` for `Universal`, `G` for the grid, `1`–`9` camera
bookmarks) is documented in the guide —
[`../guide/cameras.md`](../guide/cameras.md#the-editors-hotkeys) — because it is editor behaviour,
not engine API.

---

## Manifest & coverage notes

Two [coverage-manifest](README.md#coverage-manifest--every-public-header-maps-to-a-chapter) changes
are owed to this chapter. Neither is made here: the index is shared and other work orders are
running.

1. **`camera/Camera2DController.h` has no row at all.** It is the only one of the six cameras and
   controllers missing (found by D52). `Cosmic.h:89` includes it **unfenced**, so it belongs in the
   table unmarked (both configurations), routed to `cameras.md`.
2. **`scene/ScenePicker.h ³ᴰ` is routed to [ecs.md](ecs.md) and belongs here** (found by D53). The
   guide chapter covering it is [`../guide/cameras.md`](../guide/cameras.md), its `WorldPoint` feeds
   the orbit controller's pivot probe, and its entries are in this chapter. Re-point the row to
   `cameras.md`; `ecs.md` (still a skeleton) already carries a note to that effect.

Also worth recording:

- **`graphics/Gizmo.h` is scoped by two work orders.** The manifest routes it to `cameras.md` (and
  the `cameras.md` skeleton claims it); D8's prompt also lists it under
  [graphics-resources.md](graphics-resources.md). It is documented **here**. D8 should link rather
  than duplicate.
- **`camera/NavigationCube.h` is correctly marked ³ᴰ⁺** — the marker exists precisely because this
  header is 3D-only *without* being fenced. Do not "fix" it to plain ³ᴰ without fencing the include
  first.

---

*See also:* [`../guide/cameras.md`](../guide/cameras.md) (the task-oriented guide chapter — pick a
rig, wire it up, the full pitfall list) ·
[`../guide/sprites-and-tilemaps.md`](../guide/sprites-and-tilemaps.md) (`Camera2DController` in 2D
authoring context) · [cameras-navigation](../systems/cameras-navigation.md) (systems explainer) ·
[rendering-2d.md](rendering-2d.md) / [rendering-3d.md](rendering-3d.md) (what `BeginScene` does with
the camera you hand it) · [ecs.md](ecs.md) (`CameraComponent`, `TransformComponent`,
`Scene::OnRender3D`) · [events-input.md](events-input.md) (`Input`, the two mouse spaces,
`EventDispatcher`) · [ui.md](ui.md) (`WorkspaceLayer`, `BeginViewportOverlay`, the viewport rect) ·
[build-2d-3d-split](../systems/build-2d-3d-split.md) (what each configuration ships).

---
*Changelog:*
*2026-07-26 — created (D14). Covers `Camera`, `OrthographicCamera`, `PerspectiveCamera`,
`Camera2DController`, `OrthographicCameraController`, `OrbitCameraController` (+ `NavStyle`,
`ViewPreset`), `FlyCameraController`, `NavigationCube`, `ScenePicker` and `Gizmo`, plus the orbit
gesture / `ViewPreset` / Engine3DDemo binding tables.*
