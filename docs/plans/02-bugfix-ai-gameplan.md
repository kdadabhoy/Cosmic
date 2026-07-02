# Bugfix Gameplan — AI Work Orders

> **Purpose:** Ready-to-paste prompts for driving a smaller/cheaper AI model through the fixes in
> [`01-bug-audit.md`](01-bug-audit.md). Each work order is self-contained: exact file, exact change,
> acceptance check. Paste **one work order per session/prompt** — small models drift when given
> several at once.
>
> **Verified 2026-07-01.** If much time has passed, tell the model to re-verify the quoted line still
> exists before editing (instruction is built into the template).

---

## How to run these

1. Do them in the listed order (independent, but ordered cheapest-first so early wins build confidence).
2. One work order per prompt. Start a fresh conversation per order if the model gets confused.
3. After each order: `build.bat` from the repo root must succeed (you run builds yourself — don't let
   the AI run them unless you want it to).
4. Commit per work order: `fix: <work order title>` on a branch (e.g. `bugfix-pass-2026-07`), PR to `main`.

**Prompt preamble to paste before any work order** (sets guardrails once):

```
You are editing the Cosmic engine (C++20, OpenGL, Windows, MSVC). Rules:
- Make ONLY the change described. Do not refactor, rename, reformat, or "improve" nearby code.
- Before editing, open the file and confirm the quoted code exists; if it moved, find it by content.
- Match the file's existing style (tabs/braces/comment tone).
- Do not touch any file not named in the task.
- These claims were checked and are FALSE — do not act on them if you "notice" them:
  SerialPort::m_Connected is already atomic; JobSystem::Shutdown already resets m_Initialized;
  SerialPort's destructor already joins its threads; TelemetryPanel's ImPlotSpec is valid API.
```

---

## WO-1 — Fix `SetData` bytes-per-pixel for R8/RG8 textures

```
File: Cosmic/src/platform/OpenGL/OpenGLTexture.cpp, function OpenGLTexture::SetData (~line 162).
Current first line of the function body:
    uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
The constructor supports GL_RGBA, GL_RGB, GL_RG, GL_RED (see lines ~75-94), so this is wrong for
1- and 2-channel textures. Replace that line with a mapping covering all four:
    uint32_t bpp = 4;
    switch (m_DataFormat)
    {
        case GL_RGBA: bpp = 4; break;
        case GL_RGB:  bpp = 3; break;
        case GL_RG:   bpp = 2; break;
        case GL_RED:  bpp = 1; break;
        default:
            CS_CORE_ERROR("OpenGLTexture::SetData: unsupported data format {0:x}", m_DataFormat);
            return;
    }
Also add a guard at the very top of SetData:
    if (m_RendererID == 0) { CS_CORE_WARN("OpenGLTexture::SetData called on a failed/empty texture ({0}).", m_Path); return; }
Acceptance: compiles; an R8 texture of WxH accepts SetData(data, W*H*1) without the size-mismatch error.
```

## WO-2 — Telemetry version docstrings + DataPlayer directory fallback

```
Files: Cosmic/src/telemetry/DataRecorder.h, DataPlayer.h, DataPlayer.cpp. The binary format is v1
(DataRecorder.h ~line 40 writes version = 1). Three changes:

1. DataRecorder.h, Flush() docstring (~line 165): change "v3 binary format" to "v1 binary format".
2. DataPlayer.h, Load() docstring (~line 64): change
   "loads scene.bin (v2/v3) if present; otherwise loads all individual .bin files"
   to "loads scene.bin (v1) if present; otherwise loads every *.bin file in the directory".
3. DataPlayer.cpp, Load(), directory branch (~lines 32-43): it currently only searches for a file
   named scene.bin. After that loop, add the promised fallback: if no entities were loaded
   (m_Entities.empty() or equivalent success flag is false), iterate the directory again and call
   LoadBinaryFile() on every regular file whose extension() == ".bin". Keep the existing warning
   (~line 56) for the case where the fallback also finds nothing.
Acceptance: compiles; loading a directory containing foo.bin (no scene.bin) loads foo.bin;
loading a directory with scene.bin behaves exactly as before.
```

