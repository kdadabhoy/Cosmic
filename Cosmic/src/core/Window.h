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

        // =====================================================================
        // Window state controls (used by the custom title bar)
        // =====================================================================

        void Minimize();
        void Maximize();
        void Restore();
        void ToggleMaximize();
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
        GraphicsContext* m_Context = nullptr;   // owned; deleted in destructor

        WindowData m_Data;

        // Fullscreen state
        bool m_Fullscreen = false;

        // Saved windowed rect (client area, screen coords)
        int  m_SavedX = 100;
        int  m_SavedY = 100;
        int  m_SavedWidth = 1280;
        int  m_SavedHeight = 720;

        // Plugin-registered hotkey override — lives on Window, NOT in WindowData,
        // so Application can always reach it without going through the GLFW user pointer.
        FullscreenToggleActionFn m_HotkeyOverride;

        // Borderless custom chrome state.
        bool              m_CustomChrome = false;  // true once the WndProc subclass is installed
        TitlebarHitTestFn m_TitlebarHit;           // draggable-region predicate (client px)
        intptr_t          m_OrigWndProc = 0;       // original GLFW WNDPROC, for CallWindowProc
    };

} // namespace Cosmic