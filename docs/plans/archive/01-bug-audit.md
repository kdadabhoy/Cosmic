# Cosmic Engine — Bug Audit (2026-07-01)

> **Scope:** Fresh audit of the engine core against the current `main` (post SF-Improvements merge, `3e8b1f8`).
> Builds on [`docs/archive/IMPROVEMENTS.md`](../../archive/IMPROVEMENTS.md) (§1–§4 already implemented) and
> [`docs/archive/engine_analysis.md`](../../archive/engine_analysis.md) (2026-05-30; partially stale — see §5 below).
> **Every item in §1 was hand-verified against source at the stated file:line on 2026-07-01.**
> Items in §2 are verified design gaps (not defects). Items in §4 are claims from automated review
> that were **checked and disproved** — do not "fix" them.
>
> **Companion doc:** [`02-bugfix-ai-gameplan.md`](02-bugfix-ai-gameplan.md) turns §1–§2 into
> ready-to-paste work orders for an AI assistant.

---

## 1. Verified bugs (fix these)

### BUG-1 — `OpenGLTexture::SetData` breaks for 1/2-channel textures
**File:** `Cosmic/src/platform/OpenGL/OpenGLTexture.cpp:164`
```cpp
uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
```
The file constructor now supports `GL_RED` (1 byte/px) and `GL_RG` (2 bytes/px) (lines 85–94), but
`SetData` still assumes only RGBA/RGB. A correctly-sized grayscale upload computes the wrong expected
size and is **rejected** with a size-mismatch error.
**Failure:** create an R8 texture from a grayscale PNG, call `SetData(data, w*h*1)` → error log, no upload.
**Fix:** map `m_DataFormat` → bpp for all four formats (`GL_RGBA`=4, `GL_RGB`=3, `GL_RG`=2, `GL_RED`=1).

### BUG-2 — `DataPlayer` documents a directory fallback that does not exist (+ wrong version labels)
**Files:** `Cosmic/src/telemetry/DataPlayer.h:64`, `Cosmic/src/telemetry/DataPlayer.cpp:32–43`,
`Cosmic/src/telemetry/DataRecorder.h:165`
- `DataPlayer.h:64` claims: *"For a directory: loads scene.bin (v2/v3) if present; otherwise loads all individual .bin files"*.
  The implementation only looks for `scene.bin` and gives up (warn at `DataPlayer.cpp:56`). There is
  no v2/v3 — the format is v1 (`DataRecorder.h:40` writes `version = 1`).
- `DataRecorder.h:165` `Flush()` docstring says *"v3 binary format"* — also wrong.
**Failure:** a user with a session folder of per-entity `.bin` files (no `scene.bin`) gets a silent empty load after reading the header docs.
**Fix (decide one):** (a) implement the fallback — second pass loading every `*.bin` in the directory
when `m_Entities` is empty; or (b) delete the fallback claim. Either way correct both docstrings to "v1".

### BUG-3 — `DrawInstancedCircles` leaves `ActiveCircleShader = nullptr` → spurious flush
**File:** `Cosmic/src/renderer/Renderer2D.cpp:1354`
Every other reset site restores `s_Data.DefaultCircleShader` (lines 287, 556, 610, 631). This one sets
`nullptr`, so the next `DrawCircle()` in the same frame sees a shader mismatch and triggers a wasted
`FlushAndReset` on an empty batch.
**Fix:** `s_Data.ActiveCircleShader = s_Data.DefaultCircleShader;` (match line 556).

### BUG-4 — `EntityPicker::Pick` ignores entity rotation
**File:** `Cosmic/src/telemetry/EntityPicker.h:103–106`
Axis-aligned test against `Scale`-sized box; a rotated entity has a hit box that doesn't match its
visual bounds. **Fix:** rotate the query point into local space by `-Rotation.z` before the AABB test
(note: `TransformComponent::Rotation` is stored in **degrees** — `GetTransform()` converts with
`glm::radians`, `Components.h:49–51` — the pick math must convert too).

