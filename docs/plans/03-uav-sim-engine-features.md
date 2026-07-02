# Engine Features Required for the Viper UAV Simulator

> **Scope:** what the **engine** must grow so that a Gazebo/ArduPilot-SITL-class simulator can be
> built as a normal Cosmic project DLL. The app itself is planned in
> [`04-uav-sim-app-plan.md`](04-uav-sim-app-plan.md); the 3D rendering slice is detailed in
> [`05-3d-engine-plan.md`](05-3d-engine-plan.md).
>
> **Design rule (applies to every item):** the engine provides *generic, reusable verbs*; the
> application owns *domain logic*. A UDP socket goes in the engine; MAVLink goes in the app (or a
> shared app-side library). A quaternion helper goes in the engine; a tailsitter mixer does not.

## What the engine already provides (verified — reuse, don't rebuild)

| Need | Existing engine service |
| --- | --- |
| Deterministic sim stepping | Fixed-timestep loop with accumulator + spiral-of-death clamp (`Application.cpp:80–128`), pause/slow-mo/rewind via `SetTimeScale` |
| Parallelism | `JobSystem` + `ParallelSystem` 4-pass pipeline (`Cosmic/src/jobs/`) |
| Flight-data logging & replay | `DataRecorder` (columnar, thread-safe, autosave) + `DataPlayer` + `TelemetryPanel` (`Cosmic/src/telemetry/`) — this is the black-box recorder and replay scrubber for free |
| Serial links (HIL, real FC) | `SerialPort` (async open, overlapped reads) + `SerialLink` (connect UI/policy) (`Cosmic/src/serial/`) |
| Plots/instruments UI | ImGui + ImPlot, docked workspace, theme system |
| Plugin app model | Project DLL + launcher + `WorkspaceLayer` panels |
| Math base | glm (vectors/matrices; `gtc/quaternion` available but unused so far) |

## Feature gaps, in build order

### E1 — Configurable fixed timestep (+ sim substepping) — SMALL, DO FIRST ✅ *(done 2026-07-01)*
`Application::Run` hardcodes `1/60` (`Application.cpp:84`). Flight control loops want 250–1000 Hz;
rendering does not.
- `Application::SetFixedTimestep(float hz)` (default 60, clamped sane range) — one member + docs.
- **Recommended pattern instead of a 1 kHz engine tick:** keep the engine at 60–120 Hz and let the
  sim run N physics/control substeps per `OnFixedUpdate` inside the app. The engine change is still
  worth it (some HIL scenarios want a higher outer rate), but substepping is the primary mechanism.
- Acceptance: template project runs unchanged at default; a test layer at 240 Hz sees 4× calls.

### E2 — Sim-grade 3D viewport — see [doc 05](05-3d-engine-plan.md) — S1/S2 ✅ *(done 2026-07-01)*
`PerspectiveCamera`, `OrbitCameraController`, `Renderer3D` (lines/grid/axes → meshes/Lambert),
render-to-texture FPV inset, `FrameBuffer::GetDepthAttachmentRendererID()`. Stages S1–S3 there.
S1 + S2 shipped (acceptance app: `Projects/Engine3DDemo`); S3 remains, driven by ViperSim P4–P5.

### E3 — Quaternion & frame math helpers — SMALL ✅ *(done 2026-07-01: `math/Spatial.h` + `tests/test_spatial.cpp`)*
New header `Cosmic/src/math/Spatial.h` (engine-level, header-only where possible):
- `using Quat = glm::quat;` + helpers: `QuatFromEuler(deg)`, `EulerFromQuat`, quaternion integration
  step `Integrate(Quat q, vec3 omegaBody, float dt)` (dq = ½·q⊗ω, normalized), `Slerp` passthrough.
- Frame conventions **documented once, used everywhere**: world = **NED** (aviation standard) with a
  clearly-named render conversion `NedToRender(vec3)` (render Y-up). Getting this into the engine as
  one authoritative header prevents the classic every-file-has-its-own-sign-convention disaster.
- Rotation-matrix ↔ quaternion ↔ Euler conversions with the ZYX (yaw-pitch-roll) convention stated
  in the doc comments.
- Acceptance: unit tests (WO-14 harness) for round-trips and integration against known rotations.

### E4 — UDP sockets — MEDIUM
`Cosmic/src/net/UdpSocket.h/.cpp` (Winsock2, `COSMIC_API`):
- `Open(localPort)`, `SendTo(host, port, bytes)`, `Receive(buffer) → (bytes, fromAddr)` non-blocking,
  plus an optional background-thread receive with the same flush pattern `SerialPort` uses
  (mutexed buffer, `FlushPackets()`), so the app polls from `OnFixedUpdate` exactly like serial.
