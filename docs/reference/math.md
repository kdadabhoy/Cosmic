# API Reference — Math & Simulation Toolkit

> **STATUS: WRITTEN** — work order **D15** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/math/Spatial.h`, `math/Integrators.h`,
`math/Filters.h`, `math/LookupTable.h`, `math/Noise.h`, `math/Random.h`, `math/Frustum.h`.

**Read first:** the guide chapter [`../guide/sim-math-toolkit.md`](../guide/sim-math-toolkit.md) —
the task-oriented half ("integrate a state", "condition a signal", "make a replay reproduce"). It
owns idiom and worked walkthroughs; **this chapter is the per-call lookup and does not repeat it**.
Design record: [`../plans/archive/03-simulation-engine-plan.md`](../plans/archive/03-simulation-engine-plan.md)
(E10–E15). Systems explainer: [math-sim-toolkit](../systems/math-sim-toolkit.md) *(skeleton — D32)*.

**Configuration: both.** All seven headers are **header-only** (every function `inline` or a
template; no `COSMIC_API`, no `.cpp`, nothing to link), and all seven are included by `Cosmic.h`
**outside any `COSMIC_2D_ONLY` fence** — `Spatial.h` and `Frustum.h` at `Cosmic.h:94-95`, the five
E-series headers at `Cosmic.h:98-102`. They compile and behave identically on the 2D and 3D
engines. See [README §1.6](../../README.md#16-the-two-engine-configurations) and
[build-2d-3d-split.md](../systems/build-2d-3d-split.md).

Six of the seven have **no engine dependencies at all** — glm and the C++ standard library only, so
they work in a project DLL, a headless tool, a unit test, or MCU-portable code. The exception is
`LookupTable.h`, which includes `core/Core.h`, `core/Log.h`, `utils/DataExport.h` and
`utils/FileSystem.h`, and only for `LookupTable1D::FromCSV` and its error logging.

---

## Contents

- [Test coverage at a glance](#test-coverage-at-a-glance) — which header has a doctest and which does not
- [Determinism](#determinism) — **read this before building a fuzz or replay campaign**
- [Frames & attitude — `math/Spatial.h`](#frames--attitude--mathspatialh) — `Cosmic::Math` free functions
- [Integrators — `math/Integrators.h`](#integrators--mathintegratorsh) — `IntegrateRK4`, `IntegrateSemiImplicitEuler`, `FixedSubstepper`
- [Filters — `math/Filters.h`](#filters--mathfiltersh) — `LowPassFilter`, `Derivative`, `RateLimiter`, `MovingAverage`, `Biquad`, `Washout`
- [Lookup tables — `math/LookupTable.h`](#lookup-tables--mathlookuptableh) — `TableRangePolicy`, `LookupTable1D`, `LookupTable2D`
- [Noise — `math/Noise.h`](#noise--mathnoiseh) — `Noise`
- [Randomness — `math/Random.h`](#randomness--mathrandomh) — `Random`
- [View frustum — `math/Frustum.h`](#view-frustum--mathfrustumh) — `Frustum`
- [Worked example — spring + RK4 + LPF](#worked-example--spring--rk4--lpf)
- [Known rough edges](#known-rough-edges)

---

## Test coverage at a glance

Every behavioural claim below is either cited to a doctest or marked as **untested**. The suites
live in `tests/` and run headless (no GL, no window) as part of `CosmicTests`.

| Header | Test file | Suite | Cases | Runs in the 2D suite? | Gaps |
| --- | --- | --- | --- | --- | --- |
| `math/Spatial.h` | `tests/test_spatial.cpp` | *(no `TEST_SUITE`; top-level cases)* | 5 | **yes** | `NedToRenderMatrix` only indirectly; `GravityMss` not asserted |
| `math/Integrators.h` | `tests/test_integrators.cpp` | `Integrators (E11)` | 5 | **yes** | none material |
| `math/Filters.h` | `tests/test_filters.cpp` | `Filters (E12)` | 9 | **yes** | **`Derivative` has NO test at all**; `SetHighPass` untested |
| `math/LookupTable.h` | `tests/test_lookuptable.cpp` | `LookupTable (E13)` | 7 *(one tests `DataExport::LoadCSV`, not the table)* | **yes** | `Extrapolate` untested on 2D; no missing-file `FromCSV` case |
| `math/Noise.h` | `tests/test_noise.cpp` | `Noise (E14)` | 7 | **yes** | `GetSeed` untested; the 256-unit lattice period untested |
| `math/Random.h` | `tests/test_random.cpp` | `Random (E15)` | 5 | **yes** | `Seed()` re-seeding an existing object untested |
| `math/Frustum.h` | `tests/test_frustum.cpp` | `Frustum (F5 culling)` | 6 | **NO — 3D-only tier** | no coverage in a 2D build |

The six E-series/Spatial files are listed in the **shared** `COSMIC_TEST_SOURCES` block
(`tests/CMakeLists.txt:12-22`), so they run in both configurations.
`test_frustum.cpp` sits in the 3D-only `if(NOT COSMIC_2D_ONLY)` block
(`tests/CMakeLists.txt:85-89`) — **`math/Frustum.h` itself compiles in a 2D tree but nothing tests
it there.** That is an artefact of the split, not a property of the header.

> ### ⚠ Verification gotcha — `doctest::Approx` is RELATIVE, not absolute
>
> Anyone checking the tolerances quoted below needs this. `doctest.h:4008-4011`:
>
> ```cpp
> return std::fabs(lhs - rhs.m_value) <
>        rhs.m_epsilon * (rhs.m_scale + std::max<double>(std::fabs(lhs), std::fabs(rhs.m_value)));
> ```
>
> `m_scale` defaults to `1.0` and `m_epsilon` to `FLT_EPSILON * 100 ≈ 1.19e-5`
> (`doctest.h:3987-3990`). So the effective tolerance is `epsilon * (1 + |expected|)` — **relative
> for large values, absolute-ish for small ones**. Concretely, in this chapter's own tests:
>
> | Assertion | Reads like | Actually is |
> | --- | --- | --- |
> | `Approx(90.0f).epsilon(0.01)` — `test_spatial.cpp:60` | ±0.01° | **±0.91°** |
> | `Approx(-3.01f).epsilon(0.05)` — `test_filters.cpp:61` | ±0.05 dB | **±0.2 dB** |
> | `Approx(1.0f/60.0f).epsilon(1e-4)` — `test_integrators.cpp:139` | 0.01 % of 16.7 ms | **±1.02e-4 s** (~60× looser) |
> | `Approx(0.0f).epsilon(1e-5)` — `test_noise.cpp:71` | — | ±1e-5 absolute (the `m_scale` term is the only reason a zero-target check works at all) |
>
> Do **not** tighten these by lowering `epsilon` without also calling `.scale(0.0)`, and do not read
> a passing `Approx` as a bit-exactness claim. Where this chapter says *bit-exact*, the test uses
> `==` on integers (`test_random.cpp:27`) or on floats (`test_noise.cpp:23-25`), never `Approx`.

---

## Determinism

The property this whole tier exists to protect: **the same seed and the same step schedule must
produce the same numbers on a later run.** Phase 30's fuzz campaigns
([`../plans/29-phase30-2d-hardening-plan.md`](../plans/29-phase30-2d-hardening-plan.md)) are built
on exactly this, so the boundary is worth stating precisely rather than generously.

**Tier 1 — bit-exact, and pinned by a test.**

| Call | Why it holds | Pinned by |
| --- | --- | --- |
| `Random::NextUInt32()` | PCG32 XSH-RR is pure 64-bit integer arithmetic — no float anywhere | `test_random.cpp` → `"fixed seed reproduces the canonical PCG32 reference sequence"` (seed 42, stream 54 vs. the PCG author's published vector) |
| `Random::NextUInt32(bound)`, `RangeInt` | integer arithmetic + rejection loop | `test_random.cpp` → `"NextFloat stays in [0, 1); Range respects bounds"` (bounds only) |
| `Random::NextFloat`, `Range` | integer draw × an exact power-of-two scale (`1/2²⁴`) | as above |
| `Noise`'s permutation table | Fisher–Yates driven by PCG32 — the header refuses `std::shuffle` + `std::mt19937` for this reason (`Noise.h:38-39`) | `test_noise.cpp` → `"same seed => identical field…"` uses `==`, not `Approx` |

**Tier 2 — deterministic under IEEE-754, i.e. on every supported target, but not pinned to a
committed vector.**

- Every `Noise` sampler (`Value*`, `Perlin*`, `Fbm*`, `Ridged2D`): only `+ - * /`, comparisons,
  `std::fabs` and `std::clamp`, all correctly rounded.
- `Random::InUnitSphere`, `OnUnitSphere`, `InUnitDisc`: the above plus `std::sqrt`, which IEEE-754
  requires to be correctly rounded.
- `Math::NedToRender`, `RenderToNed`, `NedToRenderMatrix`: sign flips and component swaps — exact.
- `Math::IntegrateBodyRate`: multiply/add plus `glm::normalize` (a `sqrt` and a divide).
- `Frustum`, the integrators, and every filter's *update* path — for a given build.

**Tier 3 — rests on your C runtime, not on the standard. Same toolchain: reproducible. Different
libm: may differ in the last bits.**

| Call | The libm dependency |
| --- | --- |
| `Random::Gaussian` | `std::log`, `std::sin`, `std::cos` |
| `LowPassFilter::Update` | `std::exp` (per call — the one recursive filter that pays this every step) |
| `Biquad::SetLowPass` / `SetHighPass` / `SetNotch` | `std::sin`, `std::cos` in the coefficient setup; a recursive filter then diverges slowly from a last-bit coefficient difference |
| `Math::QuatFromEulerZYX`, `EulerZYXFromQuat` | `sin`/`cos` via `glm::angleAxis`; `std::atan2`, `std::asin` |

`Random.h:10-12`'s promise — *"a seed produces the same sensor noise everywhere, forever"* — is
therefore **slightly over-stated for `Gaussian` specifically**. It holds exactly for the integer and
uniform-float API.

**Rules for seeded work.**

1. **Seed explicitly, always.** `Random`'s default constructor uses the fixed constants
   `seed = 0x853c49e6748fea9b`, `stream = 0xda3e39cb94b95bdb` (`Random.h:32`). That is deterministic,
   but it is also *the same stream for every default-constructed instance in the process* — two
   subsystems that both default-construct silently draw the identical sequence.
2. **One stream per consumer**, via the second constructor argument. Adding a consumer to a shared
   stream shifts everyone downstream and invalidates every replay recorded before the change.
   `Projects/ViperSim/src/sim/Sensors.h:73-79` is the reference shape (streams 1–6, one per sensor).
3. **Never `std::random_device`, and never time-based seeding, in simulation or fuzz code.** Four
   in-tree files do use it and none of them are a pattern to copy into a harness:
   `Cosmic/src/core/UUID.cpp:15-18` (engine entity UUIDs — so **a UUID is not reproducible across
   runs**; anything that hashes one into a simulation is a determinism leak),
   `Cosmic/templates/ExampleProject/src/AgentSystem.h:93`, `.../TemplateRenderBenchmarkLayer.h:251`
   and `.../TemplateTelemetryLayer.cpp:49`. Doc 29 §0 ("Fuzz must be seeded") states the same rule
   for Phase 30, and adds: record the seed in the test name or a `CAPTURE`.
4. **Record the seed with the run.** A seed you cannot recover is a seed you do not have.
5. **Nothing here survives fast-math.** Contraction/reassociation (`/fp:fast`, `-ffast-math`) changes
   float results in tiers 2 and 3 alike. Cosmic does not enable it; if you do, determinism is yours
   to defend.

Two draw-count subtleties that break naive "N calls = N draws" reasoning without breaking
determinism: `Gaussian` is Box–Muller **with spare caching**, so every *other* call consumes nothing;
and `NextUInt32(bound)`, `InUnitSphere`, `OnUnitSphere`, `InUnitDisc` all **reject and retry**, so the
underlying draw count per call is variable.

---

## Frames & attitude — `math/Spatial.h`

Everything here lives in namespace **`Cosmic::Math`** (not `Cosmic`), is `inline`, pure, and
allocation-free. This header is the **one authoritative statement of the engine's coordinate
conventions**; the renderer, the cameras and every in-tree simulation agree with it, and
`camera/PerspectiveCamera.h:17` points back at it by name.

### The two frames

| | World frame (simulation) | Render frame (`TransformComponent`, cameras, `Renderer3D`) |
| --- | --- | --- |
| Name | **NED** — the aviation standard | Y-up |
| Handedness | **right-handed** (N × E = D) | **right-handed** (X × Y = Z) |
| `+X` | North | East |
| `+Y` | East | **Up** |
| `+Z` | **Down** | South |
| Gravity | `+Z`, magnitude [`GravityMss`](#mathgravitymss) | `−Y` |
| Mapping | — | `render(x, y, z) = (ned.e, −ned.d, −ned.n)` |

Euler convention throughout: **ZYX intrinsic** (yaw ψ about Z, then pitch θ about Y, then roll φ
about X), **degrees at the API boundary**, radians internally. glm's own conventions still apply:
`glm::quat` is constructed `(w, x, y, z)`, and `q1 * q2` applies `q2` first.

> **`TransformComponent::Position` is render-frame, not NED.** A simulation owns its NED state and
> converts at draw time, in one place, once per frame. Both in-tree simulations are written that way
> — `Projects/ViperSim/src/screens/FlightScreen.cpp:75-76` and
> `Projects/Engine3DDemo/src/Engine3DDemo.cpp:417-418`. Mixing the two frames in one variable is the
> single most common bug in this area, and it is silent: `+Z` means *Down* in one frame and *South*
> in the other.

Test file: `tests/test_spatial.cpp` (5 top-level `TEST_CASE`s, no `TEST_SUITE` wrapper).

### `Math::GravityMss`

```cpp
inline constexpr float GravityMss = 9.80665f;
```

**What it does** — standard gravity magnitude in m/s². It is a *magnitude*: the direction is `+Z`
(Down) in NED, and you supply the sign yourself.

**Why you'd use it** — so a project never hard-codes `9.81`. Reach for `PhysicsSettings::Gravity`
([physics.md](physics.md)) instead when the rigid-body engine is doing the integrating; use
`GravityMss` when you integrate yourself.

**Example**

```cpp
// NED: gravity is +Z. ViperSim, sim/ComposableDynamics.cpp:45.
forceNedExtra.z += mass * Cosmic::Math::GravityMss;

