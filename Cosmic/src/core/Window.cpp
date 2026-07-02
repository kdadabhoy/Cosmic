// Window.cpp
// Last Modified 5/26/2026
//
// Fullscreen implementation notes
// --------------------------------
// We use "borderless windowed fullscreen" rather than exclusive fullscreen or
// GLFW's monitor-switch path.  The technique is:
//
//   Enter fullscreen:
//     1. Save the current windowed position + size.
//     2. Strip WS_OVERLAPPEDWINDOW (title bar, borders, resize frame) from the
//        Win32 style bits via SetWindowLong.
//     3. Call SetWindowPos to cover the target monitor's full resolution.
//     4. Do NOT call glfwSetWindowMonitor — that triggers a display-mode switch
//        which causes DWM to flash black.
//
//   Exit fullscreen:
//     1. Restore WS_OVERLAPPEDWINDOW style bits.
//     2. Call SetWindowPos with the saved rect.
//     3. Tell GLFW about the new size so its internal state stays consistent.
//
// Why not ClipCursor?
//   ClipCursor prevents Win+Shift+S (Snipping Tool), screenshot overlays, and
//   multi-monitor mouse movement.  Borderless windowed fullscreen does not need
//   cursor confinement — the window already fills the monitor.
//
// Why not HWND_TOPMOST?
//   HWND_TOPMOST fights the compositor and causes rendering artefacts with
//   hardware overlays (Discord, GeForce Experience, Xbox Game Bar).  Since we
//   are not exclusive fullscreen we do not need it.

#include "core/Window.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"
#include "platform/opengl/OpenGLContext.h"
#include "codes/KeyCodes.h"
#include "core/Log.h"

// 1. Standard library dependencies
#include <iostream>

// 2. Platform Specific Windows block
#ifdef _WIN32
    #include <Windows.h>
#endif

// 3. Main library includes (Includes Windows backend nicely now)
#include <GLFW/glfw3.h>

// 4. Native extensions (Loaded only after standard GLFW declarations are mapped)
#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
    #include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
    #include <dwmapi.h>     // DwmExtendFrameIntoClientArea (link: dwmapi)
#endif

// =============================================================================
// Borderless custom chrome — Win32 WndProc subclass
// -----------------------------------------------------------------------------
// We keep the window's standard style (WS_OVERLAPPEDWINDOW), so Windows still
// gives us native resize, Aero Snap, min/max animations and the drop shadow, but
// we remove the *visual* frame in WM_NCCALCSIZE and re-implement hit-testing for
// resize borders + a client-drawn title bar in WM_NCHITTEST. The GLFW WndProc is
// chained via CallWindowProc so all normal input keeps working.
// =============================================================================
#ifdef _WIN32
namespace
{
    constexpr int kResizeBorder = 8; // resize-grip thickness in pixels

    LRESULT CosmicHitTest(Cosmic::Window* self, HWND hwnd, LPARAM lParam)
    {
        if (self->IsFullscreen())
            return HTCLIENT; // no drag / no resize while covering the monitor

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT  rc; GetWindowRect(hwnd, &rc);

        const bool maximized = IsZoomed(hwnd) != 0;
        const bool left   = pt.x <  rc.left   + kResizeBorder;
        const bool right  = pt.x >= rc.right  - kResizeBorder;
        const bool top    = pt.y <  rc.top    + kResizeBorder;
        const bool bottom = pt.y >= rc.bottom - kResizeBorder;

        if (!maximized) // resize only when not maximized
        {
            if (top && left)     return HTTOPLEFT;
            if (top && right)    return HTTOPRIGHT;
            if (bottom && left)  return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left)   return HTLEFT;
            if (right)  return HTRIGHT;
            if (top)    return HTTOP;
            if (bottom) return HTBOTTOM;
        }

