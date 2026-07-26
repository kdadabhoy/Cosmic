# Logging & Diagnostics — Guide

**What this covers:** The two loggers and their macros, where log files land, redirecting the log
directory, mirroring the log into your own UI with a sink, the editor Console panel, the 2D and 3D
renderer statistics counters, the per-pass GPU profiler, and why the assert macros do nothing.
**Source of truth:** `Cosmic/src/core/Log.{h,cpp}`, `core/Core.h`, `utils/FileSystem.h`,
`renderer/Renderer2D.{h,cpp}`, `renderer/Renderer3D.{h,cpp}`, `renderer/RendererAPI.h`,
`renderer/RenderCommand.h`, `renderer/SceneRenderer.cpp`,
`platform/OpenGL/OpenGLRendererAPI.cpp`, `Runtime/CMakeLists.txt`,
`Projects/Starforge/src/panels/{ConsolePanel,ProfilerPanel}.{h,cpp}`,
`Projects/Starforge/src/{StarforgeApp.cpp,EditorContext.h}`
**API Reference:** [../reference/core.md](../reference/core.md) · **How it works:**
[../systems/core-runtime.md](../systems/core-runtime.md)
**Configuration:** both — the only difference is that `Renderer3D`'s counters do not exist in the 2D
engine build (see [../systems/build-2d-3d-split.md](../systems/build-2d-3d-split.md)).

---

## Quick start

```cpp
#include <Cosmic.h>

void MyApp::OnAttach()
{
    CS_INFO("MyApp attached.");                       // client logger — use this in your project
    CS_WARN("Falling back to defaults: {}", path);
    CS_ERROR("Load failed: {} ({} bytes)", name, n);
}
```

That is the whole everyday API. Every message goes to a colored console (Debug builds only) **and**
to a timestamped file under `user://logs/`, flushed immediately, and — if the app runs inside
Starforge — into the editor's **Console** panel with severity and source filters.

To find your log file, read the first line the engine prints; it names its own writable root:

```
[COSMIC] [info] User data root: C:/dev/Cosmic/build/Runtime/Debug
```

---

## `CS_*` versus `CS_CORE_*`

There are exactly **two loggers**, and the only difference is the tag they stamp and who is expected
to use them.

| Macro family | Logger name in output | Who writes it | File it goes to |
| --- | --- | --- | --- |
| `CS_TRACE` … `CS_CRITICAL` | `APP` | **your project**, scripts, plugins | `App_<timestamp>.log` |
| `CS_CORE_TRACE` … `CS_CORE_CRITICAL` | `COSMIC` | engine internals | `Cosmic_<timestamp>.log` |

```cpp
CS_TRACE("Entering state {}", name);      CS_CORE_TRACE(...)
CS_INFO("Spawned at {:.2f}, {:.2f}", x, y);  CS_CORE_INFO(...)
CS_WARN("Shader missing: {}", path);      CS_CORE_WARN(...)
CS_ERROR("Texture load failed: {}", f);   CS_CORE_ERROR(...)
CS_CRITICAL("Out of GPU memory.");        CS_CORE_CRITICAL(...)
```

**Use `CS_*` in your project.** `CS_CORE_*` is not forbidden — both macros resolve through exported
`Log` accessors and work fine from a project DLL — but the split is what makes the editor's Console
source filter work, and what lets a support request say "send me `App_*.log`". Mixing them costs you
that.

Formatting is `{fmt}` via spdlog: `{}` positional, `{0}`/`{1}` indexed, and full specs like
`{:.2f}`, `{:>8}`, `{:#x}`. A malformed format string throws `fmt::format_error` at runtime rather
than failing to compile — the argument count is not checked for you.

**Both loggers are thread-safe** and safe to call from `JobSystem` workers: the sinks are spdlog's
`_mt` variants, and `GetCoreLogger()`/`GetClientLogger()` take a shared read lock. There is one
practical caveat — a log call from a worker takes that lock, so a `CS_TRACE` inside a hot
`ParallelFor` body is a real contention point. Log the summary, not the iteration.