// Removing gravity to get specific force in the body frame — sim/Sensors.h:120.
const glm::vec3 accelBody = glm::inverse(attNed) *
    (accelNed - glm::vec3(0.0f, 0.0f, Cosmic::Math::GravityMss));
```

**Notes & pitfalls**
- `constexpr`, so it costs nothing and can size arrays or feed other `constexpr`.
- In the **render** frame the same vector is `(0, −9.80665, 0)`.
- Not asserted by any test — it is a literal.

### `Math::QuatFromEulerZYX`

```cpp
inline glm::quat QuatFromEulerZYX(const glm::vec3& eulerDeg)
```

**What it does** — builds an attitude quaternion from ZYX intrinsic Euler angles in **degrees**.
Returns `qYaw * qPitch * qRoll`, so roll is applied first.

**Why you'd use it** — to turn human-authored angles (a spawn attitude, an inspector field, a replay
row) into the quaternion the rest of the math wants. The inverse is
[`EulerZYXFromQuat`](#matheulerzyxfromquat).

**Example**

```cpp
// A tailsitter standing on its pad, nose up, heading north.
// ViperSim, SimHub.cpp:176.
init.attNed = Cosmic::Math::QuatFromEulerZYX({ 0.0f, 90.0f, 0.0f });
```

**Notes & pitfalls**
- **The `glm::vec3` is `(roll, pitch, yaw)` = `(x, y, z)`**, even though the *rotation* order is
  yaw-first. Easy to write backwards; nothing will diagnose it for you.
- Degrees, not radians.
- Always returns a unit quaternion (a product of three unit `angleAxis` results).
- Round-trip verified by `test_spatial.cpp` → `"Euler ZYX <-> quaternion round-trips away from the
  gimbal poles"`, which compares via `|dot(q, q2)| > 1 − 1e-5` rather than by angle, to sidestep
  wrapping equivalences. Handedness verified by `"Pure yaw rotates North toward East (NED
  handedness)"`: `+90°` yaw sends body `+X` (North) to `(0, 1, 0)` (East).

### `Math::EulerZYXFromQuat`

```cpp
inline glm::vec3 EulerZYXFromQuat(const glm::quat& q)
```

**What it does** — extracts ZYX Euler angles in **degrees** from a unit quaternion. Returns
`(roll φ, pitch θ, yaw ψ)`.

**Why you'd use it** — for display (an attitude readout), for a control law written in angles, or to
drive per-axis actuators. `Projects/ViperSim/src/fc_glue/RigOutput.h:55` uses it to convert the
truth attitude into three servo commands before rate-limiting each axis.

**Example**

```cpp
const glm::vec3 eulerDeg = Cosmic::Math::EulerZYXFromQuat(attNed);
ImGui::Text("roll %.1f  pitch %.1f  yaw %.1f", eulerDeg.x, eulerDeg.y, eulerDeg.z);
```

**Notes & pitfalls**
- **Pitch is clamped to ±90°** — `Spatial.h:69` clamps the `asin` argument, so a round-trip through
  the gimbal poles is *not* exact. The doctest deliberately samples away from the poles.
- **Assumes a unit quaternion.** The formula uses the normalized-form identities; feeding a
  non-unit quaternion returns wrong angles silently, with no assert and no log.