## WO-3 — Restore the default circle shader after instanced circles

```
File: Cosmic/src/renderer/Renderer2D.cpp, end of DrawInstancedCircles (~line 1354).
Current: s_Data.ActiveCircleShader = nullptr;
Change to: s_Data.ActiveCircleShader = s_Data.DefaultCircleShader;
(Reference: the same restore already appears at lines ~556 and ~610 with the comment
"Prevent custom shader leakage" — match them.)
Acceptance: compiles; calling DrawCircle() after DrawInstancedCircles() in one frame no longer
triggers a FlushAndReset on an empty circle batch.
```

## WO-4 — Rotation-aware entity picking

```
File: Cosmic/src/telemetry/EntityPicker.h, Pick() hit test (~lines 100-110).
Current test is an axis-aligned box:
    float halfW = transform.Scale.x * 0.5f;
    float halfH = transform.Scale.y * 0.5f;
    bool hitX = glm::abs(worldPos.x - transform.Position.x) <= halfW;
    ...
Make it respect the entity's 2D rotation. TransformComponent::Rotation is stored in DEGREES
(GetTransform() converts with glm::radians — do the same). Replace the test with: rotate the query
point into the entity's local frame by -Rotation.z, then do the same axis-aligned test:
    const float ang  = glm::radians(-transform.Rotation.z);
    const float c = std::cos(ang), s = std::sin(ang);
    const float dx = worldPos.x - transform.Position.x;
    const float dy = worldPos.y - transform.Position.y;
    const float lx = c * dx - s * dy;
    const float ly = s * dx + c * dy;
    bool hit = std::abs(lx) <= halfW && std::abs(ly) <= halfH;
Keep the existing predicate/SelectableComponent logic untouched. Include <cmath> if not present.
Acceptance: compiles; a 45-degree-rotated quad is hit when clicking its rotated corners and missed
just outside its rotated edges. Header-only file — no .cpp change.
```

## WO-5 — Guard `SerialPort::Open` against an in-flight async connect

```
File: Cosmic/src/serial/SerialPort.cpp, SerialPort::Open (~line 24).
BeginOpen() refuses to stack connects (line ~53: if (m_State.load() == State::Connecting) return;)
but the synchronous Open() has no such guard, so calling Open() while a BeginOpen worker is inside
DoOpen races on m_Handle. Add as the FIRST statement of Open():
    if (m_State.load() == State::Connecting)
    {
        CS_CORE_WARN("SerialPort::Open: an asynchronous connect is already in flight — ignored.");
        return false;
    }
Acceptance: compiles; behavior unchanged for normal use.
```

## WO-6 — RAII for the window's GL context

```
File: Cosmic/src/core/Window.cpp (+ its header Cosmic/src/core/Window.h).
Line ~196: m_Context = new OpenGLContext(m_Handle); and manual "delete m_Context; m_Context = nullptr;"
at ~lines 369-370. Convert the member to the engine's Scope<> alias (std::unique_ptr):
- In Window.h: change the member declaration (currently a raw OpenGLContext* or GraphicsContext*)
  to Scope<OpenGLContext> m_Context; keep the name.
- In Window.cpp ~196: m_Context = CreateScope<OpenGLContext>(m_Handle);
- Remove the delete/nullptr pair in the destructor (~369-370). If the destructor relies on
  destruction order (context deleted after glfwDestroyWindow or before), preserve the current order
  by calling m_Context.reset() at the same spot instead.
- Update any m_Context-> uses if the type name changes (SwapBuffers at ~line 395 stays the same).
Acceptance: compiles; app launches and renders; closing the app is clean (no crash in Window dtor).
```

## WO-7 — Assert on incomplete framebuffer