### BUG-5 — README documents a non-existent `RenderPass` constructor
**File:** `README.md:3617`
```cpp
std::optional<glm::vec4> viewportBounds = std::nullopt);
```
The real constructor takes a **mandatory** `const glm::vec4&` (see `Cosmic/src/renderer/RenderPass.h`).
The `std::nullopt` semantics described in §34 do not exist. **Fix:** show the real signature, delete the
`std::nullopt` behavior text. (Rolled into the README plan, [`06-readme-update-plan.md`](06-readme-update-plan.md).)

---

## 2. Verified hardening / robustness gaps (real, but design gaps rather than defects)

### HARD-1 — LayerStack mutation during iteration is unguarded
**Files:** `Cosmic/src/core/Application.cpp:122, 135, 149` (range-for over `m_LayerStack`),
`Cosmic/src/core/LayerStack.cpp`
The engine's contract is that push/pop happens only in the Safe Zone (`Application.cpp:158–221`), and
engine code honors that — but nothing stops client code from calling `PushLayer()` inside `OnUpdate()`,
which invalidates the live iterator (UB). **Fix:** add an `m_Iterating` flag set around the three loops;
`PushLayer/PopLayer/PushOverlay/PopOverlay` assert (or defer to a pending queue applied in the Safe Zone)
when called mid-iteration.

### HARD-2 — `SerialPort::Open()` during an in-flight `BeginOpen()` races on `m_Handle`
**File:** `Cosmic/src/serial/SerialPort.cpp:24–38`
`BeginOpen` guards itself (`if (m_State == Connecting) return;`, line 53), but the synchronous `Open()`
has no such guard: called while a connect worker is inside `DoOpen`, both threads write `m_Handle`.
Not hit by current callers (SerialLink drives one path at a time) but a public-API trap.
**Fix:** at the top of `Open()`: `if (m_State.load() == State::Connecting) return false;`

### HARD-3 — `Window` owns its GL context via raw `new`
**File:** `Cosmic/src/core/Window.cpp:196` (`new OpenGLContext`), delete at `:369`.
Works, but not exception-safe and inconsistent with the engine's `Scope<T>` ownership rule (README §2).
**Fix:** `Scope<OpenGLContext> m_Context = CreateScope<OpenGLContext>(m_Handle);` remove manual delete.

### HARD-4 — Incomplete framebuffer only logs and carries on
**File:** `Cosmic/src/platform/OpenGL/OpenGLFrameBuffer.cpp:94–97`
On `glCheckFramebufferStatus != COMPLETE` it logs and continues; every subsequent frame renders into a
broken FBO. **Fix:** `CS_CORE_ASSERT` in debug + keep the error log in release (and consider logging the
status code).

### HARD-5 — Pause-on-minimize also stalls the Safe Zone
**File:** `Cosmic/src/core/Application.cpp:96–99`
`continue` skips not just the update/render passes but the Safe Zone too, so a pending project
load/return (`m_PendingProjectDLL`, `m_PendingReturnToLauncher`) queued just before minimizing waits
until restore. Minor. **Fix:** restructure so the Safe Zone block runs even when minimized.

### HARD-6 — Reserved `FramebufferSpecification` fields silently do nothing
**File:** `Cosmic/src/graphics/FrameBuffer.h` (`Samples`, `SwapChainTarget`)
Known/documented (IMPROVEMENTS §5.4). Minimum fix: `CS_CORE_WARN` in `FrameBuffer::Create` when
`Samples > 1` or `SwapChainTarget == true`.