        // Draggable title bar region (reported by the layer drawing it).
        POINT cp = pt; ScreenToClient(hwnd, &cp);
        if (self->TitlebarHitTest(static_cast<int>(cp.x), static_cast<int>(cp.y)))
            return HTCAPTION; // native drag + double-click maximize + snap

        return HTCLIENT;
    }

    LRESULT CALLBACK CosmicWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto*   self = reinterpret_cast<Cosmic::Window*>(GetPropW(hwnd, L"CosmicWindowPtr"));
        WNDPROC orig = self ? reinterpret_cast<WNDPROC>(self->NativeOrigWndProc()) : nullptr;

        if (self && self->HasCustomChrome())
        {
            switch (msg)
            {
            case WM_NCCALCSIZE:
                if (wParam == TRUE)
                {
                    auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                    // Maximized (but not fullscreen): inset by the frame so the
                    // window neither spills off-screen nor covers the taskbar.
                    if (!self->IsFullscreen() && IsZoomed(hwnd))
                    {
                        const int xb = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                        const int yb = GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                        p->rgrc[0].left   += xb;
                        p->rgrc[0].right  -= xb;
                        p->rgrc[0].top    += yb;
                        p->rgrc[0].bottom -= yb;
                    }
                    return 0; // client area == whole window: no OS title bar
                }
                break;

            case WM_NCHITTEST:
                return CosmicHitTest(self, hwnd, lParam);
            }
        }

        if (orig) return CallWindowProcW(orig, hwnd, msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
#endif // _WIN32

namespace Cosmic
{
	// =========================================================================
	// Construction
	// =========================================================================

	Window::Window(int width, int height, const std::string& title)
	{
		if (!glfwInit())
		{
			CS_CORE_CRITICAL("Window: Failed to initialise GLFW!");
			return;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		// Keep window visible even when focus changes; avoids an iconify on Alt+Tab
		// out of fullscreen.
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

#ifdef _WIN32
		// --- Borderless custom chrome: model the window as FRAMELESS in GLFW ---
		// We draw our own title bar, so the window must be borderless. Critically, we
		// tell GLFW it is undecorated BEFORE creation. GLFW computes every window rect
		// (creation, DPI changes, min/max, resize) with AdjustWindowRectExForDpi() using
		// its OWN notion of the style — and for a decorated window that adds a DPI-SCALED
		// caption + resize frame. If we kept GLFW "decorated" and only stripped the frame
		// visually via WM_NCCALCSIZE, GLFW's geometry would include a phantom frame that
		// grows with the monitor's scale (≈0 at 100%, tens of px at 125%+), which on a
		// HiDPI laptop pushes the custom title bar off-screen and offsets every mouse
		// click until a manual SetWindowPos (F11) overrides it. With GLFW_DECORATED=FALSE,
		// getWindowStyle() is frameless (WS_POPUP) so AdjustWindowRectExForDpi adds ZERO
		// frame at any DPI — GLFW's client model matches the real client. We re-add the
		// native Win32 style bits (resize/snap/animations/shadow) in EnableCustomChromeWin32;
		// GLFW never sees those because it always feeds its own computed style to the DPI math.
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
		// Create hidden; we show it only after the chrome is fully applied, so the first
		// visible frame already has the settled frameless client (no first-show DPI race).
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#endif

		m_Handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
		if (!m_Handle)
		{
			CS_CORE_CRITICAL("Window: glfwCreateWindow failed!");
			glfwTerminate();
			return;
		}

		m_Context = CreateScope<OpenGLContext>(m_Handle);
		m_Context->Init();

		m_Data.Title = title;
		m_Data.Width = static_cast<unsigned int>(width);
		m_Data.Height = static_cast<unsigned int>(height);
		m_Data.Self = this;

		// Default no-op so any GLFW callback that fires DURING construction never
		// invokes an empty std::function. In particular, enabling custom chrome
		// (SetCustomChrome below) triggers a WM_SIZE when the frame is removed,
		// which GLFW turns into a window-size callback — and Application doesn't
		// install the real EventCallback until after the Window is constructed.
		m_Data.EventCallback = [](Event&) {};

		// Save initial windowed position so we can restore it after fullscreen.
		glfwGetWindowPos(m_Handle, &m_SavedX, &m_SavedY);
		m_SavedWidth = width;
		m_SavedHeight = height;

		glfwSetWindowUserPointer(m_Handle, &m_Data);

		// ---------------------------------------------------------------------
		// GLFW Callbacks
		// All lambdas capture nothing from the host scope — they reach Window
		// state through the WindowData pointer stored in the GLFW user pointer.
		// ---------------------------------------------------------------------

		glfwSetWindowSizeCallback(m_Handle, [](GLFWwindow* win, int w, int h)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				data.Width = static_cast<unsigned int>(w);
				data.Height = static_cast<unsigned int>(h);

				WindowResizeEvent e(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
				data.EventCallback(e);
			});

		glfwSetWindowCloseCallback(m_Handle, [](GLFWwindow* win)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				WindowCloseEvent e;
				data.EventCallback(e);
			});

		glfwSetScrollCallback(m_Handle, [](GLFWwindow* win, double xOff, double yOff)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				MouseScrolledEvent e(static_cast<float>(xOff), static_cast<float>(yOff));
				data.EventCallback(e);
			});

		glfwSetMouseButtonCallback(m_Handle, [](GLFWwindow* win, int btn, int action, int /*mods*/)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				switch (action)
				{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent e(btn);
					data.EventCallback(e);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent e(btn);
					data.EventCallback(e);
					break;
				}
				}
			});

		glfwSetCursorPosCallback(m_Handle, [](GLFWwindow* win, double x, double y)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				MouseMovedEvent e(static_cast<float>(x), static_cast<float>(y));
				data.EventCallback(e);
			});

		// Key callback — hotkey handling delegated to Window::HandleFullscreenHotkey
		// so the override and F11 logic live in one place with clear ownership.
		glfwSetKeyCallback(m_Handle, [](GLFWwindow* win, int key, int /*scancode*/, int action, int mods)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));

				// Let the Window handle fullscreen toggling first.
				// If consumed, do not propagate the key to the engine event system.
				if (data.Self && data.Self->HandleFullscreenHotkey(key, action, mods))
					return;

				switch (action)
				{
				case GLFW_PRESS: { KeyPressedEvent  e(key, 0); data.EventCallback(e); break; }
				case GLFW_RELEASE: { KeyReleasedEvent e(key);    data.EventCallback(e); break; }
				case GLFW_REPEAT: { KeyPressedEvent  e(key, 1); data.EventCallback(e); break; }
				}
			});

		glfwSetCharCallback(m_Handle, [](GLFWwindow* win, unsigned int codepoint)
			{
				auto& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(win));
				KeyTypedEvent e(static_cast<int>(codepoint));
				data.EventCallback(e);
			});

		SetVSync(true);

