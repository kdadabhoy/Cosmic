#pragma once
// Window.h
// Last Modified 5/26/2026

/**
 * General Description:
 * The Window class acts as the primary interface between the Cosmic Engine and the
 * underlying Operating System. It abstracts the complexities of window creation,
 * hardware input management, and graphics context ownership. By utilizing GLFW,
 * it provides a platform-agnostic surface for rendering while translating native
 * OS messages (keyboard, mouse, window state) into the engine's internal Event system.
 *
 * Fullscreen Strategy (Windows):
 * Fullscreen is implemented as "borderless windowed fullscreen" — the window style
 * bits (WS_OVERLAPPEDWINDOW) are stripped and the window is stretched to cover the
 * target monitor without switching display modes. This approach:
 *   - Eliminates black-screen flashes caused by mode switches
 *   - Keeps the Windows compositor active (Win+Shift+S, screen capture tools work)
 *   - Avoids fighting GLFW's monitor-switch path, which has DWM interaction issues
 *   - Lets Alt+Tab work naturally since we remain a normal window
 * By default the cover rect is monitor height + 1 px (FullscreenCompatMode::
 * OversizeByOne, W3): an EXACTLY monitor-sized borderless window gets promoted to
 * DWM independent flip, and the demotion forced by capture overlays glitches the
 * GL present path. The 1 px oversize keeps the window permanently composed.
 *
 * Hotkey Override:
 * A per-frame global hotkey override callback can be registered via
 * SetFullscreenHotkeyOverride / ClearFullscreenHotkeyOverride.  The callback
 * receives raw GLFW key/action/mods and returns true if it consumed the input.
 * This is stored directly on Window (NOT in WindowData / GLFW user-pointer) to
 * keep DLL lifetime management straightforward: callers (e.g. Application) clear
 * the override before unloading a plugin DLL.
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. Window(int width, int height, const std::string& title)
 *    Pre:  GLFW can be initialised on the host platform.
 *    Post: Native window and OpenGL context created; event callbacks bound.
 *
 * 2. ~Window()
 *    Pre:  A valid Window instance exists.
 *    Post: Context deleted, GLFW window destroyed, windowing resources released.
 *          No Win32 handle is touched after the GLFW window is gone.
 *
 * 3. void PollEvents()
 *    Post: OS processes pending input; dispatched via registered EventCallback.
 *
 * 4. void SwapBuffers()
 *    Pre:  GraphicsContext initialised.
 *    Post: Front/back buffers swapped; frame presented.
 *
 * 5. unsigned int GetWidth() / GetHeight()
 *    Post: Returns current cached window client-area dimensions.
 *
 * 6. GLFWwindow* GetHandle()
 *    Post: Raw GLFW window pointer for API-specific operations.
 *
 * 7. void SetEventCallback(const EventCallbackFn& callback)
 *    Post: Target function that will receive all engine events from this window.
 *
 * 8. void SetVSync(bool enabled)
 *    Post: Enables or disables vertical synchronisation.
 *
 * 9. bool ShouldClose()
 *    Post: True if the OS has signalled that the window should terminate.
 *
 * 10. void GetSize(int* width, int* height)
 *     Post: Queries framebuffer size from GLFW directly (use for FBO matching).
 *
 * 11. void SetFullscreen(bool enabled)
 *     Post: Toggles borderless-windowed fullscreen on the monitor that currently
 *           contains the window centre.  No display-mode switch occurs.
 *
 * 12. bool IsFullscreen()
 *     Post: Returns the current fullscreen state.
 *
 * 13. void SetFullscreenHotkeyOverride(const FullscreenToggleActionFn& fn)
 *     Post: Registers a delegate that intercepts raw key events before the engine's
 *           default F11 handler.  Returns true from the delegate to consume the key.
 *           Safe to call across DLL boundaries; Application must clear this before
 *           unloading a plugin DLL (see ClearFullscreenHotkeyOverride).
 *
 * 14. void ClearFullscreenHotkeyOverride()
 *     Post: Removes any registered delegate.  Always call this before unloading a
 *           plugin DLL that registered an override.
 */

#include "core/Core.h"
#include "graphics/GraphicsContext.h"
#include "events/Event.h"
#include <string>
#include <functional>
#include <cstdint>

struct GLFWwindow;
struct GLFWmonitor;

namespace Cosmic
{
    // Callback signature: (key, action, mods) -> true = consumed
    using FullscreenToggleActionFn = std::function<bool(int key, int action, int mods)>;

