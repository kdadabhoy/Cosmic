# Simulation Math Toolkit — Guide

**What this covers:** the header-only math a simulation is built from — RK4 and semi-implicit Euler
integrators plus `FixedSubstepper`, the signal-conditioning filters (low-pass, derivative, rate
limit, moving average, biquad, washout), 1D/2D lookup tables for measured data, seeded procedural
noise, deterministic PCG32 randomness, and the `Cosmic::Math` frame conventions that decide which
way is up.
**Source of truth:** `Cosmic/src/math/Integrators.h`, `math/Filters.h`, `math/LookupTable.h`,
`math/Noise.h`, `math/Random.h`, `math/Spatial.h`, `Cosmic/src/Cosmic.h`,
`Projects/ViperSim/src/sim/ComposableDynamics.{h,cpp}`, `sim/Sensors.h`, `sim/Wind.h`,
`fc_glue/RigOutput.h`, `Projects/Engine3DDemo/src/Engine3DDemo.cpp`,
`Projects/Frontier/src/common/{HeightfieldComposer,ProceduralMeshes,Scatter,Boids}.h`,
`tests/test_integrators.cpp`, `test_filters.cpp`, `test_lookuptable.cpp`, `test_noise.cpp`,
`test_random.cpp`, `test_spatial.cpp`
**API Reference:** [`../reference/math.md`](../reference/math.md) *(skeleton — D15 unwritten; this
chapter is the client-facing source until it lands)* · **How it works:**
[`../systems/math-sim-toolkit.md`](../systems/math-sim-toolkit.md) *(skeleton — D32)*
**Configuration:** **both.** All six headers are included by `Cosmic.h` outside any fence and
compile identically on the 2D and 3D engines.

Every header here is **header-only, allocation-free after construction, and GL-free**, so it works
in a project DLL, in a unit test, in a headless tool, and — for the integrators and filters — in
MCU-portable code. Five of the six have no engine dependencies at all; only `LookupTable.h` reaches
into `DataExport`, `FileSystem` and `Log`, and only for its CSV loader.

**`Projects/ViperSim` is the usage exemplar.** It is a dual-motor tailsitter simulator built on this
tier and nothing else: RK4 over translation, `IntegrateBodyRate` over attitude, `FixedSubstepper` at
480 Hz inside a 60 Hz tick, seeded `Noise` for gusts, one `Random` stream per sensor, and a
`RateLimiter` per servo axis. Worked examples below are lifted from it where one exists.

> **Not everything here has an in-tree consumer.** `LookupTable1D`/`2D`, `LowPassFilter`,
> `Derivative`, `MovingAverage`, `Biquad`, `Washout` and `IntegrateSemiImplicitEuler` are used by
> their doctests and by nothing else in the repository. They are tested, not battle-worn; the
> examples for those are written against the headers rather than mined from a shipping app, and are
> marked as such.

## Quick start

```cpp
#include <Cosmic.h>

// A falling body, integrated at a fixed step.
struct PV { glm::vec3 p{}, v{}; };
inline PV operator+(const PV& a, const PV& b) { return { a.p + b.p, a.v + b.v }; }
inline PV operator*(const PV& a, float s)     { return { a.p * s,   a.v * s   }; }

void MyLayer::OnFixedUpdate(float dt)
{
    auto deriv = [](const PV& s, float) -> PV
    {
        return { s.v, glm::vec3(0.0f, 0.0f, Cosmic::Math::GravityMss) };  // NED: +Z is Down
    };

    m_State = Cosmic::IntegrateRK4(m_State, deriv, m_Time, dt);
    m_Time += dt;

    // Draw in the render frame, which is NOT the sim frame.
    m_Transform.Position = Cosmic::Math::NedToRender(m_State.p);
}
```

Three things that quick start is quietly demonstrating, and that the rest of this chapter expands:
state types only need `+` and `* float`; the simulation frame is **NED** while the renderer is
**Y-up**; and `Cosmic::Math` is where the conversion between them lives.

## When to use what