#ifdef _WIN32
		// Borderless custom chrome on by default (Windows). Call SetCustomChrome(false)
		// to fall back to the standard OS title bar if needed.
		SetCustomChrome(true);

		// The window was created hidden (GLFW_VISIBLE=FALSE) so the chrome could be fully
		// applied first. Now reveal it — the first painted frame already has the settled
		// frameless client, so the custom title bar and mouse mapping are correct from
		// frame one with no F11 nudge needed.
		glfwShowWindow(m_Handle);
#endif

		// Cache the true client size now that the window exists and chrome is applied.
		// glfwGetFramebufferSize queries the live client rect, so m_Data never starts stale.
		{
			int fbW = 0, fbH = 0;
			glfwGetFramebufferSize(m_Handle, &fbW, &fbH);
			if (fbW > 0 && fbH > 0)
			{
				m_Data.Width  = static_cast<unsigned int>(fbW);
				m_Data.Height = static_cast<unsigned int>(fbH);
			}
		}
	}

	// =========================================================================
	// Destruction
	// =========================================================================

	Window::~Window()
	{
		// Clear the hotkey override first so nothing can fire during teardown.
		m_HotkeyOverride = nullptr;

		// If we are in fullscreen, restore the window style before destroying it
		// so the DWM does not leave the desktop in an unusual state.
		if (m_Fullscreen && m_Handle)
		{
#ifdef _WIN32
			HWND hwnd = glfwGetWin32Window(m_Handle);
			if (hwnd)
			{
				LONG style = GetWindowLong(hwnd, GWL_STYLE);
				style |= WS_OVERLAPPEDWINDOW;
				SetWindowLong(hwnd, GWL_STYLE, style);
				// SetWindowPos to apply the style change immediately; position
				// doesn't matter since the window is about to be destroyed.
				SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			}
#endif
			m_Fullscreen = false;
		}

#ifdef _WIN32
		// Restore the original GLFW WndProc before the window goes away.
		if (m_CustomChrome)
			DisableCustomChromeWin32();
#endif

		// Null out the GLFW user pointer so any lingering callbacks can't
		// dereference our now-dying WindowData.
		if (m_Handle)
			glfwSetWindowUserPointer(m_Handle, nullptr);

		// Release the graphics context at the same point the manual delete used to
		// run — before glfwDestroyWindow, while the native window still exists.
		m_Context.reset();

		if (m_Handle)
		{
			glfwDestroyWindow(m_Handle);
			m_Handle = nullptr;
		}

		// ARCHITECTURAL CONSTRAINT: glfwTerminate() is intentionally called here rather than
		// at the Application level. This is safe only because the engine is single-window.
		// If a second Window is ever introduced, this call must be moved to Application::Shutdown()
		// and balanced with the glfwInit() call there — terminating GLFW inside a Window destructor
		// would invalidate all remaining GLFW handles and crash on the next glfwPollEvents.
		glfwTerminate();
	}

	// =========================================================================
	// Frame
	// =========================================================================

	void Window::PollEvents() { glfwPollEvents(); }

	void Window::SwapBuffers()
	{
		CS_CORE_ASSERT(m_Context, "SwapBuffers called on a window with no graphics context.");
		m_Context->SwapBuffers();
	}
	bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Handle); }

	void Window::GetSize(int* width, int* height) const
	{
		glfwGetFramebufferSize(m_Handle, width, height);
	}

	// =========================================================================
	// VSync
	// =========================================================================

	void Window::SetVSync(bool enabled)
	{
		glfwSwapInterval(enabled ? 1 : 0);
		m_Data.VSync = enabled;
	}

	// =========================================================================
	// Window state controls (driven by the custom title bar)
	// =========================================================================

	void Window::Minimize()       { if (m_Handle) glfwIconifyWindow(m_Handle); }
	void Window::Maximize()       { if (m_Handle) glfwMaximizeWindow(m_Handle); }
	void Window::Restore()        { if (m_Handle) glfwRestoreWindow(m_Handle); }
	void Window::Close()          { if (m_Handle) glfwSetWindowShouldClose(m_Handle, GLFW_TRUE); }

	bool Window::IsWindowMaximized() const
	{
		return m_Handle && glfwGetWindowAttrib(m_Handle, GLFW_MAXIMIZED) != 0;
	}

	void Window::ToggleMaximize()
	{
		if (IsWindowMaximized()) Restore();
		else                     Maximize();
	}

	// =========================================================================
	// Borderless custom chrome
	// =========================================================================

	void Window::SetCustomChrome(bool enabled)
	{
#ifdef _WIN32
		if (enabled == m_CustomChrome) return;
		if (enabled) EnableCustomChromeWin32();
		else         DisableCustomChromeWin32();
#else
		(void)enabled; // custom chrome is Windows-only for now
#endif
	}

	void Window::EnableCustomChromeWin32()
	{
#ifdef _WIN32
		HWND hwnd = glfwGetWin32Window(m_Handle);
		if (!hwnd) return;

		SetPropW(hwnd, L"CosmicWindowPtr", reinterpret_cast<HANDLE>(this));
		m_OrigWndProc = static_cast<intptr_t>(
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&CosmicWndProc)));
		m_CustomChrome = true;

		// GLFW created this window borderless (GLFW_DECORATED=FALSE) so its DPI geometry
		// math stays frame-free. Re-add the FULL native style on the real window so Windows
		// still provides native resize, Aero Snap, min/max animations and (with DWM below)
		// the drop shadow. The visual frame is removed by our WM_NCCALCSIZE handler, so this
		// only restores *behaviors*, not chrome. GLFW never reads GetWindowLong for its size
		// math — it uses its own (still-borderless) style — so these bits are invisible to it.
		{
			LONG style = GetWindowLong(hwnd, GWL_STYLE);
			style |= WS_OVERLAPPEDWINDOW; // WS_CAPTION|WS_THICKFRAME|WS_SYSMENU|WS_MIN/MAXIMIZEBOX
			SetWindowLong(hwnd, GWL_STYLE, style);
		}

		// A 1px frame extension gives the borderless window its drop shadow while
		// the compositor is active.
		MARGINS margins = { 1, 1, 1, 1 };
		DwmExtendFrameIntoClientArea(hwnd, &margins);

		// Recompute the non-client area now that WM_NCCALCSIZE is intercepted.
		SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

		CS_CORE_INFO("Window: borderless custom chrome enabled.");