**Everything is flushed on every message.** Both loggers are configured
`set_level(trace)` + `flush_on(trace)`, deliberately, so a crash cannot swallow the line that
explains it. Nothing is filtered out at runtime — there is no log-level control surface, and
`CS_TRACE` in a per-frame path really does write a line per frame. (One measured example: a
homescreen that re-parsed its project list every frame produced a 1.4 MB log in 95 seconds.)

---

## Where the log files go

`Application`'s constructor calls `Log::Init(FileSystem::Resolve("user://logs"))` as its **second**
statement — before the window, the renderer, or anything that can fail. Two files are created per
run, named from the wall clock at boot:

```
<user data root>/logs/Cosmic_2026-07-25_14-08-31.log     ← the COSMIC logger
<user data root>/logs/App_2026-07-25_14-08-31.log        ← the APP logger
```

Both are opened with truncate, and there is **no rotation and no cleanup** — one pair per launch,
accumulating forever. A long-lived install will grow a large `logs/` directory; pruning it is the
app's job, not the engine's.

Where `user://` points is decided once, at first use, and is covered in
[`getting-started.md`](getting-started.md#where-files-live-the-three-vfs-protocols). In short: a
writable exe directory keeps data next to the app; an installed app under *Program Files* gets
`%LOCALAPPDATA%/<AppName>/`. The engine prints the resolved root twice at boot (once from the
constructor, once from `Main.cpp`) precisely so this never has to be guessed.

### The console sink only exists in Debug

`Runtime/CMakeLists.txt` links both host executables with `/SUBSYSTEM:WINDOWS` **in Release**
(plus `/ENTRY:mainCRTStartup`, so `int main()` still works). Debug keeps the default console
subsystem.

The practical consequence, which surprises people the first time they ship:

| Configuration | Console window | Colored console sink | Log file | Extra sinks |
| --- | --- | --- | --- | --- |
| Debug | yes | visible | written | active |
| Release | none | writes into the void | written | active |

So **in Release the log file and the in-app Console panel are your only output.** If a bug
reproduces only in Release, do not go looking for a terminal — go to `user://logs/`.

---

## Redirect the log directory

```cpp
Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("user://logs"));
```

`SetLogDirectory` re-enters `Init`, which means it **creates a fresh timestamped pair of files** in
the new directory and swaps the console + file children of each logger's owning `dist_sink`. The
loggers themselves are never replaced, so:

- any extra sink registered with `AddSink` **survives** the redirect (only `RemoveSink` drops it);
- lines already written to the old files stay there — this is a redirect, not a move;
- you get an extra pair of files every time you call it.

That last point is why calling this per project mount is a real (if small) cost: entering and
leaving one project leaves four log files behind.

> **The shipped examples get this wrong — do not copy them.** `TemplateProject`, `Frontier` and
> `SF_Telem` all redirect to `FileSystem::Resolve("project://logs")` on attach, and to the bare
> relative path `"logs"` on detach. Both are wrong for a shipped app: `project://` is a **read-only
> content root** (under *Program Files* once installed), and a bare `"logs"` resolves against the
> working directory, which `Main.cpp` has set to the exe directory — also read-only when installed.
> `StarforgeApp` is the one that does it correctly:
> `Log::SetLogDirectory(FileSystem::Resolve("user://logs"))`. If you want a project-scoped log
> folder, put it under `user://`: `Resolve("user://logs/MyApp")`.

Honestly, the simplest correct answer is **do not call `SetLogDirectory` at all.** The engine already
initialized logging to `user://logs` before your code ran.

---

## Mirror the log into your own UI

`Log::AddSink` attaches an extra spdlog sink to **both** loggers at once. `CallbackSink` — declared
in `Log.h`, header-only so a client DLL can instantiate it — hands every formatted line to a
`std::function<void(spdlog::level::level_enum, const std::string&)>`.

The callback fires from **whatever thread logged**, so it must be cheap and reentrant. The
established pattern is enqueue-under-mutex, drain on the UI thread:

```cpp
// OnAttach
m_LogSink = std::make_shared<Cosmic::CallbackSink>(
    [this](spdlog::level::level_enum lvl, const std::string& line)
    {
        Severity sev = Severity::Info;
        if (lvl == spdlog::level::warn)     sev = Severity::Warn;
        else if (lvl >= spdlog::level::err) sev = Severity::Error;

        std::lock_guard<std::mutex> lk(m_LogQueueMutex);   // callback thread
        m_LogQueue.emplace_back(sev, line);
    });
m_LogSink->set_pattern("[%n] %v");        // "[COSMIC] message" — no timestamp; the panel adds one
Cosmic::Log::AddSink(m_LogSink);

// OnImGuiRender, on the UI thread
void MyApp::DrainLogQueue()
{
    std::vector<std::pair<Severity, std::string>> pending;
    {
        std::lock_guard<std::mutex> lk(m_LogQueueMutex);
        if (m_LogQueue.empty()) return;
        pending.swap(m_LogQueue);
    }
    for (auto& [sev, text] : pending)
        m_Lines.push_back({ sev, text, /*fromEngine=*/text.rfind("[COSMIC]", 0) == 0 });
}

// OnDetach — MANDATORY
Cosmic::Log::RemoveSink(m_LogSink);
m_LogSink.reset();
```

Three details that matter:

- **`RemoveSink` in `OnDetach` is not optional.** `Log`'s static sink list lives in `Cosmic.dll` and
  outlives your DLL. Leave the sink registered and the next log line calls a lambda whose code has
  been unmapped by `FreeLibrary`.
- **Set your own pattern.** The default file pattern includes a full timestamp; a panel usually wants
  `"[%n] %v"` and its own time column. `%n` is the logger name, which is how you recover
  engine-versus-app after the fact.
- **`AddSink` deduplicates** by pointer and is safe to call twice; the sink is held by each logger's
  `dist_sink`, which is why it survives `SetLogDirectory`.

---

## The editor Console panel

When your project runs inside Starforge, `CS_*` and `CS_CORE_*` both land in the **Console** panel,
via exactly the `CallbackSink` pattern above. It gives you:

- **Severity chips** — Info / Warn / Error, independently toggled. Severity is derived from the
  spdlog level: `warn` → Warn, anything `err` or worse → Error, everything else → Info. Trace,
  debug and info are therefore indistinguishable in the panel; the log *file* keeps the real level.
- **Source chips** — Engine / Editor / Game. The mapping is textual: a line beginning `[COSMIC]` is
  **Engine**, anything else from the sink is **Game**, and the editor's own messages are **Editor**.
  So your project's `CS_INFO` shows up as **Game** — including scripts during a Play session.
- **A substring filter** (case-insensitive) and **Auto-scroll**.
- **Clear**, and a right-click **Copy visible** that copies exactly what the filters left on screen.
- A **4000-line ring buffer**, trimmed 800 at a time, so a long session stays responsive. Lines
  scrolled off are gone from the panel but still in the file.

Bind it like any other panel; the window name is `"Console"`.

---

## Read renderer statistics

Both renderers expose a `Statistics` struct, `GetStats()` and `ResetStats()`. They look symmetric.
**They do not behave symmetrically**, and both halves have cost people real debugging time.

### 2D counters are opt-in

```cpp
// once, at startup
Cosmic::Renderer2D::SetStatsStatus(true);

// then, every frame BEFORE your first BeginScene
Cosmic::Renderer2D::ResetStats();

// ... draw ...

auto s = Cosmic::Renderer2D::GetStats();
ImGui::Text("2D: %u draws, %u quads, %u circles, %u lines",
            s.DrawCalls, s.QuadCount, s.CircleCount, s.LineCount);
ImGui::Text("    %u verts, %u indices", s.GetTotalVertexCount(), s.GetTotalIndexCount());
```

> ### ⚠️ `Renderer2D::StatsEnabled` defaults to `false` and nothing arms it
>
> Every counter increment in `Renderer2D.cpp` is guarded by `if (s_Data.StatsEnabled)`, the flag
> initializes to `false`, and **no engine code ever calls `SetStatsStatus`**. Across the entire
> repository there is exactly one caller: `StarforgeApp.cpp`, inside a `#ifdef COSMIC_2D_ONLY`
> block, arming it once behind a `static bool`.
>
> If you read `Renderer2D::GetStats()` without calling `SetStatsStatus(true)` first, **every field is
> zero, forever, with no warning.** This shipped as a bug: the editor's 2D stats chip read a flat
> zero until the flag was armed explicitly. A plausible-looking "0 draw calls" is the symptom.

`GetTotalIndexCount()` counts quads and circles at 6 indices each and deliberately ignores lines —
lines are drawn non-indexed via `glDrawArrays`, so they contribute vertices but no indices. That is
correct, not a bug.

### 3D counters are always on and never reset

```cpp
// every frame, BEFORE the first submission
Cosmic::Renderer3D::ResetStats();

// ... SceneRenderer::Render / DrawMesh / DrawModel ...

auto s = Cosmic::Renderer3D::GetStats();
ImGui::Text("3D: %u draws | submitted %u, culled %u, drawn %u",
            s.DrawCalls, s.MeshesSubmitted, s.MeshesCulled, s.MeshesDrawn);
ImGui::Text("    auto-inst %u batches / %u meshes | explicit %u draws / %u instances",
            s.AutoInstanceBatches, s.AutoInstancedMeshes,
            s.ExplicitInstanceDraws, s.ExplicitInstances);
```

> ### ⚠️ `Renderer3D` has no enable flag, but nothing resets it either
>
> The 3D counters increment unconditionally — there is no `SetStatsStatus` — and **no engine code
> calls `Renderer3D::ResetStats()`**. The only callers in the tree are `Engine3DDemo` and `Frontier`,
> each at the top of its own `OnUpdate`.
>
> So if you read `GetStats()` without resetting, the numbers are **cumulative lifetime totals**, not
> this frame's cost. They look plausible for the first second and then drift steadily upward, which
> reads as a leak or a runaway draw count when it is neither. Starforge's own 3D stats chip still has
> this quirk; it is documented and deliberately left alone rather than silently changed.
>
> **The rule for both renderers: reset at the top of your frame, read at the bottom.** If a counter
> only ever grows, you forgot the reset. If a counter is always zero and it is 2D, you forgot
> `SetStatsStatus(true)`.

`MeshesCulled` is the frustum-culling evidence and `AutoInstanceBatches` the auto-instancing
evidence — the two numbers to watch when a 3D scene is slower than it looks. Explicit
`DrawMeshInstanced` calls are **never engine-culled**; you cull those yourself, which is why they get
their own counters. Details in [`rendering-3d.md`](rendering-3d.md).

In a 2D engine build `Renderer3D` does not exist at all — fence any stats readout with
`#ifndef COSMIC_2D_ONLY`.

---

## Profile the GPU per pass

The engine records **GPU timer zones** around each `SceneRenderer` pass and hands you the resolved
milliseconds. Nothing to enable, no cost to reading it:

```cpp
for (const Cosmic::GpuZoneResult& z : Cosmic::RenderCommand::GetGpuZoneResults())
{
    ImGui::Indent(z.Depth * 12.0f);
    ImGui::Text("%-16s %6.2f ms", z.Name.c_str(), z.Milliseconds);
    ImGui::Unindent(z.Depth * 12.0f);
}
ImGui::Text("CPU frame: %.2f ms", ImGui::GetIO().DeltaTime * 1000.0f);
```

`GpuZoneResult` is `{ std::string Name; float Milliseconds; uint32_t Depth; }`, where `Depth` is the
nesting level (0 = top-level) so a HUD can indent.

The zones the engine emits, in `SceneRenderer::Render` order:

| Zone | 3D build | 2D build |
| --- | --- | --- |
| `Shadow` | yes | — |
| `Coverage` | yes | — |
| `Reflection` | yes | — |
| `Opaque` | yes | yes (an HDR clear) |
| `Transparents` | yes | yes (the sprites) |
| `Post+Composite` | yes | yes |
| `Outline` | when the outline pass runs | when it runs |

Two things follow from *where* the instrumentation lives:

- **You get zones only if you render through `SceneRenderer`.** An app that draws with `Renderer2D`
  directly and never calls `SceneRenderer::Render` produces an empty result list — which is a valid,
  empty table, not an error.
- **The numbers respond to feature toggles.** Turning shadows off shrinks or zeroes the `Shadow`
  row, which is what makes the panel useful for deciding what to cut.

`SceneRenderer::Render` calls `RenderCommand::GpuFrameMark()` at the top of every frame. That closes
the frame just recorded and resolves the **oldest ready** frame. Consequences worth knowing:

- **Results lag by a frame or three.** The implementation uses `GL_TIMESTAMP` queries (chosen over
  `GL_TIME_ELAPSED` so zones can nest) and only reads a frame back once
  `GL_QUERY_RESULT_AVAILABLE` says so, so **the GPU is never stalled** to satisfy the profiler. A
  three-frame ring absorbs the latency; on overflow the oldest is force-dropped and the last good
  results stay on screen. You are always looking at a recent frame, never exactly the current one.
- **`GetGpuZoneResults()` returns the last resolved frame**, unchanged between resolutions. Two reads
  in one frame give identical data; do not average consecutive reads and expect independent samples.
- An unbalanced `EndGpuZone` cannot corrupt the next frame — `GpuFrameMark` clears the zone stack.

You can add your own zones around your own passes with the same verbs:

```cpp
Cosmic::RenderCommand::BeginGpuZone("MyOverlayPass");
// ... draws ...
Cosmic::RenderCommand::EndGpuZone();     // always pair; nesting is fine
```

Zones only resolve if something calls `GpuFrameMark` once per frame. If you do not use
`SceneRenderer`, call it yourself at your frame boundary.

Starforge's **Profiler** panel combines all of this — the GPU zone table with bars scaled to the
frame total, the renderer queue counters, and a rolling 120-sample CPU/GPU sparkline. It costs
nothing when closed, because the timer queries live in the `SceneRenderer` and the panel only reads
them.

---

## Asserts are compiled out in every configuration

`Core.h` defines `CS_ASSERT`, `GLCORE_ASSERT` and `CS_CORE_ASSERT` behind:

```cpp
#if defined(GLCORE_DEBUG) || defined(CS_DEBUG)
#define CS_ENABLE_ASSERTS
#endif
```

**Neither `GLCORE_DEBUG` nor `CS_DEBUG` is defined anywhere** — not by the root `CMakeLists.txt`, not
by `Cosmic/CMakeLists.txt`, not by any target's `target_compile_definitions`, not in any source file.
The build defines `COSMIC_BUILD_DLL`, `COSMIC_2D_ONLY`, `COSMIC_DIST` (Release), `COSMIC_WITH_JOLT`,
`COSMIC_WITH_ASSIMP`, `COSMIC_STARTUP_PROJECT`, `WIN32_LEAN_AND_MEAN` and `NOMINMAX` — and that is
all.

So every `CS_ASSERT(...)` in the engine expands to nothing, in **Debug as well as Release**. This is
not a documentation quibble; it changes how failures present:

- `LayerStack::PushLayer/PopLayer/PushOverlay/PopOverlay` guard against mid-iteration mutation with
  `CS_CORE_ASSERT(!m_Iterating, …)`. That guard **does not fire**. Pushing a layer from inside
  `OnUpdate` is silent undefined behaviour. (See
  [`project-anatomy.md`](project-anatomy.md#the-safe-zone).)
- `LayerStack::Clear()`'s "all layers must be detached first" precondition is likewise unchecked.

Do not add `CS_ASSERT` to your own project code expecting a debug break. Use `CS_ERROR` — which
always fires, always reaches the file and the Console panel — or ImGui's `IM_ASSERT`, which *is*
live and does `abort()` in Debug (an unbalanced ImGui style stack is a real crash you will meet).

---

## Common patterns

**One reset point per frame, at the top.** Both stats resets and, if you own the frame,
`GpuFrameMark`. Putting them anywhere else makes the numbers mean something you did not intend.

**`CS_*` for your code, always.** It is what makes the Console panel's Game filter and the
`App_*.log` split useful. The engine keeps `COSMIC` clean for the same reason.

**Log the resolved path, not the VFS path.** `CS_INFO("Loading {}", FileSystem::Resolve(p))` turns a
"file not found" into an answer. The engine does this at boot for `user://` for exactly this reason.

**Register a sink, drain on the UI thread, remove it in `OnDetach`.** Every in-app console in this
tree follows that shape; deviating from it means either a cross-thread ImGui call or a call into
unmapped code.

**Reach for the file first when a bug is Release-only.** There is no console in Release, so the
symptom "no output at all" usually means "you were reading the wrong place."

---

## Pitfalls

**"`Renderer2D::GetStats()` returns all zeros."** `StatsEnabled` defaults to `false` and nothing
arms it. Call `Renderer2D::SetStatsStatus(true)` once at startup.

**"My draw-call count only ever goes up."** You never called `ResetStats()`. Nothing in the engine
resets either renderer's counters; without a reset you are reading lifetime totals.

**"No log output at all in Release."** Release links `/SUBSYSTEM:WINDOWS` — there is no console. The
file under `user://logs/` has everything.

**"My log file is empty / was never created."** Almost always a redirect to a read-only directory.
`SetLogDirectory("project://logs")` or `SetLogDirectory("logs")` fails once the app is installed to
*Program Files*. Route it through `user://`, or do not redirect at all.

**"Log lines stop appearing after I return to the Launcher."** A `SetLogDirectory` in your
`OnDetach` pointed the loggers somewhere else and opened a fresh pair of files. The lines are in the
new files.

**"The app crashes on project unload, in a lambda."** A `CallbackSink` was registered and never
removed. `Log`'s sink list lives in `Cosmic.dll` and outlives your DLL. `RemoveSink` in `OnDetach`.

**"The GPU profiler table is empty."** Either nothing called `SceneRenderer::Render` (so no zones
were recorded and no `GpuFrameMark` ran), or the first frames have not resolved yet — the ring holds
up to three frames before the first read lands.

**"GPU numbers don't match a frame I just changed."** They lag by one to three frames by design,
because the profiler never stalls the GPU on a query. Hold the change for a second before reading.

**"My `CS_ASSERT` never fires."** It cannot. `CS_ENABLE_ASSERTS` is never defined in any
configuration. Use `CS_ERROR`.

**"Logging from a `ParallelFor` body tanked my frame time."** Every log call takes a shared lock on
the logger and flushes to disk. Aggregate on the worker, log once on the main thread.

**"Trace spam filled a gigabyte."** There is no runtime level filter — `set_level(trace)` and
`flush_on(trace)` are unconditional. A `CS_TRACE` on a per-frame path writes ~60 lines a second and
flushes each one. Gate it yourself.

---

## See also

- [`project-anatomy.md`](project-anatomy.md) — the `Application` lifecycle (`Log::Init` runs second),
  the Safe Zone, and the `OnDetach` teardown contract that `RemoveSink` belongs to.
- [`rendering-2d.md`](rendering-2d.md) — what the 2D counters are counting: batching, every batch
  limit, and what forces a flush.
- [`rendering-3d.md`](rendering-3d.md) — culling, sorting and auto-instancing, which is what
  `MeshesCulled` and `AutoInstanceBatches` measure.
- [`lighting-and-environment.md`](lighting-and-environment.md) — the `SceneRenderer` pass graph the
  GPU zones are named after.
- [`editor-ui-and-theming.md`](editor-ui-and-theming.md) — docking the Console and Profiler panels.
- [`assets-and-vfs.md`](assets-and-vfs.md) — `user://` versus `project://` in full, and why writes
  belong in the former.
- [`jobs-and-parallelism.md`](jobs-and-parallelism.md) — `JobSystem` statistics, and thread-safety
  rules for logging from workers.
- [`../reference/core.md`](../reference/core.md) — formal signatures for `Log`, `CallbackSink` and
  the macro families.
- [`../systems/core-runtime.md`](../systems/core-runtime.md) ·
  [`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md)