    // Fullscreen sizing strategy (W3, docs/plans/09-windowing-plan.md).
    // ExactCover sizes the borderless window to exactly the monitor — DWM/the
    // driver may promote it to independent-flip "fullscreen optimizations",
    // and the forced demotion when a capture overlay (Win+Shift+S) appears
    // black-flashes/tears on the legacy GL present path. OversizeByOne adds
    // 1px of height so the window is never classified as fullscreen: no
    // promotion, so overlays composite normally. The taskbar still hides and
    // the extra row is off-screen; the only cost is composed-present latency
    // this tools engine does not need. OversizeByOne is the DEFAULT (W3
    // decision 2026-07-02 — GL has no API opt-out of the promotion heuristic;
    // the real fix, a DXGI flip-model swapchain, is out of scope per plan §4).
    // Override per-run with COSMIC_FULLSCREEN_COMPAT=exact|oversize for A/B.
    enum class FullscreenCompatMode
    {
        ExactCover,     // exact monitor cover (A/B control case)
        OversizeByOne,  // monitor height + 1 px — defeats iFlip promotion (default)
    };

    // Predicate used by the borderless-chrome hit test: given a point in CLIENT
    // pixels, return true where the cursor should DRAG the window (i.e. the custom
    // title bar, excluding its buttons/menus). Set by the layer drawing the bar.
    using TitlebarHitTestFn = std::function<bool(int x, int y)>;

    class COSMIC_API Window
    {
    public:
        using EventCallbackFn = std::function<void(Event&)>;

        // =====================================================================
        // Construction & Lifecycle
        // =====================================================================

        Window(int width, int height, const std::string& title);
        ~Window();

        // Non-copyable
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        // =====================================================================
        // Frame & Event Management
        // =====================================================================

        void PollEvents();
        void SwapBuffers();

        // =====================================================================
        // Window Attributes
        // =====================================================================

        inline unsigned int GetWidth()  const { return m_Data.Width; }
        inline unsigned int GetHeight() const { return m_Data.Height; }
        inline GLFWwindow* GetHandle() const { return m_Handle; }

        // =====================================================================
        // Configuration & Callbacks
        // =====================================================================

        void SetEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
        void SetVSync(bool enabled);
        bool IsVSync() const { return m_Data.VSync; }

        // =====================================================================
        // Fullscreen
        // =====================================================================

        void SetFullscreen(bool enabled);
        bool IsFullscreen() const { return m_Fullscreen; }

        // W3 debug/compat toggle — see FullscreenCompatMode above. Takes effect on
        // the next fullscreen enter (re-applied live if currently fullscreen).
        void                 SetFullscreenCompatMode(FullscreenCompatMode mode);
        FullscreenCompatMode GetFullscreenCompatMode() const { return m_FullscreenCompatMode; }

        // =====================================================================
        // Window trace (W1) — timestamped logging of window/DWM state changes:
        // WM_WINDOWPOSCHANGED, WM_SIZE, WM_DPICHANGED, focus, WM_SYSCOMMAND,
        // fullscreen transitions, style changes, slow SwapBuffers. Off by
        // default; enable with SetTraceEnabled(true) or the environment
        // variable COSMIC_WINDOW_TRACE=1.
        // =====================================================================

        static void SetTraceEnabled(bool enabled) { s_TraceEnabled = enabled; }
        static bool IsTraceEnabled()              { return s_TraceEnabled; }

        // =====================================================================
        // Modal frame pump (W4 — responsive rendering during drag/resize).
        // While the Win32 modal move/size loop runs, glfwPollEvents blocks and
        // the main loop starves. A WM_TIMER set on WM_ENTERSIZEMOVE pumps one
        // frame per tick through the callback (Application::RenderSingleFrame).
        // Default ON; clients opt out via Application::SetRenderWhileDragging.
        // =====================================================================

        static constexpr uintptr_t kModalFrameTimerId = 0xC05;

        void SetModalFrameCallback(const std::function<void()>& cb) { m_ModalFrameCallback = cb; }
        void SetModalRenderingEnabled(bool enabled);
        bool IsModalRenderingEnabled() const { return m_ModalRenderingEnabled; }

        // Used by the native WndProc subclass (implementation detail; public so
        // the file-local Win32 proc can reach them without exposing Win32 types).
        void BeginModalFramePump();
        void EndModalFramePump();
        void ModalFrameTick();
        // Re-issues the fullscreen cover SetWindowPos after a display/DPI change
        // while fullscreen (W5.3). No-op when not fullscreen.
        void ReassertFullscreenCoverWin32();

        // =====================================================================
        // Window state controls (used by the custom title bar)
        // =====================================================================

        void Minimize();
        void Maximize();
        void Restore();
        void ToggleMaximize();

        // Runtime window identity (S5): a packaged app opens at its authored title
        // and size (project.cproj [window] keys, applied by PlayerLayer on attach).
        // SetSize is a no-op while fullscreen and clamps out absurd values.
        void SetTitle(const std::string& title);
        void SetSize(int width, int height);
        // NOTE: named IsWindowMaximized (not IsMaximized) because <windows.h>
        // defines IsMaximized as a macro aliasing IsZoomed.
        bool IsWindowMaximized() const;
        void Close();   // request the window to close (quits the app)