```
File: Cosmic/src/platform/OpenGL/OpenGLFrameBuffer.cpp (~lines 93-97).
Current: logs "Framebuffer is incomplete!" and continues.
Change to log the actual status code and assert in debug:
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        CS_CORE_ERROR("Framebuffer is incomplete! glCheckFramebufferStatus = {0:x}", status);
        CS_CORE_ASSERT(false, "Framebuffer incomplete — see error log.");
    }
Note this block appears in the (re)build path that runs on every Resize — keep it inside that function.
Acceptance: compiles; normal run unaffected (framebuffers are complete today).
```

## WO-8 — LayerStack: forbid mutation during iteration (debug guard)

```
Files: Cosmic/src/core/LayerStack.h, LayerStack.cpp, Cosmic/src/core/Application.cpp.
Problem: Application::Run() range-fors over m_LayerStack (Application.cpp ~lines 122, 135, 149) and
Application::OnEvent (~line 312); a client calling PushLayer/PopLayer during those loops invalidates
the iterator (UB). The engine already defers its own transitions to the Safe Zone. Enforce it:
1. LayerStack.h: add private "bool m_Iterating = false;" and public "void SetIterating(bool v)"
   (or make Application a friend — prefer the simple setter, documented "engine internal").
2. LayerStack.cpp: at the top of PushLayer, PushOverlay, PopLayer, PopOverlay add:
     CS_CORE_ASSERT(!m_Iterating, "LayerStack mutated during iteration - defer to the Safe Zone (see README section 3).");
3. Application.cpp: wrap each of the four iteration sites with
     m_LayerStack.SetIterating(true);  ... loop ...  m_LayerStack.SetIterating(false);
   For OnEvent, set/clear around the rbegin/rend loop. Ensure the flag is cleared even on early
   break (the loops have no returns inside; a simple set/clear pair is fine).
Acceptance: compiles; app runs normally; a deliberate PushLayer from inside a layer's OnUpdate fires
the assert in a debug build.
```

## WO-9 — Warn on reserved framebuffer spec fields

```
File: wherever FrameBuffer::Create is implemented (Cosmic/src/graphics/FrameBuffer.h/.cpp or the
OpenGL platform file — search for "FrameBuffer::Create"). The spec fields Samples and SwapChainTarget
are reserved/unimplemented. In Create(), before constructing the FBO, add:
    if (spec.Samples > 1)      CS_CORE_WARN("FramebufferSpecification::Samples is reserved - MSAA is not implemented; rendering single-sampled.");
    if (spec.SwapChainTarget)  CS_CORE_WARN("FramebufferSpecification::SwapChainTarget is reserved and has no effect.");
Acceptance: compiles; default-spec creation logs nothing.
```

## WO-10 — `DataRecorder::GetTotalFrameCount` honest value

```
File: Cosmic/src/telemetry/DataRecorder.cpp, GetTotalFrameCount (~line 140 — verify by searching the
function name). It currently returns entity 0's timestamp count. Change it to return the MAXIMUM
timestamps.size() across all entries in m_Records, taking each record's mutex in turn (same locking
style as the current body). Update the function's header docstring in DataRecorder.h to say
"maximum frame count across all registered entities".
Acceptance: compiles; with two entities recorded at different rates the function returns the larger count.
```

## WO-11 — `Material::BindFull()` for non-batch use

```
File: Cosmic/src/graphics/Material.h (and .cpp if the class is split). Material::Bind() uploads
scalar/vector uniforms but intentionally does NOT bind textures (batch renderer manages slots).
Anyone using the low-level Renderer::Submit path with a textured material silently gets black.
Add a method BindFull():
- calls Bind() first;
- then iterates the material's cached texture uniforms (the same storage GetTexture() reads) and for
  each texture entry: texture->Bind(slot) and shader->SetInt(name, slot), assigning slots 0,1,2,...
  in iteration order.
Document on Bind(): "Does not bind textures — the batch renderer owns slot assignment. For manual /
Renderer::Submit rendering use BindFull()." Do not change Bind() behavior.
Acceptance: compiles; existing rendering unchanged.
```

## WO-12 — Compile-time nudge for async ParallelFor captures