#endif
	}

	void Window::DisableCustomChromeWin32()
	{
#ifdef _WIN32
		HWND hwnd = glfwGetWin32Window(m_Handle);
		if (hwnd && m_OrigWndProc)
		{
			SetWindowLongPtrW(hwnd, GWLP_WNDPROC, static_cast<LONG_PTR>(m_OrigWndProc));
			RemovePropW(hwnd, L"CosmicWindowPtr");
			SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
		m_OrigWndProc  = 0;
		m_CustomChrome = false;
#endif
	}

	// =========================================================================
	// Fullscreen — borderless windowed technique
	// =========================================================================

	void Window::SetFullscreen(bool enabled)
	{
		if (m_Fullscreen == enabled || !m_Handle)
			return;

		m_Fullscreen = enabled;
		ApplyFullscreenWin32(enabled);
	}

	void Window::ApplyFullscreenWin32(bool enabled)
	{
#ifdef _WIN32
		HWND hwnd = glfwGetWin32Window(m_Handle);
		if (!hwnd)
		{
			CS_CORE_ERROR("Window::ApplyFullscreenWin32: No valid HWND.");
			return;
		}

		// NOTE: even with custom chrome we use the style-strip path below. Stripping
		// WS_OVERLAPPEDWINDOW is what makes the Windows shell treat us as a true
		// fullscreen window and HIDE THE TASKBAR. (Just resizing a styled window to
		// the monitor leaves the taskbar on top.) The WndProc subclass keeps working
		// throughout — its WM_NCHITTEST/WM_NCCALCSIZE are gated on m_Fullscreen.

		if (enabled)
		{
			// ---- Entering fullscreen ----

			// 1. Save current windowed rect (window position, not client area).
			RECT winRect = {};
			GetWindowRect(hwnd, &winRect);
			m_SavedX = winRect.left;
			m_SavedY = winRect.top;
			m_SavedWidth = winRect.right - winRect.left;
			m_SavedHeight = winRect.bottom - winRect.top;

			// 2. Find the monitor the window centre currently lives on.
			GLFWmonitor* monitor = FindCurrentMonitor();
			if (!monitor)
				monitor = glfwGetPrimaryMonitor();

			int monX = 0, monY = 0;
			glfwGetMonitorPos(monitor, &monX, &monY);
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);

			// 3. Strip window decorations via style bits.
			//    We keep WS_VISIBLE so the window does not flash hidden.
			LONG style = GetWindowLong(hwnd, GWL_STYLE);
			style &= ~WS_OVERLAPPEDWINDOW;
			SetWindowLong(hwnd, GWL_STYLE, style);

			// 4. Stretch window to cover the monitor exactly.
			//    SWP_FRAMECHANGED forces the style change to be applied.
			//    No HWND_TOPMOST — we stay in the normal z-order stack.
			SetWindowPos(hwnd, HWND_TOP,
				monX, monY,
				mode->width, mode->height,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

			CS_CORE_INFO("Window: Entered borderless fullscreen ({}x{} @ {},{}).",
				mode->width, mode->height, monX, monY);
		}
		else
		{
			// ---- Exiting fullscreen ----

			// 1. Restore window decorations.
			LONG style = GetWindowLong(hwnd, GWL_STYLE);
			style |= WS_OVERLAPPEDWINDOW;
			SetWindowLong(hwnd, GWL_STYLE, style);

			// 2. Restore saved position and size.
			SetWindowPos(hwnd, HWND_TOP,
				m_SavedX, m_SavedY,
				m_SavedWidth, m_SavedHeight,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);

			CS_CORE_INFO("Window: Restored windowed mode ({}x{} @ {},{}).",
				m_SavedWidth, m_SavedHeight, m_SavedX, m_SavedY);
		}

		// Keep VSync setting intact across the transition.
		glfwSwapInterval(m_Data.VSync ? 1 : 0);

#else
		// Non-Windows fallback: use GLFW's built-in fullscreen toggle.
		// This may cause a mode switch on some platforms.
		if (enabled)
		{
			GLFWmonitor* monitor = FindCurrentMonitor();
			if (!monitor) monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);

			glfwGetWindowPos(m_Handle, &m_SavedX, &m_SavedY);
			int w, h;
			glfwGetWindowSize(m_Handle, &w, &h);
			m_SavedWidth = w;
			m_SavedHeight = h;

			glfwSetWindowMonitor(m_Handle, monitor,
				0, 0, mode->width, mode->height, mode->refreshRate);
		}
		else
		{
			glfwSetWindowMonitor(m_Handle, nullptr,
				m_SavedX, m_SavedY, m_SavedWidth, m_SavedHeight, 0);
		}
		glfwSwapInterval(m_Data.VSync ? 1 : 0);
#endif
	}

	// =========================================================================
	// Monitor detection
	// =========================================================================

	GLFWmonitor* Window::FindCurrentMonitor() const
	{
		// Window centre in screen coordinates.
		int wx = 0, wy = 0, ww = 0, wh = 0;
		glfwGetWindowPos(m_Handle, &wx, &wy);
		glfwGetWindowSize(m_Handle, &ww, &wh);

		int centreX = wx + ww / 2;
		int centreY = wy + wh / 2;

		int count = 0;
		GLFWmonitor** monitors = glfwGetMonitors(&count);
		GLFWmonitor* best = glfwGetPrimaryMonitor();

		for (int i = 0; i < count; ++i)
		{
			int mx = 0, my = 0;
			glfwGetMonitorPos(monitors[i], &mx, &my);
			const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
			if (!mode) continue;

			if (centreX >= mx && centreX < mx + mode->width &&
				centreY >= my && centreY < my + mode->height)
			{
				best = monitors[i];
				break;
			}
		}

		return best;
	}

	// =========================================================================
	// Fullscreen hotkey dispatch
	// =========================================================================

	bool Window::HandleFullscreenHotkey(int key, int action, int mods)
	{
		// Give the registered override first refusal.
		if (m_HotkeyOverride)
		{
			if (m_HotkeyOverride(key, action, mods))
				return true; // consumed by the override
		}

		// Default: F11 on press (not repeat) toggles fullscreen.
		if (key == CS_KEY_F11 && action == GLFW_PRESS)
		{
			SetFullscreen(!m_Fullscreen);
			return true;
		}

		return false;
	}

} // namespace Cosmic