- Returns degrees, matching `TransformComponent::Rotation`.
- Yaw and roll are returned via `atan2`, so they live in `(−180°, 180°]` and **wrap**. See the
  [`RateLimiter` wrap trap](#ratelimiter).

### `Math::IntegrateBodyRate`

```cpp
inline glm::quat IntegrateBodyRate(const glm::quat& q, const glm::vec3& omegaBody, float dt)
```

**What it does** — advances an attitude quaternion by a body-frame angular rate over `dt`. Standard
quaternion kinematics `q̇ = ½ · q ⊗ (0, ω_body)`, a first-order step, **renormalised on every call**.

**Why you'd use it** — this is how you integrate attitude. Do **not** hand quaternion components to
[`IntegrateRK4`](#integraterk4): treating `w,x,y,z` as a vector state drifts off the unit sphere and
is not what the kinematics say. Integrate attitude here, RK4 the translational state, and run both
at the same substep `h`.

**Example**

```cpp
// ViperSim, sim/ComposableDynamics.cpp:177 — one substep of the attitude half.
m_State.attNed = Cosmic::Math::IntegrateBodyRate(m_State.attNed, m_State.omegaBody, h);
```

**Notes & pitfalls**
- `omegaBody` is **radians per second, in the BODY frame** — not degrees, not world.
- First order. Accurate at the small `dt` of a fixed-step sim; wrap it in RK4 at the call site if you
  need more.
- The renormalise makes it self-correcting, so long runs do not lose unit length.
- `test_spatial.cpp` → `"IntegrateBodyRate accumulates a constant rate to the expected angle"` spins
  at π/2 rad/s for 1 s in 1000 steps and checks 90° yaw plus `|q| == 1`. (Note the 90° check's real
  tolerance is ±0.91° — see the [`Approx` gotcha](#test-coverage-at-a-glance).)

### `Math::NedToRender`

```cpp
inline glm::vec3 NedToRender(const glm::vec3& ned)
```

**What it does** — converts a NED vector `(N, E, D)` into the render frame: returns
`(ned.y, -ned.z, -ned.x)` = `(E, −D, −N)`.

**Why you'd use it** — the sim→render boundary, every frame. Positions, velocities to draw as
arrows, camera eye/target/up vectors: anything crossing from simulation state into a
`TransformComponent`, a `Renderer3D` call or a camera. Use
[`NedQuatToRender`](#mathnedquattorender) for attitudes and
[`NedToRenderMatrix`](#mathnedtorendermatrix) for whole bases.

**Example**

```cpp
auto& tc = m_Aircraft.GetComponent<Cosmic::TransformComponent>();
tc.Position         = Cosmic::Math::NedToRender(m_PosNed);
tc.RotationQuat     = Cosmic::Math::NedQuatToRender(m_AttNed);
tc.UseQuatRotation  = true;
```

**Notes & pitfalls**
- Exact inverse of [`RenderToNed`](#mathrendertoned) — sign flips and swaps only, no rounding.
- It converts **vectors**, so it is equally valid for a direction. ViperSim's FPV camera converts an
  eye point, a target point *and* an up direction with the same call
  (`screens/FlightScreen.cpp:93-95`).
- `test_spatial.cpp` → `"NED <-> render frame mapping and round-trip"` pins all three basis images
  (N→`-Z`, E→`+X`, D→`-Y`) and both round-trips.

### `Math::RenderToNed`

```cpp
inline glm::vec3 RenderToNed(const glm::vec3& render)
```

**What it does** — the inverse: returns `(-render.z, render.x, -render.y)`.

**Why you'd use it** — when something authored or picked in render space has to enter the
simulation: a click-picked world point, a gizmo-dragged waypoint, a designer-placed marker whose
`TransformComponent` is the source of truth.

**Example**

```cpp
// A waypoint the user dragged in the editor, fed back to the sim.
const glm::vec3 waypointNed = Cosmic::Math::RenderToNed(marker.GetComponent<Cosmic::TransformComponent>().Position);
```

**Notes & pitfalls**
- Exact inverse of `NedToRender`; both directions are covered by the same doctest.

### `Math::NedToRenderMatrix`

```cpp
inline const glm::mat3& NedToRenderMatrix()
```

**What it does** — returns the NED→render basis change as a `glm::mat3`. Columns are the images of
N, E and D: `N→(0,0,−1)`, `E→(1,0,0)`, `D→(0,−1,0)`. Determinant `+1` (a pure rotation, no
reflection — which is what makes both frames right-handed).

**Why you'd use it** — when you need to rotate an entire basis, inertia tensor or covariance rather
than a single vector, or when you want the change of basis as a matrix to compose with others. For
one vector, [`NedToRender`](#mathnedtorender) is cheaper.

**Example**

```cpp
const glm::mat3& C = Cosmic::Math::NedToRenderMatrix();
const glm::mat3 covRender = C * covNed * glm::transpose(C);   // rotate a covariance
```

**Notes & pitfalls**
- Returns a reference to a **function-local `static`**: initialised once (thread-safe under C++11
  magic statics), stable address, never mutate it through a `const_cast`.
- **Not directly asserted by any test.** It is exercised only indirectly, through
  `NedQuatToRender`'s doctest. The `det = +1` claim above is from the header comment plus the
  column values, verified by hand — not by a test.

### `Math::NedQuatToRender`

```cpp
inline glm::quat NedQuatToRender(const glm::quat& qNed)
```

**What it does** — converts an attitude expressed over NED axes into the render frame, as the change
of basis `q_render = C ⊗ q_ned ⊗ C⁻¹` with `C = quat_cast(NedToRenderMatrix())`.

**Why you'd use it** — to drive a rendered model's orientation from a simulation attitude. Pair it
with `NedToRender` for the position and set `TransformComponent::UseQuatRotation = true`.

**Example**

```cpp
// ViperSim, screens/ReplayScreen.cpp:92-95 — a recorded row, drawn.
const glm::quat attNed = Cosmic::Math::QuatFromEulerZYX({ rollDeg, pitchDeg, yawDeg });
posR = Cosmic::Math::NedToRender(nedPosition);
attR = Cosmic::Math::NedQuatToRender(attNed);
```

**Notes & pitfalls**
- **A conjugation, not a component swap.** `NedQuatToRender(q)` is *not* `quat(NedToRender(...))` of
  anything — do not try to shortcut it.
- `C` is a function-local `static` computed once.
- `test_spatial.cpp` → `"NedQuatToRender maps a NED yaw onto render axes consistently"`: a 90° NED
  yaw must send render `-Z` (North) to `+X` (East).

---

## Integrators — `math/Integrators.h`

Namespace `Cosmic`. Two free function templates and one small class, no engine dependencies (the
header includes only `<type_traits>`). Test file: `tests/test_integrators.cpp`, suite
`"Integrators (E11)"`.

### The state-type contract

There is no base class, no traits block and no concept — just duck typing. A `State` used with
[`IntegrateRK4`](#integraterk4) must support exactly:

| Expression | Meaning |
| --- | --- |
| `State + State` | component-wise sum |
| `State * float` | scale — **scalar on the right**, and only on the right |

`float` and `glm::vec2/3/4` satisfy that out of the box. A struct needs two operators; the doctest's
own two-field state (`test_integrators.cpp:16-21`) is the minimal example, and it declares them as
members rather than free functions — both work.

`deriv` must be callable as `deriv(const State&, float) -> State`, and `accel` as
`accel(const Pos&, const Vel&, float) -> Vel`.

### `IntegrateRK4`

```cpp
template<typename State, typename DerivFn>
inline State IntegrateRK4(const State& state, DerivFn&& deriv, float t, float dt);
```

**What it does** — one classic fourth-order Runge–Kutta step. Evaluates `deriv` four times (at `t`,
`t + dt/2` twice, and `t + dt`) and returns the advanced state. Global error `O(h⁴)`.

**Why you'd use it** — you have a differential equation and a fixed step, and you want accuracy per
step. Reach for [`IntegrateSemiImplicitEuler`](#integratesemiimpliciteuler) instead when the system
is springy and you care more about not exploding than about order, or when one derivative evaluation
per step is all you can afford. Reach for [`Math::IntegrateBodyRate`](#mathintegratebodyrate)
for attitude.

**Example**

```cpp
// ViperSim, sim/ComposableDynamics.cpp:181-191 — the translational half of a 6-DOF airframe,
// with attitude frozen for the substep (it advances separately at the same h).
struct PV { glm::vec3 p, v; };
inline PV operator+(const PV& a, const PV& b) { return { a.p + b.p, a.v + b.v }; }
inline PV operator*(const PV& a, float s)     { return { a.p * s,   a.v * s   }; }

PV s{ m_State.posNed, m_State.velNed };
auto deriv = [this](const PV& st, float /*t*/) -> PV
{
    glm::vec3 accel, unusedTorque;
    AccumulateForces(st.p, st.v, m_State.attNed, m_State.omegaBody, accel, unusedTorque);
    return { st.v, accel };
};
s = Cosmic::IntegrateRK4(s, deriv, 0.0f, h);
```

**Notes & pitfalls**
- **`deriv` is called four times per step, at intermediate states, and must be pure with respect to
  that state.** Accumulate a side effect inside it and you apply it four times.
- **Never RK4 quaternion components** — the header says so at `Integrators.h:14-16` and it is right.
- Returns by value; the input `state` is untouched.
- `dt` is not validated. A negative `dt` integrates backwards (occasionally useful, usually a bug);
  `dt = 0` returns `state + 0`, i.e. the state, modulo float rounding.
- Failure mode: none at runtime. A `State` missing an operator is a **compile** error, and the
  message points at the `k1 * (dt * 0.5f)` line rather than at your type.
- Pinned by three cases: `"RK4 projectile matches the exact solution"` (quadratic dynamics — exact to
  float rounding), `"RK4 mass-spring-damper decays like the analytic envelope"` (matches the
  closed-form solution at t = 2 s and loses energy, as damping requires), and
  `"RK4 error scales ~O(h^4): halving h shrinks error ~16x"` (integrating `dx/dt = x` to `x(1) = e`;
  the measured ratio is required to land between 10 and 24, theory says 16, the slack is float
  precision).

### `IntegrateSemiImplicitEuler`

```cpp
template<typename Pos, typename Vel, typename AccelFn>
inline void IntegrateSemiImplicitEuler(Pos& pos, Vel& vel, AccelFn&& accel, float t, float dt);
```

**What it does** — one symplectic Euler step, **in place**: velocity from acceleration first, then
position from the *new* velocity. `accel(pos, vel, t)` returns `d(vel)/dt`.

**Why you'd use it** — first order, but **energy-bounded**: on an oscillatory system energy wobbles
around its initial value instead of growing, which is why explicit Euler is unusable there and this
is not. One derivative evaluation per step instead of four. Reach for
[`IntegrateRK4`](#integraterk4) when accuracy per step matters more than stability.

**Example**

```cpp
// A damped mass-spring. (No in-tree consumer — this is written against the header.)
float pos = 1.0f, vel = 0.0f;
auto accel = [](float p, float v, float /*t*/) { return -50.0f * p - 0.5f * v; };
Cosmic::IntegrateSemiImplicitEuler(pos, vel, accel, m_Time, dt);
```

**Notes & pitfalls**
- **Mutates its first two arguments** and returns `void` — the odd one out in this header.
- `Pos` and `Vel` are independent template parameters, so `Pos = glm::vec3, Vel = glm::vec3` is the
  common case but nothing forces them to match.
- Requires `Vel + Vel`, `Vel * float`, `Pos + Vel`.
- Symplectic ≠ accurate: the *phase* drifts even though the energy does not.
- `test_integrators.cpp` → `"semi-implicit Euler stays energy-bounded on an undamped oscillator"`
  runs 10 s at 240 Hz and requires peak energy below `1.10 × E₀`.
- No in-tree consumer outside this test.

### `FixedSubstepper`

```cpp
class FixedSubstepper
{
public:
    template<typename StepFn> void Run(float dt, int substeps, StepFn&& step);
    void  Reset();
    float GetResidual() const;
};
```

A four-byte value type (one `float` residual) that turns "N substeps inside one tick" into a verb.
Copyable, default-constructible, no allocation. Own one per simulated object; ViperSim's
`ComposableDynamics` holds one (`sim/ComposableDynamics.h:127`).

#### `FixedSubstepper::Run`

```cpp
template<typename StepFn>
void Run(float dt, int substeps, StepFn&& step)
```

**What it does** — adds `dt` to an internal residual, computes `h = dt / substeps`, and calls
`step(h)` repeatedly while the residual is at least `h`, decrementing as it goes. Leftover time
carries into the next call, so non-divisible deltas do not drift the sim clock.

**Why you'd use it** — your solver needs a finer step than the engine's fixed tick. Call it from
`OnFixedUpdate` where `dt` is exactly `1/FixedHz`; see
[`../guide/time-and-ticks.md`](../guide/time-and-ticks.md) for the tick contract.

**Example**

```cpp
// ViperSim, sim/ComposableDynamics.cpp:211-216 — 60 Hz tick, 480 Hz solver.
void ComposableDynamics::Step(const ActuatorFrame& u, float dt)
{
    m_LastU = u;
    const int substeps = std::max(m_Params.substeps, 1);
    m_Substepper.Run(dt, substeps, [this](float h) { Integrate(h); });
}
```

**Notes & pitfalls**
- **`h` comes from *this call's* `dt`, not from a fixed rate.** Called from `OnUpdate`, `h` changes
  every frame and "480 Hz" becomes a fiction. `FixedSubstepper` divides a tick; it does not create
  one.
- **The call count is not guaranteed to equal `substeps`.** It is `floor((residual + dt) / h)`,
  which is `substeps` plus whatever the carried residual pays for — that is the whole point of the
  residual. The doctest's `calls == 8` holds because the substepper is *fresh*.
- **A shrinking `dt` with a carried residual produces a burst.** The extra calls beyond `substeps`
  are bounded by `dt_prev / dt_new`, so a 100 ms frame followed by a 1 ms frame can fire ~100 extra
  substeps. Budget for it, or `Reset()` after a hitch.
- **`substeps < 1` is clamped to 1** *before* `h` is computed, so `Run(dt, 0, …)` behaves as
  `Run(dt, 1, …)`.
- **A non-positive `h` returns without stepping — but only after `m_Residual += dt` has already
  run** (`Integrators.h:79-82`). So `dt = 0` is harmless, and a **negative `dt` silently subtracts
  from the residual** and is never validated. See [Known rough edges](#known-rough-edges).
- Failure mode: none. It logs nothing and cannot fail; a wrong `dt` just produces a wrong number of
  steps.
- `test_integrators.cpp` → `"FixedSubstepper runs N equal substeps and carries residual"`: a fresh
  substepper at `1/60` with 8 substeps makes exactly 8 calls at exactly `1/480`, and 100 calls of
  `0.0100 s` at 4 substeps satisfy `integrated + GetResidual() ≈ 1.0` — no time lost across calls.

#### `FixedSubstepper::Reset` / `GetResidual`

```cpp
void  Reset();                  // drop any accumulated residual
float GetResidual() const;      // seconds of un-stepped time carried
```

**What it does** — `Reset` zeroes the carried time; `GetResidual` reads it.

**Why you'd use it** — `Reset` on a simulation reset, a scene reload, or after a long stall, so the
first `Run` afterwards does not fire a burst of catch-up steps. `GetResidual` is for accounting: it
is what makes "no time was lost" checkable, as the doctest does.

**Example**

```cpp
void ComposableDynamics::Reset(const RigidState& init)
{
    m_State = init;
    m_Substepper.Reset();     // sim/ComposableDynamics.h:89
}
```

**Notes & pitfalls**
- `Reset()` takes no argument — unlike every `Reset(value)` in [`Filters.h`](#filters--mathfiltersh).
- After a `Run`, `GetResidual()` is always in `[0, h)`.

---

## Filters — `math/Filters.h`

Namespace `Cosmic`. Six classes plus one constant, all header-only and allocation-free (`MovingAverage`
owns a `std::array`, everything else is a handful of floats). Every one is a copyable value type.
Test file: `tests/test_filters.cpp`, suite `"Filters (E12)"`.

### The shared contract

Every filter here follows the same shape, so learn it once:

| Call | Behaviour |
| --- | --- |
| `Reset(value = 0.0f)` | re-seed the internal state |
| `Update(sample, dt)` | advance by `dt` seconds, return the new output |
| `GetValue() const` | read the current output without advancing |

Plus two rules that are easy to miss:

1. **`dt <= 0` returns the current output unchanged** — a paused frame does not corrupt state. Two
   exceptions below.
2. **The first `Update` primes the filter from its sample**, so there is no startup transient from
   zero, and priming happens *before* the `dt <= 0` check — a first `Update` with `dt = 0` still
   primes. Each type primes to a different value: `LowPassFilter` → the sample, `RateLimiter` → the
   target, `Derivative` → `0`, `Washout` → `0`.

`Biquad` and `MovingAverage` take a `dt` parameter **for uniformity and ignore it**; both default it
to `0.0f`, so `Update(sample)` is the honest call for those two.

### `kFilterTwoPi`

```cpp
constexpr float kFilterTwoPi = 6.28318530717958647692f;
```

**What it does** — 2π as a `float` constant, used by the cutoff-Hz conversions and by the doctests
when they synthesise a test sine.

**Notes & pitfalls** — it lives in namespace `Cosmic`, not in `Cosmic::Math`. Verified indirectly by
`"LPF cutoff-Hz constructor maps to tau = 1/(2*pi*fc)"`.

### `LowPassFilter`

```cpp
class LowPassFilter
{
public:
    LowPassFilter() = default;                       // tau = 1.0 s
    explicit LowPassFilter(float tau);
    static LowPassFilter FromCutoffHz(float hz);     // tau = 1 / (2*pi*hz)

    void  SetTau(float tau);
    void  SetCutoffHz(float hz);
    float GetTau() const;

    void  Reset(float value = 0.0f);
    float Update(float sample, float dt);
    float GetValue() const;
};
```

**What it does** — first-order exponential smoothing: `y += alpha * (x - y)` with
`alpha = 1 - exp(-dt / tau)`. The step response reaches **63.2 % at `t = tau`** — that is the
defining property, and the one the doctest checks.

**Why you'd use it** — smoothing a noisy scalar (a sensor reading, a telemetry plot, a camera
follow). Reach for [`Biquad`](#biquad) when you need a steeper roll-off or a notch, and
[`MovingAverage`](#movingaveragen) when you want a plain window mean rather than an exponential one.

**Example**

```cpp
Cosmic::LowPassFilter altitude(0.25f);                       // tau, seconds
auto rate = Cosmic::LowPassFilter::FromCutoffHz(10.0f);      // think in bandwidth instead

const float smoothed = altitude.Update(rawAltitude, deltaTime);
```

**Notes & pitfalls**
- **`tau <= 0` is passthrough, not a divide-by-zero**, and the passthrough **wins over the `dt <= 0`
  freeze** (`Filters.h:58-62`): with `tau <= 0`, `Update` assigns the sample even when `dt` is zero.
- **`FromCutoffHz(0.0f)` / `SetCutoffHz(0.0f)` yields `tau = +inf`**, which makes `alpha = 0` — the
  filter freezes at its primed value forever. Not diagnosed, not logged. A negative Hz gives a
  negative tau, which lands in the passthrough branch instead.
- It calls `std::exp` **every update** — the one filter here that pays a libm call per step. See
  [Determinism](#determinism) tier 3.
- `SetTau`/`SetCutoffHz` retune live and do not disturb the current output.
- Pinned by `"LPF step response hits 63.2% at t = tau (within 1%)"`,
  `"LPF first sample primes the state (no transient from zero)"`, and
  `"LPF cutoff-Hz constructor maps to tau = 1/(2*pi*fc)"`.

### `Derivative`

```cpp
class Derivative
{
public:
    Derivative() = default;                  // smoothing tau = 0.02 s
    explicit Derivative(float tau);          // 0 = raw finite difference

    void  Reset(float value = 0.0f);
    float Update(float sample, float dt);
    float GetValue() const;
};
```

**What it does** — a raw finite difference `(sample - prev) / dt` pushed through an internal
[`LowPassFilter`](#lowpassfilter), so sensor noise does not explode into the rate estimate.

**Why you'd use it** — you need a rate and only have a position. Reach for `Derivative(0.0f)` if you
genuinely want the unfiltered difference; reach for [`Washout`](#washout) instead if what you
actually want is "remove the steady part", not "differentiate".

**Example**

```cpp
Cosmic::Derivative climbRate(0.05f);         // smoothing tau for the estimate
const float mps = climbRate.Update(altitudeMeters, dt);   // 0.0f on the very first call
```

**Notes & pitfalls**
- ⚠ **`Derivative` is the one class in this chapter with NO doctest.** Nothing in `tests/` names it,
  and nothing in the engine or in any in-tree project uses it. Treat its behaviour as *read from the
  header*, not as *verified*.
- ⚠ **`Reset(value)`'s argument has no observable effect.** `Reset` writes `value` to the previous
  sample but sets `m_Primed = false` (`Filters.h:89-94`), and the next `Update` overwrites the
  previous sample with its own argument before returning `0.0f`. So `Reset(5.0f)` and `Reset()` are
  indistinguishable. Every other filter here sets `m_Primed = true` in `Reset`. See
  [Known rough edges](#known-rough-edges).
- The first `Update` returns exactly `0.0f`; `dt <= 0` returns the last *filtered* value.
- `explicit Derivative(tau)` **replaces** the 0.02 s default rather than adding to it.
- Inherits `LowPassFilter`'s `std::exp` determinism caveat.

### `RateLimiter`

```cpp
class RateLimiter
{
public:
    RateLimiter() = default;                 // maxRate = 1.0 / s
    explicit RateLimiter(float maxRatePerSecond);

    void  SetMaxRate(float ratePerSecond);
    void  Reset(float value = 0.0f);
    float Update(float target, float dt);
    float GetValue() const;
};
```

**What it does** — clamps the output's change to `±maxRate * dt` per update, so the output slews
toward the target instead of jumping to it.

**Why you'd use it** — a command that must not step-jump: a servo, a throttle, a camera FOV, a UI
value being scrubbed. ViperSim runs one per gimbal axis so a buggy firmware command can never slam a
servo (`fc_glue/RigOutput.h:79`).

**Example**

```cpp
Cosmic::RateLimiter roll(120.0f), pitch(120.0f), yaw(120.0f);   // degrees per second

const glm::vec3 eulerDeg = Cosmic::Math::EulerZYXFromQuat(attNed);
const float r = roll .Update(eulerDeg.x, dt);
const float p = pitch.Update(eulerDeg.y, dt);
const float y = yaw  .Update(eulerDeg.z, dt);
```

**Notes & pitfalls**
- ⚠ **Angle wrap.** `RateLimiter` sees a plain `float` and knows nothing about ±180°: a yaw crossing
  `+179° → -179°` looks like a 358° error, and the limiter sweeps the long way at max rate. Unwrap
  the angle into a continuous value first, or limit the quaternion instead. This bites precisely
  because [`EulerZYXFromQuat`](#matheulerzyxfromquat) returns wrapped angles.
- A target *inside* the allowed step is reached exactly, not approached asymptotically.
- A negative `maxRate` inverts the clamp (`std::clamp` with `lo > hi` is **undefined behaviour**) —
  never pass one.
- `test_filters.cpp` → `"RateLimiter clamps the slew exactly"` checks both directions and the
  reached-exactly case.

### `MovingAverage<N>`

```cpp
template<size_t N>
class MovingAverage
{
public:
    void  Reset(float value = 0.0f);
    float Update(float sample, float dt = 0.0f);   // dt ignored
    float GetValue() const;
};
```

**What it does** — fixed-window boxcar mean over the last `N` samples, `O(1)` per update via a
running sum.

**Why you'd use it** — you want a plain N-sample mean with a hard window, not the exponential decay
of [`LowPassFilter`](#lowpassfilter). Good for a display readout or a duty-cycle estimate.

**Example**

```cpp
Cosmic::MovingAverage<16> frameMs;
frameMs.Update(deltaTime * 1000.0f);
ImGui::Text("frame %.2f ms", frameMs.GetValue());
```

**Notes & pitfalls**
- `N` is a compile-time `size_t` and `static_assert`s `N >= 1`.
- **`Reset(value)` seeds the *whole* window**, so `GetValue()` equals `value` immediately and the
  next samples *replace* slots rather than adding on top of a phantom fill. The header comment at
  `Filters.h:166-169` explains why `m_Count` must be `N` there; the doctest
  `"MovingAverage<4> Reset(value) seeds the whole window"` pins the exact sequence.
- **Before the window fills, `GetValue()` averages only the samples seen so far** — a fresh filter's
  first `Update` returns that sample. There is no `m_Primed` flag; the partial-fill count does the
  job.
- `dt` is accepted and ignored (there is no time constant), and it is defaulted — call
  `Update(sample)`.
- `GetValue()` on an untouched, un-`Reset` filter returns `0.0f`.
- Pinned by `"MovingAverage<4> averages the window, handles partial fill"` and the `Reset` case above.

### `Biquad`

```cpp
class Biquad
{
public:
    enum class Type { LowPass, HighPass, Notch };

    Biquad();                                                                   // = SetLowPass(100, 1000)

    void  SetLowPass (float cutoffHz, float sampleRateHz, float q = 0.70710678f);
    void  SetHighPass(float cutoffHz, float sampleRateHz, float q = 0.70710678f);
    void  SetNotch   (float centerHz, float sampleRateHz, float q = 10.0f);

    void  Reset(float value = 0.0f);
    float Update(float sample, float dt = 0.0f);   // dt ignored
    float GetValue() const;
};
```

**What it does** — a two-pole/two-zero filter in direct form I with RBJ-cookbook coefficients.
Low-pass, high-pass or notch, chosen by which setter you call.

**Why you'd use it** — a steeper roll-off than first-order smoothing (−12 dB/octave vs −6), or one
specific frequency to kill (mains hum, a rotor tone). Reach for
[`LowPassFilter`](#lowpassfilter) when first-order is enough and you want a time constant rather than
a sample rate.

**Example**

```cpp
Cosmic::Biquad gyroX;
gyroX.SetLowPass(80.0f, 480.0f);           // cutoff Hz, sample rate Hz — the sim's fixed rate
gyroX.SetNotch  (50.0f, 480.0f, 20.0f);    // or: kill mains hum, q = 20 (narrower)

const float filtered = gyroX.Update(rawGyroX);
```

**Notes & pitfalls**
- ⚠ **The sample rate is baked in at configure time.** `Update`'s `dt` is ignored entirely; if your
  step rate changes, call the setter again. In a fixed-step sim `1/dt` is constant and this is a
  non-issue — that is the case the header was written for.
- A default-constructed `Biquad` is `SetLowPass(100.0f, 1000.0f)`, not an identity filter. Configure
  before use.
- **`Reset(value)` sets the output state to `value * H(DC)`, not to `value`** — the steady-state
  output for a constant input. That is `value` for a low-pass or notch (both have unity DC gain by
  construction) and `0` for a high-pass.
- ⚠ **Degenerate configurations produce `NaN` and are not diagnosed.** `SetLowPass(0, fs)` (or any
  `freqHz` that is a multiple of `sampleRateHz`) gives `cos(w0) = 1`, which makes `Reset`'s DC-gain
  divide `0/0`. Cutoffs at or above Nyquist are likewise unstable. Nothing logs. See
  [Known rough edges](#known-rough-edges).
- `q` defaults differ per setter on purpose: `0.70710678` (Butterworth) for low/high-pass, `10.0` for
  the notch, where higher `q` means narrower.
- **`Biquad::Type` is public but unreachable** — no public method takes it, since `Configure` is
  private. The three setters are the only entry points.
- Pinned by `"Biquad LPF magnitude at cutoff is ~-3 dB"` (drive a sine at the cutoff, measure the
  steady-state peak) and `"Biquad notch kills the center frequency, passes DC"` (>26 dB rejection at
  the notch, unity at DC). **`SetHighPass` has no test.**

### `Washout`

```cpp
class Washout
{
public:
    Washout() = default;                     // tau = 1.0 s
    explicit Washout(float tau);

    void  SetTau(float tau);
    void  Reset(float value = 0.0f);
    float Update(float sample, float dt);
    float GetValue() const;
};
```

**What it does** — a first-order high-pass: `y = alpha * (y_prev + x - x_prev)` with
`alpha = tau / (tau + dt)`. Transients pass; a sustained input decays to zero.

**Why you'd use it** — the motion-cue filter (a sustained acceleration washes out so a platform can
recentre), and equally useful for removing a slow bias from a rate signal. Reach for
[`Derivative`](#derivative) when you want an actual rate rather than a de-biased signal.

**Example**

```cpp
Cosmic::Washout surgeCue(2.0f);              // tau, seconds
const float cue = surgeCue.Update(sustainedAccel, dt);   // decays toward 0
```

**Notes & pitfalls**
- **Primes to zero**, not to the sample: it starts washed out, so a constant input from frame one
  produces no output at all.
- **`Reset(value)` sets the previous *input* to `value` and the output to `0`** — the argument seeds
  `x_prev`, not `y`.
- `tau = 0` gives `alpha = 0`: the output is identically zero. No divide-by-zero unless
  `tau == -dt` exactly.
- Larger `tau` = slower washout = more low frequency retained.
- `test_filters.cpp` → `"Washout passes transients and decays steady input to zero"`: a step passes
  at >0.9 immediately and is below 0.01 after 5 s at `tau = 0.2`.

---

## Lookup tables — `math/LookupTable.h`

Namespace `Cosmic`. Measured data belongs in a table, not in code: aero polars, motor thrust maps,
gain schedules, battery discharge curves are **app data**, and the engine's job is to interpolate
them. Test file: `tests/test_lookuptable.cpp`, suite `"LookupTable (E13)"`.

This is the only header in the chapter with engine dependencies (`DataExport`, `FileSystem`, `Log`),
and only [`LookupTable1D::FromCSV`](#lookuptable1dfromcsv) uses them.

### `TableRangePolicy`

```cpp
enum class TableRangePolicy
{
    Clamp,        // hold the first/last value outside the breakpoint range
    Extrapolate,  // extend the first/last segment's slope
};
```

**What it does** — chooses out-of-range behaviour, per table, at construction.

**Why you'd use it** — `Clamp` (the default) for anything physical where the edge value is the
sensible answer beyond the data. `Extrapolate` when the curve genuinely continues and you would
rather have a plausible wrong answer than a flat one.

**Notes & pitfalls**
- It applies to **both** axes of a [`LookupTable2D`](#lookuptable2d) — there is no per-axis policy.
- Mechanically it is applied by *not* clamping the interpolation parameter `t` to `[0, 1]`
  (`LookupTable.h:54-63`); the bracketing segment is always the first or last one.
- Both policies pinned by `"1D: clamp holds edges; extrapolate continues the edge slope"`.
  **`Extrapolate` is untested on `LookupTable2D`.**

### `LookupTable1D`

```cpp
class LookupTable1D
{
public:
    LookupTable1D() = default;
    LookupTable1D(std::vector<std::pair<float, float>> points,
                  TableRangePolicy policy = TableRangePolicy::Clamp);

    static LookupTable1D FromCSV(const std::string& filepath,
                                 TableRangePolicy policy = TableRangePolicy::Clamp);

    bool   IsValid() const;      // size >= 2
    size_t Size()    const;
    float  Sample(float x) const;
};
```

`y = f(x)` by linear interpolation over sorted breakpoints. A copyable value type holding two
`std::vector<float>`s; a default-constructed one is empty and `Sample`s to `0.0f`.

#### `LookupTable1D::LookupTable1D`

```cpp
LookupTable1D(std::vector<std::pair<float, float>> points,
              TableRangePolicy policy = TableRangePolicy::Clamp);
```

**What it does** — takes `(x, y)` points **by value**, `std::stable_sort`s them by `x`, and splits
them into parallel x/y arrays.

**Why you'd use it** — an in-code curve, or points you assembled at runtime. For a curve that lives
on disk, use [`FromCSV`](#lookuptable1dfromcsv).

**Example**

```cpp
// Points need not arrive sorted.
Cosmic::LookupTable1D thrust({ {0.0f, 0.0f}, {0.5f, 6.9f}, {1.0f, 14.6f} });
const float newtons = thrust.Sample(0.75f);      // 10.75
```

**Notes & pitfalls**
- **Duplicate `x` values form a step, not an average.** The sort is stable and lookup uses
  `upper_bound`, so an exact hit lands on the **last** duplicate: `{{1,0},{1,10}}` jumps.
- **Zero or one point is accepted silently** — no log, no throw. `IsValid()` reports `false` and
  `Sample` returns `0.0f` (empty) or that single point's `y`. This differs from
  [`LookupTable2D`](#lookuptable2d), which *does* log its shape errors.
- Takes `points` by value and moves out of it — pass an rvalue or expect a copy.
- Pinned by `"1D: exact at breakpoints, linear at midpoints"` and
  `"1D: unsorted input points are sorted by x"`.

#### `LookupTable1D::FromCSV`

```cpp
static LookupTable1D FromCSV(const std::string& filepath,
                             TableRangePolicy policy = TableRangePolicy::Clamp);
```

**What it does** — loads a two-column CSV (header row optional) into a table. Resolves `filepath`
through `FileSystem::Resolve` first, then hands it to `DataExport::LoadCSV`.

**Why you'd use it** — the curve is data, so it should ship as data and be editable without a
rebuild. Pair it with [`Config`](assets-io.md) for scalars.

**Example**

```cpp
auto polar = Cosmic::LookupTable1D::FromCSV("project://polars/cl.csv");
if (!polar.IsValid())
    CS_WARN("polar missing — falling back to the built-in curve");
```

**Failure mode** — returns an **empty, valid-but-useless object** (never throws, never returns
`nullptr`): `IsValid()` is `false`, `Sample` returns `0.0f`, and `CS_CORE_ERROR` names the path.
Triggered when the file is missing, has fewer than two columns, or has fewer than two data rows.

**Notes & pitfalls**
- **It resolves the VFS path itself**, unlike `DataExport::LoadCSV` underneath it, which does no
  resolution at all. A raw absolute path passes through `Resolve` unchanged, which is how the doctest
  loads a temp file.
- ⚠ **The header comment at `LookupTable.h:95-96` is stale.** It says `project://` "resolves against
  the CALLING DLL's active project" — that DLL-side rule was retired in Phase 20/A1 when the mount
  moved into the engine DLL. There is now **one active project per process**. Do not carry the old
  rule forward; see [assets-io.md](assets-io.md).
- **Columns past the second are ignored** — `cols[0]` is x, `cols[1]` is y, the rest is dropped
  without comment.
- Values are read as `double` by the CSV loader and narrowed to `float` here.
- `"1D: FromCSV round-trip (with header row)"` covers the happy path. **There is no doctest for the
  missing-file or one-column failure path** — the failure behaviour above is read from
  `LookupTable.h:101-105`.

#### `LookupTable1D::Sample`

```cpp
float Sample(float x) const;
```

**What it does** — linear interpolation on the bracketing segment, honouring the table's
`TableRangePolicy` outside the breakpoint range.

**Notes & pitfalls**
- `const` and allocation-free — safe to call from many threads on a shared table, and safe inside a
  `ParallelFor`.
- Returns `0.0f` on an empty table and `m_Y[0]` on a single-point one — **both are indistinguishable
  from a legitimate result**, so check `IsValid()` once at load time rather than per sample.

#### `LookupTable1D::IsValid` / `Size`

```cpp
bool   IsValid() const;   // m_X.size() >= 2
size_t Size()    const;   // number of breakpoints
```

**Notes & pitfalls** — `IsValid()` means "has at least two breakpoints", i.e. "can interpolate". A
one-point table is *not* valid even though `Sample` returns something sensible.

### `LookupTable2D`

```cpp
class LookupTable2D
{
public:
    LookupTable2D() = default;
    LookupTable2D(std::vector<float> xBreaks,
                  std::vector<float> yBreaks,
                  std::vector<float> values,
                  TableRangePolicy policy = TableRangePolicy::Clamp);

    bool  IsValid() const;
    float Sample(float x, float y) const;
};
```

**What it does** — `z = f(x, y)` by bilinear interpolation over a rectangular grid. `values` is
**row-major**: `values[ix * yBreaks.size() + iy]`.

**Why you'd use it** — a gain schedule over (alpha, speed), a thrust map over (throttle, altitude),
anything measured on a grid.

**Example**

```cpp
// alpha x speed gain schedule.
Cosmic::LookupTable2D gains(alphaBreaks, speedBreaks, values);
const float kp = gains.Sample(alphaDeg, airspeed);
```

**Failure mode** — a bad shape leaves the table **empty**: `CS_CORE_ERROR` with the offending
dimensions, `IsValid()` false, `Sample` returns `0.0f`. "Bad shape" means fewer than two breakpoints
on either axis, `values.size() != xBreaks.size() * yBreaks.size()`, or either breakpoint vector not
ascending.

**Notes & pitfalls**
- ⚠ **Transposing `values` produces a plausible-looking surface that is wrong everywhere**, and the
  shape check cannot catch it when the grid is square.
- **There is no `FromCSV` for 2D.** Assemble the vectors yourself.
- `std::is_sorted` is non-strict, so **duplicate breakpoints are accepted** and give a zero-width
  segment, which interpolates as a hold (`SegmentT` returns `0` when the span is zero).
- Constructor arguments are taken by value and `std::move`d in — pass rvalues.
- The `policy` applies to both axes.
- Pinned by `"2D: bilinear against hand-computed values"` (corners, centre, edge midpoints and
  clamping) and `"2D: invalid shape yields an empty table"`.

---

## Noise — `math/Noise.h`

Namespace `Cosmic`. One class. Seeded value noise, Perlin gradient noise, fBm octave stacks and a
ridged multifractal, in 1D/2D/3D. Test file: `tests/test_noise.cpp`, suite `"Noise (E14)"`.

| Family | Calls | Range | Guaranteed how |
| --- | --- | --- | --- |
| Value noise — cheap, blockier | `Value1D` / `Value2D` / `Value3D` | `[-1, 1]` | by construction (lattice values are already in range, interpolation cannot leave it) |
| Perlin gradient noise — the workhorse | `Perlin1D` / `Perlin2D` / `Perlin3D` | `[-1, 1]`, **exactly 0 on the integer lattice** | rescaled then `std::clamp`ed |
| fBm octave stack | `Fbm1D` / `Fbm2D` / `Fbm3D` | `[-1, 1]` for any octave count | re-normalised by the amplitude sum |
| Ridged multifractal | `Ridged2D` | `[0, 1]` | normalised then `std::clamp`ed |

The ranges are **guaranteed, not approximate** — `"all variants stay within [-1, 1]"` asserts it over
3000 samples of all nine value/Perlin/fBm entry points, and the `Ridged2D` case asserts `[0, 1]` over
2000.

### `Noise::Noise`

```cpp
explicit Noise(uint32_t seed = 0);
```

**What it does** — builds a 256-entry permutation table by Fisher–Yates shuffle, driven by
[`Random`](#randomness--mathrandomh) seeded as `Random(0x9E3779B97F4A7C15 ^ seed, seed)`, then
mirrors it into a 512-entry array for wrap-free indexing.

**Why you'd use it** — one per noise field, held for the lifetime of that field.

**Example**

```cpp
// Frontier, worlds/BlizzardWorld.cpp:173 — one Noise captured by value into the height function.
tspec.HeightFunction = [noise = Cosmic::Noise(kSeed)](float u, float v)
{
    return 40.0f * noise.Fbm2D(u * 3.0f, v * 3.0f, 5);
};
```

**Notes & pitfalls**
- ⚠ **Construction does real work (a 256-element shuffle).** Building a `Noise` per sample is the
  classic mistake and it is slow. Build once and capture it — Frontier's `HeightfieldComposer`
  caches a `thread_local` instance keyed on the seed (`common/HeightfieldComposer.h:76-83`).
- The **stream is also the seed**, so two `Noise` objects with different seeds differ in both
  generator state and stream.
- `Noise` is a copyable value type (~516 bytes) with no heap allocation.
- Sampling is `const` and allocation-free, so **a shared `Noise` is safe to sample from many threads**
  concurrently — including inside a `ParallelFor`.
- `"same seed => identical field; different seed => different field"` asserts bit-equality with `==`
  (not `Approx`) for `Perlin2D`, `Value3D` and `Fbm2D`, and requires >400/500 samples to differ
  across seeds.

### `Noise::GetSeed`

```cpp
uint32_t GetSeed() const;
```

**What it does** — returns the seed the object was constructed with.

**Why you'd use it** — to record the seed with a run, or to decide whether a cached instance needs
rebuilding (which is exactly what `HeightfieldComposer::IslandNoise` does).

**Notes & pitfalls** — untested; it is a plain accessor.

### `Noise::Value1D` / `Value2D` / `Value3D`

```cpp
float Value1D(float x) const;
float Value2D(float x, float y) const;
float Value3D(float x, float y, float z) const;
```

**What it does** — random values on the integer lattice, smoothly interpolated with the quintic fade
`6t⁵ − 15t⁴ + 10t³`. Range `[-1, 1]`.

**Why you'd use it** — cheaper than Perlin and visually blockier. Fine for wind gusts, jitter, or
anything that is not going to be looked at as a surface. Reach for `Perlin*` for terrain and
textures.

**Example**

```cpp
Cosmic::Noise noise(1337);
const float jitter = noise.Value1D(t * 2.0f);      // [-1, 1]
```

**Notes & pitfalls**
- Not zero on the lattice (that is a Perlin property) — it is *exactly* the lattice value there.
- Range covered by `"all variants stay within [-1, 1]"`; determinism by the seed case above.

### `Noise::Perlin1D` / `Perlin2D` / `Perlin3D`

```cpp
float Perlin1D(float x) const;
float Perlin2D(float x, float y) const;
float Perlin3D(float x, float y, float z) const;
```

**What it does** — Ken Perlin's improved gradient noise over the seeded permutation table, rescaled
to `[-1, 1]` and clamped. `Perlin1D` scales by 2 (1D gradient noise peaks at ±0.5), `Perlin2D` by
`√2`, `Perlin3D` by `2/√3`.

**Why you'd use it** — the terrain and texture workhorse: smoother spectral character than value
noise. Stack it with `Fbm*` for detail.

**Example**

```cpp
// ViperSim, sim/Wind.h:34-36 — three offset channels from ONE Noise object.
const float x = t * gustFreqHz;
const glm::vec3 gust{
    m_Noise.Perlin1D(x)          + 0.5f * m_Noise.Perlin1D(x * 3.1f + 11.3f),
    m_Noise.Perlin1D(x + 101.7f) + 0.5f * m_Noise.Perlin1D(x * 3.1f + 57.9f),
    0.35f * m_Noise.Perlin1D(x + 233.1f),
};
```

**Notes & pitfalls**
- **Exactly 0 on the integer lattice** — pinned by `"Perlin is zero on the integer lattice"` for all
  three dimensions. Sample on integers and you get a field of zeros; always scale your coordinates.
- ⚠ **The lattice hash wraps every 256 integer units** (`Noise.h:250-256` masks with `& 255`), so
  the field is **periodic with period 256** on each axis. A world larger than 256 units in lattice
  space repeats. Scale your input so the region of interest fits, or accept the tiling. **Untested.**
- Negative coordinates are handled correctly (`FloorToInt` floors rather than truncating, and the
  mask is well-defined on two's-complement `int`), so the field is continuous across zero.
- Offsetting each channel in *noise space* (`+101.7f`, `+233.1f` above) is how you get
  independent-looking channels from one object without three seeds.

### `Noise::Fbm1D` / `Fbm2D` / `Fbm3D`

```cpp
float Fbm1D(float x, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
float Fbm2D(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
float Fbm3D(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
```

**What it does** — fractal Brownian motion: sums `octaves` Perlin samples, multiplying frequency by
`lacunarity` and amplitude by `gain` each octave, then divides by the amplitude sum so the `[-1, 1]`
bound holds for any octave count.

**Why you'd use it** — terrain, clouds, any surface that needs detail at several scales. Reach for
[`Ridged2D`](#noiseridged2d) when you want mountain spines rather than hills.

**Example**

```cpp
Cosmic::Noise noise(1337);
const float height = 60.0f * noise.Fbm2D(x * 0.01f, y * 0.01f, 6);
```

**Notes & pitfalls**
- **`octaves <= 0` returns exactly `0.0f`** (the `norm > 0` guard), not the 1-octave value.
- **One octave reduces exactly to `Perlin*`** — asserted by the doctest.
- `octaves` is not clamped upward; each octave is a full Perlin evaluation, so cost is linear in it.
- `"fBm octave falloff: successive octaves contribute geometrically less"` requires each added
  octave to perturb the normalised field by less than 0.9× the previous octave's perturbation, for
  k = 2…6.

### `Noise::Ridged2D`

```cpp
float Ridged2D(float x, float y, int octaves = 5,
               float lacunarity = 2.0f, float gain = 0.5f) const;
```

**What it does** — Musgrave's ridged multifractal. Each octave takes `r = 1 − |Perlin2D|`, squares it
to sharpen the crest, and weights it by the previous octave's value; the sum is normalised by the
amplitude sum and clamped to **`[0, 1]`**.

**Why you'd use it** — mountain spines and canyon walls. Where `Fbm2D` sums *signed* noise and rounds
its crests off, the `abs()` folds every zero crossing into a ridgeline.

**Example**

```cpp
const float ridge = noise.Ridged2D(u * 4.0f, v * 4.0f, 5);   // [0, 1]
const float h     = baseHeight + 120.0f * ridge;
```

**Notes & pitfalls**
- **Range is `[0, 1]`, not `[-1, 1]`** — the odd one out. Mixing it into an fBm-based height field
  without recentring shifts the whole terrain up.
- The only sampler with a **default octave count** (5).
- 2D only; there is no `Ridged1D` or `Ridged3D`.
- Pinned by `"Ridged2D: [0,1] bound, seed determinism, decorrelation"` and
  `"Ridged2D actually ridges: sharper creases than Fbm2D"`, which scans a line through both fields
  and requires ridged peak curvature to exceed fBm's by >1.5×.

---

## Randomness — `math/Random.h`

Namespace `Cosmic`. One class: PCG32 (O'Neill 2014, XSH-RR variant) plus every distribution the
engine needs. Test file: `tests/test_random.cpp`, suite `"Random (E15)"`.

**Why this exists instead of `<random>`:** `std::mt19937`'s *sequence* is portable, but the standard
**distributions are not** — `std::normal_distribution` and `std::uniform_int_distribution` are free
to differ between implementations. A replay recorded on one toolchain then diverges on another with
no error and no obvious cause. `Random` owns both the generator and the distributions. Read
[Determinism](#determinism) for exactly how far that guarantee reaches.

`Random` is a **plain copyable value type** (two `uint64_t`s plus the Gaussian spare): copy it to
snapshot a stream, assign to rewind one. It is **not thread-safe** — every draw mutates state, so
give each thread its own.

### `Random::Random`

```cpp
explicit Random(uint64_t seed   = 0x853c49e6748fea9bULL,
                uint64_t stream = 0xda3e39cb94b95bdbULL);
```

**What it does** — constructs a seeded generator. Equivalent to default-constructing and calling
[`Seed`](#randomseed).

**Why you'd use it** — one per consumer, seeded from your run's recorded seed. The `stream` argument
selects an independent sequence from the same seed.

**Example**

```cpp
// ViperSim, sim/Sensors.h:73-79 — one stream per sensor.
m_RngGyro  = Cosmic::Random(m_P.seed, 1);
m_RngAccel = Cosmic::Random(m_P.seed, 2);
m_RngBaro  = Cosmic::Random(m_P.seed, 3);
```

**Notes & pitfalls**
- ⚠ **The defaults are fixed constants, not entropy.** Two default-constructed `Random`s draw the
  identical sequence. Always seed explicitly.
- ⚠ **Use one stream per consumer.** If every consumer draws from one stream, *adding* a consumer
  shifts everyone else's sequence and every replay recorded before the change stops reproducing.
- The seeding recipe forces the increment odd and advances the state twice, so `seed = 0` is a
  perfectly good seed.
- `"same seed => identical stream; different stream => different values"` requires 100/100 matches
  for identical `(seed, stream)` and fewer than 5/100 collisions across streams.

### `Random::Seed`

```cpp
void Seed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL);
```

**What it does** — re-seeds an existing object in place and **clears the cached Gaussian spare**.

**Why you'd use it** — restarting a run without reconstructing the objects that hold the generator.

**Example**

```cpp
m_Rng.Seed(runSeed, /*stream*/ 4);   // fresh run, same object
```

**Notes & pitfalls**
- ⚠ **Re-seeding mid-run silently invalidates every replay taken before it.** Seed at setup, not in
  a loop.
- Clearing the Gaussian spare matters: without it, the first `Gaussian` after a re-seed would return
  a value derived from the *old* stream.
- Untested directly — the doctests all seed through the constructor.

### `Random::NextUInt32()`

```cpp
uint32_t NextUInt32();
```

**What it does** — one raw PCG32 draw: a 64-bit LCG state step, then an xorshift-and-rotate output.
Uniform over the full `uint32_t` range.

**Why you'd use it** — hashing, bit fiddling, or building your own distribution. For a bounded
integer use [`NextUInt32(bound)`](#randomnextuint32bound); for a float use
[`NextFloat`](#randomnextfloat).

**Example**

```cpp
Cosmic::Random rng(1234);
const uint32_t bits = rng.NextUInt32();
```

**Notes & pitfalls**
- **This is the bit-exact core of the whole determinism story.**
  `"fixed seed reproduces the canonical PCG32 reference sequence"` checks seed 42 / stream 54
  against the PCG author's published values (`0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293,
  0xbfa4784b, 0xcbed606e`) with `==`. **If that test ever fails, the generator is no longer canonical
  PCG32 and every committed replay is invalid.**
- Every other method on this class is built on it, so its draw count is the currency of
  reproducibility.

### `Random::NextUInt32(bound)`

```cpp
uint32_t NextUInt32(uint32_t bound);
```

**What it does** — uniform in `[0, bound)` with **no modulo bias**: computes a rejection threshold
and re-draws until the sample is above it.

**Why you'd use it** — picking an index, a bucket, a variant. Bias-free matters more than it sounds
when the bound is large relative to 2³².

**Example**

```cpp
const uint32_t face = rng.NextUInt32(6);          // [0, 6)
```

**Notes & pitfalls**
- **`bound == 0` returns `0` and consumes no draw at all** — no divide-by-zero, but also no state
  advance, which is a determinism trap if a bound can be zero on some runs and not others.
- The rejection loop means the draw count per call is **variable** (usually 1).
- Bounds covered by `"NextFloat stays in [0, 1); Range respects bounds"`; the *uniformity* of the
  rejection is not tested.

### `Random::NextFloat`

```cpp
float NextFloat();
```

**What it does** — uniform float in `[0, 1)` at 24-bit resolution: `(NextUInt32() >> 8) * 2⁻²⁴`.

**Why you'd use it** — the base uniform for everything else. Exact on the float grid, so it is
bit-reproducible wherever `NextUInt32` is.

**Example**

```cpp
if (rng.NextFloat() < 0.05f)
    SpawnRareEvent();
```

**Notes & pitfalls**
- **`1.0f` is never returned**; `0.0f` is.
- Exactly one underlying draw per call.
- Bounds pinned over 10 000 samples by `"NextFloat stays in [0, 1); Range respects bounds"`.

### `Random::Range`

```cpp
float Range(float min, float max);
```

**What it does** — uniform float in `[min, max)`, as `min + (max - min) * NextFloat()`.

**Example**

```cpp
const float spawnAngle = rng.Range(0.0f, 360.0f);
```

**Notes & pitfalls**
- **No validation**: `Range(max, min)` with the arguments swapped silently produces values in
  `(max, min]` rather than asserting.
- Exactly one draw per call.

### `Random::RangeInt`

```cpp
int RangeInt(int min, int max);
```

**What it does** — uniform int in `[min, max]` — **inclusive at both ends**, unlike every other
range in this header.

**Example**

```cpp
const int d6 = rng.RangeInt(1, 6);        // 1..6 inclusive
```

**Notes & pitfalls**
- ⚠ **Inclusive `max`.** `RangeInt(0, n)` yields `n + 1` distinct values.
- **`max <= min` returns `min` and consumes no draw** — same determinism trap as `NextUInt32(0)`.
- Builds on the bias-free bounded draw, so the underlying draw count is variable.
- Bounds pinned over 1000 samples by the same doctest as `NextFloat`.

### `Random::Gaussian`

```cpp
float Gaussian(float mean = 0.0f, float sigma = 1.0f);
```

**What it does** — normally distributed float via Box–Muller **with spare caching**: it draws two
uniforms, produces two normals, returns one and keeps the other.

**Why you'd use it** — sensor noise, jitter, anything that should be normally distributed. This is
the reason `std::normal_distribution` is banned here: a fixed algorithm means portable sequences.

**Example**

```cpp
// ViperSim, sim/Sensors.h:112-115 — three axes of gyro noise from one stream.
gyro += m_GyroBias + glm::vec3(
    m_RngGyro.Gaussian(0.0f, m_P.gyro_noise),
    m_RngGyro.Gaussian(0.0f, m_P.gyro_noise),
    m_RngGyro.Gaussian(0.0f, m_P.gyro_noise));
```

**Notes & pitfalls**
- ⚠ **The only call here that is not bit-portable across C runtimes** — it uses `std::log`,
  `std::sin` and `std::cos`, which are not required to be correctly rounded. Within one toolchain a
  seed reproduces exactly. See [Determinism](#determinism) tier 3.
- **Every other call consumes nothing from the generator** (the cached spare), so "N `Gaussian` calls
  consume 2N uniforms" is wrong. [`Seed`](#randomseed) clears the spare.
- The spare is cached *before* `mean`/`sigma` are applied, so alternating `Gaussian(0, 1)` and
  `Gaussian(5, 2)` is well defined — each call scales the spare by its own arguments.
- `u1` is redrawn while `<= 1e-12f` to avoid `log(0)` — another variable-draw-count path, though it
  essentially never fires.
- `"Gaussian mean/sigma within tolerance over 1e5 samples"` checks the first two moments against
  `mean = 3, sigma = 2`. Note the `epsilon(0.011)` tolerance is really ±0.044 — see the
  [`Approx` gotcha](#test-coverage-at-a-glance).

### `Random::InUnitSphere` / `OnUnitSphere` / `InUnitDisc`

```cpp
glm::vec3 InUnitSphere();     // uniform INSIDE the unit sphere
glm::vec3 OnUnitSphere();     // uniform ON the unit sphere surface
glm::vec2 InUnitDisc();       // uniform inside the unit disc (z = 0)
```

**What it does** — geometric samplers. `InUnitSphere` and `InUnitDisc` reject samples outside the
volume/area; `OnUnitSphere` uses Marsaglia (1972), which avoids the latitude bias of the naive
two-angle method.

**Why you'd use it** — scatter offsets, random directions, disc sampling for a lens or a spawn area.

**Example**

```cpp
const glm::vec3 offset    = 2.0f * rng.InUnitSphere();     // jitter a spawn point
const glm::vec3 direction = rng.OnUnitSphere();            // uniform random direction
```

**Notes & pitfalls**
- All three **loop** — draw count per call is variable (`InUnitSphere` averages ~1.9 iterations).
  Deterministic, but not countable.
- `InUnitSphere` **includes** the surface (`dot(v,v) <= 1.0f`); `OnUnitSphere`'s rejection is
  `s >= 1.0f`, i.e. strictly inside.
- Deterministic under IEEE-754 (`sqrt` is correctly rounded) — tier 2, not tier 3.
- `"InUnitSphere/OnUnitSphere/InUnitDisc geometric bounds"` checks the bounds over 2000 samples each
  and that `InUnitSphere` populates all eight octants (>200 hits each over 4000 samples) — a sanity
  check against sign bugs.

---

## View frustum — `math/Frustum.h`

Namespace `Cosmic`. One `struct`, pure math (glm only, no GPU types). Test file:
`tests/test_frustum.cpp`, suite `"Frustum (F5 culling)"`.

> **Manifest routing note.** The [coverage manifest](README.md#coverage-manifest--every-public-header-maps-to-a-chapter)
> currently routes `math/Frustum.h` to [rendering-3d.md](rendering-3d.md), where it sits next to its
> main consumer, `Renderer3D`. D15's scope put it here with the rest of `math/`. **The entries below
> are the canonical ones** — [rendering-3d.md](rendering-3d.md) should link to them rather than
> restate them (execution note 7: each fact has exactly one home). The manifest row needs re-pointing
> either way; a chapter file cannot be in two rows.

```cpp
struct Frustum
{
    glm::vec4 Planes[6];   // Left, Right, Bottom, Top, Near, Far

    static Frustum FromViewProjection(const glm::mat4& m);
    bool IntersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const;
    bool IntersectsSphere(const glm::vec3& center, float radius) const;
};
```

A plain aggregate — 96 bytes, no constructor, no invariant beyond what
[`FromViewProjection`](#frustumfromviewprojection) establishes. Copy it freely; build one per camera
per frame.

### `Frustum::Planes`

```cpp
glm::vec4 Planes[6];
```

**What it does** — the six world-space clip planes, in the order **Left, Right, Bottom, Top, Near,
Far**. Each is `(a, b, c, d)` for `a·x + b·y + c·z + d = 0`, with `(a, b, c)` the **inward-pointing
unit normal**. A point is inside when `dot(plane, vec4(p, 1)) >= 0` for all six.

**Notes & pitfalls**
- **Public and writable** — nothing stops you assembling a frustum by hand, but the
  `IntersectsSphere` distance is only metric if the normals are unit length.
- `"extracted planes carry unit normals"` asserts exactly that after extraction.

### `Frustum::FromViewProjection`

```cpp
static Frustum FromViewProjection(const glm::mat4& m);
```

**What it does** — extracts the six planes from a view-projection matrix by the Gribb–Hartmann method
(sums and differences of the matrix rows), then normalises each plane so signed distances are in
world units.

**Why you'd use it** — once per frame, per camera, before culling anything. The engine does this
itself for `Renderer3D`'s automatic mesh culling (`Renderer3D.cpp:312`); you build your own when you
cull app-side geometry that never enters the renderer's queue.

**Example**

```cpp
// Frontier, worlds/IslandWorld.cpp:552 — cull scattered props against the main camera.
const Cosmic::Frustum frustum = Cosmic::Frustum::FromViewProjection(cam->GetViewProjectionMatrix());
m_Trees.CullAndUpload(frustum);
```

**Notes & pitfalls**
- ⚠ **OpenGL clip-space convention** — it assumes `z ∈ [-1, 1]` (`near = row3 + row2`). A projection
  built with `GLM_FORCE_DEPTH_ZERO_TO_ONE` gives a wrong near plane. Cosmic is OpenGL throughout, so
  this is correct today; it is the first thing to revisit under another backend.
- Pass `projection * view`, in that order. Passing only the projection yields a view-space frustum,
  which is occasionally what you want and usually a bug.
- Planes with a near-zero normal (`len <= 1e-8f`) are left un-normalised rather than producing
  `NaN` — a degenerate matrix degrades instead of poisoning.
- Cannot fail: no allocation, no logging, always returns six planes.

### `Frustum::IntersectsAABB`

```cpp
bool IntersectsAABB(const glm::vec3& mn, const glm::vec3& mx) const;
```

**What it does** — returns `false` only when the box is **fully outside** some single plane, using
the positive-vertex test (the corner farthest along each inward normal).

**Why you'd use it** — the standard coarse cull for a bounded object. `Renderer3D` calls it per
submitted mesh when `CullingEnabled` (`Renderer3D.cpp:565`).

**Example**

```cpp
if (!frustum.IntersectsAABB(worldMin, worldMax))
    continue;                    // skip this object entirely
```

**Notes & pitfalls**
- **Conservative in the safe direction**: it can report a just-outside box as intersecting (the
  classic corner-region false positive), and never rejects a visible box. Correct for culling,
  wrong for "is this exactly inside".
- **Inflate the box for anything that moves after you cull it.** `Renderer3D` pads skinned meshes by
  half their extent per side (`Renderer3D.cpp:559-564`) precisely because a posed skeleton can leave
  its bind-pose bounds; the same logic applies to near-offscreen shadow casters.
- Pinned by `"unit cube at the origin is inside"`, `"cubes outside each face are culled"` (all six
  faces), `"a box enclosing the whole frustum intersects"` and `"a box straddling the near plane
  intersects"`.

### `Frustum::IntersectsSphere`

```cpp
bool IntersectsSphere(const glm::vec3& center, float radius) const;
```

**What it does** — returns `false` only when the sphere is fully behind some plane, i.e. when the
signed distance is below `-radius` for any plane.

**Why you'd use it** — cheaper than the AABB test and the natural fit for scattered instances with a
bounding radius. Frontier's `Scatter` culls per instance this way and uploads only the survivors
(`common/Scatter.h:134`).

**Example**

```cpp
for (const ScatterInstance& inst : m_Instances)
    if (frustum.IntersectsSphere(inst.Position + glm::vec3(0.0f, m_CullRadius, 0.0f), m_CullRadius))
        m_Visible.push_back(inst.Transform);
```

**Notes & pitfalls**
- **The distance is only metric because `FromViewProjection` normalises the planes.** A hand-built
  frustum with un-normalised planes makes `radius` meaningless.
- `radius = 0` degenerates to a point-inside test — the doctest uses exactly that to validate the
  plane sign convention.
- Same conservative bias as the AABB test.
- Pinned by `"sphere variants: inside, outside, straddling"`, including a sphere centred well past
  the far plane whose radius reaches back into the volume.

---

## Worked example — spring + RK4 + LPF

One compiling layer that uses four of the seven headers together: a 1-DOF spring integrated with
RK4 inside a substepper, its position smoothed for display, and the result converted from NED into
the render frame.

```cpp
#include <Cosmic.h>

namespace Demo
{
    // The minimal RK4 state: two operators, no base class.
    struct PV { float p, v; };
    inline PV operator+(const PV& a, const PV& b) { return { a.p + b.p, a.v + b.v }; }
    inline PV operator*(const PV& a, float s)     { return { a.p * s,   a.v * s   }; }

    class SpringLayer : public Cosmic::Layer
    {
    public:
        SpringLayer() : Layer("Spring") {}

        void OnFixedUpdate(float deltaFixedTime) override
        {
            // 8 substeps inside the fixed tick: 60 Hz tick -> 480 Hz solver.
            m_Substepper.Run(deltaFixedTime, 8, [this](float h)
            {
                auto deriv = [](const PV& s, float /*t*/) -> PV
                {
                    return { s.v, -50.0f * s.p - 0.5f * s.v };   // must be PURE
                };
                m_State = Cosmic::IntegrateRK4(m_State, deriv, m_Time, h);
                m_Time += h;
            });

            // A display value that does not flicker. tau = 0.1 s.
            m_Display.Update(m_State.p, deltaFixedTime);
        }

        void OnUpdate(float /*deltaTime*/) override
        {
            // Sim state is NED (+Z Down); the scene graph is Y-up. Convert here, once.
            const glm::vec3 posNed{ 0.0f, 0.0f, -m_Display.GetValue() };
            m_Marker.GetComponent<Cosmic::TransformComponent>().Position =
                Cosmic::Math::NedToRender(posNed);
        }

    private:
        Cosmic::FixedSubstepper m_Substepper;
        Cosmic::LowPassFilter   m_Display{ 0.1f };
        Cosmic::Entity          m_Marker;
        PV    m_State{ 1.0f, 0.0f };
        float m_Time = 0.0f;
    };
}
```

Four contracts are load-bearing in those thirty lines, and each is documented above: the state type
needs only `+` and `* float`; `deriv` runs four times per step and must be pure;
`FixedSubstepper` divides the tick it is given, so it belongs in `OnFixedUpdate`; and NED is not the
render frame.

---

## Known rough edges

Found while writing this chapter, verified against the headers, and worth a Phase 30 fuzz case or a
one-line fix. None is documented anywhere else.

| # | Where | What |
| --- | --- | --- |
| 1 | `Filters.h:89-94` | **`Derivative::Reset(value)`'s argument is dead.** It writes `m_PrevSample` but leaves `m_Primed = false`, so the next `Update` overwrites it and returns `0.0f`. Every other filter's `Reset` sets `m_Primed = true`. `Derivative` also has **no doctest and no in-tree consumer**, which is presumably why this survived. |
| 2 | `Filters.h:226-232` + `247-252` | **`Biquad` degenerates to `NaN` on a zero cutoff.** `SetLowPass(0, fs)` gives `cos(w0) = 1`, so `Reset`'s DC-gain term divides `0/0`. Cutoffs at or above Nyquist are unstable. Nothing validates or logs. |
| 3 | `Integrators.h:79-82` | **`FixedSubstepper::Run` adds `dt` to the residual before its `h <= 0` guard**, so a negative `dt` silently subtracts carried time and corrupts the clock. A `dt < 0` early-out would be one line. |
| 4 | `Integrators.h:84-88` | **A shrinking `dt` with a carried residual fires a burst** of up to `dt_prev / dt_new` extra substeps. Correct by design (no time is lost) but unbounded-looking in a profiler after a hitch. |
| 5 | `LookupTable.h:95-96` | **Stale comment** teaching the retired "resolves against the CALLING DLL's active project" VFS rule. The mount moved into the engine DLL in Phase 20/A1: one active project per *process*. |
| 6 | `Noise.h:250-256` | **The noise lattice wraps every 256 integer units** and nothing says so — a world sampled beyond that range tiles. Untested. |
| 7 | `Filters.h:217` | **`Biquad::Type` is public but unreachable** — no public method accepts it (`Configure` is private). |
| 8 | `Random.h:65-66`, `91-92` | **`NextUInt32(0)` and `RangeInt(max <= min)` return without consuming a draw**, so a bound that is zero on some runs and not others desynchronises a stream. |
| 9 | `tests/CMakeLists.txt:85-89` | **`test_frustum.cpp` is in the 3D-only tier**, so `math/Frustum.h` — which compiles fine in a 2D build — has zero coverage there. |

---

*See also:* [`../guide/sim-math-toolkit.md`](../guide/sim-math-toolkit.md) (the task-oriented guide
chapter — start there) · [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md) (the fixed vs
variable tick that `FixedSubstepper` divides) · [physics.md](physics.md) (when to let the rigid-body
engine integrate for you) · [assets-io.md](assets-io.md) (`Config`, `DataExport` and the VFS under
`LookupTable1D::FromCSV`) · [world-systems.md](world-systems.md) (`Noise` on the terrain side) ·
[rendering-3d.md](rendering-3d.md) (`Renderer3D`'s use of [`Frustum`](#view-frustum--mathfrustumh)) ·
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md) (why all seven headers are
unfenced) · [`../systems/math-sim-toolkit.md`](../systems/math-sim-toolkit.md) *(skeleton — D32)*

---
*Changelog:*
*2026-07-26 — created (D15). Covers `Cosmic::Math` (frames, Euler, attitude integration),
`IntegrateRK4`, `IntegrateSemiImplicitEuler`, `FixedSubstepper`, `LowPassFilter`, `Derivative`,
`RateLimiter`, `MovingAverage`, `Biquad`, `Washout`, `TableRangePolicy`, `LookupTable1D`,
`LookupTable2D`, `Noise`, `Random` and `Frustum`, with a per-header doctest citation, a determinism
box and nine recorded rough edges.*
