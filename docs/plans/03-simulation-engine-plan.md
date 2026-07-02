# Simulation Engine Plan — Generic Verbs for Any Simulation

> **Rewritten 2026-07-01** (was `03-uav-sim-engine-features.md`, which was scoped to the UAV
> simulator). **Scope:** the engine-side toolkit that makes Cosmic a host for *any* simulation —
> flight dynamics, ground vehicles, fluids, controls experiments, robotics — with the Viper
> simulator ([doc 04](04-viper-sim-plan.md)) as the first consumer. The 3D rendering slice lives
> in [doc 05](05-3d-engine-plan.md).
>
> **Design rule (binding):** the engine provides *generic, reusable verbs*; the application owns
> *domain logic*. A UDP socket, an RK4 template, a lookup table, a noise function → engine. A
> tailsitter mixer, MAVLink dialect, aero polar data, a Navier–Stokes solver → app. When an app
> needs something the engine lacks, the engine grows a *general* verb, never a domain-shaped one.

## What the engine already provides (verified — reuse, don't rebuild)

| Need | Existing engine service |
| --- | --- |
| Deterministic stepping | Fixed-timestep loop + accumulator + spiral-of-death clamp; `Application::SetFixedTimestepHz`; pause/slow-mo via `SetTimeScale`. Pattern for high-rate sims: engine at 60–120 Hz, N substeps inside `OnFixedUpdate` |
| Frame/attitude math | `math/Spatial.h` — quaternions, ZYX Euler, body-rate integration, NED↔render (+ unit tests) |
| Data logging & replay | `DataRecorder` (columnar, thread-safe, autosave) + `DataPlayer` + `TelemetryPanel` — black-box recorder + replay scrubber for free |
| Serial links (HIL, real hardware) | `SerialPort` (async open, overlapped I/O) + `SerialLink` (connect UI/policy) + `serial/Framing.h` (COBS+CRC16, compiles on MCU targets) |
| Parallelism | `JobSystem`, `ParallelFor`, `ParallelSystem` 4-pass pipeline |
| Plots & dashboards | ImGui + ImPlot, docked workspace, themes, `ui/Widgets.h` |
| 3D visualization | `Renderer3D` + `PerspectiveCamera` + `OrbitCameraController` (doc 05 S1/S2 ✅) |
| Plugin app model | Project DLL + launcher + `--project` boot flag |
| CSV export | `utils/DataExport.h` |

## Status of the original E-series

| Item | Status |
| --- | --- |
| E1 configurable fixed timestep | ✅ 2026-07-01 |
| E2 sim-grade 3D viewport | ✅ S1/S2 (doc 05); S3 pending |
| E3 quaternion/frame math (`Spatial.h`) | ✅ 2026-07-01 |
| E5 COBS+CRC framing (`Framing.h`) | ✅ 2026-07-01 |
| E9 audio | → [doc 08](08-audio-plan.md) |
| **E10 config (TOML)** | ✅ 2026-07-02 — `utils/Config.h/.cpp`, toml++ vendored, `test_config.cpp` (7 cases), template demo |
| **E11 integrators** | ✅ 2026-07-02 — `math/Integrators.h`, `test_integrators.cpp` (projectile/spring/order/substepper) |
| **E12 filters** | ✅ 2026-07-02 — `math/Filters.h`, `test_filters.cpp` (LPF τ, biquad −3 dB, rate-limit, notch, washout) |
| **E13 lookup tables** | ✅ 2026-07-02 — `math/LookupTable.h` + `DataExport::LoadCSV`, `test_lookuptable.cpp` (1D/2D/CSV) |
| **E14 noise** | ✅ 2026-07-02 — `math/Noise.h` (value/Perlin/fBm 1-3D), `test_noise.cpp` (seed determinism, bounds, falloff) |
| **E15 RNG** | ✅ 2026-07-02 — `math/Random.h` (PCG32), `test_random.cpp` (canonical reference seq, Gaussian moments) |
| **E7 gamepad** | ✅ 2026-07-02 — `Input::GetGamepadAxis/…` + `codes/GamepadCodes.h`; template layer live axis readout (needs a physical pad to see values) |