### HARD-7 — Async job capture convention has no compile-time enforcement
**File:** `Cosmic/src/jobs/ParallelFor.h`
`ParallelForAsync` requires by-value captures (documented), but a by-reference lambda compiles fine and
dangles. **Fix:** `static_assert(std::is_copy_constructible_v<std::decay_t<Func>>)` plus a doc block;
optionally a `[[nodiscard]]`-style comment at call sites. (Full type-level enforcement isn't practical.)

### HARD-8 — `DataRecorder::GetTotalFrameCount` proxies entity 0 only
**File:** `Cosmic/src/telemetry/DataRecorder.cpp` (~line 140 — re-verify at fix time)
Misleading when entities register at different times. **Fix:** return the max across entities, or rename.

### HARD-9 — `Material::Bind()` does not bind textures (footgun outside the batch renderer)
**File:** `Cosmic/src/graphics/Material.h` (~line 66 — re-verify at fix time)
By design for the batch path; anyone using `Renderer::Submit` with a textured material gets black.
**Fix:** add `BindFull()` (uploads uniforms *and* binds cached textures to slots) and point the header
docs at it.

---

## 3. Infrastructure gaps (no defect, but industry-standard practice missing)

| Gap | Note |
| --- | --- |
| **No tests at all** | No test target anywhere. Highest-leverage starting set: pure-logic units — telemetry binary round-trip (`DataRecorder::Flush` → `DataPlayer::Load`), `Shader` preprocessor paths, `EntityPicker` math, `BufferLayout` offsets/strides. None of these need a GL context or window. |
| **No CI** | No `.github/workflows/`. A single job that configures CMake + builds Release + runs the (new) tests catches MSVC breakage and keeps `package.bat` honest. |
| **No static analysis** | No `.clang-tidy` / `/analyze`. Even a small check set (bugprone-*, concurrency-*) would have flagged several §2 items. |
| **No version identity** | No engine version constant, no `VERSIONINFO` in `Runtime/CosmicApp.rc`, no app manifest (DPI awareness is set programmatically, not declared). Handled in [`07-installer-packaging-plan.md`](07-installer-packaging-plan.md). |
| **Writable-data assumption** | `Log::Init("logs")` (`Application.cpp:41`) and recorder autosave paths are CWD-relative; fine for a dev tree, breaks under `Program Files`. Handled in doc 07 (FileSystem user-data mount). |

---

## 4. Disproved claims — do NOT "fix" these

Recorded so nobody (human or AI) regresses correct code:

1. **`SerialPort::m_Connected` is already `std::atomic<bool>`** (`SerialPort.h:127`), as are `m_State` and `m_Abandon`. No data race.
2. **`SerialPort` has no dangling-`this` in `BeginOpen`** — the destructor calls `Close()`, which joins `m_ConnectThread` then the read thread (`SerialPort.cpp:253–260`). The connect worker can never outlive the object. (The destructor *can block* up to ~20 s on a dead Bluetooth port — acceptable, documented behavior.)
3. **`JobSystem::Shutdown` does reset `m_Initialized`** (`JobSystem.cpp:115`) and joins all workers. Second call is a clean no-op.
4. **`strnlen` in `GetAvailablePorts` is bounded** by `dataSize` ≤ 256 (`SerialPort.cpp:291`). No over-read.
5. **`LayerStack::PopLayer` order is correct** — `OnDetach()` fires before `erase` (`LayerStack.cpp:80–81`).
6. **`TelemetryPanel`'s `ImPlotSpec` usage is valid** — the vendored ImPlot provides it (already recorded in IMPROVEMENTS §7).
7. **Handle teardown in `CloseReadSession` is race-free** — the read thread is joined *before* `m_Handle` is closed (`SerialPort.cpp:229–236`).

---

## 5. Stale internal docs noticed during this audit

- `docs/engine_analysis.md` §6/5.1 says world-space text rendering is impossible — **stale**: `Font` + SDF `Text.glsl` exist now (README §27). Its P1/P2 items are all fixed per IMPROVEMENTS §1–§4; P3-A/P3-E and the DataPlayer item were still live and are re-listed here as BUG-2/3/4.
- README §34 `RenderPass` signature (BUG-5) and the `docs/old/` folder duplication → see [`06-readme-update-plan.md`](06-readme-update-plan.md).

---

## 6. Still-open feature roadmap (carried forward, not bugs)

From IMPROVEMENTS §5, still the right list, in the right order: **asset cache** (§5.1, unlocks the most),
sprite animation (§5.2), shader hot-reload (§5.3), MSAA-or-remove-fields (§5.4). The UAV-sim feature set
lives separately in [`03-simulation-engine-plan.md`](../03-simulation-engine-plan.md) (rewritten 2026-07-01; was `03-uav-sim-engine-features.md`).