| You have | Reach for | Header |
| --- | --- | --- |
| a differential equation and a fixed step | `IntegrateRK4` | `Integrators.h` |
| a second-order system where stability matters more than order | `IntegrateSemiImplicitEuler` | `Integrators.h` |
| an attitude quaternion and body rates | `Math::IntegrateBodyRate` | `Spatial.h` |
| an engine tick slower than your solver needs | `FixedSubstepper` | `Integrators.h` |
| a noisy scalar to smooth | `LowPassFilter` | `Filters.h` |
| a rate estimated from a noisy position | `Derivative` | `Filters.h` |
| a command that must not step-jump | `RateLimiter` | `Filters.h` |
| a plain N-sample mean | `MovingAverage<N>` | `Filters.h` |
| a steeper roll-off, or one frequency to kill | `Biquad` | `Filters.h` |
| transients kept and steady state removed | `Washout` | `Filters.h` |
| measured data — polar, thrust curve, gain schedule | `LookupTable1D` / `LookupTable2D` | `LookupTable.h` |
| terrain height, wind gusts, procedural texture | `Noise` | `Noise.h` |
| randomness a replay must reproduce | `Random` | `Random.h` |
| frames, gravity, Euler↔quaternion | `Cosmic::Math` | `Spatial.h` |

## Integrate a state

### RK4

```cpp
template<typename State, typename DerivFn>
State IntegrateRK4(const State& state, DerivFn&& deriv, float t, float dt);
```

`deriv(state, t)` returns `d(state)/dt`. Classic fourth-order Runge-Kutta; global error is
`O(h⁴)`, which `tests/test_integrators.cpp` pins directly — integrating `dx/dt = x` to `x(1) = e`
and halving the step shrinks the error by a factor between 10 and 24 (theory says 16; the slack is
float precision).

**The state type needs exactly two operations:** `State + State` and `State * float`. `float` and
`glm::vec2/3/4` satisfy that out of the box; a struct needs two free operators, as in the quick
start above. That is the whole concept requirement — there is no base class and no traits block.

ViperSim runs the translational half of a 6-DOF airframe this way, with attitude and body rates
frozen for the substep because they advance separately at the same rate
(`ComposableDynamics.cpp:181-191`):

```cpp
PV s{ m_State.posNed, m_State.velNed };
auto deriv = [this](const PV& st, float /*t*/) -> PV
{
    glm::vec3 a, dummy;
    AccumulateForces(st.p, st.v, m_State.attNed, m_State.omegaBody, a, dummy);
    return { st.v, a };
};
s = Cosmic::IntegrateRK4(s, deriv, 0.0f, h);
```

Note that `deriv` is called **four times per step** with intermediate states. It must be pure with
respect to the state you pass it — do not accumulate side effects inside it, or you will apply them
four times.

**Do not RK4 quaternion components.** Naively integrating `w,x,y,z` as if they were a vector state
drifts off the unit sphere and is not what quaternion kinematics says. Integrate attitude with
`Math::IntegrateBodyRate` and RK4 the translational state; the two compose cleanly at fixed
substeps, which is exactly what ViperSim does.

### Semi-implicit Euler

```cpp
template<typename Pos, typename Vel, typename AccelFn>
void IntegrateSemiImplicitEuler(Pos& pos, Vel& vel, AccelFn&& accel, float t, float dt);
```

Updates **in place**: velocity from acceleration first, then position from the *new* velocity.
`accel(pos, vel, t)` returns `d(vel)/dt`. First order, but **symplectic** — energy oscillates around
its initial value rather than growing, which is why it is the workhorse for oscillatory systems
where explicit Euler blows up. The doctest runs an undamped spring for 10 s at 240 Hz and asserts
peak energy stays under 110% of `E₀`.

Reach for it over RK4 when the system is stiff-ish and springy, when you need one derivative
evaluation instead of four, or when the "right" answer matters less than not exploding.

```cpp
// Untested-in-tree pattern: a damped mass-spring, written against the header.
float pos = 1.0f, vel = 0.0f;
auto accel = [](float p, float v, float) { return -50.0f * p - 0.5f * v; };
Cosmic::IntegrateSemiImplicitEuler(pos, vel, accel, m_Time, dt);
```

### `FixedSubstepper` — N substeps inside one tick

```cpp
Cosmic::FixedSubstepper sub;
sub.Run(dt, 8, [&](float h) { StepPhysics(h); });   // 60 Hz tick -> 480 Hz solver
```