- WSAStartup/WSACleanup ref-counted in a tiny `NetContext` singleton.
- **Engine stops here.** MAVLink framing/dialects live app-side (header-only `mavlink/` generated
  headers vendored by the app). Rationale: MAVLink is domain protocol, not engine infrastructure.
- Acceptance: loopback echo test in the unit harness; app-level QGroundControl handshake later.

### E5 — Binary-safe serial framing helper — SMALL ✅ *(done 2026-07-01: `serial/Framing.h` + `tests/test_framing.cpp`)*
Today's serial stack returns raw text chunks (`FlushBuffer`); the SF apps parse ASCII lines. HIL needs
binary frames with integrity. Add `Cosmic/src/serial/Framing.h`: COBS encode/decode + CRC16 helpers
(pure functions, no I/O — the same header compiles on the Teensy side, which is exactly the point).
- Acceptance: encode→decode round-trip tests incl. payloads containing zero bytes.

### E6 — Asset cache — MEDIUM (carried from IMPROVEMENTS §5.1)
`AssetLibrary` keyed by resolved path returning existing `Ref<Texture2D>/Ref<Shader>` (and `Ref<Mesh>`
once E2-S2 lands). The sim app reloads panels/screens frequently; this stops duplicate GPU uploads.
Not a blocker for first flight — schedule it with E2-S2.

### E7 — Gamepad/joystick input — SMALL-MEDIUM
Manual flying needs an axis input better than WASD. GLFW (already vendored) has
`glfwGetGamepadState` — expose `Input::GetGamepadAxis(axis)` / `IsGamepadButtonPressed(btn)` +
a connected query. Later, an RC transmitter in USB-joystick mode works through the same API.
- Acceptance: template layer prints stick values with a pad plugged in.

### E8 — (Optional, later) scene serialization
Not needed for the sim (the app defines vehicles in code/config it owns). Park it.

## Explicit non-goals for the engine

- **No physics middleware (Bullet/Jolt/Box2D).** Flight dynamics is smooth 6DOF ODE integration —
  gravity + aero + rotor forces, RK4, no contact resolution beyond "ground plane touched." A vendored
  rigid-body engine adds a dependency and its own integrator for zero benefit here. Landing-gear
  contact = a few spring-damper point contacts in the app. Revisit only if a future project needs
  stacks of colliding bodies.
- **No MAVLink/protocol code in the engine** (app-side).
- **No full 3D tier as a sim prerequisite** — but S1/S2 are built under doc 05's forward-compatibility
  contract, and the full tier is now a planned trajectory (doc 05 S4–S5, roadmap Phase 8) rather than
  parked.

### E9 — Audio (optional for the sim; see [doc 08](08-audio-plan.md))
miniaudio-based `AudioEngine` + `Ref<Sound>`. Sim uses: failsafe/mode alert tones (A2), RPM-pitched
motor loop, variometer beep. Not on the sim critical path — slot A1 as filler after Phase 1.

## Suggested implementation order & why

| # | Item | Rationale | Status |
| --- | --- | --- | --- |
| 1 | E1 timestep | 20-minute change; unblocks control-rate experiments immediately | ✅ 2026-07-01 |
| 2 | E3 math | Everything downstream (E2 cameras, dynamics in doc 04) consumes it | ✅ 2026-07-01 |
| 3 | E2-S1 viewport (lines/grid/orbit cam) | First visible 3D; lets the app's dynamics work be *seen* early | ✅ 2026-07-01 |
| 4 | E5 framing | Tiny, pure; write alongside early FC-core work (doc 04 P2) | ✅ 2026-07-01 |
| 5 | E2-S2 meshes | Placeholder aircraft visual | ✅ 2026-07-01 |
| 6 | E7 gamepad | Needed when manual-flying the hover model (doc 04 P3) | — |
| 7 | E2-S3 FPV inset + ribbon | Needed for transition/orbit phases (doc 04 P4–P5) | — |
| 8 | E4 UDP | Needed only when GCS/ArduPilot interop starts (doc 04 P6) | — |
| 9 | E6 asset cache | Quality-of-life; slot anywhere after E2-S2 | — |

Each item should land as its own PR with the acceptance check listed above; items 1–4 are safely
parallel with the bugfix pass ([doc 02](02-bugfix-ai-gameplan.md)) as long as they're separate branches.