```
File: Cosmic/src/jobs/ParallelFor.h. The Async variants capture func by value (correct); a caller
whose lambda captures locals BY REFERENCE gets dangling references with no warning. Inside each of
the three Async templates (ParallelForAsync, ParallelForEachAsync, ParallelForEachIndexedAsync) add:
    static_assert(std::is_copy_constructible_v<std::decay_t<Func>>,
        "Async ParallelFor requires a copyable functor; it is stored by value. "
        "Capture by value ([=] or explicit copies) - by-reference captures dangle.");
(where Func is that template's functor parameter name — adjust to the actual names). Include
<type_traits> if missing. This is a nudge, not a proof — keep the existing header comment too.
Acceptance: compiles (all current callers are copyable).
```

## WO-13 — Safe Zone runs while minimized

```
File: Cosmic/src/core/Application.cpp, Run() (~lines 96-99).
Current: if (m_Minimized && m_PauseOnMinimize) { continue; } — this also skips the Safe Zone at the
bottom of the loop, so a pending project transition queued right before minimizing stalls until restore.
Restructure Run() so the minimized check skips ONLY passes 1A, 1B and 2 (fixed update, variable
update, ImGui render + SwapBuffers), and the Safe Zone block (everything from the
"THE SAFE ZONE" banner comment to the end of the while body) always executes:
    const bool skipPasses = (m_Minimized && m_PauseOnMinimize);
    if (!skipPasses) { ...passes 1A/1B/2 + SwapBuffers... }
    ...safe zone (unconditional)...
Do not change pass ordering or the accumulator logic — just move it inside the conditional.
Acceptance: compiles; app runs; minimize/restore behaves as before.
```

---

## Batch B — infrastructure (bigger orders; do after WO-1…13)

### WO-14 — Minimal unit test target
```
Create a new CMake target CosmicTests under a new top-level tests/ directory, using doctest
(vendor the single header into Cosmic/dependencies/doctest/doctest.h). It must NOT create a window
or GL context. Wire it into the root CMakeLists.txt behind option COSMIC_BUILD_TESTS (default ON for
Debug). First tests:
1. Telemetry round-trip: DataRecorder — Register 2 entities x 3 channels, Record ~100 frames,
   Flush to a temp dir; DataPlayer::Load the result; assert entity/channel names, frame counts, and
   a few interpolated SampleAt values match what was recorded.
2. EntityPicker math: ScreenToWorld + rotated-pick cases (after WO-4).
3. BufferLayout: element offsets/strides for a mixed layout (Float3, Float4, Int).
Add tests/README.md explaining how to run (ctest or run the exe from build/).
Acceptance: cmake configure + build succeeds; test exe runs green from the build tree.
```

### WO-15 — CI workflow
```
Create .github/workflows/ci.yml: on push/PR to main; windows-latest; steps: checkout,
configure CMake (-A x64), build Release, build and run CosmicTests. Cache the build directory
(actions/cache keyed on CMakeLists hashes) to keep runtimes sane. No packaging/signing in CI yet.
Acceptance: workflow file is valid YAML; runs green on GitHub.
```

### WO-16 — .clang-tidy baseline
```
Add a .clang-tidy at repo root enabling: bugprone-*, performance-*, concurrency-*, modernize-use-nullptr,
readability-braces-around-statements; disable noisy ones (modernize-use-trailing-return-type,
readability-magic-numbers). Do NOT fix findings in this order — config only, plus a short
docs/engineering-notes/static-analysis.md on how to run it with CMake (CMAKE_CXX_CLANG_TIDY).
Acceptance: file present; documented.
```

---

## Suggested commit/PR grouping

| PR | Contains | Risk |
| --- | --- | --- |
| `fix/renderer-and-telemetry` | WO-1, WO-2, WO-3, WO-10 | Low |
| `fix/picking` | WO-4 | Low |
| `fix/hardening` | WO-5, WO-6, WO-7, WO-9, WO-12 | Low-medium (WO-6 touches window teardown — test app close) |
| `fix/layerstack-guard` | WO-8, WO-13 | Medium (touches the main loop — test launcher→project→launcher, minimize/restore) |
| `infra/tests-ci` | WO-14, WO-15, WO-16 | Isolated |
