# `glDelete*` access violation in `opengl32.dll` on close

> **Verified against commit:** `b168754` (references to `Cosmic/src/platform/OpenGL/*`,
> `Cosmic/src/renderer/Renderer2D.cpp`, `Cosmic/src/core/Application.cpp`, and `Runtime/Main.cpp`).
> **Status:** Fixed. Keep — the teardown-ordering hazard is generic to any static/late-freed GPU handle.

## Symptom

Closing the app (more often on one laptop than the main PC) broke in the debugger with an **access violation
reading a near-null address inside `opengl32.dll`**, with the call stack
`[External Code] → OpenGLTexture::~OpenGLTexture()` at `glDeleteTextures(1, &m_RendererID)`. The crashing
object had `m_Path == ""` and `m_RendererID == 1` — i.e. the engine's **1×1 white texture** (the first
texture created by `Renderer2D::Init`).

The `[External Code]` frame (no `Renderer2D::Shutdown` on the stack) is the tell: the texture was being freed
by **CRT static destruction at process exit**, not by the normal shutdown path.

## Root cause

`Renderer2D` stores its GPU handles — including the white texture — in a **file-scope
`static Renderer2DData s_Data;`** (`Renderer2D.cpp:184`). Issuing any `glDelete*` after the OpenGL context has
been destroyed dereferences thread-local driver state that no longer exists → access violation.

`Renderer2D::Shutdown()` already resets those `Ref<>`s **while the context is alive** (`Renderer2D.cpp` ~479-481,
with a comment describing exactly this crash), and `Application::Shutdown()` calls it **before** the window/
context is destroyed. So the **normal** exit path is safe.

The crash happens on the **abnormal** path: `Runtime/Main.cpp` did `new Application … Run() … delete app`
with no guard. If `Run()` threw (or the process otherwise aborted), `delete app` — and therefore
`Renderer2D::Shutdown()` — was skipped, the GL context was torn down, and the leftover static
`s_Data.WhiteTexture` was destroyed later (process-exit static destruction) with **no current context**.

## Fix

Two layers — make graceful shutdown the default, and make teardown crash-proof regardless:

1. **Context-guarded destructors (safety net).** Added `OpenGLContext::HasCurrentContext()`
   (`OpenGLContext.cpp`), a thin wrapper over `glfwGetCurrentContext() != nullptr` (safe to call even after
   `glfwTerminate`, which returns null). Every OpenGL resource destructor guards its `glDelete*`:

   ```cpp
   OpenGLTexture::~OpenGLTexture()
   {
       if (m_RendererID != 0 && OpenGLContext::HasCurrentContext())
           glDeleteTextures(1, &m_RendererID);
   }
   ```

   Applied to **all six**: `OpenGLTexture`, `OpenGLShader`, `OpenGLFrameBuffer`, `OpenGLVertexArray`,
   `OpenGLVertexBuffer`, `OpenGLIndexBuffer`. When the context is already gone the driver has reclaimed the
   GPU memory with it, so skipping the call leaks nothing.

2. **`main()` try/catch (graceful path).** `Runtime/Main.cpp` wraps `Run()` so an exception still reaches
   `delete app` (graceful shutdown, context alive, `Renderer2D::Shutdown()` runs) and is logged instead of
   `std::terminate`.

## Client-developer rule

Reset your own `Ref<>` GPU handles (textures, shaders, materials, framebuffers) in your layer's `OnDetach()`
so they free while the context is live. The destructor guard is a crash *safety net* for teardown ordering —
**not** a license to leak resources during normal operation.

## Caveat

`HasCurrentContext()` checks for a current context on the *calling thread*. Engine resources are created and
destroyed on the render thread (which has the context current during normal operation), so the guard only
ever suppresses deletes during teardown. If a future system creates/destroys GL objects on a worker thread
**without** a current context, the guard would silently skip the delete (a leak) — that thread should make a
context current instead.