        // =====================================================================
        // Borderless custom chrome (Windows)
        //
        // When enabled the OS title bar/frame is removed but the window keeps
        // native resize, Aero Snap, min/max animations and the drop shadow (via a
        // WndProc subclass + DWM). The app draws its own title bar and reports the
        // draggable region through SetTitlebarHitTestCallback(). On by default on
        // Windows; SetCustomChrome(false) restores the standard OS frame.
        // =====================================================================

        void SetCustomChrome(bool enabled);
        bool HasCustomChrome() const { return m_CustomChrome; }

        void SetTitlebarHitTestCallback(const TitlebarHitTestFn& fn) { m_TitlebarHit = fn; }
        void ClearTitlebarHitTestCallback() { m_TitlebarHit = nullptr; }

        // Used by the native WndProc subclass (implementation detail; public so
        // the file-local Win32 proc can reach them without exposing Win32 types).
        bool      TitlebarHitTest(int x, int y) const { return m_TitlebarHit ? m_TitlebarHit(x, y) : false; }
        intptr_t  NativeOrigWndProc() const { return m_OrigWndProc; }

        // =====================================================================
        // Hotkey Override (cleared by Application before plugin DLL unload)
        // =====================================================================

        void SetFullscreenHotkeyOverride(const FullscreenToggleActionFn& fn)
        {
            m_HotkeyOverride = fn;
        }

        void ClearFullscreenHotkeyOverride()
        {
            m_HotkeyOverride = nullptr;
        }

        // =====================================================================
        // Utility
        // =====================================================================

        bool ShouldClose()                          const;
        void GetSize(int* width, int* height)       const;

    private:
        // =====================================================================
        // Internal helpers
        // =====================================================================

        // Called from the GLFW key callback before dispatching engine events.
        // Returns true if the key was consumed by the override or the F11 handler.
        bool HandleFullscreenHotkey(int key, int action, int mods);

        // Applies or removes borderless-fullscreen Win32 style bits.
        // Safe to call only while m_Handle is valid.
        void ApplyFullscreenWin32(bool enabled);

        // Install / remove the borderless WndProc subclass (Windows only).
        void EnableCustomChromeWin32();
        void DisableCustomChromeWin32();

        // Finds the GLFWmonitor whose work-area contains the window centre.
        GLFWmonitor* FindCurrentMonitor() const;

        // =====================================================================
        // State shared with GLFW static callbacks via glfwSetWindowUserPointer
        // =====================================================================
        struct WindowData
        {
            std::string     Title;
            unsigned int    Width = 0;
            unsigned int    Height = 0;
            bool            VSync = false;
            EventCallbackFn EventCallback;

            // Pointer back to the owning Window so static GLFW lambdas can reach it.
            Window* Self = nullptr;
        };

        // =====================================================================
        // Members
        // =====================================================================

        GLFWwindow* m_Handle = nullptr;
        Scope<GraphicsContext> m_Context;   // owned; released before glfwDestroyWindow in the destructor

        WindowData m_Data;

        // Fullscreen state
        bool m_Fullscreen = false;
        // W3 default: oversize-by-one (see FullscreenCompatMode above). The
        // ctor honours COSMIC_FULLSCREEN_COMPAT=exact|oversize for A/B runs.
        FullscreenCompatMode m_FullscreenCompatMode = FullscreenCompatMode::OversizeByOne;

        // Saved windowed rect (window rect, screen coords) + maximize state (W5.1:
        // a window that was maximized when it entered fullscreen must restore as
        // maximized, not as a floating window with the maximized rect).
        int  m_SavedX = 100;
        int  m_SavedY = 100;
        int  m_SavedWidth = 1280;
        int  m_SavedHeight = 720;
        bool m_SavedMaximized = false;

        // Modal frame pump state (W4)
        bool m_InModalLoop = false;
        bool m_ModalRenderingEnabled = true;
        std::function<void()> m_ModalFrameCallback;

        // Window trace switch (W1) — process-wide, cheap to test per message.
        static bool s_TraceEnabled;

        // Plugin-registered hotkey override — lives on Window, NOT in WindowData,
        // so Application can always reach it without going through the GLFW user pointer.
        FullscreenToggleActionFn m_HotkeyOverride;

        // Borderless custom chrome state.
        bool              m_CustomChrome = false;  // true once the WndProc subclass is installed
        TitlebarHitTestFn m_TitlebarHit;           // draggable-region predicate (client px)
        intptr_t          m_OrigWndProc = 0;       // original GLFW WNDPROC, for CallWindowProc
    };

} // namespace Cosmic