> **E-series acceptance (2026-07-02):** all seven merged on `main` with green tests — `CosmicTests`
> reports 55 cases / 103,870 assertions passing. The `ExampleProject` template (instantiated via the
> Launcher's New Project) demonstrates E10 config load (`project://config/template.toml`) and E7
> live gamepad axes in its root inspector's "Sim Toolkit" section.

## Live items, in build order

Each is one PR with the stated acceptance. Sizes: S < 1 day of AI work, M = 1–3 days.

### E10 — Config files (TOML) — S/M, DO FIRST
Every sim wants data-driven parameters (masses, gains, noise levels) without recompiling; today
apps hand-roll or hardcode. Add `Cosmic/src/utils/Config.h/.cpp` (`COSMIC_API`):
- Vendor **toml++** (`dependencies/tomlplusplus/`, single header, MIT). TOML over JSON/INI:
  comments + human-friendly + typed; JSON stays possible later behind the same facade.
- API: `Config::Load("project://config/vehicle.toml")` → `Ref<Config>`;
  typed getters with defaults: `Get<float>("battery.capacity_wh", 100.0f)`,
  `Get<glm::vec3>`, `GetTable("motors")` for arrays-of-tables; `Has(key)`; dotted-path keys.
- Read-only first; `Save` deferred until a consumer exists (Theme files already have their own format).
- **Acceptance:** unit tests (parse from string; defaults on missing keys; vec3 array); template
  project loads one value from a sample TOML.

### E11 — ODE integrators — S (header-only)
`Cosmic/src/math/Integrators.h`: generic fixed-step integrators over user state types:
- `IntegrateRK4(state, deriv_fn, t, dt)` and `IntegrateSemiImplicitEuler(...)` where `state` is
  any type with `+` and `scalar *` (works for structs of vec3/quats via user-provided ops, and for
  plain `float`/`glm::vecN` out of the box).
- `FixedSubstepper` helper: wraps the "N substeps inside OnFixedUpdate" pattern
  (`substepper.Run(dt, 8, [&](float h){ ... })`).
- Doc comment: quaternions are integrated via `Spatial.h::Integrate` (dq = ½·q⊗ω), *not* by
  naive RK4 on components; the two compose (RK4 the translational state, quaternion-integrate attitude).
- **Acceptance:** unit tests vs. analytic solutions — projectile (exact), mass-spring-damper
  (energy decay), RK4 error ~O(h⁴) demonstrated by halving h.

### E12 — Signal filters — S (header-only)
`Cosmic/src/math/Filters.h`: the toolbox every estimator/controller/telemetry display reaches for:
- `LowPassFilter` (first-order, tau or cutoff-Hz constructor), `Derivative` (filtered),
  `RateLimiter` (slew), `MovingAverage<N>`, `Biquad` (LPF/HPF/notch via RBJ cookbook),
  `Washout` (high-pass for motion cues).
- All: `Reset(value)`, `Update(sample, dt)` → filtered value; no allocations.
- **Acceptance:** unit tests — LPF step response hits 63.2% at t=tau (±1%); biquad magnitude at
  cutoff ≈ −3 dB; rate limiter clamps exactly.

### E13 — Lookup tables — S (header-only)
`Cosmic/src/math/LookupTable.h`: `LookupTable1D` / `LookupTable2D` — sorted breakpoints, linear
interpolation, clamp-or-extrapolate policy flag. This is how apps encode aero polars, motor thrust
maps, gain schedules, battery discharge curves — pure data in, engine does the interp.
- Constructors from `std::vector` pairs and from two-column CSV (`utils/DataExport` gains a tiny
  `LoadCSV` counterpart).
- **Acceptance:** unit tests — exact at breakpoints, midpoint interp, clamp + extrapolate modes,
  2D bilinear against hand values.

### E14 — Noise & procedural utilities — S/M (header-only where possible)
`Cosmic/src/math/Noise.h`: seeded value noise + **Perlin/simplex** + fBm octaves (1D/2D/3D).
Consumers on both tracks: terrain heightmaps (doc 05 S8), wind gusts / Dryden-ish turbulence
(doc 04), procedural textures.
- Deterministic for a given seed (cross-run reproducibility is the point — replays must match).
- **Acceptance:** unit tests — same seed ⇒ same field; range bounds; fBm octave falloff.

### E15 — Deterministic RNG — S (header-only)
`Cosmic/src/math/Random.h`: PCG32 wrapper — `Random rng(seed)`, `NextFloat()`, `Gaussian(mean,
sigma)` (Box-Muller), `InUnitSphere()`. Sensor-noise models and particle systems both need
*seedable, portable* randomness (`std::mt19937` distributions differ across compilers —
that breaks replay determinism).
- **Acceptance:** unit tests — fixed seed produces a committed reference sequence; Gaussian
  mean/sigma within tolerance over 10⁵ samples.

### E4 — UDP sockets — M *(unchanged from the original plan)*
`Cosmic/src/net/UdpSocket.h/.cpp` (Winsock2, `COSMIC_API`): `Open(localPort)`,
`SendTo(host, port, bytes)`, non-blocking `Receive`, optional background-thread receive with the
same mutex+flush pattern `SerialPort` uses; WSAStartup ref-counted in a tiny `NetContext`.
**Engine stops here** — MAVLink/protocols are app-side. **Acceptance:** loopback echo unit test.

### E7 — Gamepad/joystick input — S/M *(unchanged)*
GLFW is already vendored: `Input::GetGamepadAxis(axis)`, `IsGamepadButtonPressed(btn)`,
`IsGamepadConnected()`. An RC transmitter in USB-joystick mode works through the same API.
**Acceptance:** template layer prints stick values with a pad plugged in.

### E6 — Asset cache — M *(shared with 3D; spec lives in doc 05 S4.4)*
Listed here because sims reload screens/panels frequently; implement once under S4.4.

### E8 — Scene serialization — parked
Not needed for sims (apps own their config via E10). Unlock condition tracked in doc 05 S14.

## Where fluids/CFD-class work fits (so the rule is explicit)

A grid/particle fluid solver is **app-side domain logic** built on engine primitives: `JobSystem`/
`ParallelFor` for CPU solvers, **compute shaders + SSBOs (doc 05 S4.7)** for GPU solvers, E14
noise for initialization, `DataRecorder` for capture, `Renderer3D`/particles for display. The
engine's job is to make those primitives excellent — not to ship a Navier–Stokes solver. Same
logic that keeps flight dynamics (and physics middleware) out of the engine: revisit only if
multiple projects duplicate identical solver plumbing.

## Suggested order & rationale

| # | Item | Size | Why this order |
| --- | --- | --- | --- |
| 1 | E10 config | S/M | Viper P0 wants `viper.toml` on day one |
| 2 | E11 integrators | S | Viper P1 dynamics + every future sim |
| 3 | E13 lookup tables | S | Viper P1/P4 (thrust maps, polars) |
| 4 | E12 filters | S | Viper P2 estimator; telemetry smoothing everywhere |
| 5 | E15 RNG | S | needed before any sensor-noise modeling (P3) |
| 6 | E14 noise | S/M | gusts (P3) + terrain (S8) — two consumers |
| 7 | E7 gamepad | S/M | when manual flying starts (P3) |
| 8 | E4 UDP | M | only when GCS/ArduPilot interop starts (P8) |
| 9 | E6 asset cache | M | quality-of-life; implement per doc 05 S4.4 |

E10–E15 are header-heavy, test-verified, and independent — ideal lower-tier-AI tasks and safe to
run in parallel branches. All land with unit tests in the `tests/` doctest harness per its README.