`Run` computes `h = dt / substeps` and calls `step(h)` until the accumulated residual is used up,
carrying the leftover into the next call so non-divisible deltas do not drift the sim clock. A
`substeps` below 1 is clamped to 1; a non-positive `h` returns without stepping. `Reset()` drops the
residual (do this on a sim reset), `GetResidual()` reads it.

The doctest pins both halves: `Run(1/60, 8, …)` makes exactly **8** calls at exactly `1/480`, and
100 calls of `0.0100 s` at 4 substeps satisfy `integrated + GetResidual() ≈ 1.0` — no time lost
across calls.

> **`h` is derived from *this* call's `dt`, not from a fixed rate.** Call `Run` from
> `OnFixedUpdate`, where `dt` is exactly `1/FixedHz`, and `h` is genuinely constant. Call it from
> `OnUpdate`, where `dt` is whatever the frame took, and `h` changes every frame — the residual
> still conserves total time, but "480 Hz" becomes a fiction and your solver sees a varying step.
> `FixedSubstepper` divides a tick; it does not create one. See
> [`time-and-ticks.md`](time-and-ticks.md).

ViperSim owns one substepper per dynamics object and drives it from the flight-controller step
(`ComposableDynamics.cpp:211-216`):

```cpp
void ComposableDynamics::Step(const ActuatorFrame& u, float dt)
{
    m_LastU = u;
    const int substeps = std::max(m_Params.substeps, 1);
    m_Substepper.Run(dt, substeps, [this](float h) { Integrate(h); });
}
```

## Condition a signal

Every filter in `Filters.h` shares one contract:

- `Reset(value)` re-seeds the internal state — **except `Derivative`, whose argument is silently
  discarded** (see [below](#derivative--a-rate-you-can-actually-use)).
- `Update(sample, dt)` advances by `dt` seconds and returns the filtered value.
- `GetValue()` peeks at the current output without advancing.
- **`dt <= 0` returns the current output unchanged** — a paused frame does not corrupt the state.
- **The first `Update` primes the filter**, so there is no startup transient from zero. Each type
  primes differently and it matters: `LowPassFilter` returns the sample, `RateLimiter` returns the
  target, `Derivative` returns `0`, `Washout` returns `0` (it starts washed out).

`Biquad` and `MovingAverage` take a `dt` parameter for uniformity and **ignore it** — `MovingAverage`
has no time constant, and `Biquad` had its sample rate baked in at configure time.

### `LowPassFilter` — first-order smoothing

```cpp
Cosmic::LowPassFilter lpf(0.25f);                              // tau, seconds
auto  hz  = Cosmic::LowPassFilter::FromCutoffHz(10.0f);        // tau = 1 / (2*pi*fc)
float out = lpf.Update(noisySample, dt);
```

`y += alpha * (x - y)` with `alpha = 1 - exp(-dt/tau)`. The step response reaches **63.2 % at
t = tau** — that is the defining property and the doctest checks it to within 1 %. `SetTau` and
`SetCutoffHz` retune live. **`tau = 0` is passthrough**, not a divide-by-zero.

### `Derivative` — a rate you can actually use

```cpp
Cosmic::Derivative d(0.02f);          // smoothing tau for the estimate; 0 = raw
float rate = d.Update(position, dt);  // returns 0.0f on the very first call
```

A raw finite difference through a first-order LPF, so sensor noise does not explode into the rate.
The default smoothing tau is `0.02 s`.

> **`Derivative::Reset(value)`'s argument does nothing.** It assigns `m_PrevSample = value` but then
> sets `m_Primed = false` (`Filters.h:89-94`), and the next `Update` re-primes by overwriting
> `m_PrevSample` with the incoming sample. So the seed is discarded and the first `Update` after a
> reset returns `0` regardless of what you passed. Every *other* filter's `Reset` sets
> `m_Primed = true` and honours the value — `Derivative` is the odd one out. Pass nothing and expect
> a zero first sample. (`Derivative` also has **no test anywhere** and no in-tree consumer, which is
> presumably why this survived.)

### `RateLimiter` — the slew clamp

```cpp
Cosmic::RateLimiter r(120.0f);        // degrees per second
float commanded = r.Update(target, dt);
```

Clamps the output's change to `±maxRate * dt`. ViperSim's gimbal rig runs one per axis so a buggy
firmware command can never slam a servo (`fc_glue/RigOutput.h`):

```cpp
Cosmic::RateLimiter m_Roll, m_Pitch, m_Yaw;   // degrees, per axis

const glm::vec3 eulerDeg = Cosmic::Math::EulerZYXFromQuat(attNed);
const float r = m_Roll .Update(eulerDeg.x, dt);
const float p = m_Pitch.Update(eulerDeg.y, dt);
const float y = m_Yaw  .Update(eulerDeg.z, dt);
```

**Rate-limiting an angle has a wrap trap.** `RateLimiter` operates on a plain `float` and knows
nothing about ±180°: a yaw crossing from `+179°` to `-179°` is a 358° error, and the limiter
sweeps the long way round at max rate. Unwrap the angle into a continuous value before limiting it,
or limit the quaternion instead.

### `MovingAverage<N>` — fixed-window boxcar

```cpp
Cosmic::MovingAverage<16> avg;
avg.Reset(startingValue);              // seeds the WHOLE window, so GetValue() == startingValue
float mean = avg.Update(sample);       // dt optional and unused
```

O(1) per update via a running sum. `N` is a compile-time template argument and must be ≥ 1. Before
the window fills, `GetValue()` averages only the samples seen so far.

### `Biquad` — two-pole LPF / HPF / notch

```cpp
Cosmic::Biquad bq;
bq.SetLowPass(80.0f, 1.0f / dt);                 // cutoff Hz, sample rate Hz
bq.SetNotch  (50.0f, 1.0f / dt, 20.0f);          // kill mains hum, q = 20
float out = bq.Update(sample);                   // dt ignored — the rate is baked in
```

RBJ-cookbook coefficients, direct form I. `q` defaults to `0.70710678` (Butterworth) for the
low/high-pass and `10.0` for the notch; higher `q` on a notch means narrower. The doctests check
−3 dB at the low-pass cutoff and that the notch kills its centre frequency while passing DC.

**The sample rate is a construction-time fact, not a per-call one.** A default-constructed `Biquad`
is `SetLowPass(100, 1000)`. If your step rate changes, call the setter again — passing a different
`dt` to `Update` does nothing. In a fixed-step sim, `1/dt` is a constant and this is a non-issue,
which is exactly the case the header was written for. `Reset(value)` sets the state to the
steady-state output for a constant input (`value * H(DC)`), not to `value`.

### `Washout` — keep transients, drop steady state

```cpp
Cosmic::Washout w(2.0f);              // tau, seconds
float cue = w.Update(sustainedInput, dt);   // -> decays toward 0
```

First-order high-pass: `y = alpha * (y_prev + x - x_prev)` with `alpha = tau / (tau + dt)`. The
motion-cue filter — a sustained input washes out to zero while a step still shows up — and equally
useful for removing a slow bias from a rate signal.

## Table-driven data

Measured data belongs in a table, not in code. That is a deliberate line: aero polars, motor thrust
maps, gain schedules and battery discharge curves are **app data**, and the engine's job is to
interpolate them.

```cpp
// Points need not arrive sorted; they are stable-sorted by x.
Cosmic::LookupTable1D thrust({ {0.0f, 0.0f}, {0.5f, 6.9f}, {1.0f, 14.6f} });
float n = thrust.Sample(0.75f);        // 10.75

// Or from a two-column CSV (header row optional). VFS-resolved here.
auto polar = Cosmic::LookupTable1D::FromCSV("project://polars/cl.csv");
if (!polar.IsValid())
    CS_WARN("polar missing — using the built-in curve");
```

```cpp
// 2D: values are ROW-MAJOR, values[ix * yBreaks.size() + iy].
Cosmic::LookupTable2D gains(alphaBreaks, speedBreaks, values);
float kp = gains.Sample(alpha, speed);
```

**Out-of-range policy is per table**, chosen at construction:

| Policy | Below the first / above the last breakpoint |
| --- | --- |
| `TableRangePolicy::Clamp` *(default)* | hold the edge value |
| `TableRangePolicy::Extrapolate` | continue the edge segment's slope |

Failure behaviour:

| Situation | Result |
| --- | --- |
| `FromCSV` on a missing/short/one-column file | empty table, `IsValid() == false`, `CS_CORE_ERROR` naming the path |
| 2D constructed with a bad shape (unsorted breaks, wrong value count, < 2 breaks) | empty table, `CS_CORE_ERROR`, `Sample` returns `0` |
| `Sample` on an empty table | `0.0f` |
| `Sample` on a single-point 1D table | that point's `y` |

Two edge behaviours worth knowing. **Duplicate `x` values form a step**: the sort is stable, and an
exact hit lands on the *last* duplicate (`upper_bound` semantics), so `{{1,0},{1,10}}` jumps rather
than averages. And **`LookupTable1D::FromCSV` resolves the VFS path itself**, unlike
`DataExport::LoadCSV` underneath it, which does no resolution at all.

## Procedural noise

```cpp
Cosmic::Noise noise(1337);

float h    = noise.Fbm2D(x * 0.01f, y * 0.01f, 6);   // terrain height, [-1, 1]
float gust = noise.Perlin1D(t * 0.4f);               // one wind channel
float ridge = noise.Ridged2D(x, y, 5);               // mountain spines, [0, 1]
```

| Family | Calls | Range |
| --- | --- | --- |
| Value noise — cheap, blockier | `Value1D/2D/3D` | `[-1, 1]` |
| Perlin gradient noise — the workhorse | `Perlin1D/2D/3D` | `[-1, 1]`, **exactly 0 on the integer lattice** |
| fBm octave stack | `Fbm1D/2D/3D(…, octaves, lacunarity = 2, gain = 0.5)` | `[-1, 1]` for any octave count |
| Ridged multifractal | `Ridged2D(x, y, octaves = 5, lacunarity = 2, gain = 0.5)` | `[0, 1]` |

The ranges are **guaranteed, not approximate** — each result is clamped, and fBm re-normalises by
its amplitude sum so the bound holds however many octaves you stack. The doctests check all of it,
including that Perlin is zero at integer coordinates and that `Ridged2D` really does crease more
sharply than `Fbm2D`.

Ridged is `1 − |Perlin|`, squared to sharpen the crest and weighted by the previous octave. Where
`Fbm2D` sums *signed* noise and rounds its crests off, the `abs()` folds every zero crossing into a
ridgeline — which is why it looks like mountains and fBm looks like hills.

**Construction shuffles a 256-entry permutation table**, so build a `Noise` once and keep it. A
terrain height function that constructs one per sample is the classic mistake; Frontier's
`HeightfieldComposer` caches a `thread_local` instance keyed on the seed, and its world terrains
capture one into the lambda (`tspec.HeightFunction = [noise = Cosmic::Noise(kSeed)](float u, float v)`).

Sampling is `const` and allocation-free, so a cached `Noise` is safe to share across a `ParallelFor`.

ViperSim's wind field is the compact example — three offset Perlin channels approximating
low-passed turbulence, deterministic in sim time so a recording replays exactly (`sim/Wind.h`):

```cpp
glm::vec3 Sample(float t) const
{
    if (gustSigma <= 0.0f)
        return steadyNed;

    const float x = t * gustFreqHz;
    const glm::vec3 gust{
        m_Noise.Perlin1D(x)          + 0.5f * m_Noise.Perlin1D(x * 3.1f + 11.3f),
        m_Noise.Perlin1D(x + 101.7f) + 0.5f * m_Noise.Perlin1D(x * 3.1f + 57.9f),
        0.35f * m_Noise.Perlin1D(x + 233.1f),
    };
    return steadyNed + gust * (2.0f * gustSigma);
}
```

Offsetting each channel in *noise space* (`+101.7f`, `+233.1f`) is how you get independent-looking
channels from one `Noise` object without three seeds.

## Deterministic randomness

```cpp
Cosmic::Random rng(1234);                  // seed
Cosmic::Random gyro(seed, /*stream*/ 1);   // independent stream, same seed

uint32_t  u  = rng.NextUInt32();           // full 32-bit
uint32_t  b  = rng.NextUInt32(6);          // [0, 6) — no modulo bias
float     f  = rng.NextFloat();            // [0, 1), 24-bit exact grid
float     r  = rng.Range(-1.0f, 1.0f);     // [min, max)
int       i  = rng.RangeInt(1, 6);         // [min, max] INCLUSIVE
float     g  = rng.Gaussian(0.0f, 0.02f);  // mean, sigma
glm::vec3 p  = rng.InUnitSphere();         // uniform inside
glm::vec3 s  = rng.OnUnitSphere();         // uniform on the surface
glm::vec2 d  = rng.InUnitDisc();
```

**Why this exists instead of `<random>`:** `std::mt19937`'s *sequence* is portable, but the standard
**distributions are not** — `std::normal_distribution` and `std::uniform_int_distribution` are free
to differ between implementations. A replay recorded on one toolchain then diverges on another, with
no error and no obvious cause. `Random` owns both the generator (PCG32, XSH-RR variant) and every
distribution, so a seed means the same thing everywhere.

That claim is load-bearing enough to have a canonical-vector test: `tests/test_random.cpp` checks
seed `42`, stream `54` against the sequence published by PCG's author
(`0xa15c02b7, 0x7b47f409, 0xba1d3330, …`). If that test fails, the generator is no longer canonical
PCG32 and **every committed replay is invalid**.

**Use one stream per consumer.** The second constructor argument selects an independent stream from
the same seed. The reason is subtle and worth internalising: if every sensor draws from one stream,
*adding a consumer shifts everybody else's sequence*, and a replay recorded before the change no
longer reproduces. ViperSim gives each sensor its own (`sim/Sensors.h`):

```cpp
m_RngGyro  = Cosmic::Random(m_P.seed, 1);
m_RngAccel = Cosmic::Random(m_P.seed, 2);
m_RngBaro  = Cosmic::Random(m_P.seed, 3);
m_RngMag   = Cosmic::Random(m_P.seed, 4);
m_RngPitot = Cosmic::Random(m_P.seed, 5);
m_RngGps   = Cosmic::Random(m_P.seed, 6);
```

Two implementation details that leak into behaviour. `Gaussian` is Box-Muller **with spare
caching** — it draws two uniforms and returns two normals, so every *other* call consumes nothing
from the generator. And `NextUInt32(bound)`, `InUnitSphere`, `OnUnitSphere` and `InUnitDisc` all
**loop** (rejection sampling), so the number of underlying draws per call is not fixed. Neither
breaks determinism, but both mean "N `Gaussian` calls consume 2N uniforms" is wrong.

`Random` is a plain value type — copy it to snapshot a stream, assign to rewind one.

## Frames and attitude

`math/Spatial.h` is the **one authoritative place** for the engine's coordinate conventions, and
everything else — the renderer, the cameras, every sim — agrees with it.

```
WORLD FRAME (simulation): NED, right-handed
  +X = North   +Y = East   +Z = Down        gravity is +Z, magnitude Math::GravityMss = 9.80665

RENDER FRAME (Renderer3D, cameras, TransformComponent): right-handed, Y-up
  +X = East    +Y = Up     +Z = South

  render(x, y, z) = (ned.e, -ned.d, -ned.n)
```

> **The scene graph is *not* NED.** `TransformComponent::Position` is render-frame, Y-up. A
> simulation keeps its own NED state and converts at draw time — every frame, in one place. Both
> ViperSim and Engine3DDemo are written that way, and mixing the two frames in one variable is the
> single most common bug in this area.

```cpp
// Sim -> render, once per frame.
transform.Position    = Cosmic::Math::NedToRender(m_State.posNed);
transform.RotationQuat = Cosmic::Math::NedQuatToRender(m_State.attNed);
transform.UseQuatRotation = true;
```

`NedToRender` / `RenderToNed` are exact inverses; `NedToRenderMatrix()` is the same basis change as
a `glm::mat3` (determinant `+1`) when you need to rotate a whole basis rather than a vector.
`NedQuatToRender` is the change of basis `C ⊗ q ⊗ C⁻¹` for attitudes.

### Euler angles

```cpp
glm::quat q     = Cosmic::Math::QuatFromEulerZYX({ rollDeg, pitchDeg, yawDeg });
glm::vec3 euler = Cosmic::Math::EulerZYXFromQuat(q);   // (roll, pitch, yaw), degrees
```

- **ZYX intrinsic** — yaw ψ about Z, then pitch θ about Y, then roll φ about X. The aerospace
  standard.
- **Degrees at the API boundary**, matching `TransformComponent::Rotation`. Radians internally.
- **The `glm::vec3` is `(roll, pitch, yaw)` = `(x, y, z)`**, not yaw-first, even though the rotation
  order is yaw-first. Easy to get backwards.
- `EulerZYXFromQuat` clamps pitch to ±90° at the gimbal poles, so a round-trip near vertical is not
  exact — the doctest deliberately tests away from the poles.

### Attitude integration

```cpp
q = Cosmic::Math::IntegrateBodyRate(q, omegaBodyRadPerSec, dt);
```

Standard quaternion kinematics, `q̇ = ½ · q ⊗ (0, ω_body)`, a first-order step, **renormalised every
call**. Accurate at the small `dt` of a fixed-step sim; wrap it in RK4 at the call site if you need
higher order. The doctest spins at π/2 rad/s for 1 s in 1000 steps and checks both the resulting
90° yaw and that `|q| == 1`.

Remember glm's own conventions while you are here: the `glm::quat` constructor is `(w, x, y, z)`,
and `q1 * q2` applies `q2` first.

## Determinism: what is actually guaranteed

This is the property the whole tier exists to protect — a recording replayed later, or on another
machine, must produce the same numbers. It is worth being precise about how far the guarantee
reaches.

**Bit-exact, and pinned by a test:**

- `Random`'s integer stream. PCG32 XSH-RR is pure 64-bit integer arithmetic, and
  `tests/test_random.cpp` checks it against the algorithm's canonical reference vector.
- `Random::NextFloat`, `Range`, `RangeInt`, `NextUInt32(bound)` — integer arithmetic plus one exact
  power-of-two scale.
- `Noise`'s permutation table — a Fisher-Yates shuffle driven by PCG32, which is why the header
  refuses `std::shuffle` + `std::mt19937`.

**Deterministic under IEEE-754, which is what you have on every supported target:**

- Every `Noise` sampler. `Value*`, `Perlin*`, `Fbm*` and `Ridged2D` use only `+ - * /`, comparisons,
  `fabs` and `clamp` — all correctly rounded. Same seed and coordinate give the same value.
- `Random::OnUnitSphere` and `InUnitSphere` (`sqrt` is correctly rounded per IEEE-754).
- The integrators and filters, for a given build.

**Rests on your C runtime, not on the standard:**

- `Random::Gaussian` calls `std::log`, `std::sqrt`, `std::sin` and `std::cos`. `sqrt` is exact;
  the transcendentals are **not required to be correctly rounded** and do differ by a few ULP
  between libm implementations. Within one toolchain a seed reproduces exactly — which is what
  replay-on-the-same-build needs — but "the same sensor noise everywhere, forever", as `Random.h`
  puts it, is a slight over-promise for the Gaussian specifically.
- `LowPassFilter` (`std::exp`) and `Biquad`'s coefficient setup (`std::sin`, `std::cos`) inherit the
  same caveat, one step removed: the coefficients may differ in the last bits, and a recursive
  filter then diverges slowly. Configure once, not per frame, and the difference stays in the noise.

**Nothing here is protected against fast-math.** Building with contraction or reassociation enabled
changes float results in all of the above. Cosmic does not enable it; if you do, determinism is
your problem.

The practical rule: **seed everything explicitly, one stream per consumer, and record the seed
alongside the run.** ViperSim writes its config revision into every recording for exactly this
reason.

## Common patterns

**Own your sim state; convert at the boundary.** Keep NED position, NED velocity, an attitude
quaternion and body rates in your own struct. Touch `TransformComponent` once per frame, through
`NedToRender` / `NedQuatToRender`. Never store a half-converted value.

**Integrate at a fixed step, render at a variable one.** Sim state advances in `OnFixedUpdate`;
`OnUpdate` only reads it. `FixedSubstepper` subdivides the fixed tick when the solver needs a
finer step than the physics rate. See [`time-and-ticks.md`](time-and-ticks.md) for the tick
contract, including the fact that `TimeScale` changes how *often* the fixed pass fires and never the
magnitude of its `dt`.

**Split rotational and translational integration.** Semi-implicit or direct on `ω` and the
quaternion, RK4 on `(p, v)` with attitude frozen for the substep. That is the ViperSim shape and it
composes cleanly because both halves advance at the same `h`.

**One `Random` per consumer, one `Noise` per field.** Both are cheap to hold and expensive to
recreate — and re-seeding mid-run silently invalidates every replay taken before it.

**Measured data goes in a table or a TOML file, not in a constant.** `LookupTable1D::FromCSV`
for curves, [`Config`](assets-and-vfs.md#configure-with-toml) for scalars. ViperSim's entire
airframe — masses, inertias, aero coefficients, motor lag — is `viper.toml`, and its config
revision is recorded with every take.

**Prime filters by just calling them.** Every filter's first `Update` seeds itself from the sample,
so there is no need for a warm-up loop. Call `Reset(knownValue)` only when you *want* a specific
starting state.

## Pitfalls

**"My RK4 state won't compile."**
`State` needs `operator+(State, State)` and `operator*(State, float)`, in that exact shape — the
scalar goes on the right. `glm::vec3` has both; your struct needs two free functions.

**"My derivative function has side effects and everything is four times too big."**
`deriv` is evaluated four times per step, at intermediate states. It must be pure.

**"My quaternion drifted off unit length / my attitude is garbage."**
Something RK4'd the quaternion components. Use `Math::IntegrateBodyRate`, which renormalises.

**"`FixedSubstepper` isn't giving me 480 Hz."**
It gives you `dt / substeps`, whatever `dt` is. From `OnUpdate` that varies per frame. Drive it
from `OnFixedUpdate`.

**"My yaw servo sweeps the wrong way past 180°."**
`RateLimiter` sees a plain float and treats a ±180 wrap as a 358° error. Unwrap the angle first.

**"My `Biquad` behaves differently at a different frame rate."**
Its sample rate is baked in by `SetLowPass`/`SetHighPass`/`SetNotch`. Passing a different `dt` to
`Update` does nothing — reconfigure instead.

**"My filter output jumps on the first frame."**
It shouldn't — every filter primes from its first sample. If it does, something called
`Reset(0.0f)` before a non-zero first sample.

**"Everything froze during a pause and now the filter is stale."**
`dt <= 0` returns the last output unchanged, by design. That is usually what you want; if you need
the filter to keep up with wall time, do not feed it zero.

**"My lookup table returns 0 everywhere."**
It is empty. Check `IsValid()` and the log: a bad 2D shape or a failed CSV both log an error and
leave the table empty rather than throwing.

**"My 2D table samples the wrong cell."**
`values` is row-major: `values[ix * yBreaks.size() + iy]`. Transposing it produces a plausible-
looking surface that is wrong everywhere.

**"Terrain generation is slow."**
Something is constructing a `Noise` per sample. Build it once and capture it — sampling is `const`
and thread-safe.

**"Two runs with the same seed diverged."**
Either a consumer was added and shifted a shared stream (use per-consumer streams), or the
divergence is in `Gaussian`/`exp`-based code across different toolchains. See
[the determinism section](#determinism-what-is-actually-guaranteed).

**"My object renders at the wrong place and rotates strangely."**
NED and render-frame values got mixed. `+Z` is *Down* in the sim and *South* in the renderer;
gravity is `+Z` in one and `-Y` in the other.

## See also

- [`time-and-ticks.md`](time-and-ticks.md) — the fixed vs variable tick, `TimeScale`, and why the
  fixed `dt` magnitude never changes
- [`physics.md`](physics.md) — when to reach for the rigid-body engine instead of integrating
  yourself
- [`assets-and-vfs.md`](assets-and-vfs.md#configure-with-toml) — `Config` for tunables and
  `DataExport` under `LookupTable1D::FromCSV`
- [`world-systems.md`](world-systems.md) — `Noise` on the terrain side, and the `32·2^k+1`
  resolution rule
- [`serial-and-telemetry.md`](serial-and-telemetry.md) — recording a run so a seed is worth having
- [`../reference/math.md`](../reference/math.md) *(skeleton)* ·
  [`../systems/math-sim-toolkit.md`](../systems/math-sim-toolkit.md) *(skeleton)*
