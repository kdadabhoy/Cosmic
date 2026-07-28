# API Reference — Core Runtime

> **STATUS: WRITTEN** — work order **D6** (2026-07-26) in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/core/Core.h`, `core/Application.h`, `core/Layer.h`,
`core/LayerStack.h`, `core/Timestep.h`, `core/Log.h`, `core/UUID.h`, `core/CommandStack.h`,
`core/Version.h`, `core/Window.h` *(client-reachable via `Application::GetWindow()`)*,
`layers/PlayerLayer.h`, plus the plugin-export boundary in `Cosmic/src/Cosmic.h`
(`HostContext`, `CreatePluginLayer`, `InitializePluginContexts`).

**Read first — the guide owns the task half, this chapter is the per-call lookup behind it:**

| Guide chapter | Owns |
| --- | --- |
| [`../guide/project-anatomy.md`](../guide/project-anatomy.md) | the plugin-DLL model, construction/shutdown order in prose, writing a layer, the Safe Zone, `Ref`/`Scope` ownership, the composite-layer pattern |
| [`../guide/time-and-ticks.md`](../guide/time-and-ticks.md) | the four clocks, fixed-vs-variable, the pause-vs-`TimeScale` narrative, per-layer local time |
| [`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md) | the three rectangles, fullscreen workflow, custom title bars, high-DPI, viewport space |
| [`../guide/logging-and-diagnostics.md`](../guide/logging-and-diagnostics.md) | `CS_*` vs `CS_CORE_*`, where the files land, mirroring the log into a panel |
| [`../guide/getting-started.md`](../guide/getting-started.md) | the first project, end to end |

Systems explainers: [core-runtime](../systems/core-runtime.md) and
[windowing](../systems/windowing.md) *(both skeletons — D26)*. Design specs this chapter summarises
rather than restates: [`../design/frame-lifecycle.md`](../design/frame-lifecycle.md),
[`../design/responsive-rendering-and-pause.md`](../design/responsive-rendering-and-pause.md).

> **Scope boundary.** `WorkspaceLayer` — the editor shell that `Application::GetWorkspaceLayer()`
> hands you — is documented in [ui.md](ui.md#workspacelayer), not here. This chapter documents only
> `Application`'s *forwarding* members (`GetViewportPos`, `GetViewportSize`) and the fact that a
> plugin layer is hosted by it rather than pushed on the `LayerStack`. `ImGuiLayer` likewise lives
> in [ui.md](ui.md#imguilayer). `Input` and the `Event` hierarchy are
> [events-input.md](events-input.md).

---

## Contents

- [Configuration](#configuration) — both builds, no exceptions
- [Linkage: what actually links from a project DLL](#linkage-what-actually-links-from-a-project-dll)
- [**Three source comments in this scope are wrong**](#three-source-comments-in-this-scope-are-wrong) — read before trusting a docstring
- [`core/Core.h` — macros and smart-pointer aliases](#corecoreh)
- [`core/Version.h` — the version macros](#coreversionh)
- [`Timestep`](#timestep)
- [`Application`](#application) — every public member
  - [Lifecycle](#application--lifecycle) · [Layers](#application--layers) ·
    [Subsystem accessors](#application--subsystem-accessors) ·
    [Viewport bounds](#application--viewport-bounds) · [Time control](#application--time-control) ·
    [Pause](#application--pause) · [Window-facing state](#application--window-facing-state) ·
    [Project transitions](#application--project-transitions)
- [`Layer`](#layer) — the hook table, and the hook that is dead
- [`LayerStack`](#layerstack) — engine-internal, documented for the error message
- [`PlayerLayer`](#playerlayer) — the standalone scene player
- [`Window`](#window) — every public member
- [`Log`](#log), [`CallbackSink`](#callbacksink), and the [logging macros](#logging-macros)
- [`UUID`](#uuid)
- [`ICommand`](#icommand) and [`CommandStack`](#commandstack)
- [The plugin-export boundary](#the-plugin-export-boundary) — `HostContext`, `CreatePluginLayer`, `InitializePluginContexts`
- [Failure-mode summary](#failure-mode-summary)
- [Manifest & coverage notes](#manifest--coverage-notes)

---

## Configuration

**Every header in this chapter ships in both engine configurations.** None of them is inside an
`#ifndef COSMIC_2D_ONLY` fence in `Cosmic.h` (`Cosmic.h:17-24`, `:183`), and the CMake 2D partition
block (`Cosmic/CMakeLists.txt:178-210`) filters only `terrain|voxel|water|nav|particles`, five
`renderer/` sources, four `graphics/` sources, `camera/NavigationCube`, five `scene/` sources,
`reflect/TypeRegistry3D` and `assets/MeshImport.cpp`. It touches nothing under `core/` or `layers/`.
So neither the ³ᴰ failure (fenced → clean compile error) nor the ³ᴰ⁺ failure (unfenced, `.cpp`
dropped → `LNK2019`) applies to anything documented here. Background:
[`../systems/build-2d-3d-split.md`](../systems/build-2d-3d-split.md), README §1.6.

One entry has *behaviour* that differs between the builds — [`PlayerLayer`](#playerlayer) carries
in-file fences around navigation and skeletal animation (`PlayerLayer.cpp:169-171`, `:188-204`,
`:260-263`, `:303-305`). The class, its constructor and all six hooks exist and link in both.

> **No pre-condition in this chapter is enforced by an assertion.** `CS_ASSERT` / `CS_CORE_ASSERT` /
> `GLCORE_ASSERT` expand to **nothing** in every shipped configuration: they are gated on
> `CS_ENABLE_ASSERTS` (`Core.h:61-63`), which is defined only when `GLCORE_DEBUG` or `CS_DEBUG` is,
> and **no CMake target defines either**. The four `CS_CORE_ASSERT` calls in `LayerStack.cpp`
> (`:46`, `:63`, `:80`, `:103`, `:129`) and the one in `Window.cpp:581` are dead text. Where a
> header writes "Pre: …", read it as a documented expectation, not a check. See
> [`../guide/logging-and-diagnostics.md#asserts-are-compiled-out-in-every-configuration`](../guide/logging-and-diagnostics.md#asserts-are-compiled-out-in-every-configuration).

---

## Linkage: what actually links from a project DLL

Four of the classes here are `COSMIC_API`-exported and link from a project DLL with no ceremony:
`Application` (`Application.h:51`), `Layer` (`Layer.h:76`), `Timestep` (`Timestep.h:36`), `Log`
(`Log.h:26`), `Window` (`Window.h:126`), `UUID` (`UUID.h:23`), `ICommand` / `CommandStack`
(`CommandStack.h:52`, `:82`) and `PlayerLayer` (`PlayerLayer.h:46`). Export applies to the whole
class, static data members included — which is why `Application::Get()` returns the *host's*
singleton and not a per-DLL copy.

**`LayerStack` is the exception.** It is declared plainly, `class LayerStack` (`LayerStack.h:62`),
with **no** `COSMIC_API`. Its verbs (`PushLayer`, `PushOverlay`, `PopLayer`, `PopOverlay`, `Clear`,
`ForceCleanForShutdown`, and both constructors) are defined in `LayerStack.cpp`, so calling any of
them from a project DLL is an unresolved external. This is deliberate — `Application::PushLayer` /
`PushOverlay` are the client verbs, and the coverage checker records the header as engine plumbing
by name (`tests/check_docs_coverage.ps1:75`). Only the inline `SetIterating` and the four iterator
accessors would link, and none of them is useful without the object.

Two headers in scope are **not** reachable through `<Cosmic.h>` and need an explicit include:

```cpp
#include <Cosmic.h>
#include "core/Window.h"     // only needed if you name the type; Application::GetWindow() returns Window&
#include "core/Version.h"    // NOT included by Cosmic.h — StarforgeApp.cpp:33 includes it directly
```

`core/Window.h` is pulled in transitively by `core/Application.h:28`, so `Application::GetWindow()`
compiles without it; you need the explicit include only for a forward-declared context.
`core/Version.h` is genuinely absent from the `Cosmic.h` include list and is reachable only because
shipped client code includes it (`Projects/Starforge/src/StarforgeApp.cpp:33`) — that is the whole
reason it has a manifest row.

---

## Three source comments in this scope are wrong

Verified against the code they sit next to. Each is repeated in the entry that owns it; they are
collected here because all three have cost someone time.

| Where | What it says | What the code does |
| --- | --- | --- |
| `Application.h:93` | `GetViewportPos`/`GetViewportSize` are *"Viewport bounds in GLFW window-space pixels"* | The value is `ImGui::GetCursorScreenPos()` (`WorkspaceLayer.cpp:214-215`) — **ImGui screen / OS virtual-desktop pixels**. `WorkspaceLayer.h:271-278` states it correctly. See [the entry](#application--viewport-bounds). |
| `Layer.h:42-43` | numbered lifecycle step 6: *"OnRender() … Dispatches traditional world-space rasterization draw commands"* | **Nothing calls `Layer::OnRender()`.** `Application::RenderSingleFrame` has no such pass. See [the entry](#layeronrender). |
| `UUID.cpp:12-14` | *"Guarded so concurrent CreateEntity calls from worker threads (JobSystem) can't corrupt the generator state"* | **There is no guard.** No mutex, no `thread_local`. Concurrent `UUID()` construction is a data race on the shared `std::mt19937_64`. See [the entry](#uuiduuid). |

A fourth, milder one: `Application.cpp:188` reads *"Skip execution passes while minimized
(default). Disabled via `SetPauseOnMinimize(false)`"*, which implies the skip is on by default. It
is not — `m_PauseOnMinimize = false` (`Application.h:187`), and the header comment at
`Application.h:142-143` says so correctly.

---

## `core/Core.h`

`Core.h` defines the engine's vocabulary: the export macro, platform detection, the assertion
macros, two utility macros and the smart-pointer aliases. It has no classes, so nothing here is a
"call" in the usual sense — but every other entry in every chapter depends on it, and `Cosmic.h:17`
includes it **first, deliberately**, because `COSMIC_API` must be defined before any exported class
is parsed.

### `COSMIC_API`

```cpp
// Core.h:46-56
#ifdef COSMIC_PLATFORM_WINDOWS
	#ifdef COSMIC_BUILD_DLL
		#define COSMIC_API __declspec(dllexport)
	#else
		#define COSMIC_API __declspec(dllimport)
	#endif
#else
	#define COSMIC_API
#endif
```

**What it does** — resolves to `dllexport` while compiling `Cosmic.dll` (the engine target defines
`COSMIC_BUILD_DLL`) and to `dllimport` everywhere else, so a class marked with it is one class
shared by the host exe, the engine DLL and every project DLL in the process.

**Why you'd use it** — you don't, in a project DLL: your own layer classes are compiled into your
own module and never cross a boundary as a *type*. You will see it constantly in headers, and it is
the answer to "why does `Application::Get()` return the same object my host created".

**Notes & pitfalls**
- Marking a class `COSMIC_API` exports **every** member, including private ones and static data.
  That is why `Application::s_Instance` (`Application.cpp:36`) is process-wide rather than per-DLL.
- A class *without* it is not exported at all. Only members defined inline in the header are then
  reachable from another module — the trap that produces `LNK2019` for `WorkspaceLayer` and
  `LayerStack`. See [Linkage](#linkage-what-actually-links-from-a-project-dll) and
  [ui.md → Linkage](ui.md#linkage-what-actually-links-from-a-project-dll).
- Non-Windows builds define it to nothing (`Core.h:55`). The engine has never been built anywhere
  but Windows x64; `Core.h:35-41` `#error`s out an x86 build.

### `COSMIC_PLATFORM_WINDOWS`

```cpp
// Core.h:35-41
#ifdef _WIN32
#ifdef _WIN64
#define COSMIC_PLATFORM_WINDOWS
#else
#error "x86 Builds are not supported! Please switch to x64."
#endif
#endif
```

**What it does** — defines `COSMIC_PLATFORM_WINDOWS` on 64-bit Windows and hard-fails a 32-bit
configure with a compiler error.

**Notes & pitfalls** — this is the *only* platform gate in `Core.h`. Individual `.cpp` files use
`#ifdef _WIN32` directly (`Window.cpp:707`, `PlayerLayer` has none), so don't treat this macro as a
complete portability seam.

### `CS_ASSERT` / `CS_CORE_ASSERT` / `GLCORE_ASSERT`

```cpp
// Core.h:70-82
#ifdef CS_ENABLE_ASSERTS
#define CS_ASSERT(x, ...)          { if(!(x)) { fprintf(stderr, "Assertion Failed\n"); CS_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define GLCORE_ASSERT(x, ...)      { if(!(x)) { fprintf(stderr, "Assertion Failed\n"); CS_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define CS_CORE_ASSERT(x, ...)     CS_ASSERT(x, __VA_ARGS__)
#else
#define CS_ASSERT(x, ...)
#define GLCORE_ASSERT(x, ...)
#define CS_CORE_ASSERT(x, ...)
#endif
```

**What it does** — in a build that defines `CS_ENABLE_ASSERTS`, evaluates `x`, and on false prints
to `stderr`, logs at error level and breaks into the debugger. Otherwise it expands to **nothing at
all**.

**Notes & pitfalls**
- **`CS_ENABLE_ASSERTS` is never defined.** It is set only when `GLCORE_DEBUG` or `CS_DEBUG` is
  (`Core.h:61-63`), and no CMake target defines either — not the Debug configuration, not the
  tests. Every assertion in the engine is compiled out in every configuration you can build.
  Treat every `CS_CORE_ASSERT` you read as a comment.
- The `else` branch expands to *nothing*, not to `((void)0)`. `CS_ASSERT(x, "msg");` therefore
  becomes a bare `;`, which is fine at statement scope but will silently vanish as the body of an
  `if` without braces.
- **The condition is not evaluated** when asserts are off, so an assert must never carry a side
  effect. Nothing in the engine currently does.
- `CS_CORE_ERROR` is used inside the macro but `Core.h` deliberately does **not** include `Log.h`
  (`Core.h:71-72`); the expansion only has to compile at the call site.

### `BIT(x)`

```cpp
// Core.h:92
#define BIT(x) (1u << (x))
```

**What it does** — produces a `1u` shifted left by `x`, for flag enumerations.

**Why you'd use it** — the engine's own use is `EventCategory` in
[events-input.md](events-input.md#eventcategory). Reach for it when you need overlapping category
bits; for a plain sequential enum, don't.

**Notes & pitfalls** — the result is `unsigned int`, so `BIT(32)` and above are undefined
behaviour. `x` is expanded twice into the parenthesised expression only once, so arguments with
side effects are safe here (unlike the asserts).

### `CS_BIND_EVENT_FN(fn)`

```cpp
// Core.h:100
#define CS_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)
```

**What it does** — wraps a member function of the enclosing `this` into a one-argument
`std::bind` expression, for handing to `EventDispatcher::Dispatch`.

**Why you'd use it** — you shouldn't. `Core.h:97-98` says so itself: prefer a lambda, which is what
every engine call site now uses (`Application.cpp:449-450`, `:551`).

**Example**

```cpp
// Preferred — what Application::OnEvent actually does (Application.cpp:449):
Cosmic::EventDispatcher dispatcher(e);
dispatcher.Dispatch<Cosmic::WindowCloseEvent>(
    [this](Cosmic::WindowCloseEvent& e) { return OnWindowClose(e); });
```

**Notes & pitfalls** — `std::bind` on a member pointer produces a heavier, less inlinable callable
than a lambda and hides arity errors behind template diagnostics. The macro is kept for source
compatibility only.

### `Scope<T>` / `CreateScope`

```cpp
// Core.h:111-118
template<typename T>
using Scope = std::unique_ptr<T>;

template<typename T, typename ... Args>
constexpr Scope<T> CreateScope(Args&& ... args)
{
	return std::make_unique<T>(std::forward<Args>(args)...);
}
```

**What it does** — `Scope<T>` *is* `std::unique_ptr<T>` (a type alias, not a wrapper), and
`CreateScope<T>(...)` *is* `std::make_unique<T>(...)`.

**Why you'd use it** — for anything with exactly one owner: `Application` holds the `Window` and
the `ImGuiLayer` this way (`Application.h:174-175`), `PlayerLayer` holds its camera
(`PlayerLayer.h:73`), `CommandStack` holds each `ICommand` (`CommandStack.h:149-150`). Reach for
[`Ref<T>`](#reft--createref) instead the moment a second system needs to keep the object alive.

**Example**

```cpp
Cosmic::Scope<MySimulation> sim = Cosmic::CreateScope<MySimulation>(60.0f);
sim->Step(1.0f / 60.0f);
// destroyed when `sim` leaves scope — no delete
```

**Notes & pitfalls**
- Because it is an alias, `Scope<T>` and `std::unique_ptr<T>` are the same type: they interoperate
  freely, and `std::move` is required to transfer ownership exactly as usual.
- **Never move a `Scope<T>` across the DLL boundary** unless `T` is `COSMIC_API`-exported and both
  modules link the one `Cosmic.dll` — the `delete` must run against the allocator that `new`'d.
  The engine ships a single shared CRT for exactly this reason; see
  [`../guide/project-anatomy.md#why-one-cosmicdll-makes-this-safe`](../guide/project-anatomy.md#why-one-cosmicdll-makes-this-safe).
- The one place ownership crosses the boundary as a **raw** pointer is `CreatePluginLayer` — see
  [the plugin-export boundary](#createpluginlayer).

### `Ref<T>` / `CreateRef`

```cpp
// Core.h:125-132
template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename ... Args>
constexpr Ref<T> CreateRef(Args&& ... args)
{
	return std::make_shared<T>(std::forward<Args>(args)...);
}
```

**What it does** — `Ref<T>` *is* `std::shared_ptr<T>`; `CreateRef<T>(...)` *is*
`std::make_shared<T>(...)`.

**Why you'd use it** — every GPU resource factory in the engine returns one (`Shader::Create`,
`Texture2D::Create`, `FrameBuffer::Create`, `Scene::Create`), because the asset cache, the material
and your layer all legitimately hold the same object. See
[graphics-resources.md](graphics-resources.md) for the per-resource lifetime rules.

**Example**

```cpp
Cosmic::Ref<Cosmic::FrameBuffer> fb = Cosmic::Application::Get().GetFrameBuffer();
fb->Bind();
```

**Notes & pitfalls**
- **GPU-backed `Ref`s must die while the OpenGL context is alive.** `Application::Shutdown` is built
  around this: layers are deleted (`Application.cpp:412-415`), then `AudioEngine::Shutdown`,
  `AssetLibrary::Clear` and `Renderer::Shutdown` run (`:424-430`), and only then is the window —
  and the context with it — released (`:433`). A `Ref<Texture2D>` you park in a file-scope static
  outlives all of that and destroys its GL handle with no current context.
- `CreateRef` uses `make_shared`, so the control block and the object share one allocation; a
  `weak_ptr` therefore keeps the object's *storage* alive until it too is released. Irrelevant for
  the sizes involved here, but worth knowing for a large mesh.
- `Ref` is not thread-safe for the *pointee*. The refcount is atomic; the object is not.

---

## `core/Version.h`

Four object-like macros and nothing else. The header is **not** included by `Cosmic.h` — include
`"core/Version.h"` explicitly.

```cpp
// Version.h:17-20
#define COSMIC_VERSION_MAJOR 0
#define COSMIC_VERSION_MINOR 9
#define COSMIC_VERSION_PATCH 0
#define COSMIC_VERSION_STRING "0.9.0"
```

**What they do** — carry the engine's semantic version. `COSMIC_VERSION_STRING` is a string literal
(usable in `printf`-style formatting and in ImGui text directly); the other three are integers.

**Why you'd use them** — to stamp a version into an About box, a packaged manifest or an export
header. Starforge does all three (`StarforgeApp.cpp:2670`, `:3786`, `:4389`, `:4719`), and the
engine banner logs it at boot (`Application.cpp:95`).

**Example**

```cpp
#include <Cosmic.h>
#include "core/Version.h"

ImGui::Text("Engine version: %s", COSMIC_VERSION_STRING);
static_assert(COSMIC_VERSION_MINOR >= 9, "needs the 0.9 physics backend seam");
```

**Notes & pitfalls**
- **Bumping the version is a four-file edit, and the header's own checklist lists only two of
  them.** `Version.h:9-14` names `Runtime/CosmicApp.rc` and `installer/CosmicSetup.iss`. It omits
  **`Runtime/Starforge.rc`**, which carries its own hard-coded `FILEVERSION 0,9,0,0` /
  `"FileVersion", "0.9.0"` (`Starforge.rc:20`, `:34`). Grep for `COSMIC_VERSION` *and* for the
  literal version string.
- The installer is the one consumer that is **not** hand-maintained: `package_installer.bat:30`
  parses `COSMIC_VERSION_STRING` out of this header with `findstr`, so the `#define AppVersion
  "0.9.0"` at `CosmicSetup.iss:17` is only a standalone-invocation fallback.
- There is no runtime version accessor — no `Application::GetVersion()`. These are compile-time
  macros, so a project DLL reports the version of the SDK **it was compiled against**, not the
  version of the `Cosmic.dll` it is running inside. For a mismatched pair those differ silently.

---

## `Timestep`

```cpp
// Timestep.h:36
class COSMIC_API Timestep
```

A one-`float` wrapper around a duration in seconds, with an implicit conversion back to `float`.
Declared in `Cosmic/src/core/Timestep.h`; trivially copyable, no allocation, no ownership.

Its practical role in the current engine is **small and shrinking**: it appears exactly twice in the
frame loop (`Application.cpp:183`, `:238`) and is unwrapped with `.GetSeconds()` before being handed
to any layer. **No `Layer` hook takes a `Timestep`** — `OnUpdate` and `OnFixedUpdate` both take a
plain `float` (`Layer.h:97-98`). Treat this class as a unit-safety helper for your own code, not as
the engine's delta-time currency.

> **Header hazard: `Timestep.h` has no `#include`s at all.** It uses `COSMIC_API` on line 36 while
> including neither `core/Core.h` nor anything else. Compiling `#include "core/Timestep.h"` as the
> first engine include in a translation unit is a hard error. It works everywhere in-tree only
> because `Cosmic.h:17` includes `Core.h` first and `Application.h:27` does the same before
> `:34`. Include `<Cosmic.h>`, or `"core/Core.h"` first, and you will never hit it.

### `Timestep::Timestep`

```cpp
// Timestep.h:43-46
Timestep(float time = 0.0f)
	: m_Time(time)
{
}
```

**What it does** — stores `time`, interpreted as **seconds**. Default-constructs to zero.

**Why you'd use it** — to give a duration a unit at an API boundary, so a caller cannot pass
milliseconds by accident.

**Notes & pitfalls** — the constructor is **not `explicit`**, so a bare `float` converts implicitly.
Combined with `operator float()` this makes `Timestep` and `float` freely interchangeable, which is
convenient and also means the unit safety is advisory rather than enforced. Negative values are
accepted and meaningful (the fixed pass computes a negative delta at `Application.cpp:218`).

### `Timestep::GetSeconds`

```cpp
// Timestep.h:52
float GetSeconds() const				{ return m_Time; }
```

**What it does** — returns the stored value unchanged.

**Why you'd use it** — to make the unit explicit at a call site instead of relying on the implicit
conversion. This is what the frame loop does (`Application.cpp:185`, `:207`, `:243`, `:246`).

### `Timestep::GetMilliseconds`

```cpp
// Timestep.h:53
float GetMilliseconds() const			{ return m_Time * 1000.0f; }
```

**What it does** — returns the stored seconds multiplied by 1000.

**Why you'd use it** — frame-time readouts and profiler chips, where milliseconds read better.

**Notes & pitfalls** — the conversion is computed every call; it is not cached and not rounded.

### `Timestep::operator float`

```cpp
// Timestep.h:60
operator float() const					{ return m_Time; }
```

**What it does** — implicitly converts to the stored **seconds** value in any float context.

**Notes & pitfalls**
- Not `explicit`. A `Timestep` will silently participate in arithmetic, comparisons and overload
  resolution as a `float`, which is the intent — but it also means `Timestep` and `float`
  overloads of the same function are ambiguous.
- Combined with the non-explicit constructor, the type offers no protection against the
  seconds/milliseconds mix-up its own header docstring cites as its purpose (`Timestep.h:13-15`).
  The protection is stylistic.

## `Application`

```cpp
// Application.h:51
class COSMIC_API Application
```

The root singleton. It owns the `Window`, the `ImGuiLayer`, the shared `FrameBuffer`, the
`LayerStack` and the loaded project DLL, and it drives the frame loop. Declared in
`Cosmic/src/core/Application.h`; one instance per process, constructed by the host exe
(`Runtime/Main.cpp:135`) and destroyed by it.

**Ownership.** `Application` holds **absolute ownership of every `Layer*` handed to it** — including
one returned across the DLL boundary by `CreatePluginLayer`. You `new` a layer, pass it to
`PushLayer`, and never `delete` it. The header states the policy at `Application.h:13-24`;
`Shutdown` implements it at `Application.cpp:398-416`.

**Lifecycle map.** The full frame sequence is diagram **DG-3** in
[`../guide/project-anatomy.md#dg-3--the-frame-sequence`](../guide/project-anatomy.md#dg-3--the-frame-sequence);
the state machine (Launcher ⇄ Workspace ⇄ shutdown) is **DG-11**
([`#dg-11--application-states`](../guide/project-anatomy.md#dg-11--application-states)); the
plugin-DLL load/unload sequence is **DG-5**
([`#dg-5--the-plugin-dll-lifecycle`](../guide/project-anatomy.md#dg-5--the-plugin-dll-lifecycle));
the time waterfall is **DG-10** in
[`../guide/time-and-ticks.md#dg-10--the-time-waterfall`](../guide/time-and-ticks.md#dg-10--the-time-waterfall).
Those four are built — this chapter links them and does not redraw them.

<a id="application--lifecycle"></a>
### Lifecycle

### `Application::Application`

```cpp
// Application.h:65
Application(const std::string& startupProjectDll = "");
```

**What it does** — constructs the engine and **runs the entire subsystem boot inside the
constructor**. In order (`Application.cpp:83-101` → `Initialize()` at `:536-612`):

| # | Step | Source |
| --- | --- | --- |
| 0 | store `startupProjectDll` | `:87` |
| 1 | `Log::Init(FileSystem::Resolve("user://logs"))` | `:92` |
| 2 | banner + user-data-root line at INFO | `:94-97` |
| 3 | `s_Instance = this` | `:99` |
| 4 | `JobSystem::Get().Initialize()` — **first statement of `Initialize()`** | `:543` |
| 5 | `AudioEngine::Init()` (headless-safe; a failed device logs a warning and no-ops) | `:547` |
| 6 | create the `Window` at 1280×720, install the event callback | `:550-551` |
| 7 | boot app icon via `Branding::ResolveProcessIcon()` (no file ⇒ platform default) | `:558-562` |
| 8 | `SetVSync(true)` | `:566` |
| 9 | `Renderer::Init()` | `:569` |
| 10 | create the shared `FrameBuffer` at 1280×720 | `:572-575` |
| 11 | create + `PushOverlay` the `ImGuiLayer` | `:578-579` |
| 12 | queue the startup project **or** `PushLayer(new LauncherLayer())` | `:585-593` |
| 13 | `SynchronizeRenderingState()` — resize to the real framebuffer size | `:604` |
| 14 | install the modal frame-pump callback on the `Window` | `:611` |

**Why you'd use it** — you construct exactly one, in `main`. `startupProjectDll` is what makes
`CosmicApp.exe --project <Name>` work; it **must** be a constructor argument rather than a setter
because step 12 happens inside the constructor.

**Example**

```cpp
// Runtime/Main.cpp:135 — the shipped host, condensed
Cosmic::Application* app = nullptr;
try
{
    app = new Cosmic::Application(startupProject);   // "" => Launcher
    app->Run();
    delete app;
    return 0;
}
catch (const std::exception& e)
{
    CS_CORE_CRITICAL("Fatal: unhandled exception escaped main(): {0}", e.what());
}
delete app;   // safe on nullptr; runs the full teardown with the GL context live
return 1;
```

**Notes & pitfalls**
- `startupProjectDll` accepts `"Name"`, `"Name.dll"` or an absolute path. Resolution order is
  `<cwd>/projects/<name>.dll` then `<cwd>/<name>.dll` (`ResolveProjectDLLPath`,
  `Application.cpp:47-74`). **A miss is not fatal**: the helper logs `CS_CORE_ERROR` and
  `ProcessDeferredTransitions` pushes a `LauncherLayer` to land on (`:318-330`).
- **Failures inside step 6 are not checked.** `Window`'s constructor logs `CS_CORE_CRITICAL` and
  returns with a null handle if `glfwInit` or `glfwCreateWindow` fails (`Window.cpp:321-324`,
  `:355-361`); `Initialize()` proceeds regardless and the first `ShouldClose()` dereferences a
  null `GLFWwindow*`. There is no recovery path — see the [failure-mode
  summary](#failure-mode-summary).
- **This can throw.** `Log::Init` calls `std::filesystem::create_directories` (`Log.cpp:42`) and
  constructs `basic_file_sink_mt` (`:65`, `:70`), both of which throw on an unwritable directory.
  Nothing between `main` and there catches, which is exactly why the shipped host wraps
  construction in `try` and `delete`s in the handler.
- **`s_Instance` is assigned but never cleared.** `~Application` does not null it
  (`Application.cpp:109-112`), so `Application::Get()` after `delete app` returns a dangling
  reference rather than crashing at the call.
- Construct only one. A second `Application` overwrites `s_Instance` and its destructor calls
  `glfwTerminate` through the `Window` destructor (`Window.cpp:570`), invalidating the first one's
  handles. The engine is single-window by architectural constraint, stated at `Window.cpp:565-569`.

### `Application::~Application`

```cpp
// Application.h:66
virtual ~Application();
```

**What it does** — calls `Shutdown()` (`Application.cpp:111`) and nothing else.

**Notes & pitfalls** — virtual, so deleting a derived application through `Application*` is safe.
Nothing in the engine derives from it, and the shipped host instantiates `Application` directly.

### `Application::Get`

```cpp
// Application.h:82
static Application& Get();
```

**What it does** — returns `*s_Instance` (`Application.cpp:524-527`).

**Why you'd use it** — it is how every layer, panel and script reaches the window, the framebuffer
and the pause/time controls. There is no other accessor.

**Example**

```cpp
auto& app = Cosmic::Application::Get();
app.GetWindow().SetTitle("Forge Isle");
app.SetFixedTimestepHz(120.0f);
if (app.IsPaused())
    return;
```

**Notes & pitfalls**
- **There is no null check.** Calling `Get()` before the `Application` constructor has reached
  `Application.cpp:99` — that is, from a static initializer, or from another DLL's `DllMain` — is
  an immediate null dereference. Construction order is spelled out in
  [`../guide/project-anatomy.md#construction-order`](../guide/project-anatomy.md#construction-order).
- Safe from a project DLL from `OnAttach` onward: the plugin layer is created at
  `Application.cpp:721`, long after `s_Instance` is set.
- Returns a reference, so you cannot test it. If you need "is the engine up", you need your own
  flag.
- Not thread-safe to call during construction/destruction; safe to call concurrently once
  constructed, though almost everything it returns is main-thread-only.

### `Application::Run`

```cpp
// Application.h:68
void		Run();
```

**What it does** — seeds the frame clock (`Application.cpp:136`) and loops until `m_Running` is
false or the window signals close (`:138`). Each iteration is exactly three steps:

1. `m_Window->PollEvents()` (`:140`)
2. `RenderSingleFrame()` — the whole frame body (`:146`)
3. `ProcessDeferredTransitions()` — **the Safe Zone** (`:155`)

**Why you'd use it** — the host exe calls it once. A project DLL never does.

**Notes & pitfalls**
- **The Safe Zone is deliberately outside `RenderSingleFrame`.** Layer pushes/pops and DLL
  load/unload happen only at step 3, where no `LayerStack` iteration is in flight — and therefore
  never from inside the Win32 modal move/size pump, which calls `RenderSingleFrame` directly.
  Narrative: [`../guide/project-anatomy.md#the-safe-zone`](../guide/project-anatomy.md#the-safe-zone).
- The Safe Zone runs **even while minimized** (`:148-155`), so a project transition queued just
  before the user minimizes does not stall.
- `Run()` does not catch exceptions. One escaping a layer's `OnUpdate` unwinds out of `main`; the
  shipped host catches it there so `~Application` still runs with the GL context alive
  (`Runtime/Main.cpp:141-155`).
- Re-entrant calls are not guarded at this level; `RenderSingleFrame` has its own re-entrancy guard
  (`:178-180`).

### `Application::Shutdown`

```cpp
// Application.h:69
void		Shutdown();
```

**What it does** — the ordered teardown (`Application.cpp:380-436`):

| # | Step | Line |
| --- | --- | --- |
| 0 | `JobSystem::Get().Shutdown()` — **the first subsystem action** | `:385` |
| 1 | `UnloadProjectDLL()` | `:388` |
| 2 | pop the `Scope`-owned `ImGuiLayer` off the stack so it is never raw-`delete`d | `:392-395` |
| 3 | snapshot the remaining `Layer*`s into a local vector | `:398-402` |
| 4 | `m_LayerStack.ForceCleanForShutdown()` — empty the vectors before any deletion | `:407` |
| 5 | `delete` every snapshotted layer | `:412-415` |
| 6 | `m_ImGuiLayer.reset()` | `:419` |
| 7 | `AudioEngine::Shutdown()`, then `AssetLibrary::Clear()`, then `Renderer::Shutdown()` | `:424-430` |
| 8 | `m_Window.reset()` — destroys the GL context and calls `glfwTerminate` | `:433` |

**Why you'd use it** — you don't call it; `~Application` does. It is documented because the *order*
is a contract other subsystems depend on.

**Notes & pitfalls**
- **Step 0 is what guarantees no job outlives the game DLL.** `JobSystem::Initialize` is the first
  statement of `Initialize()` (`:543`) and `Shutdown` is the first of `Shutdown()` (`:385`), so the
  worker pool is strictly the outermost subsystem — it is joined before step 1 unloads the project
  DLL that a queued job's code lives in.
- **Steps 5–8 exist so GPU resources die with a live context.** Layer destructors run while the
  window still exists; the asset cache is cleared before `Renderer::Shutdown`; the window goes last.
  Anything holding a `Ref<Texture2D>` past step 8 destroys a GL handle with no current context.
- **It is public and has no re-entry guard.** Calling `Shutdown()` yourself and then letting the
  destructor run calls it twice. Most steps tolerate that (`UnloadProjectDLL` early-returns on a
  null handle, `reset()` is idempotent, the layer vector is empty the second time), but
  `JobSystem::Shutdown`, `AudioEngine::Shutdown` and `Renderer::Shutdown` are invoked twice and are
  not documented as idempotent. **Don't call it.**
- `ForceCleanForShutdown` (step 4) intentionally skips every `OnDetach()`. Layers that were still
  attached at exit are destructed without it — put GL cleanup in the destructor, not only in
  `OnDetach`.

### `Application::Close`

```cpp
// Application.h:138
void						Close()					{ m_Running = false; }
```

**What it does** — sets the loop flag false. The current frame finishes, the Safe Zone runs once
more, and `Run()` returns.

**Why you'd use it** — a "Quit" menu item. Compare `Window::Close()`
([below](#windowclose)), which sets GLFW's should-close flag instead — the loop condition tests
both (`Application.cpp:138`), so either works and neither is instant.

**Example**

```cpp
if (ImGui::MenuItem("Exit", "Alt+F4"))
    Cosmic::Application::Get().Close();
```

**Notes & pitfalls** — there is **no veto**. Nothing dispatches a "closing" event you can cancel,
and `OnWindowClose` (`Application.cpp:637-641`) marks the event handled unconditionally. If you need
an "unsaved changes?" prompt, gate the menu item, not the close.

### `Application::OnEvent`

```cpp
// Application.h:70
void		OnEvent(Event& e);
```

**What it does** — the engine's single event entry point, installed on the window at
`Application.cpp:551`. It dispatches `WindowCloseEvent` and `WindowResizeEvent` to its own private
handlers (`:449-450`), then walks the `LayerStack` **top-to-bottom** (`rbegin`→`rend`, `:454`),
stopping as soon as `e.Handled` is true (`:456-459`).

**Why you'd use it** — you don't call it; you *receive* the result in `Layer::OnEvent`. It is
documented so the propagation order is pinned: overlays (pushed last) see an event before layers.

**Notes & pitfalls**
- The two global handlers run **before** any layer and cannot be intercepted. `OnWindowResize`
  returns `false` (`:660`) so the resize keeps propagating; `OnWindowClose` returns `true` (`:640`)
  so the close does not.
- The stack is marked iterating for the whole walk (`:453`, `:463`), so a layer that pushes or pops
  from inside `OnEvent` is violating the contract. Defer to the Safe Zone.
- Event *filtering* against ImGui happens in `ImGuiLayer` (see
  [ui.md](ui.md#imguilayerblockevents)), not here.

<a id="application--layers"></a>
### Layers

### `Application::PushLayer`

```cpp
// Application.h:71
void		PushLayer(Layer* inLayer);
```

**What it does** — forwards to `LayerStack::PushLayer` (`Application.cpp:485`), which inserts the
layer at the *layer/overlay boundary* — below every overlay, above every earlier layer — and calls
`OnAttach()` synchronously (`LayerStack.cpp:48-50`).

**Why you'd use it** — to add world/simulation content. Use
[`PushOverlay`](#applicationpushoverlay) for chrome that must draw last and see input first.

**Example**

```cpp
// A host exe adding its own layer before Run()
Cosmic::Application app;
app.PushLayer(new MySimulationLayer());   // app owns it from here
app.Run();
```

**Notes & pitfalls**
- **Ownership transfers to `Application`.** Never `delete` a pushed layer; `Shutdown` does
  (`Application.cpp:412-415`).
- **`OnAttach()` runs inside this call**, before it returns. If it touches GL, the renderer must
  already be initialised — true for anything pushed after the `Application` constructor.
- **Never call it from inside a `LayerStack` walk** — from `OnUpdate`, `OnImGuiRender`, `OnEvent` or
  `OnFixedUpdate`. Mutating the vector invalidates the live iterator (UB). The intended guard,
  `CS_CORE_ASSERT(!m_Iterating, …)` at `LayerStack.cpp:46`, **is compiled out in every
  configuration** and will not warn you. Queue the request and act in the Safe Zone.
- A project DLL almost never calls this: its layer is hosted by `WorkspaceLayer`, not stacked. See
  [the plugin-export boundary](#createpluginlayer).

### `Application::PushOverlay`

```cpp
// Application.h:72
void		PushOverlay(Layer* inOverlay);
```

**What it does** — forwards to `LayerStack::PushOverlay` (`Application.cpp:495`), appending to the
very back of the vector and calling `OnAttach()` (`LayerStack.cpp:65-66`).

**Why you'd use it** — for something that must **render last** (bottom-to-top iteration reaches it
last) and **receive events first** (top-to-bottom iteration reaches it first): a debug console, a
global HUD. The engine's own `ImGuiLayer` is pushed this way (`Application.cpp:579`).

**Notes & pitfalls** — same ownership and same iteration hazard as `PushLayer`. There is no
`Application::PopLayer` / `PopOverlay`: removal is engine-internal and happens only in the Safe Zone
(`Application.cpp:299`, `:345`) and in `Shutdown`.

<a id="application--subsystem-accessors"></a>
### Subsystem accessors

### `Application::GetWindow`

```cpp
// Application.h:89
inline Window&					GetWindow()							{ return *m_Window; }
```

**What it does** — returns a reference to the single `Window`. Every member of
[`Window`](#window) below is reached through this.

**Notes & pitfalls** — **dereferences without a null check.** `m_Window` is null before
`Initialize()` step 6 and after `Shutdown` step 8, and holds a `Window` with a null `GLFWwindow*` if
GLFW failed to start. In practice: safe from `OnAttach` through `OnDetach`, unsafe in a static
initializer or after teardown.

### `Application::GetFrameBuffer`

```cpp
// Application.h:90
inline Ref<FrameBuffer>         GetFrameBuffer()					{ return m_Framebuffer; }
```

**What it does** — returns the engine's shared offscreen colour target, created at 1280×720
(`Application.cpp:572-575`) and resized on every window resize (`:657`) and, in an editor host, to
the viewport panel size every frame (`WorkspaceLayer.cpp:78-84`).

**Why you'd use it** — this is where a hosted layer's world rendering lands: `WorkspaceLayer::OnUpdate`
binds it, clears it and calls your `OnUpdate` inside (`WorkspaceLayer.cpp:86-102`), then displays it
as `ImGui::Image` (`:221-224`). `PlayerLayer` binds the same target
(`PlayerLayer.cpp:350`). Query it for the render aspect ratio rather than the window size.

**Example**

```cpp
Cosmic::Ref<Cosmic::FrameBuffer> fb = Cosmic::Application::Get().GetFrameBuffer();
const float aspect = (float)fb->GetWidth() / (float)fb->GetHeight();
```

**Notes & pitfalls**
- Returns by **value**, so each call copies a `shared_ptr` (an atomic increment). Cache it in a
  local for a hot loop; do **not** cache it in a member across frames — the `Ref` keeps a resized-away
  framebuffer alive.
- Can be null before `Initialize()` step 10. `PlayerLayer::RenderScene` null-checks it and also
  guards against a 0×0 target (`PlayerLayer.cpp:340-341`) — copy that pattern.
- Not `const`, because it hands out a mutable `Ref`.

### `Application::GetWorkspaceLayer`

```cpp
// Application.h:91
inline WorkspaceLayer*			GetWorkspaceLayer()					{ return m_WorkspaceLayer; }
```

**What it does** — returns the editor shell layer, or **`nullptr`**.

**Why you'd use it** — to dock a panel, hide the viewport, set the project name. All of that API is
[ui.md → `WorkspaceLayer`](ui.md#workspacelayer).

**Notes & pitfalls**
- **Null on the Launcher screen** and after a teardown; non-null only between
  `ProcessDeferredTransitions` creating it (`Application.cpp:350`) and deleting it (`:300`). Every
  shipped project null-checks (`FrontierApp.cpp:262-263`, `PlayerLayer.cpp:97`).
- `Application.h:49` only **forward-declares** `WorkspaceLayer`. You must
  `#include "layers/WorkspaceLayer.h"` yourself — `Cosmic.h` does not.
- The class is not `COSMIC_API`-exported, so only its inline members link from a project DLL. The
  per-member table is in [ui.md](ui.md#linkage-what-actually-links-from-a-project-dll).

### `Application::GetImGuiLayer`

```cpp
// Application.h:137
inline ImGuiLayer*			GetImGuiLayer()			{ return m_ImGuiLayer.get(); }
```

**What it does** — returns the engine's ImGui/ImPlot host overlay (created and pushed at
`Application.cpp:578-579`).

**Why you'd use it** — almost exclusively for `BlockEvents(bool)`, which is how a host tells the
engine whether ImGui or the client owns input this frame (`WorkspaceLayer.cpp:210-211`, `:232`).
The class is documented in [ui.md](ui.md#imguilayer).

**Notes & pitfalls** — returns a **raw** pointer into a `Scope`-owned object; do not delete or
store it past `Shutdown` step 6. Null before `Initialize()` step 11.

<a id="application--viewport-bounds"></a>
### Viewport bounds

### `Application::GetViewportPos`

```cpp
// Application.h:95
glm::vec2			GetViewportPos()  const;
```

**What it does** — returns the top-left corner of the **rendered image content** inside the editor's
central Viewport panel — the point just below the tab bar, *not* the panel's window origin. It
forwards to `WorkspaceLayer::GetViewportPos()`, or returns `{0, 0}` when no workspace is active
(`Application.cpp:468-471`).

> **The comment above the declaration is wrong.** `Application.h:93` says *"Viewport bounds in GLFW
> window-space pixels"*. The value is captured as `ImGui::GetCursorScreenPos()` at
> `WorkspaceLayer.cpp:214-215` and is therefore in **ImGui screen space — OS virtual-desktop
> pixels**, because multi-viewport is enabled and every ImGui rect lives in that space.
> `WorkspaceLayer.h:271-278` documents it correctly. This stale comment is the likeliest origin of a
> recurring picking bug: mixing it with the window-relative mouse position works perfectly on a
> borderless-maximized window at the desktop origin and is off by the window position everywhere
> else.

**Why you'd use it** — to convert a mouse position into viewport-local coordinates for picking,
gizmos or a custom overlay.

**Example**

```cpp
// Correct: BOTH operands in ImGui screen space.
auto& app = Cosmic::Application::Get();
const glm::vec2 vpPos  = app.GetViewportPos();
const glm::vec2 vpSize = app.GetViewportSize();
const glm::vec2 mouse  = Cosmic::Input::GetMouseScreenPosition();   // NOT GetMousePosition()

const glm::vec2 local = mouse - vpPos;
const bool inside = local.x >= 0.0f && local.y >= 0.0f &&
                    local.x <  vpSize.x && local.y <  vpSize.y;
```

**Notes & pitfalls**
- Pair it with `Input::GetMouseScreenPosition()` (`Input.h:65`), never with
  `Input::GetMousePosition()` (`Input.h:55`), which is window-client-relative. The `Input` header
  spells this out at `Input.h:63-65`; see [events-input.md](events-input.md#inputgetmousescreenposition).
- **Returns `{0, 0}` — not an error — when there is no workspace**, i.e. on the Launcher and in a
  packaged `PlayerLayer` app. `{0,0}` is also a legitimate value, so you cannot distinguish them.
  Check `GetWorkspaceLayer() != nullptr` if it matters.
- The value is refreshed during `OnImGuiRender`, so reading it from `OnUpdate` gives you **last
  frame's** rectangle. That is one frame of lag, which is fine for picking and wrong for anything
  that must be pixel-exact on the frame a panel is resized.
- `WorkspaceLayer` is not exported, but these two `Application` members are — this is the supported
  way to reach the bounds from a project DLL.

### `Application::GetViewportSize`

```cpp
// Application.h:96
glm::vec2			GetViewportSize() const;
```

**What it does** — returns the size in pixels of the rendered image content, from
`ImGui::GetContentRegionAvail()` (`WorkspaceLayer.cpp:217-219`), or `{0, 0}` with no workspace
(`Application.cpp:473-476`).

**Notes & pitfalls**
- Same space, same one-frame lag, same `{0,0}` fallback as `GetViewportPos`.
- **The stored size is only updated when both components are positive** (`WorkspaceLayer.cpp:218`),
  so a fully collapsed viewport panel leaves the last non-degenerate size in place rather than
  reporting `{0,0}`. Do not use it to detect "viewport hidden" — use
  `WorkspaceLayer::IsViewportVisible()`.
- Not the same as the framebuffer size on the frame the panel is resized: `WorkspaceLayer::OnUpdate`
  resizes the FBO *to* this value (`WorkspaceLayer.cpp:78-84`), so for aspect-ratio maths prefer
  `GetFrameBuffer()->GetWidth()/GetHeight()`, which is what `PlayerLayer` does
  (`PlayerLayer.cpp:343`).

<a id="application--time-control"></a>
### Time control

The frame's two passes and the exact meaning of each delta are the guide's job
([`../guide/time-and-ticks.md`](../guide/time-and-ticks.md), diagram DG-10). What follows is the
per-call contract.

### `Application::UseFixedTimeStep`

```cpp
// Application.h:103
void			UseFixedTimeStep(bool useFixedTimeStep)		{ m_UseFixedTimestep = useFixedTimeStep; }
```

**What it does** — enables or disables the entire fixed-step pass. When false, the accumulator is
never touched and **no layer's `OnFixedUpdate` is ever called** (`Application.cpp:200`).

**Why you'd use it** — to shut off deterministic ticking in a tool that has no simulation. Default
is **true** (`Application.h:185`).

**Notes & pitfalls**
- There is no getter. Track the value yourself if you need it.
- Turning it off silently stops physics: `Scene::OnPhysicsStep` is driven from `OnFixedUpdate`
  (`PlayerLayer.cpp:302`). Nothing warns.
- Turning it back on does **not** clear the accumulator, but the accumulator also does not advance
  while off, so there is no catch-up burst.
- Takes effect at the top of the next frame; it is read once per frame at `:200`.

### `Application::SetFixedTimestepHz`

```cpp
// Application.h:111
void			SetFixedTimestepHz(float hz);
```

**What it does** — sets the fixed-pass rate. The value is **clamped to `[1, 1000]`**, and a clamped
call logs `CS_CORE_WARN("SetFixedTimestepHz({0}) clamped to {1} Hz.", …)`
(`Application.cpp:514-522`). The new rate is sampled once at the top of the next frame
(`:205`), so a change mid-frame cannot tear the drain loop.

**Why you'd use it** — a control loop or vehicle sim that needs 120 Hz or 240 Hz. Default 60 Hz
(`Application.h:214`). `PlayerLayer` calls it on attach from the project manifest's `fixed_dt_hz`
key (`PlayerLayer.cpp:70`, `:89`).

**Example**

```cpp
Cosmic::Application::Get().SetFixedTimestepHz(240.0f);   // 4.17 ms fixed dt
```

**Notes & pitfalls**
- **This is global.** Raising it ticks **every** layer's `OnFixedUpdate` faster, not just yours. For
  a single high-rate loop, substep inside your own `OnFixedUpdate` instead — the header says so at
  `Application.h:108-110`.
- Out-of-range values are clamped and logged, never rejected: `SetFixedTimestepHz(0.0f)` becomes
  1 Hz, not "off". Use `UseFixedTimeStep(false)` for off.
- `NaN` survives `std::clamp` unchanged on MSVC and would make the drain loop's condition false
  forever — fixed updates would silently stop. Nothing validates for it.
- Raising the rate raises the **maximum** drain iterations per frame: the 0.25 s spiral clamp
  (`:210-213`) divided by `1/hz` — 15 at 60 Hz, 60 at 240 Hz, 250 at 1000 Hz.

### `Application::GetFixedTimestepHz`

```cpp
// Application.h:112
float			GetFixedTimestepHz() const					{ return m_FixedTimestepHz; }
```

**What it does** — returns the current (already clamped) rate. The fixed delta your
`OnFixedUpdate` receives is exactly `1.0f / GetFixedTimestepHz()` in magnitude.

### `Application::SetTimeScale`

```cpp
// Application.h:104
void			SetTimeScale(float timescale)				{ m_TimeScale = timescale; }
```

**What it does** — sets the global time multiplier, default `1.0f` (`Application.h:212`). It is
applied in **two** places per frame:

- the fixed accumulator fills at scaled rate: `m_Accumulator += frameTime * m_TimeScale`
  (`Application.cpp:215`);
- the variable delta is scaled: `rawTimestep.GetSeconds() * m_TimeScale` (`:238`).

**It does not change the magnitude of the fixed delta**, which stays `1/hz` so fixed-step
integration remains stable (`:205`, `:218`).

**Why you'd use it** — slow-motion, fast-forward, a simulation speed slider. For a *pause*, use
[`Pause()`](#applicationpause), not `SetTimeScale(0)`.

**Example**

```cpp
static float speed = 1.0f;
if (ImGui::SliderFloat("Speed", &speed, 0.0f, 4.0f))
    Cosmic::Application::Get().SetTimeScale(speed);
```

**Notes & pitfalls**
- **No clamping, no validation.** Negative, zero, huge and `NaN` are all accepted.
- **A negative scale does not rewind the fixed pass.** The accumulator runs *backwards*
  (`:215`), so the drain condition `m_Accumulator >= fixedDeltaTime` (`:221`) never fires and the
  signed delta computed at `:218` is unreachable in practice. Worse, the accumulated negative debt
  must be repaid before fixed updates resume after you restore a positive scale. Rewind works for
  visuals, which read `Layer::GetLocalTime()`. Documented in
  [`../guide/time-and-ticks.md#rewind-and-the-accumulator-debt`](../guide/time-and-ticks.md#rewind-and-the-accumulator-debt).
- A very large scale makes the accumulator overrun the 0.25 s spiral clamp's protection: the clamp
  is applied to `frameTime` *before* the multiply (`:210-215`), so `TimeScale = 100` still queues
  25 s of simulated time into one frame.
- Orthogonal to pause. While paused the variable delta is forced to `0` regardless of scale
  (`:238`), and the fixed pass is skipped entirely (`:200`).

### `Application::GetTimeScale`

```cpp
// Application.h:105
float			GetTimeScale() const						{ return m_TimeScale; }
```

**What it does** — returns the current multiplier, exactly as set (never clamped).

**Notes & pitfalls** — `GetTimeScale() == 0.0f` is **not** a reliable "is paused" test: the engine
can be paused with a scale of 1, and scaled to 0 without being paused. Use
[`IsPaused()`](#applicationispaused).

### `Application::GetAbsoluteTime`

```cpp
// Application.h:114
inline float	GetAbsoluteTime() const						{ return m_AbsoluteTime; } // seconds
```

**What it does** — returns unscaled process uptime in seconds, accumulated from the raw frame delta
at the very top of every frame (`Application.cpp:185`) — **before** the minimized early-out, before
the pause checks, before any scaling.

**Why you'd use it** — anything that must keep moving regardless of simulation state: a spinner on
the pause screen, a session clock, a UI shimmer. For anything that should freeze with the
simulation, use `Layer::GetLocalTime()` instead.

**Example**

```cpp
// A pause-menu spinner that keeps turning while the world is frozen
const float t = Cosmic::Application::Get().GetAbsoluteTime();
const float angle = std::fmod(t * 90.0f, 360.0f);
```

**Notes & pitfalls**
- Keeps advancing while **paused**, while **minimized** and at any `TimeScale`, including negative.
  It is uptime, not simulated time.
- It accumulates a `float` per frame from zero, so precision decays with session length: at 60 fps
  the increment stops resolving cleanly past a few hours of uptime. Fine for UI, wrong for a
  long-running timestamp — record wall-clock time for that.
- There is no setter and no reset.

<a id="application--pause"></a>
### Pause

First-class pause, orthogonal to `TimeScale`, specified in
[`../design/responsive-rendering-and-pause.md`](../design/responsive-rendering-and-pause.md)
(Feature B). **What paused actually means, per subsystem** — the condensed form of README §7's
table, so each entry below can just say "see the state table":

| While `IsPaused()` | Behaviour | Source |
| --- | --- | --- |
| `Layer::OnFixedUpdate` | **skipped entirely**; the accumulator does not advance, so `Resume()` triggers no catch-up burst | `Application.cpp:200` |
| `Layer::OnUpdate` | **still runs, with `dt = 0`** — this is where drawing happens, so skipping it would blank the scene | `:238-247` |
| `Layer::UpdateLayerTime` | called with `0`, so `GetLocalTime()` freezes | `:243` |
| `Layer::OnImGuiRender` + present | run normally — pause menus stay live and animated | `:254-263` |
| `GetAbsoluteTime()` | keeps advancing | `:185` |
| `GetTimeScale()` | untouched; `Resume()` restores the user's speed exactly | `:122-124` |
| script events (`PlayerLayer`) | `ScriptHost::DispatchEvent` is skipped | `PlayerLayer.cpp:430-431` |
| script ticks (`PlayerLayer`) | `Tick`, sprite and skeletal animation all skipped | `PlayerLayer.cpp:254-265` |

Versus `SetTimeScale(0.0f)`: the fixed pass also stops (the accumulator stops growing) and `OnUpdate`
also gets `0`, but the user's chosen speed is **destroyed** — you must save and restore it yourself
— and the state is only queryable as the ambiguous `GetTimeScale() == 0`. Both can be active at
once; pause wins, because it forces `dt = 0` regardless of scale.

### `Application::Pause`

```cpp
// Application.h:122
void			Pause()										{ m_Paused = true; }
```

**What it does** — enters the paused state described in the table above. Idempotent.

**Why you'd use it** — a user-facing pause. It is the right call in preference to
`SetTimeScale(0)` in every case.

**Example**

```cpp
// PlayerLayer.cpp:402-403 — the shipped player's Escape handling
if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    Cosmic::Application::Get().TogglePause();
```

**Notes & pitfalls** — **the engine binds no pause hotkey**; the line above is `PlayerLayer`'s own.
Pausing does not stop `Window::PollEvents` or the Safe Zone, so a project transition queued while
paused still completes.

### `Application::Resume`

```cpp
// Application.h:123
void			Resume()									{ m_Paused = false; }
```

**What it does** — leaves the paused state. **Never touches `TimeScale`** — that is the entire point
of pause being a separate flag.

**Notes & pitfalls** — because the accumulator was frozen, the first frame after `Resume()` runs the
normal number of fixed steps. There is no burst, and no time is silently simulated for the paused
interval.

### `Application::TogglePause`

```cpp
// Application.h:124
void			TogglePause()								{ m_Paused = !m_Paused; }
```

**What it does** — flips the flag. Equivalent to `IsPaused() ? Resume() : Pause()`.

**Notes & pitfalls** — bind it to a key **edge**, not to a polled `Input::IsKeyPressed`, or it will
toggle every frame the key is held. `PlayerLayer` uses `ImGui::IsKeyPressed(key, /*repeat=*/false)`
(`PlayerLayer.cpp:402`); an event-driven layer should check `KeyPressedEvent::GetRepeatCount() == 0`.

### `Application::IsPaused`

```cpp
// Application.h:125
bool			IsPaused() const							{ return m_Paused; }
```

**What it does** — returns the pause flag.

**Why you'd use it** — to gate your own simulation work and to decide whether to draw a pause
overlay. `PlayerLayer` gates scripts, UI, sprite animation and event dispatch on it
(`PlayerLayer.cpp:231`, `:254`, `:405`, `:430`).

**Notes & pitfalls** — unrelated to `GetPauseOnMinimize()`. A minimized-and-skipping application is
**not** `IsPaused()`.

<a id="application--window-facing-state"></a>
### Window-facing state

### `Application::SetPauseOnMinimize` / `GetPauseOnMinimize`

```cpp
// Application.h:144-145
void						SetPauseOnMinimize(bool pause)	{ m_PauseOnMinimize = pause; }
bool						GetPauseOnMinimize() const		{ return m_PauseOnMinimize; }
```

**What it does** — when `true`, `RenderSingleFrame` returns early while the window is minimized,
skipping the fixed pass, the variable pass, ImGui and the buffer swap alike
(`Application.cpp:189-193`). When `false`, the engine keeps ticking and drawing into an invisible
window.

**Default: `false`** (`Application.h:187`) — the engine keeps running while minimized, which is what
a simulation, a telemetry tool or a long-running server wants. Set it `true` for game-like
behaviour.

**Example**

```cpp
Cosmic::Application::Get().SetPauseOnMinimize(true);   // opt in to game-like behaviour
```

**Notes & pitfalls**
- **`Application.cpp:188`'s comment contradicts the header and is wrong.** It reads "Skip execution
  passes while minimized (default). Disabled via `SetPauseOnMinimize(false)`", which reads as
  default-on. `Application.h:142-143` is correct: the default is off.
- The **Safe Zone still runs while minimized** (`Application.cpp:148-155`), by design, so a queued
  project transition does not stall until the window is restored.
- `GetAbsoluteTime()` still advances even when frames are skipped — the accumulation at `:185`
  precedes the early-out at `:189`.
- The minimized state is derived from a `WindowResizeEvent` with a zero dimension
  (`Application.cpp:650-656`), not from a GLFW iconify query.

### `Application::SetRenderWhileDragging` / `IsRenderWhileDragging`

```cpp
// Application.h:129-130
void			SetRenderWhileDragging(bool enabled);
bool			IsRenderWhileDragging() const;
```

**What it does** — enables or disables the Win32 modal frame pump: whether the engine keeps
rendering while the user drags or resizes the OS window. Both calls forward straight to the
`Window` (`Application.cpp:621-630`), null-safely. **Default: on** (`Window.h:349`).

**Why you'd use it** — turn it **off** for a minimal or low-power tool that would rather freeze than
burn a core repainting during a drag. Everything else wants it on: without it, `glfwPollEvents`
blocks for the whole modal loop and the window shows stale content.

**Notes & pitfalls**
- Disabling it mid-drag stops the pump immediately (`Window.cpp:780-781`).
- The pump calls `RenderSingleFrame` **directly**, bypassing `PollEvents` and the Safe Zone — so no
  layer push/pop or DLL load can happen mid-drag, by construction (`Application.cpp:142-146`,
  `:611`).
- `IsRenderWhileDragging()` returns `false` when there is no window at all, which is
  indistinguishable from "explicitly disabled".
- Composes with pause: a paused application still repaints during a drag, because the ImGui pass and
  the swap are outside the pause checks. See
  [`../guide/windowing-and-viewport.md#it-composes-with-pause`](../guide/windowing-and-viewport.md#it-composes-with-pause).

<a id="application--project-transitions"></a>
### Project transitions

### `Application::TransitionFromLauncherToWorkspace`

```cpp
// Application.h:74
void		TransitionFromLauncherToWorkspace(const std::string& projectDllFilename);
```

**What it does** — **queues** a project load. The call itself only caches the string
(`Application.cpp:500-503`); the work happens in the next Safe Zone
(`ProcessDeferredTransitions`, `:311-355`), where it: resolves the DLL path *before* tearing
anything down, pops and deletes the `LauncherLayer`, creates and pushes a `WorkspaceLayer`, then
`LoadProjectDLL`s.

**Why you'd use it** — a launcher or project-picker UI. The engine's own `LauncherLayer` calls it.

**Notes & pitfalls**
- **Nothing has happened when this returns.** Do not assume `GetWorkspaceLayer()` is non-null on the
  next line.
- Accepts the same three spellings as the constructor argument (`"Name"`, `"Name.dll"`, absolute
  path). Resolution failure logs an error and leaves you on the Launcher — pushing one first if the
  boot path never created one (`:318-330`).
- Calling it twice before the Safe Zone runs keeps only the **last** request; `m_PendingProjectDLL`
  is a single string.
- Load failures after the point of no return are *not* recoverable: a DLL that loads but is missing
  either export logs `"Plugin is missing required engine export signatures!"`, frees the library and
  leaves an empty `WorkspaceLayer` (`:705-710`).

### `Application::TransitionToLauncher`

```cpp
// Application.h:75
void		TransitionToLauncher();
```

**What it does** — **queues** a return to the Launcher by setting a flag
(`Application.cpp:507-510`). The Safe Zone then unloads the project DLL, asks the `WorkspaceLayer`
to begin its multi-stage ImGui teardown, and only deletes it once it reports
`IsReadyForDeletion()` — after which a fresh `LauncherLayer` is pushed and the renderer state is
resynchronised (`:281-308`).

**Why you'd use it** — a "Quit to Launcher" menu item. `PlayerLayer`'s pause menu does exactly this
(`PlayerLayer.cpp:419-423`), and its flow runtime does it on a quit signal (`:242-246`).

**Example**

```cpp
if (ImGui::Button("Quit to Launcher", ImVec2(180, 0)))
{
    auto& app = Cosmic::Application::Get();
    app.Resume();                 // never leave the engine paused behind the Launcher
    app.TransitionToLauncher();
}
```

**Notes & pitfalls**
- **Deletion of your layer is deferred by at least one frame**, and in practice by the
  `WorkspaceLayer`'s dock-teardown handshake. Do not assume your `OnDetach` has run when this
  returns.
- The teardown deletes the plugin layer (`:791`), clears the fullscreen hotkey override (`:797`) and
  resets the engine's active VFS project to `""` (`:805`). Project-registered themes and fonts are
  deliberately **left registered** (`:800-804`) — dropping them would dangle `ImFont*` handles other
  systems may still hold this frame.
- Idempotent: setting the flag twice is one transition.
- Returning to the Launcher paused leaves the Launcher paused. Call `Resume()` first, as above.

### `Cosmic::CreateApplication`

```cpp
// Application.h:271
Application* CreateApplication();
```

**A dead declaration.** Nothing in the repository defines it and nothing calls it — the shipped host
constructs `new Cosmic::Application(startupProject)` directly (`Runtime/Main.cpp:135`). It is a
vestige of the Hazel-style `CreateApplication` entry-point convention. **Do not implement it
expecting the engine to call it.** Documented here only so a reader who finds it in the header stops
looking.

## `Layer`

```cpp
// Layer.h:76
class COSMIC_API Layer
```

The polymorphic base for every unit of application behaviour. Declared in `Cosmic/src/core/Layer.h`;
**every member is defined inline in the header**, so the whole class is available to a project DLL
with no linkage question at all. Each layer carries its own local clock (`m_LocalTime`) and time
scale (`m_LocalTimeScale`).

**Ownership**: heap-allocate with `new`, hand to `Application::PushLayer`/`PushOverlay` (or return
from `CreatePluginLayer`), and never delete. See
[`../guide/project-anatomy.md#write-a-layer`](../guide/project-anatomy.md#write-a-layer) for the
task-level walkthrough.

**The hook table** — what actually calls what, and when:

| Hook | Called by | When | Argument |
| --- | --- | --- | --- |
| `OnAttach()` | `LayerStack::PushLayer`/`PushOverlay` (`LayerStack.cpp:50`, `:66`) or `WorkspaceLayer::SetViewportLayer` (`WorkspaceLayer.cpp:54`) | synchronously, inside the push/mount call | — |
| `OnDetach()` | `LayerStack::PopLayer`/`PopOverlay` (`:86`, `:135`) or `WorkspaceLayer::SetViewportLayer`/`ClearViewportLayer` (`:45`, `:64`) | on removal — **not** on `ForceCleanForShutdown` | — |
| `OnFixedUpdate(float)` | `Application::RenderSingleFrame` (`Application.cpp:225`) | pass 1A, 0–N times per frame, skipped while paused | `±1/hz` |
| `OnUpdate(float)` | `Application::RenderSingleFrame` (`:246`) | pass 1B, exactly once per frame — **and where all drawing happens** | scaled delta, `0` while paused |
| `OnRender()` | **nothing** | never | — |
| `OnImGuiRender()` | `Application::RenderSingleFrame` (`:258`) | pass 2, between `ImGuiLayer::Begin/End` | — |
| `OnEvent(Event&)` | `Application::OnEvent` (`:461`) | top-to-bottom, stops at `e.Handled` | the event |

For a **plugin layer** every row above is instead forwarded by hand by `WorkspaceLayer` — see
[the hosting note](#a-plugin-layer-is-not-on-the-layerstack).

### `Layer::Layer`

```cpp
// Layer.h:83-86
Layer(const std::string& name = "Layer")
    : m_DebugName(name), m_LocalTime(0.0f), m_LocalTimeScale(1.0f)
{
}
```

**What it does** — stores a debug name and initialises the local clock to `0.0f` and the local scale
to `1.0f`.

**Why you'd use it** — pass a name; it is what shows up in the engine's mount/evict logs
(`WorkspaceLayer.cpp:43-44`, `:52-53`) and in your own diagnostics.

**Example**

```cpp
class MySimLayer : public Cosmic::Layer
{
public:
    MySimLayer() : Cosmic::Layer("MySimLayer") {}
    void OnUpdate(float dt) override    { /* simulate AND draw */ }
    void OnFixedUpdate(float h) override{ /* integrate at exactly 1/hz */ }
};
```

**Notes & pitfalls** — not `explicit`, so a bare string literal converts. The default name `"Layer"`
makes log lines useless; always pass one.

### `Layer::~Layer`

```cpp
// Layer.h:88
virtual ~Layer() = default;
```

**What it does** — virtual, so `delete` through a `Layer*` runs your derived destructor. This is
load-bearing: `Application::Shutdown` deletes through the base pointer (`Application.cpp:414`), and
`LayerStack::PushLayer` guards it with a **compile-time** check that survives the assert purge:

```cpp
// LayerStack.cpp:44-45
static_assert(std::has_virtual_destructor_v<Layer>,
    "Layer::~Layer() must be virtual. Deleting a derived layer through Layer* without it is UB.");
```

**Notes & pitfalls** — the destructor runs at `Application::Shutdown` step 5
(`Application.cpp:412-415`), which is **before** `AssetLibrary::Clear`, `Renderer::Shutdown` and the
window reset — so the OpenGL context is still current there. That is the safety net; the *guidance*
is still to release GPU-backed `Ref<>`s in `OnDetach`, which for a plugin layer always runs. See
[`OnDetach`](#layerondetach) for exactly when it does and does not, and
[`../guide/project-anatomy.md#teardown-ordering-for-gpu-resources`](../guide/project-anatomy.md#teardown-ordering-for-gpu-resources).

### `Layer::OnAttach`

```cpp
// Layer.h:90
virtual void OnAttach() {};
```

**What it does** — nothing, by default. Override for one-time setup: create shaders, textures,
scenes, subscribe to buses.

**Why you'd use it** — it is the earliest point at which the renderer, the window and
`Application::Get()` are all guaranteed live. A constructor is too early for GL work when the layer
is constructed before the push.

**Notes & pitfalls**
- **Runs synchronously inside `PushLayer`/`PushOverlay`** — before the call returns
  (`LayerStack.cpp:50`).
- For a plugin layer it runs inside `WorkspaceLayer::SetViewportLayer` (`WorkspaceLayer.cpp:54`),
  which is called from `LoadProjectDLL` **in the Safe Zone** (`Application.cpp:744`). Pushing layers
  or loading another DLL from here is therefore reentering the Safe Zone — don't.
- It cannot fail. There is no return value and no engine-level error path; log and degrade.

### `Layer::OnDetach`

```cpp
// Layer.h:91
virtual void OnDetach() {};
```

**What it does** — nothing, by default. Override to release things that must not outlive the layer's
place in the stack: unsubscribe from buses, release the cursor, clear callbacks you registered on
the `Window`.

**Notes & pitfalls**
- **Whether it runs at shutdown depends on how the layer is mounted**, and the answer is good news
  for client code:

  | Layer | `OnDetach` at `Application::Shutdown`? | Why |
  | --- | --- | --- |
  | a **plugin layer** (what you write) | **yes** | step 1 calls `UnloadProjectDLL` → `WorkspaceLayer::ClearViewportLayer` → `OnDetach` (`Application.cpp:388`, `:785`; `WorkspaceLayer.cpp:64`) |
  | the engine's `ImGuiLayer` | yes | step 2 pops it properly (`Application.cpp:392-395`) |
  | anything else still on the `LayerStack` (`LauncherLayer`, `WorkspaceLayer`) | **no** | step 4 is `ForceCleanForShutdown`, which skips it by design (`LayerStack.cpp:111-117`) |

  So a client layer can rely on it. A layer you push directly with
  [`Application::PushLayer`](#applicationpushlayer) cannot — put anything that must always happen in
  the destructor as well.
- It *is* called on both pop paths (`LayerStack.cpp:86`, `:135`) and on plugin eviction/clear
  (`WorkspaceLayer.cpp:45`, `:64`).
- **Do not call ImGui from it.** `WorkspaceLayer::OnDetach` says so in-line
  (`WorkspaceLayer.cpp:32`): during teardown the ImGui context may already be gone.
- `PlayerLayer::OnDetach` is the model for a heavy layer: release the cursor, stop the flow, stop nav
  and physics, destroy scripts, shut down the renderer's GPU subsystems, then drop the scene and
  camera (`PlayerLayer.cpp:163-179`).

### `Layer::OnUpdate`

```cpp
// Layer.h:97
virtual void OnUpdate(float deltaTime) {};
```

**What it does** — the per-frame variable pass, called exactly once per frame for every layer on the
stack, bottom to top (`Application.cpp:240-247`).

> **The argument is a plain `float`, not a `Timestep`.** `Application` unwraps its `Timestep` with
> `.GetSeconds()` before the call (`Application.cpp:246`). An override declared
> `void OnUpdate(Cosmic::Timestep ts)` does **not** override anything — it silently becomes an
> overload, and the base's empty body runs instead. Add `override` and let the compiler catch it.

**Why you'd use it** — this is where **all drawing happens** in Cosmic. There is no separate render
pass; see [`OnRender`](#layeronrender). Cameras, animation, interpolation and every draw call belong
here. Physics and control loops belong in [`OnFixedUpdate`](#layeronfixedupdate).

**Example**

```cpp
void MyLayer::OnUpdate(float dt)
{
    m_Camera.OnUpdate(dt);

    Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
    Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 1.0f, 1.0f }, m_Color);
    Cosmic::Renderer2D::EndScene();
}
```

**Notes & pitfalls**
- `deltaTime` is **already scaled** by `Application::GetTimeScale()` and is exactly `0.0f` while
  paused (`Application.cpp:238`). Do not multiply by the global scale again.
- For a layer on the `LayerStack` it is **not** scaled by that layer's own
  `Layer::GetTimeScale()` — the local scale only feeds `UpdateLayerTime`
  (`Application.cpp:243`, `:246`). For a **plugin layer** it *is*
  (`WorkspaceLayer.cpp:99`). This asymmetry is real; see
  [the hosting note](#a-plugin-layer-is-not-on-the-layerstack).
- The first frame's delta is near zero, not the boot duration — `Run()` seeds the clock immediately
  before the loop (`Application.cpp:136`).
- The delta is **not** clamped. The 0.25 s spiral-of-death clamp applies only to the fixed pass
  (`:207-213`); a 2-second hitch delivers a 2-second `dt` here.
- Never push/pop layers from inside this call: the stack is marked iterating (`:239`, `:248`) and
  the assert that would catch you is compiled out.

### `Layer::OnFixedUpdate`

```cpp
// Layer.h:98
virtual void OnFixedUpdate(float deltaFixedTime) {};
```

**What it does** — the deterministic pass. `Application` accumulates scaled frame time and drains it
in whole steps, calling this on every layer once per step
(`Application.cpp:200-230`). It may run **0 to N times in a single frame**, where N is the 0.25 s
clamp divided by `1/hz` — 15 at the default 60 Hz.

**Why you'd use it** — physics, integrators, control loops, serial polling: anything whose result
must not depend on frame rate.

**Example**

```cpp
void MySimLayer::OnFixedUpdate(float h)   // h == 1/60 s by default, every time
{
    m_State = Cosmic::Integrators::RK4(m_State, h, Derivatives);
}
```

**Notes & pitfalls**
- **The magnitude is constant**: exactly `1.0f / GetFixedTimestepHz()` (`Application.cpp:205`).
  `SetTimeScale` changes how *often* this fires, never how big the step is. That invariant is what
  keeps fixed-step integration stable.
  **Exception: a plugin layer.** `WorkspaceLayer::OnFixedUpdate` multiplies the step by the client
  layer's own `GetTimeScale()` before forwarding (`WorkspaceLayer.cpp:110-111`), so a plugin layer
  with a local scale other than `1.0f` receives a *variable-magnitude* "fixed" step. Leave the local
  scale at `1.0f` in any layer that integrates.
- **Skipped entirely while paused** (`:200`), with the accumulator frozen — no catch-up burst on
  `Resume()`.
- **Never called at all when `UseFixedTimeStep(false)`** (`:200`).
- The sign flips with a negative `TimeScale` at `:218` — but that branch is unreachable in practice,
  because a negative scale drives the accumulator away from the drain threshold. See
  [`SetTimeScale`](#applicationsettimescale).
- The stack is marked iterating for the whole drain (`:220`, `:229`).

### `Layer::OnRender`

```cpp
// Layer.h:104
virtual void OnRender() {};
```

**⚠️ Nothing calls this hook. Overriding it does nothing.**

`Application::RenderSingleFrame` has three passes — fixed, variable, ImGui — and none of them
invokes `OnRender` (`Application.cpp:173-267`). `WorkspaceLayer` forwards six hooks to its client
layer and this is not one of them. A repository-wide search for `OnRender(` finds exactly two hits
against this declaration: the declaration itself (`Layer.h:104`) and the docstring that describes
it. (`Scene::OnRender(const OrthographicCamera&)` at `Scene.h:164` is an unrelated method on a
different class — and is itself callerless; see [ecs.md](ecs.md).)

> **`Layer.h:42-43` contradicts the code.** Its numbered lifecycle list presents `OnRender()` as step
> 6, *"Dispatches traditional world-space rasterization draw commands (sprites, geometry)"*. That
> pass does not exist. **Issue draw calls from [`OnUpdate`](#layeronupdate).**

**Why the hook still exists** — removing a virtual from an exported base class is an ABI break for
every compiled project DLL. It is kept as a no-op.

### `Layer::OnImGuiRender`

```cpp
// Layer.h:105
virtual void OnImGuiRender() {};
```

**What it does** — called once per frame, for every layer bottom-to-top, **between**
`ImGuiLayer::Begin()` and `ImGuiLayer::End()` (`Application.cpp:254-261`). Inside it, an ImGui frame
is live and you may call any ImGui/ImPlot function.

**Why you'd use it** — all editor panels, HUD chrome, debug windows and pause menus.

**Example**

```cpp
void MyLayer::OnImGuiRender()
{
    ImGui::Begin("Diagnostics");
    ImGui::Text("dt: %.2f ms", m_LastDt * 1000.0f);
    ImGui::End();
}
```

**Notes & pitfalls**
- **This pass runs while paused and while `TimeScale` is 0** — that is what keeps a pause menu
  clickable.
- Do **not** call it yourself and do not call `ImGui::NewFrame`/`Render`; `ImGuiLayer` owns the
  frame.
- Docking a window belongs here too, via `WorkspaceLayer::DockWindow` — see
  [ui.md → the docking model](ui.md#the-docking-model).
- An unbalanced ImGui style/ID stack in one layer corrupts every layer after it in the same frame.

### `Layer::OnEvent`

```cpp
// Layer.h:111
virtual void OnEvent(Event& event) {};
```

**What it does** — receives every engine event not already consumed. Propagation is **top-to-bottom**
(overlays first) and stops at the first layer that sets `event.Handled`
(`Application.cpp:454-462`).

**Why you'd use it** — edge-triggered input (key press vs. key held), window resize response,
anything where "did it *just* happen" matters. Polled state goes through `Input` instead — see
[events-input.md](events-input.md).

**Example**

```cpp
void MyLayer::OnEvent(Cosmic::Event& e)
{
    Cosmic::EventDispatcher d(e);
    d.Dispatch<Cosmic::KeyPressedEvent>([this](Cosmic::KeyPressedEvent& k)
    {
        if (k.GetKeyCode() == CS_KEY_ESCAPE && k.GetRepeatCount() == 0)
        {
            Cosmic::Application::Get().TogglePause();
            return true;    // handled — stop propagation
        }
        return false;
    });
}
```

**Notes & pitfalls**
- `WindowCloseEvent` and `WindowResizeEvent` are handled by `Application` **first**
  (`:449-450`). Close is marked handled and never reaches you; resize is not, and does.
- Mouse and keyboard events are filtered by `ImGuiLayer::BlockEvents` before they reach client
  layers when ImGui wants them — see [ui.md](ui.md#imguilayerblockevents).
- **`F11` is not an event.** The fullscreen hotkey is consumed inside the GLFW key callback
  (`Window.cpp:1103-1120`) and never becomes a `KeyPressedEvent`. Register a
  [`SetFullscreenHotkeyOverride`](#windowsetfullscreenhotkeyoverride) to intercept it.
- The stack is marked iterating (`:453`, `:463`) — no pushes or pops from here.

### `Layer::GetName`

```cpp
// Layer.h:113
inline const std::string& GetName() const { return m_DebugName; };
```

**What it does** — returns the debug name given at construction, by const reference.

**Why you'd use it** — logging and editor lists. The engine uses it in its mount/evict messages
(`WorkspaceLayer.cpp:43-44`, `:52-53`, `:62-63`).

**Notes & pitfalls** — the reference is valid only as long as the layer is. There is no setter;
`m_DebugName` is `protected`, so a derived class can assign it directly if it must.

### `Layer::UpdateLayerTime`

```cpp
// Layer.h:119
inline void UpdateLayerTime(float deltaTime) { m_LocalTime += deltaTime * m_LocalTimeScale; }
```

**What it does** — advances the layer's local clock by `deltaTime × m_LocalTimeScale`.

**Why you'd use it** — you almost never call it: **the engine calls it for you**, immediately before
`OnUpdate` (`Application.cpp:243`), and `WorkspaceLayer` does the same for a hosted plugin layer
(`WorkspaceLayer.cpp:98`). It is public so a composite layer that owns sub-layers can drive their
clocks the same way.

**Notes & pitfalls** — calling it yourself inside `OnUpdate` **double-advances** the clock. If you
own child layers, you own their `UpdateLayerTime` calls and their `OnUpdate` calls, not one of each.

### `Layer::GetLocalTime` / `Layer::SetLocalTime`

```cpp
// Layer.h:121-122
inline float GetLocalTime() const { return m_LocalTime; }
inline void SetLocalTime(float time) { m_LocalTime = time; }
```

**What it does** — reads or overwrites the local clock (seconds).

**Why you'd use it** — `GetLocalTime()` is the **correct source for shader `u_Time` and particle
age**: it already carries both the global scale and the layer's own, and it freezes when the engine
pauses. `GetAbsoluteTime()` does none of that. `SetLocalTime` exists for timeline scrubbers and level
resets.

**Example**

```cpp
m_Shader->Bind();
m_Shader->SetFloat("u_Time", GetLocalTime());   // freezes on pause, follows slow-mo
```

**Notes & pitfalls** — `SetLocalTime` is an unchecked assignment; a discontinuity is visible to any
effect that differentiates the clock. Precision decays the same way `GetAbsoluteTime()`'s does, but
`SetLocalTime(0.0f)` gives you a reset that uptime does not have.

### `Layer::GetTimeScale` / `Layer::SetTimeScale`

```cpp
// Layer.h:124-125
inline float GetTimeScale() const { return m_LocalTimeScale; }
inline void SetTimeScale(float scale) { m_LocalTimeScale = scale; }
```

**What it does** — reads or sets the per-layer time multiplier (default `1.0f`). `0.0f` freezes the
layer's local clock, `0.5f` halves it, negatives run it backwards.

**Why you'd use it** — a slow-motion effect confined to one layer, or an editor previewing a
timeline at a different rate, without touching the global scale.

**Notes & pitfalls**
- **What it affects depends on how the layer is hosted, and the difference is easy to trip over:**

  | | On the `LayerStack` | Hosted as a plugin layer |
  | --- | --- | --- |
  | `GetLocalTime()` advance | scaled ✅ (`Application.cpp:243`) | scaled ✅ (`WorkspaceLayer.cpp:98`) |
  | `OnUpdate` argument | **not** scaled ❌ (`:246`) | **scaled** ✅ (`WorkspaceLayer.cpp:99`) |
  | `OnFixedUpdate` argument | **not** scaled ❌ (`:225`) | **scaled** ✅ (`WorkspaceLayer.cpp:110-111`) |

  So the same layer class behaves differently depending on where it is mounted. In a plugin layer,
  do not multiply `dt` by `GetTimeScale()` again — it has already been applied.
- Not clamped or validated.
- Setting it on a layer that integrates physics changes the *magnitude* of its fixed step when the
  layer is a plugin layer, which breaks fixed-step determinism. Leave it at `1.0f` there.

---

## `LayerStack`

```cpp
// LayerStack.h:62
class LayerStack
```

**Engine-internal, and deliberately not exported.** It is an execution router: it *borrows* raw
`Layer*` pointers to fix update, render and event order and **owns none of them** (`LayerStack.h:9-15`).
`Application` owns the memory.

It has **no manifest row** by design — the coverage checker classifies `core/LayerStack.h` as engine
plumbing with the justification *"Application owns it; PushLayer/PushOverlay are the client verbs"*
(`tests/check_docs_coverage.ps1:75`). It is documented here so that the `LNK2019` you get from
touching it is explicable, and so the ordering rules have one home.

**Ordering, exactly:**

| Operation | Where it lands | Iteration that sees it first |
| --- | --- | --- |
| `PushLayer` | inserted at `m_LayerInsertIndex`, i.e. after the last layer and **before every overlay** (`LayerStack.cpp:48-49`) | render/update (`begin`→`end`) |
| `PushOverlay` | `emplace_back` — the very end (`:65`) | events (`rbegin`→`rend`) |

`Application` iterates `begin()`→`end()` for update, fixed-update and ImGui (bottom to top) and
`rbegin()`→`rend()` for events (top to bottom) — so **overlays draw last and hear input first**.

**Members** (`LayerStack.h:65-96`): `LayerStack()`, `~LayerStack()`, `PushLayer(Layer*)`,
`PushOverlay(Layer*)`, `PopLayer(Layer*)`, `PopOverlay(Layer*)`, `Clear()`,
`ForceCleanForShutdown()`, `SetIterating(bool)`, and `begin`/`end`/`rbegin`/`rend`.

**Notes & pitfalls**
- **Not exported.** Every member except the inline `SetIterating` and the four iterator accessors is
  defined in `LayerStack.cpp` and will not link from a project DLL. Use
  [`Application::PushLayer`](#applicationpushlayer)/[`PushOverlay`](#applicationpushoverlay).
- `~LayerStack` clears the vector and calls **neither** `OnDetach` nor `delete` (`:23-28`).
- `PopLayer`/`PopOverlay` call `OnDetach()` but never `delete`. They search only their own half of
  the vector (`:82`, `:131`) and silently do nothing when the pointer is not found — pass a layer to
  the wrong one and it stays attached with no diagnostic.
- `ForceCleanForShutdown()` is a raw wipe that skips `OnDetach` **on purpose**, for
  `Application::Shutdown` only (`:111-117`).
- `Clear()` is the safe public path and expects the stack to already be empty — enforced by a
  `CS_CORE_ASSERT` that is **compiled out in every configuration** (`:103-105`). Calling it with
  layers still attached leaks them silently.
- `SetIterating(true)` is the guard `Application` raises around every walk (`Application.cpp:220`,
  `:239`, `:255`, `:453`). The `CS_CORE_ASSERT(!m_Iterating, …)` calls in all four push/pop verbs
  are likewise compiled out — **the guard is documentation, not enforcement.** Mutating the stack
  mid-iteration is straightforward undefined behaviour. Defer to the Safe Zone.

---

## A plugin layer is not on the `LayerStack`

The single most surprising structural fact in this chapter, and worth stating once in its own
section because four entries above refer to it.

When a project DLL is loaded, `Application::LoadProjectDLL` calls `CreatePluginLayer()` and then
**mounts** the returned layer on the workspace shell — it does **not** push it:

```cpp
// Application.cpp:721, :744
m_ActivePluginLayer = createPluginLayer();
…
m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer);
```

`WorkspaceLayer` then forwards each hook **by hand**, and adds behaviour of its own:

| Hook | Forwarding site | What it adds |
| --- | --- | --- |
| `OnAttach` | `WorkspaceLayer.cpp:54` | evicts (and `OnDetach`es) any previous client layer first (`:41-46`) |
| `OnDetach` | `:45`, `:64` | — |
| `OnUpdate` | `:96-100` | binds the shared `FrameBuffer`, clears it, sets the GL + `Renderer2D` viewport to the panel size, **calls `UpdateLayerTime(ts)` and then `OnUpdate(ts × layer's own TimeScale)`**, unbinds |
| `OnFixedUpdate` | `:106-113` | multiplies the step by the layer's own `TimeScale` |
| `OnImGuiRender` | `:256-259` | runs after the dockspace and Viewport panel exist for the frame |
| `OnEvent` | `:522-527` | early-outs when `e.Handled` is already set |
| `OnRender` | — | not forwarded (nothing calls it anyway) |

**Consequences you can feel:**

- Your world rendering lands in the **offscreen `FrameBuffer`**, displayed as `ImGui::Image` in the
  Viewport panel (`:221-224`), not directly on the window.
- Your layer's `Layer::SetTimeScale` **does** affect the `dt` you receive here, unlike a stacked
  layer. See [the table](#layergettimescale--layersettimescale).
- Only **one** client layer can be mounted; mounting a second evicts the first with a
  `CS_CORE_WARN` (`:43-44`).
- Your layer is deleted by `Application::UnloadProjectDLL` (`Application.cpp:791`), not by the
  `LayerStack`.
- `WorkspaceLayer` itself *is* on the stack (`Application.cpp:351`), so the engine's own passes reach
  you exactly once, through it.

This is true for `PlayerLayer` too: a packaged app still boots a `WorkspaceLayer` and mounts the
player inside it — which is why `PlayerLayer::OnAttach` reaches for
`Application::Get().GetWorkspaceLayer()` at `PlayerLayer.cpp:97`.

Diagram: **DG-5**,
[`../guide/project-anatomy.md#dg-5--the-plugin-dll-lifecycle`](../guide/project-anatomy.md#dg-5--the-plugin-dll-lifecycle).

---

## `PlayerLayer`

```cpp
// PlayerLayer.h:46
class COSMIC_API PlayerLayer : public Layer
```

The engine-generic **standalone scene player**: it runs a Starforge-made project with no editor
anywhere. Declared in `Cosmic/src/layers/PlayerLayer.h`, included by `Cosmic.h:183` (both
configurations). It is what `CS_MODULE_END` returns from a scaffolded project's `CreatePluginLayer`
(`ModuleMacros.h:99`), so shipping a project is free: the *same* DLL the editor hot-reloads is the
one the player runs.

**What it does per frame**, in order: reads the project manifest, loads the startup scene (or screen
flow), instantiates and ticks the scene's native scripts, drives physics and navigation from the
fixed pass, renders through the engine `SceneRenderer` — the **same** env/sky/shadow/HDR/post path
as the editor viewport, which is why a packaged app matches the editor — and offers an Escape pause
menu.

**Ownership** — it owns a `SceneManager`, a `ScriptHost`, a `SceneRenderer`, a `PhysicsWorld`, a
`FlowMachine` and its own camera (`PlayerLayer.h:70-80`). All of them are released in `OnDetach`
(`PlayerLayer.cpp:163-179`).

**Public surface** — the constructor, the destructor, and six `Layer` overrides. Everything else is
private.

### `PlayerLayer::PlayerLayer`

```cpp
// PlayerLayer.h:51
explicit PlayerLayer(const std::string& projectName = "");
```

**What it does** — records the VFS project folder name (`assets/projects/<name>`) and nothing else;
all real work is deferred to `OnAttach`. The base `Layer` is constructed with the debug name
`"PlayerLayer"` (`PlayerLayer.cpp:47`).

**Why you'd use it** — you return one from `CreatePluginLayer`. `CS_MODULE_END` does it for you with
the module name (`ModuleMacros.h:99`); write it by hand only if you are not using the macro DSL.

**Example**

```cpp
// Equivalent to what CS_MODULE_END generates (ModuleMacros.h:95-100)
extern "C" __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
{
    return new Cosmic::PlayerLayer("MyGame");
}
```

**Notes & pitfalls** — an empty `projectName` **leaves the active VFS project as-is**
(`PlayerLayer.cpp:55-56`), which is correct when the host already mounted it —
`Application::LoadProjectDLL` sets the engine-side active project from the DLL stem *before* mounting
(`Application.cpp:739-740`). Passing a name that does not match a folder under `assets/projects/`
resolves every `project://` path against a directory that does not exist; the startup-scene load
then fails with a logged error and you get an empty scene.

### `PlayerLayer::~PlayerLayer`

```cpp
// PlayerLayer.h:52
~PlayerLayer() override;
```

**What it does** — `= default` (`PlayerLayer.cpp:51`); the members release themselves. The ordered
teardown that matters is in `OnDetach`.

**Notes & pitfalls** — because the destructor is trivial, **`OnDetach` is load-bearing** for this
class: it is the only place physics, navigation, the scripts and the `SceneRenderer`'s GPU
subsystems are shut down. That is safe, because every path that removes a plugin layer runs
`OnDetach` first — `Application::UnloadProjectDLL` → `WorkspaceLayer::ClearViewportLayer`
(`Application.cpp:785`, `WorkspaceLayer.cpp:64`) — and `Application::Shutdown`'s **step 1** is
`UnloadProjectDLL` (`Application.cpp:388`). Copy the pattern only if your layer is mounted the same
way; a layer pushed with `Application::PushLayer` does **not** get `OnDetach` at shutdown.

### `PlayerLayer::OnAttach`

```cpp
// PlayerLayer.h:54
void OnAttach() override;
```

**What it does** — the boot sequence (`PlayerLayer.cpp:53-149`):

1. `FileSystem::SetActiveProject(projectName)` when non-empty (`:55-56`).
2. Load `project://project.cproj` via `Config::Load` (`:66`). Every key is optional:

   | Key | Effect | Default |
   | --- | --- | --- |
   | `startup_scene` | scene to load | `scenes/Main.cscene` (`PlayerLayer.h:69`) |
   | `startup_flow` | screen flow that owns scene selection instead | none |
   | `fixed_dt_hz` | `Application::SetFixedTimestepHz` | 60 |
   | `icon` | runtime window/taskbar icon (project-relative) | none |
   | `window.title` → `window_title` → `name` | OS window title | project name, else `"Cosmic Player"` |
   | `window.width` / `window.height` | `Window::SetSize` when both > 0 | keep current |
   | `pixel_art` | `AssetLibrary::SetDefaultTextureSampling(Nearest, ClampToEdge)` | false |
   | `capture_cursor` | capture the cursor at boot | false |

3. Apply the window identity: `SetTitle`, optional `SetSize`, and `WorkspaceLayer::SetProjectName`
   when a workspace exists (`:94-98`).
4. Re-resolve the app icon now that the project is mounted (`:104-110`).
5. `m_Physics.Init()` (`:112`).
6. Load the startup **flow** if named — it then owns scene selection — otherwise the single startup
   scene (`:117-144`).
7. Log a summary line (`:146-148`).

**Notes & pitfalls**
- **A missing `project.cproj` is not an error.** `Config::Load` returning null skips the whole block
  and every default above applies (`:66`).
- **A missing startup scene is not fatal either**: it logs `CS_CORE_ERROR("PlayerLayer: could not
  load startup scene …")` (`:141-142`) and the layer runs with no scene — a black viewport, no
  crash.
- `SetFixedTimestepHz` is called **unconditionally**, with 60 when the manifest is absent (`:89`).
  This overwrites whatever the host had set.
- The `[window]` keys fall back to the legacy flat `window_title` / `name` keys (`:73`).
- `SetSize` is ignored while fullscreen and clamps absurd values — see
  [`Window::SetSize`](#windowsetsize).

### `PlayerLayer::OnUpdate`

```cpp
// PlayerLayer.h:56
void OnUpdate(float ts) override;
```

**What it does** (`PlayerLayer.cpp:208-268`), in order: cursor-capture lifecycle (Escape releases, a
click recaptures) when `capture_cursor` is on; advance any queued scene transition and rebind scripts
on a swap; **UI pointer interaction** (skipped while paused); **screen-flow** advance, which may
request a return to the Launcher or swap the active scene; then, **only when not paused**, script
`Tick`, sprite-flipbook advance and (3D builds only) skeletal animators; finally `RenderScene(ts)`.

**Notes & pitfalls**
- **Rendering happens here**, at the end — consistent with the engine having no `OnRender` pass.
- Almost everything is gated on `Application::IsPaused()` (`:231`, `:254`), so pause freezes the sim
  while the frame keeps drawing.
- When a flow is active it **owns Escape** (`:237-239`) and the built-in ImGui pause menu stands
  down (`:402`).
- `UpdateAnimators` is fenced out of the 2D build (`:260-263`); sprite animation is not.
- If the flow requests a quit the method **returns early** (`:242-246`) — nothing renders that frame.

### `PlayerLayer::OnFixedUpdate`

```cpp
// PlayerLayer.h:57
void OnFixedUpdate(float fixedDt) override;
```

**What it does** — the J4 tick-order contract, with **no pause guard of its own** because
`Application` skips the whole pass while paused (`PlayerLayer.cpp:294-308`):

1. `ScriptHost::FixedTick(fixedDt)`
2. `Scene::OnPhysicsStep(fixedDt)`
3. `Scene::OnNavStep(fixedDt)` — **3D builds only** (`:303-305`)
4. `Scene::DispatchPhysicsEvents(m_Scripts)`

**Notes & pitfalls** — collision callbacks reach your scripts in step 4, i.e. **after** the physics
step that produced them, never during it. Navigation advances **after** physics, on purpose. Details
in [physics.md](physics.md).

### `PlayerLayer::OnImGuiRender`

```cpp
// PlayerLayer.h:58
void OnImGuiRender() override;
```

**What it does** — toggles pause on an Escape *press* when no flow is active (`:402-403`), then, when
paused, draws a centered modal-looking "Paused" window with **Resume** and **Quit to Launcher**
(`:405-425`). "Quit to Launcher" resumes first, then calls `Application::TransitionToLauncher()`.

**Notes & pitfalls** — the pause menu is deliberately minimal and is **not** themed or skinnable.
A project that wants its own pause UI should ship a `startup_flow`, which suppresses this one
entirely.

### `PlayerLayer::OnEvent`

```cpp
// PlayerLayer.h:59
void OnEvent(Event& e) override;
```

**What it does** — forwards the event to `ScriptHost::DispatchEvent` **only when not paused**
(`PlayerLayer.cpp:428-432`).

**Notes & pitfalls** — this is why gameplay scripts go quiet the instant you pause while the pause UI
stays live. It never marks an event handled, so events continue propagating down the stack.

## `Window`

```cpp
// Window.h:126
class COSMIC_API Window
```

The single OS window and OpenGL context, declared in `Cosmic/src/core/Window.h`. It is
`Scope`-owned by `Application` (`Application.h:174`) and reached only through
[`Application::GetWindow()`](#applicationgetwindow) — you never construct one.

**Not in `Cosmic.h`.** The header is pulled in transitively by `core/Application.h:28`, and the
coverage manifest lists it under the *"Not in `Cosmic.h` but client-reachable, documented anyway"*
footnote.

**Non-copyable** (`Window.h:139-140`), by deleted copy constructor and assignment.

**Architectural constraint: the engine is single-window.** `~Window` calls `glfwTerminate()`
(`Window.cpp:570`), which is only safe because there is exactly one. The comment at
`Window.cpp:565-569` records what would have to move if a second window were ever added.

**On Windows the window is borderless with custom chrome by default** — the constructor creates it
with `GLFW_DECORATED = FALSE` (`Window.cpp:349`) and then calls `SetCustomChrome(true)`
(`:487`), which re-adds the *behavioural* Win32 style bits (native resize, Aero Snap, min/max
animations, DWM drop shadow) while the visual frame stays removed. The app draws its own title bar.
The workflow half of this — writing that title bar, high-DPI, the three rectangles — is
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md).

**Two environment variables affect it, read once in the constructor:**

| Variable | Effect | Source |
| --- | --- | --- |
| `COSMIC_WINDOW_TRACE=1` | enables the W1 window trace (equivalent to `SetTraceEnabled(true)`) | `Window.cpp:288-300` |
| `COSMIC_FULLSCREEN_COMPAT=exact\|oversize` | overrides the fullscreen sizing strategy for the run | `Window.cpp:304-318` |

### Free types in `core/Window.h`

```cpp
// Window.h:101
using FullscreenToggleActionFn = std::function<bool(int key, int action, int mods)>;

// Window.h:124
using TitlebarHitTestFn = std::function<bool(int x, int y)>;

// Window.h:115-119
enum class FullscreenCompatMode
{
    ExactCover,     // exact monitor cover (A/B control case)
    OversizeByOne,  // monitor height + 1 px — defeats iFlip promotion (default)
};

// Window.h:129
using EventCallbackFn = std::function<void(Event&)>;   // nested in Window

// Window.h:196
static constexpr uintptr_t kModalFrameTimerId = 0xC05;
```

- **`FullscreenToggleActionFn`** — raw GLFW `key`/`action`/`mods`; return `true` to consume. See
  [`SetFullscreenHotkeyOverride`](#windowsetfullscreenhotkeyoverride).
- **`TitlebarHitTestFn`** — a point in **client** pixels; return `true` where the cursor should drag
  the window. See [`SetTitlebarHitTestCallback`](#windowsettitlebarhittestcallback).
- **`FullscreenCompatMode`** — `OversizeByOne` is the default and makes the borderless cover rect one
  pixel taller than the monitor, so Windows never classifies the window as fullscreen and never
  promotes it to DWM independent flip. The promotion's forced demotion (when a capture overlay like
  Win+Shift+S appears) black-flashes the legacy GL present path. The reasoning is recorded at
  `Window.h:103-114`; the real fix, a DXGI flip-model swapchain, is out of scope.
- **`kModalFrameTimerId`** — the `WM_TIMER` id used by the modal frame pump. Exposed so the
  file-local `WndProc` can match it; do not create a timer with the same id on this window.

### `Window::PollEvents`

```cpp
// Window.h:146
void PollEvents();
```

**What it does** — `glfwPollEvents()` (`Window.cpp:577`). Every OS message is dispatched here, which
means every engine `Event` originates inside this call.

**Notes & pitfalls** — called by `Application::Run` once per iteration (`Application.cpp:140`) and by
nothing else. It **blocks for the whole duration of a Win32 modal move/size loop** — which is the
entire reason the modal frame pump exists (see
[`Application::SetRenderWhileDragging`](#applicationsetrenderwhiledragging--isrenderwhiledragging)).
Do not call it yourself; a nested poll re-enters the event callback.

### `Window::SwapBuffers`

```cpp
// Window.h:147
void SwapBuffers();
```

**What it does** — presents the back buffer through the graphics context (`Window.cpp:579-597`).
When the W1 trace is enabled it times the present and logs any swap over 25 ms as a possible
occlusion throttle (`:586-594`).

**Notes & pitfalls** — called once per frame at the end of `RenderSingleFrame`
(`Application.cpp:263`). The `CS_CORE_ASSERT(m_Context, …)` at `Window.cpp:581` is compiled out; a
null context here would crash. Not something a client can reach.

### `Window::GetWidth` / `Window::GetHeight`

```cpp
// Window.h:153-154
inline unsigned int GetWidth()  const { return m_Data.Width; }
inline unsigned int GetHeight() const { return m_Data.Height; }
```

**What it does** — returns the **cached** client-area size, updated by GLFW's window-size callback
(`Window.cpp:391-399`), which is also where `WindowResizeEvent` is fired.

**Why you'd use it** — window-level layout decisions. For anything that must match the GL backbuffer
use [`GetSize`](#windowgetsize); for anything that must match what the camera renders into, use
`Application::GetFrameBuffer()->GetWidth()/GetHeight()` instead — in an editor host the render target
is the *viewport panel*, which is much smaller than the window.

**Notes & pitfalls** — this is the **window**, not the framebuffer and not the viewport. Mixing the
three is the classic Cosmic aspect-ratio bug; the guide names them explicitly in
[`../guide/windowing-and-viewport.md#the-three-rectangles`](../guide/windowing-and-viewport.md#the-three-rectangles).

### `Window::GetSize`

```cpp
// Window.h:286
void GetSize(int* width, int* height)       const;
```

**What it does** — queries `glfwGetFramebufferSize` **directly** (`Window.cpp:600-603`) — no cache.

**Why you'd use it** — matching an FBO or a `glViewport` to the real backbuffer.
`Application::SynchronizeRenderingState` uses exactly this to drive a synthetic
`WindowResizeEvent` after a layout change (`Application.cpp:824-833`).

**Notes & pitfalls** — **no null check on either pointer and none on `m_Handle`.** GLFW tolerates a
null out-parameter, but a null window handle (creation failed) does not survive. On Windows the
framebuffer size and the client size coincide, so this normally agrees with `GetWidth`/`GetHeight`;
the distinction is kept because GLFW does not guarantee it.

### `Window::GetHandle`

```cpp
// Window.h:155
inline GLFWwindow* GetHandle() const { return m_Handle; }
```

**What it does** — returns the raw GLFW window pointer, for API-specific work the wrapper does not
expose.

**Notes & pitfalls** — **can be `nullptr`** if GLFW or window creation failed (`Window.cpp:321-324`,
`:355-361`). Every internal user null-checks it; so should you. Using it to install your own GLFW
callback **replaces** the engine's and silently kills the corresponding events.

### `Window::SetEventCallback`

```cpp
// Window.h:161
void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
```

**What it does** — installs the single sink that receives every translated event.

**Notes & pitfalls** — **`Application` owns this.** It installs
`[this](Event& e) { OnEvent(e); }` at `Application.cpp:551`; overwriting it detaches the entire
engine from input. The constructor pre-installs a no-op (`Window.cpp:376`) so callbacks that fire
*during* construction — enabling custom chrome triggers a `WM_SIZE` — never invoke an empty
`std::function`. To receive events, override [`Layer::OnEvent`](#layeronevent).

### `Window::SetVSync` / `Window::IsVSync`

```cpp
// Window.h:162-163
void SetVSync(bool enabled);
bool IsVSync() const { return m_Data.VSync; }
```

**What it does** — `glfwSwapInterval(enabled ? 1 : 0)` and caches the flag (`Window.cpp:609-613`).
`Application::Initialize` enables it at boot (`Application.cpp:566`).

**Why you'd use it** — turn it off to measure raw frame cost, or on a machine where the compositor
already paces you.

**Notes & pitfalls**
- The cached flag is **re-applied across every fullscreen transition** (`Window.cpp:1004`, and
  `:979` on the non-Windows path) so the setting survives F11.
- Swap interval is a driver hint. A driver forcing vsync on, or a compositor pacing the present,
  will ignore it — `IsVSync()` reports what you asked for, not what happened.
- Turning it off uncaps the frame rate and therefore shrinks the variable `dt`; anything
  frame-rate-dependent you wrote will change behaviour.

### `Window::SetFullscreen` / `Window::IsFullscreen`

```cpp
// Window.h:169-170
void SetFullscreen(bool enabled);
bool IsFullscreen() const { return m_Fullscreen; }
```

**What it does** — toggles **borderless-windowed** fullscreen on the monitor containing the window
centre. No display-mode switch occurs. Entering strips `WS_OVERLAPPEDWINDOW` (which is what makes
the shell hide the taskbar) and covers the monitor; exiting restores the saved rect — and
re-maximizes if the window was maximized when it entered (`Window.cpp:828-842`, `:861`+).

Immediately after applying the style change it calls `ModalFrameTick()` (`:841`), rendering and
presenting **one frame inside the same call**, so the transition never shows a stale or black frame.

**Why you'd use it** — a "Fullscreen" menu item, or your own hotkey. The engine already binds `F11`
by default.

**Example**

```cpp
auto& win = Cosmic::Application::Get().GetWindow();
win.SetFullscreen(!win.IsFullscreen());
```

**Notes & pitfalls**
- **Early-outs when the state already matches, or when there is no handle** (`:830-831`) — so it is
  idempotent and safe to call blindly.
- **It re-enters your rendering code.** `ModalFrameTick` → `Application::RenderSingleFrame` runs
  every layer's `OnUpdate`/`OnImGuiRender` *before this call returns*. Calling `SetFullscreen` from
  inside `OnUpdate` is safe only because `RenderSingleFrame` has a re-entrancy guard
  (`Application.cpp:178-180`) — the nested frame is skipped, not run twice.
- The whole path is Windows-specific; the `#else` branch uses `glfwSetWindowMonitor`, which *does*
  mode-switch (`Window.cpp:970-999`). Nothing ships on that branch today.
- The cover rect is one pixel taller than the monitor by default — see
  [`SetFullscreenCompatMode`](#windowsetfullscreencompatmode--getfullscreencompatmode).

<a id="windowsetfullscreencompatmode--getfullscreencompatmode"></a>
### `Window::SetFullscreenCompatMode` / `Window::GetFullscreenCompatMode`

```cpp
// Window.h:174-175
void                 SetFullscreenCompatMode(FullscreenCompatMode mode);
FullscreenCompatMode GetFullscreenCompatMode() const { return m_FullscreenCompatMode; }
```

**What it does** — selects `ExactCover` or `OversizeByOne` (default). Takes effect on the next
fullscreen enter, and is **re-applied live** if you are already fullscreen — the reassert is followed
by another `ModalFrameTick()` so the change is visible immediately (`Window.cpp:844-859`).

**Why you'd use it** — A/B debugging a present/flicker problem. It is a diagnostic knob, not a
feature; prefer the `COSMIC_FULLSCREEN_COMPAT` environment variable for a one-off run.

**Notes & pitfalls** — early-outs when the mode is unchanged (`:846-847`), and logs the new mode at
INFO. `ExactCover` re-enables the DWM independent-flip promotion the default exists to defeat;
expect black flashes when a capture overlay appears.

### `Window::SetTraceEnabled` / `Window::IsTraceEnabled`

```cpp
// Window.h:185-186
static void SetTraceEnabled(bool enabled) { s_TraceEnabled = enabled; }
static bool IsTraceEnabled()              { return s_TraceEnabled; }
```

**What it does** — toggles the W1 window trace: timestamped `CS_CORE_TRACE` lines for
`WM_WINDOWPOSCHANGED`, `WM_SIZE`, `WM_DPICHANGED`, focus changes, `WM_SYSCOMMAND`, fullscreen
transitions, style changes and slow `SwapBuffers`. **Off by default** (`Window.cpp:279`).

**Why you'd use it** — diagnosing a window/DPI/present problem. It is the first thing to turn on
before filing one.

**Notes & pitfalls** — `static`, so it is **process-wide**, not per-window. Also settable without a
code change via `COSMIC_WINDOW_TRACE=1`, read once at first construction (`Window.cpp:288-300`).
Tracing is chatty; leave it off in a shipped app.

### `Window::SetModalFrameCallback`

```cpp
// Window.h:198
void SetModalFrameCallback(const std::function<void()>& cb) { m_ModalFrameCallback = cb; }
```

**What it does** — installs the function the modal pump (and every fullscreen transition) calls to
render one frame.

**Notes & pitfalls** — **`Application` owns this**, installing `[this] { RenderSingleFrame(); }` at
`Application.cpp:611`. Overwriting it breaks responsive drag/resize and the paint-through-transition.
`~Window` clears it first thing (`Window.cpp:520`) so no frame can fire into a dying `Application`.

### `Window::SetModalRenderingEnabled` / `Window::IsModalRenderingEnabled`

```cpp
// Window.h:199-200
void SetModalRenderingEnabled(bool enabled);
bool IsModalRenderingEnabled() const { return m_ModalRenderingEnabled; }
```

**What it does** — enables/disables the modal frame pump. Disabling it **also stops an in-progress
pump immediately** (`Window.cpp:777-782`). Default **on** (`Window.h:349`).

**Notes & pitfalls** — prefer
[`Application::SetRenderWhileDragging`](#applicationsetrenderwhiledragging--isrenderwhiledragging),
which is the same switch with a null-safe wrapper and is the documented client-facing name.

### `Window::BeginModalFramePump` / `EndModalFramePump` / `ModalFrameTick`

```cpp
// Window.h:204-206
void BeginModalFramePump();
void EndModalFramePump();
void ModalFrameTick();
```

**What they do** — `Begin` sets a `WM_TIMER` (id `kModalFrameTimerId`, `USER_TIMER_MINIMUM` ≈ 10 ms,
floored near 15 ms by the scheduler) on `WM_ENTERSIZEMOVE`; `End` kills it; `ModalFrameTick` invokes
the callback once (`Window.cpp:784-822`).

**Notes & pitfalls** — **implementation detail, public only so the file-local Win32 `WndProc` can
reach them without exposing Win32 types in the header** (`Window.h:202-203`). `Begin` no-ops when
modal rendering is disabled or a pump is already running. `ModalFrameTick` is also the
paint-through-transition mechanism, called from `SetFullscreen` (`:841`) and
`SetFullscreenCompatMode` (`:857`). Do not call these.

### `Window::ReassertFullscreenCoverWin32`

```cpp
// Window.h:209
void ReassertFullscreenCoverWin32();
```

**What it does** — re-issues the fullscreen cover `SetWindowPos` after a display or DPI change
(W5.3). **No-op when not fullscreen** (`Window.cpp:1013`+).

**Notes & pitfalls** — driven by the `WndProc` on `WM_DISPLAYCHANGE`/`WM_DPICHANGED`; public for the
same Win32-isolation reason as the pump verbs. You should not need it.

### `Window::Minimize` / `Maximize` / `Restore` / `ToggleMaximize`

```cpp
// Window.h:215-218
void Minimize();
void Maximize();
void Restore();
void ToggleMaximize();
```

**What they do** — thin, null-safe wrappers over `glfwIconifyWindow`, `glfwMaximizeWindow` and
`glfwRestoreWindow` (`Window.cpp:619-621`); `ToggleMaximize` picks between the last two based on
[`IsWindowMaximized()`](#windowiswindowmaximized) (`:695-699`).

**Why you'd use it** — wiring a custom title bar's caption buttons.

**Notes & pitfalls** — minimizing fires a `WindowResizeEvent` with zero dimensions, which is how
`Application` learns it is minimized (`Application.cpp:650-656`). Whether that skips frames depends
on [`SetPauseOnMinimize`](#applicationsetpauseonminimize--getpauseonminimize) — **default off**.
None of these interacts with fullscreen state; maximizing while fullscreen is meaningless.

### `Window::IsWindowMaximized`

```cpp
// Window.h:243
bool IsWindowMaximized() const;
```

**What it does** — queries `GLFW_MAXIMIZED` live (`Window.cpp:690-693`); `false` when there is no
handle.

**Notes & pitfalls** — **named `IsWindowMaximized`, not `IsMaximized`, on purpose**: `<windows.h>`
defines `IsMaximized` as a macro aliasing `IsZoomed`, which would rewrite the call site
(`Window.h:241-242`).

### `Window::Close`

```cpp
// Window.h:244
void Close();   // request the window to close (quits the app)
```

**What it does** — sets GLFW's should-close flag (`Window.cpp:622`). `Application::Run`'s loop
condition tests it (`Application.cpp:138`), so the current frame finishes and then `Run()` returns.

**Notes & pitfalls** — equivalent in effect to [`Application::Close()`](#applicationclose); use
whichever is in reach. Neither is vetoable and neither is instant. Note that setting the flag does
**not** fire a `WindowCloseEvent` — that arrives from the OS close button through
`glfwSetWindowCloseCallback` (`Window.cpp:401-407`).

### `Window::SetTitle`

```cpp
// Window.h:223
void SetTitle(const std::string& title);
```

**What it does** — sets the OS/taskbar window title. **Idempotent**: it early-outs when the title is
unchanged, so callers may sync it every frame (`Window.cpp:624-630`).

**Why you'd use it** — a shipped app opens under its own name rather than "Cosmic Engine".
`PlayerLayer` calls it on attach from the manifest (`PlayerLayer.cpp:94`).

**Notes & pitfalls** — with borderless custom chrome on, this is the **OS-level** title (taskbar,
Alt+Tab). The name drawn in the custom title bar is `WorkspaceLayer::SetProjectName` — a different
string, which is why `PlayerLayer` sets both (`PlayerLayer.cpp:94`, `:98`). The engine boots the
window as `"Cosmic Engine"` at 1280×720 (`Application.h:208-210`).

### `Window::SetSize`

```cpp
// Window.h:224
void SetSize(int width, int height);
```

**What it does** — resizes the window, and lets GLFW's resize event re-sync the framebuffer and
viewport as usual.

**Notes & pitfalls** — **three silent no-ops** (`Window.cpp:632-640`): no handle; **while
fullscreen** (the cover rect owns the size); and out of the range `[64, 16384]` in either dimension.
There is no return value and nothing is logged, so a rejected call is invisible. `PlayerLayer`
applies it only when both manifest values are positive (`PlayerLayer.cpp:95-96`).

### `Window::SetIcon`

```cpp
// Window.h:232
bool SetIcon(const std::string& imagePath);
```

**What it does** — decodes an image via `utils/ImageIO` (PNG/JPG/BMP/TGA), rescales it to the
standard **16 / 32 / 48 / 256 px** levels and applies them to the live window and taskbar
(`Window.cpp:642-671`). Returns `true` on success.

**Why you'd use it** — the K1 "drop a file" branding convention: put an icon next to the exe and the
app picks it up. `Application::Initialize` resolves one at boot (`Application.cpp:558-562`) and
`PlayerLayer::OnAttach` re-resolves it with the project manifest's `icon` key once the project is
mounted (`PlayerLayer.cpp:104-110`).

**Failure mode — the useful part:** on a decode failure it returns `false` and **keeps the current
icon** (`Window.cpp:652-653`). This is deliberate: a hot-swap can catch a half-written file, and a
half-written file must not blank the brand. It also returns `false` for an empty path or a null
handle, with no logging in any case.

**Example**

```cpp
auto& win = Cosmic::Application::Get().GetWindow();
if (!win.SetIcon(Cosmic::FileSystem::Resolve("project://icon.png")))
    CS_WARN("icon decode failed — keeping the previous icon");
```

**Notes & pitfalls** — the four levels are always generated, whatever the source resolution;
supply at least 256×256 to avoid an upscale. The rescale is `ImageIO::ResizeRgba`, not a filtered
mip chain. See [assets-io.md](assets-io.md) *(skeleton — D16)* for `ImageIO` itself.

### `Window::ClearIcon`

```cpp
// Window.h:233
void ClearIcon();
```

**What it does** — `glfwSetWindowIcon(handle, 0, nullptr)`, restoring the platform default — the
exe's embedded icon, or the GLFW logo (`Window.cpp:673-677`).

### `Window::SetCursorCaptured` / `Window::IsCursorCaptured`

```cpp
// Window.h:238-239
void SetCursorCaptured(bool captured);
bool IsCursorCaptured() const { return m_CursorCaptured; }
```

**What it does** — enters or leaves GLFW's **disabled-cursor** mode: the cursor is hidden, locked to
the window, and mouse deltas keep flowing with no edge clamping. **Idempotent**, and early-outs when
the state already matches (`Window.cpp:679-688`).

**Why you'd use it** — first-person mouse-look. `PlayerLayer` drives the whole lifecycle from the
manifest's `capture_cursor` key: captured at boot, **Escape releases**, a left-click inside the
window recaptures (`PlayerLayer.cpp:215-223`).

**Example**

```cpp
auto& win = Cosmic::Application::Get().GetWindow();
if (Cosmic::Input::IsKeyPressed(CS_KEY_ESCAPE))
    win.SetCursorCaptured(false);
```

**Notes & pitfalls**
- **Always release it on teardown.** `PlayerLayer::OnDetach` calls `SetCursorCaptured(false)` with
  the comment *"never leak capture"* (`PlayerLayer.cpp:165`) — a captured cursor that outlives your
  layer leaves the user unable to click anything.
- Hosts release on Escape **by convention**, not by engine enforcement.
- While captured, ImGui receives a locked cursor position; do not capture while an ImGui panel needs
  the mouse.

### `Window::SetCustomChrome` / `Window::HasCustomChrome`

```cpp
// Window.h:256-257
void SetCustomChrome(bool enabled);
bool HasCustomChrome() const { return m_CustomChrome; }
```

**What it does** — installs or removes the borderless `WndProc` subclass. Enabling stores a back
pointer on the HWND, swaps in `CosmicWndProc`, re-adds `WS_OVERLAPPEDWINDOW` for behaviour,
`DwmExtendFrameIntoClientArea`s a 1 px margin for the drop shadow, and forces a frame recompute
(`Window.cpp:716-750`). **On by default on Windows** (`:487`); `SetCustomChrome(false)` restores the
standard OS frame.

**Notes & pitfalls**
- **Windows-only.** The non-Windows branch is `(void)enabled;` (`:711-713`), so `HasCustomChrome()`
  stays false there.
- Early-outs when the state already matches (`:708`).
- With chrome on you **must** supply a draggable region via
  [`SetTitlebarHitTestCallback`](#windowsettitlebarhittestcallback) or the window cannot be dragged
  at all.
- `~Window` disables it before destroying the window so the original GLFW `WndProc` is restored
  (`Window.cpp:544-547`).

### `Window::SetTitlebarHitTestCallback`

```cpp
// Window.h:259-260
void SetTitlebarHitTestCallback(const TitlebarHitTestFn& fn) { m_TitlebarHit = fn; }
void ClearTitlebarHitTestCallback() { m_TitlebarHit = nullptr; }
```

**What it does** — registers the predicate the borderless hit-test asks: *given this point in client
pixels, should the cursor drag the window?* Return `true` over your custom title bar and `false`
over its buttons and menus.

**Why you'd use it** — you drew your own title bar and want it to behave like one.

**Example**

```cpp
// WorkspaceLayer.cpp:24-25 — the engine's own registration; the flag is recomputed
// each frame while drawing the bar (one frame of lag is fine for dragging).
Cosmic::Application::Get().GetWindow().SetTitlebarHitTestCallback(
    [this](int, int) { return m_TitlebarDrag; });
```

**Notes & pitfalls**
- **Clear it before your layer dies.** `WorkspaceLayer::OnDetach` does
  (`WorkspaceLayer.cpp:30`); a stale `std::function` capturing a destroyed `this` is called from the
  `WndProc`.
- One callback, last writer wins — there is no chain.
- The predicate is called from the Win32 message loop, so it must be cheap and must not call ImGui.
- Coordinates are **client** pixels, unrelated to the ImGui screen space
  [`GetViewportPos`](#applicationgetviewportpos) uses.

### `Window::TitlebarHitTest` / `Window::NativeOrigWndProc`

```cpp
// Window.h:264-265
bool      TitlebarHitTest(int x, int y) const { return m_TitlebarHit ? m_TitlebarHit(x, y) : false; }
intptr_t  NativeOrigWndProc() const { return m_OrigWndProc; }
```

**What they do** — evaluate the registered predicate (safely returning `false` when none is set), and
return the original GLFW `WNDPROC` as an `intptr_t` for `CallWindowProc`.

**Notes & pitfalls** — **implementation detail**, public only so the file-local Win32 proc can reach
them without Win32 types in the header (`Window.h:262-263`). `NativeOrigWndProc()` returns `0` when
custom chrome was never enabled.

### `Window::SetFullscreenHotkeyOverride`

```cpp
// Window.h:271-274
void SetFullscreenHotkeyOverride(const FullscreenToggleActionFn& fn)
{
    m_HotkeyOverride = fn;
}
```

**What it does** — registers a delegate that sees **raw GLFW key input before the engine's default
F11 handler**. Return `true` to consume the key; return `false` and the default runs
(`Window.cpp:1103-1120`).

**Why you'd use it** — to bind fullscreen to your own key, or to suppress F11 entirely (register a
callback that returns `true` for `CS_KEY_F11` and does nothing).

**Example**

```cpp
// Alt+Enter toggles fullscreen; F11 is suppressed.
auto& win = Cosmic::Application::Get().GetWindow();
win.SetFullscreenHotkeyOverride([&win](int key, int action, int mods)
{
    if (key == CS_KEY_F11)
        return true;                              // consume, do nothing
    if (key == CS_KEY_ENTER && action == GLFW_PRESS && (mods & GLFW_MOD_ALT))
    {
        win.SetFullscreen(!win.IsFullscreen());
        return true;
    }
    return false;
});
```

**Notes & pitfalls**
- **The hotkey path is not the event path.** It runs inside the GLFW key callback, so a consumed key
  never becomes a `KeyPressedEvent` and never reaches `Layer::OnEvent`. The default F11 is
  consumed this way and is invisible to layers. See
  [`../guide/windowing-and-viewport.md#the-f11-press-is-not-an-event`](../guide/windowing-and-viewport.md#the-f11-press-is-not-an-event).
- **It sees every key, not just F11** — return `false` promptly for everything you do not handle, or
  you will swallow the keyboard.
- `action` and `mods` are **raw GLFW values** (`GLFW_PRESS`, `GLFW_REPEAT`, `GLFW_MOD_ALT`), while
  `key` matches the `CS_KEY_*` codes. Filter on `action == GLFW_PRESS` or you fire on release and
  repeat too.
- **The override lives on `Window`, not in `WindowData`, specifically so `Application` can clear it
  before unloading a plugin DLL** (`Window.h:30-32`) — which it does at
  `Application.cpp:795-798`. A callback whose code lives in an unloaded DLL is a jump into freed
  pages.
- Only one override; registering a second replaces the first.

### `Window::ClearFullscreenHotkeyOverride`

```cpp
// Window.h:276-279
void ClearFullscreenHotkeyOverride()
{
    m_HotkeyOverride = nullptr;
}
```

**What it does** — removes the registered delegate; the default F11 handling resumes.

**Notes & pitfalls** — the engine clears it for you on plugin unload (`Application.cpp:797`) and
`~Window` clears it first thing (`Window.cpp:516`). Clear it yourself in `OnDetach` if your layer can
be removed without a DLL unload.

### `Window::ShouldClose`

```cpp
// Window.h:285
bool ShouldClose()                          const;
```

**What it does** — `glfwWindowShouldClose(m_Handle)` (`Window.cpp:598`).

**Notes & pitfalls** — **no null check.** If window creation failed, `m_Handle` is null and the first
call from `Application::Run` (`Application.cpp:138`) dereferences it. That is the practical shape of
a GLFW startup failure in this engine.

---

## `Log`

```cpp
// Log.h:26
class COSMIC_API Log
```

The logging service: two `spdlog` loggers — **`COSMIC`** (engine) and **`APP`** (client) — each
fanning out through one owning `dist_sink` to a colored console child, a timestamped file child, and
any extra sinks you register. Declared in `Cosmic/src/core/Log.h`. All members are `static`; there is
no instance.

You will normally use the [macros](#logging-macros) rather than these methods.

**Where the files land** (`Log.cpp:58-59`, given the directory `Application` passes at
`Application.cpp:92`):

```
<user:// root>/logs/Cosmic_YYYY-MM-DD_HH-MM-SS.log   <- the COSMIC logger
<user:// root>/logs/App_YYYY-MM-DD_HH-MM-SS.log      <- the APP logger
```

`user://` resolves to `logs/` next to the exe in a dev tree (portable mode) or
`%LOCALAPPDATA%/<App>/` when installed under a read-only location — see
[assets-io.md](assets-io.md) *(skeleton — D16)* and
[`../guide/logging-and-diagnostics.md#where-the-log-files-go`](../guide/logging-and-diagnostics.md#where-the-log-files-go).

> **There is no console in a Release build.** `Runtime/CMakeLists.txt:35-36` links `CosmicApp` (and
> `:68-69` `Starforge`) with `/SUBSYSTEM:WINDOWS` and `/ENTRY:mainCRTStartup` in Release, so **no
> console window is created at all**. The colored console sink still exists and still runs; its
> output goes nowhere. **The log files are the only diagnostic surface in Release.** Debug keeps the
> default console subsystem.

**Both loggers are configured identically** (`Log.cpp:86-91`): level `trace` (everything passes) and
`flush_on(trace)` — **every single line is flushed to disk immediately**. That is deliberate
crash-safety: the last line before a hard crash is on disk. It is also a per-line file write, which
is why a hot loop logging every frame is a measurable cost.

### `Log::Init`

```cpp
// Log.h:34
static void Init(const std::string& logDirectory = "logs");
```

**What it does** — creates `logDirectory` if missing (`Log.cpp:40-43`), builds a fresh console + file
sink pair per logger with timestamped filenames, and, **on the first call only**, creates the two
`dist_sink`s and the two loggers (`:79-92`). On every call it replaces each dist-sink's children with
`{ console, file, …every registered extra sink }` (`:94-100`).

**Why you'd use it** — you don't. `Application`'s constructor calls it first thing with
`FileSystem::Resolve("user://logs")` (`Application.cpp:92`). Call it yourself only in a headless
tool that has no `Application` — the unit-test binary does exactly that.

**Notes & pitfalls**
- **Logging before `Init` is a null dereference.** `GetCoreLogger()` returns a null `Ref` and the
  macros call `->trace(...)` on it. Since `Init` is the first statement of the `Application`
  constructor, anything logging from a static initializer runs first and crashes.
- **It throws on failure, and nothing in the engine catches it.**
  `std::filesystem::create_directories` throws `filesystem_error`; `basic_file_sink_mt` throws
  `spdlog_ex` when the file cannot be opened. Both propagate out of the `Application`
  constructor — which is exactly why `Runtime/Main.cpp:132-155` wraps construction in `try`.
- The file sinks truncate (`true` as the second argument, `:65`, `:70`), but the filename carries a
  second-resolution timestamp, so a new run gets a new file. **Two runs started within the same
  second share a filename and the second truncates the first.**
- **Nothing prunes old logs.** The `logs/` directory grows one file pair per run forever.
- The console pattern colours the level token; the **file** pattern is deliberately marker-free so no
  escape junk lands in the files (`Log.cpp:25-26`).

### `Log::SetLogDirectory`

```cpp
// Log.h:39
static void SetLogDirectory(const std::string& logDirectory);
```

**What it does** — warns, calls `Init(logDirectory)`, then logs a confirmation
(`Log.cpp:144-152`). Because `Init` only rebuilds the **children** of the two long-lived dist-sinks,
this rotates the console and file children while **every sink registered via `AddSink` survives**.

**Why you'd use it** — a tool that lets the user pick where diagnostics go.

**Notes & pitfalls**
- **The old files are left behind, not moved or merged.** Everything logged before the call stays in
  the previous pair.
- Same throw behaviour as `Init` — an unwritable target directory throws out of this call.
- Same-second collision caveat as `Init`.

### `Log::AddSink`

```cpp
// Log.h:50
static void AddSink(const spdlog::sink_ptr& sink);
```

**What it does** — attaches an extra sink to **both** loggers, under the exclusive lock. Null sinks
are ignored and duplicates are rejected by pointer identity (`Log.cpp:104-114`).

**Why you'd use it** — mirroring the engine log into your own UI. The editor Console panel registers
a [`CallbackSink`](#callbacksink) here.

**Example**

```cpp
auto sink = std::make_shared<Cosmic::CallbackSink>(
    [this](spdlog::level::level_enum lvl, const std::string& line)
    {
        std::scoped_lock lock(m_Mutex);      // called from ANY logging thread
        m_Pending.push_back({ lvl, line });  // drain on the UI thread
    });
Cosmic::Log::AddSink(sink);
```

**Notes & pitfalls**
- **The sink persists across `SetLogDirectory`** — that is the whole reason the dist-sink layer
  exists (`Log.h:46-49`). Only `RemoveSink` detaches it.
- It receives **both** loggers' output, interleaved. There is no per-logger registration.
- **Remove it before the object it captures dies.** A `CallbackSink` holding a dangling `this` is
  called from whatever thread logs next.
- Thread-safe to call. Registering while another thread is logging is fine.

### `Log::RemoveSink`

```cpp
// Log.h:51
static void RemoveSink(const spdlog::sink_ptr& sink);
```

**What it does** — detaches the sink from the persistent list and from both dist-sinks
(`Log.cpp:116-122`). Removing a sink that was never added is a silent no-op.

**Notes & pitfalls** — you must pass the **same `shared_ptr`** (same control block) you added; keep
it in a member. There is no "remove all".

### `Log::GetCoreLogger` / `Log::GetClientLogger`

```cpp
// Log.h:57-58
static Ref<spdlog::logger>& GetCoreLogger();
static Ref<spdlog::logger>& GetClientLogger();
```

**What it does** — returns a reference to the `COSMIC` / `APP` logger, taking a **shared read lock**
for the duration of the accessor (`Log.cpp:126-140`), so many threads can fetch concurrently.

**Why you'd use it** — you normally use the [macros](#logging-macros), which call these. Reach for
the logger directly only to change a level or format at runtime.

**Notes & pitfalls**
- **Returns a null `Ref` before `Init`.** The macros do not check.
- **The lock is released when the accessor returns** — it protects the pointer fetch, not your use of
  the logger. `spdlog`'s `_mt` sinks make the logger itself thread-safe, so this is correct, but do
  not read it as "logging is serialised".
- Returns a **non-const reference to the static `Ref`**. Assigning through it replaces the process's
  logger; don't.

---

## `CallbackSink`

```cpp
// Log.h:82
class CallbackSink : public spdlog::sinks::base_sink<std::mutex>
```

A generic `spdlog` sink that hands each **formatted** line to a `std::function`
(`Log.h:75-101`). Header-only and **deliberately not `COSMIC_API`** so a client DLL can instantiate
it in its own module and register it via [`Log::AddSink`](#logaddsink).

```cpp
// Log.h:85-86
using Callback = std::function<void(spdlog::level::level_enum, const std::string&)>;
explicit CallbackSink(Callback cb) : m_Callback(std::move(cb)) {}
```

**What it does** — on each log message, formats it with the sink's own formatter and invokes the
callback with the severity and the finished string (`Log.h:89-96`). A null callback is a no-op
(`:91-92`). `flush_()` does nothing (`:97`).

**Why you'd use it** — routing the engine log into an editor Console panel, a network stream, or a
crash report buffer. This is the intended extension point;
[`../guide/logging-and-diagnostics.md#mirror-the-log-into-your-own-ui`](../guide/logging-and-diagnostics.md#mirror-the-log-into-your-own-ui)
has the worked example.

**Notes & pitfalls**
- **The callback can fire from any thread that logs.** The base is `base_sink<std::mutex>`, so
  calls are serialised against each other — but not against your UI thread. Enqueue and drain
  elsewhere; do not touch ImGui from inside it.
- **Keep it cheap and reentrant.** Logging from inside the callback re-enters the logger.
- The line arrives **already formatted** with the sink's pattern, including the trailing newline.
  Trim it before displaying if you add your own.
- The sink holds the callback by value; its captures must outlive every possible log call. Pair
  construction with a `RemoveSink` in the owner's destructor.

---

## Logging macros

```cpp
// Log.h:109-113 — engine channel, logger name "COSMIC"
#define CS_CORE_TRACE(...)    ::Cosmic::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define CS_CORE_INFO(...)     ::Cosmic::Log::GetCoreLogger()->info(__VA_ARGS__)
#define CS_CORE_WARN(...)     ::Cosmic::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define CS_CORE_ERROR(...)    ::Cosmic::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CS_CORE_CRITICAL(...) ::Cosmic::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Log.h:116-120 — client channel, logger name "APP"
#define CS_TRACE(...)         ::Cosmic::Log::GetClientLogger()->trace(__VA_ARGS__)
#define CS_INFO(...)          ::Cosmic::Log::GetClientLogger()->info(__VA_ARGS__)
#define CS_WARN(...)          ::Cosmic::Log::GetClientLogger()->warn(__VA_ARGS__)
#define CS_ERROR(...)         ::Cosmic::Log::GetClientLogger()->error(__VA_ARGS__)
#define CS_CRITICAL(...)      ::Cosmic::Log::GetClientLogger()->critical(__VA_ARGS__)
```

**What they do** — forward to `spdlog` at the named severity. Formatting is `fmt`-style — `{}` and
`{0}` placeholders, **not** `printf`'s `%s`.

**Why you'd use them** — `CS_*` in your project, `CS_CORE_*` in engine code. The split is what makes
the two log files separable; the guide explains when the distinction matters in
[`../guide/logging-and-diagnostics.md#cs_-versus-cs_core_`](../guide/logging-and-diagnostics.md#cs_-versus-cs_core_).

**Example**

```cpp
CS_INFO("Loaded scene '{0}' with {1} entities", path, count);
CS_WARN("Texture '{}' missing — using the checkerboard fallback", path);
```

**Notes & pitfalls**
- **`{}` not `%s`.** A `printf`-style format string is printed literally, with no error.
- A **mismatched placeholder count throws `fmt::format_error` at runtime**, not at compile time.
- **Not compiled out in any configuration.** Unlike the asserts, every one of these is live in
  Release, and every one flushes to disk immediately (`Log.cpp:87`, `:91`). A log line on a
  per-frame path therefore costs a synchronous file write **every frame** — never put one in
  `OnUpdate` without a rate limit or an edge check.
- All ten dereference the logger without a null check, so they crash before `Log::Init`.
- Severity ordering is `trace < info < warn < error < critical`; the level is `trace`, so nothing is
  filtered.

## `UUID`

```cpp
// UUID.h:23
class COSMIC_API UUID
```

A 64-bit stable identity. Declared in `Cosmic/src/core/UUID.h`, included by `Cosmic.h:23`. It wraps
one `uint64_t` and is trivially copyable — pass it by value.

**Why 64 bits, not 128.** It is a *scene-scale* identity, not a universally unique one: cheap to
store on every entity, cheap to compare, and collision-safe at the entity counts a scene reaches
(`UUID.h:8-12`). Do not treat it as an RFC 4122 UUID; it will not survive being merged with an
identifier space you do not control.

**Where it shows up** — every entity created through `Scene` carries an `IDComponent{ UUID }`
(`Components.h:40-46`), and that value is what parent links, `EntityRef` script fields and prefab
sources serialise (`SceneSerializer.cpp:212`, `:317`, `:442`, `:508`), so those references survive
save/load and session restarts. Entity-level usage is [ecs.md](ecs.md); there is **no
`Entity::GetUUID()`** — read `GetComponent<IDComponent>().ID`.

**`UUID(0)` is the reserved null.** The default constructor never produces it; `FromString` returns
it on any parse failure; `IsValid()` tests for it.

`std::hash<Cosmic::UUID>` is specialised (`UUID.h:47-57`), so a `UUID` works as an
`unordered_map`/`unordered_set` key out of the box.

<a id="uuiduuid"></a>
### `UUID::UUID` *(default — random)*

```cpp
// UUID.h:26
UUID();                                   // random, never 0
```

**What it does** — draws a uniform `uint64_t` from a process-wide `std::mt19937_64` seeded once from
`std::random_device`, redrawing on the (astronomically rare) zero (`UUID.cpp:29-36`).

**Why you'd use it** — you generally don't call it directly; `Scene::CreateEntity` does. Call it when
you need a fresh identity for something you are about to serialise.

> **⚠️ It is not thread-safe, and the comment above it says it is.** `UUID.cpp:12-14` claims *"Guarded
> so concurrent CreateEntity calls from worker threads (JobSystem) can't corrupt the generator
> state"* — but there is **no mutex, no `thread_local`, no atomic**. Both `Engine()` and `Dist()`
> return references to function-local statics (`UUID.cpp:15-26`); C++ guarantees their *initialisation*
> is thread-safe and nothing else. Two threads constructing a `UUID` concurrently is a data race on
> `std::mt19937_64`'s internal state, which can produce duplicate or correlated values as well as
> being formally UB. **Create entities on the main thread.**

**Notes & pitfalls** — the sequence is **not** reproducible across runs (`random_device` seeding), so
a `UUID` cannot be used where a deterministic test needs a stable value. Use the explicit-value
constructor there.

### `UUID::UUID` *(explicit value)*

```cpp
// UUID.h:27
UUID(uint64_t value) : m_UUID(value) {}   // explicit value (e.g. from JSON)
```

**What it does** — wraps a value you already have.

**Why you'd use it** — deserialisation, tests, and reconstructing an identity from a `uint64_t` field
(the reflection layer stores `EntityRef` as a raw `uint64_t`, `SceneSerializer.cpp:53`, `:76`).

**Notes & pitfalls** — **not `explicit`**, so any `uint64_t` converts implicitly. Combined with
`operator uint64_t()` this makes `UUID` and `uint64_t` freely interchangeable. It performs **no
validation**: `UUID(0)` constructs the null value happily.

### `UUID::Value`

```cpp
// UUID.h:30
uint64_t Value() const { return m_UUID; }
```

**What it does** — returns the raw 64-bit value.

**Why you'd use it** — the explicit form, preferable to relying on the implicit conversion. It is
also how you read an entity's id: `e.GetComponent<IDComponent>().ID.Value()`.

### `UUID::IsValid`

```cpp
// UUID.h:31
bool     IsValid() const { return m_UUID != 0; }
```

**What it does** — `m_UUID != 0`.

**Why you'd use it** — the correct test after `FromString`, and the correct test on a deserialised
reference field that may point at a deleted entity.

**Notes & pitfalls** — it only proves the value is not the reserved null. It says nothing about
whether an entity with that id exists in the current scene.

### `UUID::operator uint64_t`

```cpp
// UUID.h:33
operator uint64_t() const { return m_UUID; }
```

**What it does** — implicitly converts to the raw value.

**Notes & pitfalls** — not `explicit`. Together with the non-explicit `uint64_t` constructor it means
`UUID` participates in integer arithmetic, comparisons and overload resolution. Arithmetic on an
identity is meaningless; prefer [`Value()`](#uuidvalue) where you want the number.

### `UUID::operator==` / `UUID::operator!=`

```cpp
// UUID.h:35-36
bool operator==(const UUID& o) const { return m_UUID == o.m_UUID; }
bool operator!=(const UUID& o) const { return m_UUID != o.m_UUID; }
```

**What it does** — value comparison.

**Notes & pitfalls** — there is **no `operator<`**, so a `UUID` cannot be a `std::map`/`std::set` key
without a comparator. Use `std::unordered_map`, which the `std::hash` specialisation
(`UUID.h:49-56`) supports directly.

### `UUID::ToString`

```cpp
// UUID.h:39
std::string        ToString() const;
```

**What it does** — formats as **exactly 16 lowercase hex characters, zero-padded**, via
`snprintf("%016llx", …)` (`UUID.cpp:38-43`).

**Why you'd use it** — this is the on-disk form. Every scene, prefab and hierarchy link writes it
(`SceneSerializer.cpp:212`, `:317`, `:508`).

**Notes & pitfalls** — no `0x` prefix and no separators. Always 16 characters, even for small values,
which is what makes the round-trip with [`FromString`](#uuidfromstring) exact.

### `UUID::FromString`

```cpp
// UUID.h:40
static UUID        FromString(const std::string& hex);
```

**What it does** — parses base-16 via `std::stoull` inside a `try`/`catch(...)`, returning
**`UUID(0)` on empty input or on any exception** (`UUID.cpp:45-57`).

**Why you'd use it** — deserialising an id from JSON. `SceneSerializer` uses it at `:76`, `:117`,
`:442` and `:539`.

**Example**

```cpp
Cosmic::UUID id = Cosmic::UUID::FromString(json["id"].get<std::string>());
if (!id.IsValid())
    CS_WARN("scene entity has an unparseable id — link dropped");
```

**Notes & pitfalls**
- **Failure is `UUID(0)`, never an exception and never a log line.** Always `IsValid()` the result;
  a silently-null id turns into a silently-broken parent link.
- **It is more permissive than `ToString` is strict.** `std::stoull` skips leading whitespace,
  accepts a leading `+`/`-` and a `0x` prefix, and **stops at the first non-hex character** rather
  than failing — so `"12abZZ"` parses as `0x12ab`, not as an error. Only a value that is entirely
  unparseable or out of range yields `0`.
- `"0000000000000000"` parses successfully to `UUID(0)`, which `IsValid()` then reports as invalid.
  That is intended — the null value has one spelling on disk.

---

## `ICommand`

```cpp
// CommandStack.h:52
class COSMIC_API ICommand
```

The interface for one reversible mutation. Declared in `Cosmic/src/core/CommandStack.h`, included by
`Cosmic.h:24`. Subclass it, hand ownership to a [`CommandStack`](#commandstack) as a
`Scope<ICommand>`, and never touch it again.

The engine ships **no** concrete commands: Starforge's (reflected-field edits, gizmo transforms,
entity create/destroy, reparent) live in `Projects/Starforge`, and the stack is reusable by any tool
(`CommandStack.h:10-12`).

### `ICommand::~ICommand`

```cpp
// CommandStack.h:55
virtual ~ICommand() = default;
```

Virtual, because the stack owns and destroys through `Scope<ICommand>`.

### `ICommand::Do`

```cpp
// CommandStack.h:60
virtual void Do() = 0;
```

**What it does** — applies the change. **Also used for redo** — there is no separate `Redo()`
(`CommandStack.cpp:65`).

**Notes & pitfalls** — **`Do()` must be idempotent with respect to the current state.** A command
added with [`Push`](#commandstackpush) has already had its effect applied live, and `Redo()` will call
`Do()` on it later, so `Do()` must *set the "after" value*, never *apply a delta*. A command that
does `value += 1` is broken here; one that does `value = m_After` is correct.

### `ICommand::Undo`

```cpp
// CommandStack.h:63
virtual void Undo() = 0;
```

**What it does** — restores the pre-command state.

**Notes & pitfalls** — capture the "before" value at **construction**, not at `Do()` time — a
`Push`'d command is constructed *after* the live mutation, so it must have been given the old value
explicitly. The canonical shape is the test's `SetIntCommand`, which stores both `Before` and
`After` (`tests/test_commandstack.cpp:20-43`).

### `ICommand::Name`

```cpp
// CommandStack.h:66
virtual std::string Name() const { return "Command"; }
```

**What it does** — a human-readable label for the Edit menu ("Undo Move"). Default `"Command"`.

**Notes & pitfalls** — returns by value, so it is called fresh each time; keep it cheap.
`CommandStack::UndoName`/`RedoName`/`UndoNameAt`/`RedoNameAt` all route through it, and a history UI
calls those every frame.

### `ICommand::TryMerge`

```cpp
// CommandStack.h:72
virtual bool TryMerge(const ICommand& next) { (void)next; return false; }
```

**What it does** — the coalescing hook. `next` is a freshly-added command; if `this` can absorb it,
update `this`'s **post-state** from `next` and return `true`, and `next` is discarded. Default: never
merges.

**Why you'd use it** — so a continuous drag becomes **one** undo step instead of sixty.

**Example**

```cpp
// tests/test_commandstack.cpp:35-42
bool TryMerge(const Cosmic::ICommand& next) override
{
    const auto* n = dynamic_cast<const SetIntCommand*>(&next);
    if (!n || n->Key.empty() || n->Key != Key)
        return false;
    After = n->After;   // absorb the newer post-state; keep our Before
    return true;
}
```

**Notes & pitfalls**
- **It is only offered when the merge keys are equal and non-empty and no barrier intervened**
  (`CommandStack.cpp:30-31`) — but you should still `dynamic_cast` and re-check, as the test does.
  Two different command types can share a key.
- **Keep your own "before" and take only the other's "after".** Getting that backwards makes the
  merged step undo to the wrong state, and the bug only appears after two or more merges.
- Returning `true` means `next` is destroyed immediately. Do not keep a pointer to it.

### `ICommand::MergeKey`

```cpp
// CommandStack.h:76
virtual std::string MergeKey() const { return {}; }
```

**What it does** — the coalescing key. Default **empty**, which means *never coalesce*.

**Why you'd use it** — return something that identifies the specific continuous edit, e.g.
`"xform:" + uuid.ToString()`. Two commands merge only when their keys are **equal and non-empty**
(`CommandStack.cpp:30-31`; `tests/test_commandstack.cpp:135-143` pins the empty-key case).

**Notes & pitfalls** — make the key specific enough. A key of `"drag"` shared by every field in the
inspector coalesces two unrelated edits into one undo step whenever they happen back to back.

---

## `CommandStack`

```cpp
// CommandStack.h:82
class COSMIC_API CommandStack
```

A bounded do/undo/redo history of `Scope<ICommand>`. Declared in
`Cosmic/src/core/CommandStack.h`; GL-free, headless, and **not thread-safe — drive it from the main
(UI) thread only** (`CommandStack.h:38`). It knows nothing about scenes, ImGui or the editor.

**The two ways to add a command**, and the distinction is the whole design:

| | [`Execute(cmd)`](#commandstackexecute) | [`Push(cmd)`](#commandstackpush) |
| --- | --- | --- |
| Has the mutation happened yet? | **no** — the stack calls `Do()` now | **yes** — it happened live this frame |
| Calls `Do()` on add? | yes | **no** |
| Use for | discrete actions: create, delete, menu ops | widget/gizmo edits captured on activate, committed on deactivate |

Both then run the same tail: clear the redo branch, try to coalesce, otherwise append, trim to depth,
fire the dirty callback (`CommandStack.cpp:22-42`).

**Copy/move**: non-copyable, movable — declared explicitly (`CommandStack.h:90-93`) so the
`dllexport`ed class does not try to emit implicit copy operations it cannot compile.

The full behaviour is pinned by nine test cases in `tests/test_commandstack.cpp`; each entry below
cites the one that covers it.

### `CommandStack::CommandStack`

```cpp
// CommandStack.h:85
explicit CommandStack(size_t maxDepth = 256) : m_MaxDepth(maxDepth ? maxDepth : 1) {}
```

**What it does** — constructs an empty history with a bounded depth. **`0` is coerced to `1`**, not
to "unbounded".

**Example**

```cpp
Cosmic::CommandStack history;                 // 256 steps
Cosmic::CommandStack shallow(3);              // keep only the 3 newest
shallow.SetDirtyCallback([this] { m_Dirty = true; });
```

**Notes & pitfalls** — `explicit`, so `CommandStack s = 10;` will not compile. There is no unbounded
mode; pass a large number if you want one.

### `CommandStack::Execute`

```cpp
// CommandStack.h:97
void Execute(Scope<ICommand> cmd);
```

**What it does** — calls `cmd->Do()` **now**, then records it (`CommandStack.cpp:7-13`). A null
`cmd` is a **silent no-op** (`:9-10`). Recording clears the redo branch (`:25`).

**Why you'd use it** — discrete actions where the stack is the thing that performs the change:
"Delete Entity", "Add Component", a menu operation.

**Example**

```cpp
// tests/test_commandstack.cpp:51 — Execute applies and records one step
stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 5));
CHECK(v == 5);
CHECK(stack.CanUndo());
```

**Notes & pitfalls**
- **Ownership transfers.** Pass `std::make_unique<T>(…)` directly, or `std::move` a named `Scope`.
- **An exception from `Do()` escapes before the command is recorded**, so a partially applied change
  is left with no undo entry. Make `Do()` non-throwing.
- The redo branch is discarded (`tests/test_commandstack.cpp:76-89`).
- It can still coalesce into the previous entry — `Execute` and `Push` share `Record`. If you do not
  want that, override `MergeKey()` to return `{}` or call
  [`SetMergeBarrier()`](#commandstacksetmergebarrier) first.

### `CommandStack::Push`

```cpp
// CommandStack.h:100
void Push(Scope<ICommand> cmd);
```

**What it does** — records the command **without** calling `Do()` (`CommandStack.cpp:15-20`), because
the effect is already live. Null is a silent no-op.

**Why you'd use it** — an ImGui drag or a gizmo already mutated the component this frame. Capture the
"before" on activate, build the command on deactivate, and `Push` it.

**Example**

```cpp
// tests/test_commandstack.cpp:91-102 — Push must not re-run Do()
v = 7;                                              // an ImGui drag already set it
stack.Push(std::make_unique<SetIntCommand>(&v, 0, 7));
CHECK(v == 7);                                      // unchanged
CHECK(stack.Undo()); CHECK(v == 0);
CHECK(stack.Redo()); CHECK(v == 7);                 // redo re-applies through Do()
```

**Notes & pitfalls**
- **`Do()` still runs on the first redo**, so it must be idempotent — see
  [`ICommand::Do`](#icommanddo).
- Using `Push` where you meant `Execute` silently records a step that never applied; using `Execute`
  where you meant `Push` applies the change **twice** — harmless for an idempotent `Do()`, wrong for
  anything else.

### `CommandStack::Undo`

```cpp
// CommandStack.h:103
bool Undo();
```

**What it does** — pops the most recent command, calls its `Undo()`, moves it to the redo stack,
**sets the merge barrier**, and fires the dirty callback. Returns `false` when there is nothing to
undo (`CommandStack.cpp:44-56`).

**Example**

```cpp
if (ImGui::MenuItem("Undo", "Ctrl+Z", false, stack.CanUndo()))
    stack.Undo();
```

**Notes & pitfalls** — the barrier at `:53` is why *a fresh edit made after an undo never merges into
old history*. Nothing is validated: if the object a command points at has been destroyed, `Undo()`
dereferences it. Clear the stack when you close the document it edits.

### `CommandStack::Redo`

```cpp
// CommandStack.h:106
bool Redo();
```

**What it does** — pops the most recently undone command, calls `Do()`, moves it back to the undo
stack, sets the barrier, fires dirty. `false` when the redo stack is empty
(`CommandStack.cpp:58-70`).

**Notes & pitfalls** — **the redo branch is destroyed by the next `Execute`/`Push`**
(`:25`), which is standard linear-history behaviour and worth surfacing in your UI.

### `CommandStack::CanUndo` / `CanRedo`

```cpp
// CommandStack.h:108-109
bool CanUndo() const { return !m_Undo.empty(); }
bool CanRedo() const { return !m_Redo.empty(); }
```

**What it does** — whether the corresponding call would do anything. Use them to enable/disable menu
items.

### `CommandStack::UndoName` / `RedoName`

```cpp
// CommandStack.h:112-113
std::string UndoName() const { return m_Undo.empty() ? std::string{} : m_Undo.back()->Name(); }
std::string RedoName() const { return m_Redo.empty() ? std::string{} : m_Redo.back()->Name(); }
```

**What it does** — the label of the command the next `Undo`/`Redo` would apply; **empty string** when
unavailable.

**Example**

```cpp
const std::string u = stack.UndoName();
ImGui::MenuItem(u.empty() ? "Undo" : ("Undo " + u).c_str(), "Ctrl+Z", false, stack.CanUndo());
```

### `CommandStack::UndoCount` / `RedoCount`

```cpp
// CommandStack.h:115-116
size_t UndoCount() const { return m_Undo.size(); }
size_t RedoCount() const { return m_Redo.size(); }
```

**What it does** — history depths. `UndoCount()` is the assertion the coalescing tests make
(`tests/test_commandstack.cpp:114`, `:130`, `:142`, `:153`) — a merged run counts as **one**.

### `CommandStack::UndoNameAt` / `RedoNameAt`

```cpp
// CommandStack.h:120-127
std::string UndoNameAt(size_t i) const
{
    return i < m_Undo.size() ? m_Undo[m_Undo.size() - 1 - i]->Name() : std::string{};
}
std::string RedoNameAt(size_t i) const
{
    return i < m_Redo.size() ? m_Redo[m_Redo.size() - 1 - i]->Name() : std::string{};
}
```

**What it does** — the label of the *i*-th most recent entry, where **`0` is what the next
`Undo`/`Redo` applies**. Out of range returns `""`.

**Why you'd use it** — a history panel listing recent steps.

**Notes & pitfalls** — index `0` is the **newest**, not the oldest; the reversal is done inside. It is
read-only: there is no "jump to step *i*". Each call re-invokes `Name()`.

### `CommandStack::Clear`

```cpp
// CommandStack.h:131
void Clear();
```

**What it does** — drops both stacks and resets the barrier (`CommandStack.cpp:72-77`).
**Does not fire the dirty callback** — pinned by `tests/test_commandstack.cpp:175-178`.

**Why you'd use it** — closing a project, loading a new scene: the old history points at objects that
no longer exist.

**Notes & pitfalls** — the commands are destroyed here, so their destructors run. If a command holds a
`Ref` to a scene object, this is where that reference is released. It does **not** undo anything;
whatever is applied stays applied.

### `CommandStack::SetMergeBarrier`

```cpp
// CommandStack.h:135
void SetMergeBarrier() { m_Barrier = true; }
```

**What it does** — forces the **next** added command to start a fresh history entry even if its merge
key matches the current top.

**Why you'd use it** — the user released the mouse and started a new drag on the same field. Without
the barrier, both drags coalesce into one undo step.

**Example**

```cpp
// tests/test_commandstack.cpp:121-133
v = 1; stack.Push(std::make_unique<SetIntCommand>(&v, 0, 1, "drag"));
stack.SetMergeBarrier();                              // user released the mouse
v = 2; stack.Push(std::make_unique<SetIntCommand>(&v, 1, 2, "drag"));
CHECK(stack.UndoCount() == 2);                        // two steps despite the same key
```

**Notes & pitfalls** — it is **consumed by the next add**, whether or not a merge was attempted
(`CommandStack.cpp:33`, `:39`). `Undo()` and `Redo()` set it implicitly (`:53`, `:67`); `Clear()`
resets it to false (`:76`).

### `CommandStack::SetMaxDepth` / `GetMaxDepth`

```cpp
// CommandStack.h:137-138
void   SetMaxDepth(size_t depth) { m_MaxDepth = depth ? depth : 1; TrimToDepth(); }
size_t GetMaxDepth() const { return m_MaxDepth; }
```

**What it does** — changes the cap and **trims immediately**. `0` is coerced to `1`.

**Notes & pitfalls**
- **Trimming drops the OLDEST entries** (`CommandStack.cpp:79-86`) — the front of the undo vector.
  Those steps are gone; the change stays applied.
  `tests/test_commandstack.cpp:145-161` pins it: with depth 3 and ten commands, only the last three
  are reversible.
- **The redo stack is not bounded** by `m_MaxDepth`. It is only ever filled by `Undo()`, so it cannot
  exceed the undo depth in practice.
- Memory is bounded by *count*, not by bytes. A command holding a big payload still counts as one.

### `CommandStack::SetDirtyCallback`

```cpp
// CommandStack.h:142
void SetDirtyCallback(std::function<void()> cb) { m_OnDirty = std::move(cb); }
```

**What it does** — installs a callback fired after **any** state-changing operation:
`Execute`, `Push`, a successful coalesce, `Undo`, `Redo`. **`Clear()` does not fire it**
(`CommandStack.cpp:76`).

**Why you'd use it** — flipping the window-title `*`.

**Example**

```cpp
// tests/test_commandstack.cpp:163-178 — three mutating ops, three fires; Clear is silent
stack.SetDirtyCallback([&] { ++dirtyCount; });
stack.Execute(std::make_unique<SetIntCommand>(&v, 0, 1));  // +1
stack.Undo();                                              // +1
stack.Redo();                                              // +1
CHECK(dirtyCount == 3);
```

**Notes & pitfalls**
- **`Undo()` fires it too**, so undoing back to the saved state still marks the document dirty. If
  you want true clean/dirty tracking, compare a saved-history-position marker yourself.
- One callback, last writer wins.
- It fires **synchronously inside** `Execute`/`Push`/`Undo`/`Redo` — do not add another command from
  inside it.
- Null-safe: `MarkDirty()` checks before calling (`CommandStack.h:147`).

## The plugin-export boundary

`Cosmic.h` ends with the entire contract between the engine and a project DLL: one struct and two
`extern "C"` functions.

```cpp
// Cosmic.h:235-239
extern "C" {
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
}
```

**These are declarations of functions the engine will look up in *your* DLL** — not functions the
engine provides. `Application::LoadProjectDLL` resolves both by name with `GetProcAddress`
(`Application.cpp:702-703`), and **a DLL missing either one is rejected**: it logs
`"Plugin is missing required engine export signatures!"`, calls `FreeLibrary` and returns
(`:705-710`). The Launcher uses the presence of `CreatePluginLayer` as its "is this a Cosmic project"
test when scanning a folder (`LauncherLayer.cpp:621`).

`extern "C"` matters: the lookup is by the **undecorated** name. Defining them inside a namespace, or
without `extern "C"`, produces a mangled export the engine will not find.

Full narrative + diagram **DG-5**:
[`../guide/project-anatomy.md#the-exports`](../guide/project-anatomy.md#the-exports).

### `HostContext`

```cpp
// Cosmic.h:213-217
struct COSMIC_API HostContext
{
    ImGuiContext* ImGuiCtx;
    ImPlotContext* ImPlotCtx;
};
```

**What it is** — the data bucket that carries the **host's** ImGui and ImPlot contexts across the DLL
boundary. Two raw pointers; nothing is owned.

**Why it exists** — ImGui and ImPlot each keep a *per-module* global "current context" pointer. A
project DLL linking its own copy of the ImGui symbols starts with a null one and would draw into
nothing (or crash). Handing over the host's pointer makes both modules share one UI.

**Notes & pitfalls**
- **The engine fills it, you consume it.** `LoadProjectDLL` builds it from
  `ImGui::GetCurrentContext()` and `ImPlot::GetCurrentContext()` and calls your
  `InitializePluginContexts` **before** `CreatePluginLayer` (`Application.cpp:715-721`) — so the
  contexts are live by the time your layer's constructor runs.
- Neither pointer is owned by the struct or by you. Never delete or `DestroyContext` them.
- The struct is also documented in [ui.md](ui.md#hostcontext), which owns the ImGui side of the
  story; this entry owns the DLL-boundary side.

### `InitializePluginContexts`

```cpp
// Cosmic.h:238
__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context);
```

**What it does** — your DLL's opportunity to adopt the host's UI contexts. The body is always the
same two lines.

**Example** — the shipped idiom, identical in every project
(`Engine3DDemo.cpp:2044-2048`, `FrontierApp.cpp:425`, `SF_Telem.cpp:458`, `ViperSim.cpp:302`,
`StarforgeApp.cpp:4806`, `templates/ExampleProject/src/TemplateProject.cpp:410`):

```cpp
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }
}
```

**Notes & pitfalls**
- **Called exactly once per load, before `CreatePluginLayer`** (`Application.cpp:718`, `:721`).
- **Omitting `ImPlot::SetCurrentContext` is the classic bug**: ImGui works, and the first ImPlot call
  crashes. Set both.
- Nothing checks that you did anything. There is no return value and no verification.
- If you use `CS_MODULE_BEGIN`/`CS_MODULE_END`, this function is **generated for you**
  (`ModuleMacros.h:101-106`) — do not also write it by hand, or you get a duplicate-symbol error.

### `CreatePluginLayer`

```cpp
// Cosmic.h:237
__declspec(dllexport) Cosmic::Layer* CreatePluginLayer();
```

**What it does** — constructs and returns your project's root layer. The engine stores it as
`m_ActivePluginLayer` and mounts it on the workspace shell
(`Application.cpp:721`, `:744`).

**The ownership rule, stated once:** *you `new` it, the engine `delete`s it.*
`UnloadProjectDLL` deletes the pointer **before** `FreeLibrary` (`Application.cpp:789-792` then
`:808`), so your destructor runs against still-mapped code. Never delete it yourself, never return a
pointer to a static or stack object, and never hand the engine a second layer.

**Example — hand-written** (`Engine3DDemo.cpp:2050-2053`):

```cpp
extern "C"
{
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::Engine3DDemo();
    }
}
```

**Example — generated by `CS_MODULE_END`** (`ModuleMacros.h:95-100`), which is what a scaffolded
project gets: it registers the module's scripts and returns a
[`PlayerLayer`](#playerlayer) named after the module.

```cpp
CS_MODULE_BEGIN(MyGame)
    CS_SCRIPT(PlayerController);
CS_MODULE_END()
// -> CreatePluginLayer() { CosmicModule_Register(ModuleRegistry::Get());
//                          return new Cosmic::PlayerLayer("MyGame"); }
```

**Notes & pitfalls**
- **Returning `nullptr` is handled, not ignored**: the engine logs
  `"Plugin's CreatePluginLayer() returned nullptr — aborting load."`, calls `FreeLibrary` and clears
  the handle (`Application.cpp:723-729`). You are left on an empty `WorkspaceLayer`.
- **The returned layer is never pushed onto the `LayerStack`** — see
  [that section](#a-plugin-layer-is-not-on-the-layerstack). Its hooks are forwarded by hand and its
  local `TimeScale` behaves differently than a stacked layer's.
- **The engine sets the VFS active project before mounting.** `LoadProjectDLL` calls
  `FileSystem::SetActiveProject(<dll stem>)` at `Application.cpp:739-740` — deliberately from the
  *engine* side, because `FileSystem` is header-only with per-DLL static state, so your own call to
  it in `OnAttach` would only update your module's copy. Engine-compiled code resolving `project://`
  (themes, fonts, `Config::Load`) therefore works from your `OnAttach` onward.
- **Project themes and fonts are rescanned right after your layer mounts** —
  `ThemeManager::LoadFolder(project://themes)`, `UI::Fonts::LoadProjectFonts()` and
  `Font::LoadProjectFonts()` (`Application.cpp:762-764`) — because their `Init` runs at ImGuiLayer
  attach, long before any project exists. All three are idempotent.
- **Do not push child layers for the engine to own.** Compose inside your own layer instead; the
  pattern is [`../guide/project-anatomy.md#the-composite-layer-pattern`](../guide/project-anatomy.md#the-composite-layer-pattern).
- A raw pointer is used rather than a `Ref<Layer>` deliberately: a `shared_ptr` would put the
  control block on one side of the boundary and the deleter on the other. See
  [`../guide/project-anatomy.md#the-layer-exception-and-what-the-engine-owns-it-buys-you`](../guide/project-anatomy.md#the-layer-exception-and-what-the-engine-owns-it-buys-you).

> `Cosmic.h` also defines two inline `SetImGuiTheme` helpers (`:220-231`). They are theming, not core
> runtime — see [ui.md → `Cosmic::SetImGuiTheme`](ui.md#cosmicsetimguitheme).

---

## Failure-mode summary

Every call in this chapter that can fail, and exactly how. The Cosmic convention varies on purpose.

| Call | On failure |
| --- | --- |
| `Application::Application` | **throws** if `Log::Init` cannot create the log directory or open a file; returns normally but leaves an unusable `Window` if GLFW/window creation failed (logged `CS_CORE_CRITICAL`, no exception) |
| `Application::Application` — bad `--project` | **logs `CS_CORE_ERROR` and degrades to the Launcher**, pushing one if none exists |
| `Application::Get()` before construction | **null dereference** — no check |
| `Application::Get()` after `delete` | **dangling reference** — `s_Instance` is never cleared |
| `Application::GetWindow()` before/after the window exists | **null dereference** — no check |
| `Application::GetFrameBuffer()` before init | returns a **null `Ref`** |
| `Application::GetWorkspaceLayer()` outside a workspace | returns **`nullptr`** (normal; every caller null-checks) |
| `Application::GetViewportPos`/`GetViewportSize()` with no workspace | returns **`{0, 0}`**, indistinguishable from a real value |
| `Application::SetFixedTimestepHz(out of range)` | **clamped to `[1, 1000]` and logged `CS_CORE_WARN`** — never rejected |
| `Application::SetTimeScale(anything)` | **no validation at all** — negatives silently stall the fixed pass |
| `Application::TransitionFromLauncherToWorkspace` — unresolvable DLL | logs `CS_CORE_ERROR`, stays on the Launcher |
| `Application::TransitionFromLauncherToWorkspace` — DLL missing an export | logs `CS_CORE_ERROR`, `FreeLibrary`, leaves an **empty workspace** |
| `CreatePluginLayer()` returning `nullptr` | logs `CS_CORE_ERROR`, `FreeLibrary`, leaves an empty workspace |
| `LayerStack::PopLayer`/`PopOverlay` with an unknown pointer | **silent no-op** |
| `LayerStack::Clear()` with layers attached | **silently leaks** (the guard assert is compiled out) |
| any push/pop during iteration | **undefined behaviour, undiagnosed** (assert compiled out) |
| `Window::GetHandle()` after a GLFW failure | returns **`nullptr`** |
| `Window::ShouldClose()`/`GetSize()` after a GLFW failure | **null dereference** — no check |
| `Window::SetSize` out of `[64, 16384]`, or while fullscreen | **silent no-op**, no log, no return value |
| `Window::SetIcon` — decode failure / empty path / no handle | returns **`false` and keeps the current icon** (deliberate: a half-written file must not blank the brand) |
| `Window::SetFullscreen`/`SetCursorCaptured`/`SetCustomChrome` — state already matches | **idempotent early-out** |
| `Log::Init` / `Log::SetLogDirectory` — unwritable directory | **throws** (`filesystem_error` / `spdlog_ex`); nothing in the engine catches |
| any `CS_*` / `CS_CORE_*` macro before `Log::Init` | **null dereference** |
| any `CS_*` / `CS_CORE_*` macro with mismatched `{}` | **throws `fmt::format_error` at runtime** |
| `Log::AddSink(nullptr)` or a duplicate | **silent no-op** |
| `Log::RemoveSink` with an unknown sink | **silent no-op** |
| `UUID::FromString` — unparseable | returns **`UUID(0)`**, no log, no exception |
| `UUID()` from multiple threads | **data race** on the shared generator (the source comment claiming a guard is wrong) |
| `CommandStack::Execute`/`Push(nullptr)` | **silent no-op** |
| `CommandStack::Undo`/`Redo` with nothing to do | returns **`false`** |
| `CommandStack::UndoNameAt`/`RedoNameAt` out of range | returns **`""`** |
| `CommandStack::SetMaxDepth` smaller than the history | **silently drops the oldest entries** |
| `CS_ASSERT` / `CS_CORE_ASSERT` anywhere | **compiled out in every configuration** — never a guard |

---

## Manifest & coverage notes

**Manifest rows.** Eleven of this chapter's headers have rows in
[`README.md`'s coverage manifest](README.md#coverage-manifest--every-public-header-maps-to-a-chapter):
`core/Core.h`, `core/Application.h`, `core/Layer.h`, `core/Timestep.h`, `core/Log.h`,
`core/CommandStack.h`, `core/UUID.h`, `core/Version.h`, `layers/PlayerLayer.h`, and `Cosmic.h`
(shared with [ui.md](ui.md)); `core/Window.h` is carried by the *"Not in `Cosmic.h` but
client-reachable"* footnote under the table. **No row needed to be added.**

**`core/LayerStack.h` has no row on purpose.** The checker classifies it as engine plumbing with the
justification *"Application owns it; PushLayer/PushOverlay are the client verbs"*
(`tests/check_docs_coverage.ps1:74-78`). It is documented [above](#layerstack) anyway, because its
ordering rules are client-visible and its `LNK2019` needs an explanation.

**Strict mode.** With the skeleton banner removed, every `COSMIC_API` class declared by a header in
scope must be named in this file. That set is `Application`, `Layer`, `Timestep`, `Log`, `Window`,
`UUID`, `ICommand`, `CommandStack`, `PlayerLayer` and `HostContext` — all ten have entries.
`CallbackSink` and `LayerStack` are documented here but are **not** `COSMIC_API`, so strict mode does
not require them.

**Deliberately not covered here**, with the reason:

| Symbol | Why not |
| --- | --- |
| `WorkspaceLayer` (all members) | [ui.md](ui.md#workspacelayer) owns it — only `Application`'s two forwarding members are here |
| `ImGuiLayer` (all members) | [ui.md](ui.md#imguilayer) |
| `Cosmic::SetImGuiTheme` (both overloads) | [ui.md](ui.md#cosmicsetimguitheme) — theming, not core runtime |
| `Application`'s private members (`Initialize`, `RenderSingleFrame`, `ProcessDeferredTransitions`, `LoadProjectDLL`, `UnloadProjectDLL`, `SynchronizeRenderingState`, `OnWindowClose`, `OnWindowResize`) | not callable; their **behaviour** is cited throughout because it is the observable contract |
| `Window`'s private helpers (`HandleFullscreenHotkey`, `ApplyFullscreenWin32`, `Enable`/`DisableCustomChromeWin32`, `FindCurrentMonitor`, `WindowData`) | not callable; behaviour cited in the public entries that trigger them |
| `Input`, the `Event` hierarchy, `EventDispatcher`, `CS_KEY_*` | [events-input.md](events-input.md) |
| `JobSystem` | [jobs.md](jobs.md) *(skeleton — D17)* — only the ordering guarantee is stated here |
| `FileSystem`, `Config`, `Branding`, `ImageIO` | [assets-io.md](assets-io.md) *(skeleton — D16)* |
| `SceneManager`, `ScriptHost`, `SceneRenderer`, `PhysicsWorld`, `FlowMachine` | `PlayerLayer`'s members, each owned by its own chapter — this chapter documents only how `PlayerLayer` sequences them |
| `spdlog` itself | third-party; only the engine's configuration of it is documented |

---
*Changelog:*
- 2026-07-26 — **D6**: chapter written from the headers. Scope expanded by the D61 integration with
  `core/CommandStack.h`, `core/UUID.h`, `core/Version.h` and `layers/PlayerLayer.h`. README §7's
  Pause-vs-`TimeScale` table condensed into per-subsystem form; the plugin-export boundary
  (`HostContext`, `CreatePluginLayer`, `InitializePluginContexts`) documented here, with
  `WorkspaceLayer` itself left to [ui.md](ui.md).
