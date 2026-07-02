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
#include <cstdlib>    // std::getenv (COSMIC_WINDOW_TRACE, non-Windows path)
#include <algorithm>  // std::min / std::clamp (restore-rect clamping)

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
// Window trace (W1, docs/plans/09-windowing-plan.md)
// -----------------------------------------------------------------------------
// Timestamped logging of every window-state transition the OS hands us, plus
// our own fullscreen steps and slow SwapBuffers. spdlog's pattern already
// carries millisecond timestamps. Enable via Window::SetTraceEnabled(true) or
// the environment variable COSMIC_WINDOW_TRACE=1 (read once at first Window
// construction).
// =============================================================================
#define CS_WINTRACE(...) do { if (::Cosmic::Window::IsTraceEnabled()) { CS_CORE_TRACE("[WinTrace] " __VA_ARGS__); } } while (0)

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

    // W1 trace helper — logs the message with the current framebuffer size so
    // every event line shows what size the engine believes it is rendering at.
    void TraceWindowMessage(Cosmic::Window* self, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (!Cosmic::Window::IsTraceEnabled())
            return;

        int fbW = 0, fbH = 0;
        self->GetSize(&fbW, &fbH);

        switch (msg)
        {
        case WM_WINDOWPOSCHANGED:
        {
            const auto* wp = reinterpret_cast<const WINDOWPOS*>(lParam);
            CS_CORE_TRACE("[WinTrace] WM_WINDOWPOSCHANGED pos=({},{}) size={}x{} flags=0x{:X} fb={}x{}",
                wp->x, wp->y, wp->cx, wp->cy, wp->flags, fbW, fbH);
            break;
        }
        case WM_SIZE:
            CS_CORE_TRACE("[WinTrace] WM_SIZE type={} client={}x{} fb={}x{}",
                wParam == SIZE_MINIMIZED ? "MINIMIZED" : wParam == SIZE_MAXIMIZED ? "MAXIMIZED" : "RESTORED",
                LOWORD(lParam), HIWORD(lParam), fbW, fbH);
            break;
        case WM_DPICHANGED:
            CS_CORE_TRACE("[WinTrace] WM_DPICHANGED dpi={} suggested=({},{},{},{}) fb={}x{}",
                HIWORD(wParam),
                reinterpret_cast<RECT*>(lParam)->left,  reinterpret_cast<RECT*>(lParam)->top,
                reinterpret_cast<RECT*>(lParam)->right, reinterpret_cast<RECT*>(lParam)->bottom,
                fbW, fbH);
            break;
        case WM_ACTIVATE:
            CS_CORE_TRACE("[WinTrace] WM_ACTIVATE state={} minimized={} fb={}x{}",
                LOWORD(wParam) == WA_INACTIVE ? "INACTIVE" : LOWORD(wParam) == WA_CLICKACTIVE ? "CLICKACTIVE" : "ACTIVE",
                HIWORD(wParam) != 0, fbW, fbH);
            break;
        case WM_SETFOCUS:
            CS_CORE_TRACE("[WinTrace] WM_SETFOCUS fb={}x{}", fbW, fbH);
            break;
        case WM_KILLFOCUS:
            CS_CORE_TRACE("[WinTrace] WM_KILLFOCUS fb={}x{}", fbW, fbH);
            break;
        case WM_SYSCOMMAND:
            CS_CORE_TRACE("[WinTrace] WM_SYSCOMMAND cmd=0x{:X} fb={}x{}", wParam & 0xFFF0u, fbW, fbH);
            break;
        case WM_DISPLAYCHANGE:
            CS_CORE_TRACE("[WinTrace] WM_DISPLAYCHANGE {}x{} bpp={} fb={}x{}",
                LOWORD(lParam), HIWORD(lParam), wParam, fbW, fbH);
            break;
        case WM_ENTERSIZEMOVE:
            CS_CORE_TRACE("[WinTrace] WM_ENTERSIZEMOVE (modal move/size loop begins) fb={}x{}", fbW, fbH);
            break;
        case WM_EXITSIZEMOVE:
            CS_CORE_TRACE("[WinTrace] WM_EXITSIZEMOVE (modal move/size loop ends) fb={}x{}", fbW, fbH);
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK CosmicWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto*   self = reinterpret_cast<Cosmic::Window*>(GetPropW(hwnd, L"CosmicWindowPtr"));
        WNDPROC orig = self ? reinterpret_cast<WNDPROC>(self->NativeOrigWndProc()) : nullptr;

        // ---------------------------------------------------------------------
        // Chrome-independent handling: trace (W1), the modal frame pump (W4),
        // and fullscreen re-assertion on display/DPI changes (W5.3). The modal
        // move/size freeze exists with or without custom chrome, so none of
        // this is gated on HasCustomChrome().
        // ---------------------------------------------------------------------
        if (self)
        {
            TraceWindowMessage(self, hwnd, msg, wParam, lParam);

            switch (msg)
            {
            case WM_ENTERSIZEMOVE:
            case WM_ENTERMENULOOP:
                self->BeginModalFramePump();
                break;  // fall through to GLFW (cursor handling)
            case WM_EXITSIZEMOVE:
            case WM_EXITMENULOOP:
                self->EndModalFramePump();
                break;  // fall through to GLFW
            case WM_TIMER:
                if (wParam == Cosmic::Window::kModalFrameTimerId)
                {
                    self->ModalFrameTick();
                    return 0;
                }
                break;
            case WM_DISPLAYCHANGE:
                // Resolution/monitor layout changed under us. If fullscreen,
                // re-cover the (possibly re-sized) monitor after GLFW has seen
                // the message.
                if (self->IsFullscreen() && orig)
                {
                    const LRESULT r = CallWindowProcW(orig, hwnd, msg, wParam, lParam);
                    self->ReassertFullscreenCoverWin32();
                    return r;
                }
                break;
            case WM_DPICHANGED:
                // Scale changed while fullscreen (settings change — you can't
                // drag a fullscreen window between monitors): let GLFW process
                // the DPI change, then re-assert the exact monitor cover so the
                // suggested-rect reposition can't shrink us out of fullscreen.
                if (self->IsFullscreen() && orig)
                {
                    const LRESULT r = CallWindowProcW(orig, hwnd, msg, wParam, lParam);
                    self->ReassertFullscreenCoverWin32();
                    return r;
                }
                break;
            }
        }

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
	// Window trace switch (W1) — off unless SetTraceEnabled(true) or
	// COSMIC_WINDOW_TRACE=1 in the environment (checked once, below).
	bool Window::s_TraceEnabled = false;

	// =========================================================================
	// Construction
	// =========================================================================

	Window::Window(int width, int height, const std::string& title)
	{
		// One-time environment check so the trace can be enabled without a
		// client code change (COSMIC_WINDOW_TRACE=1).
		static const bool s_TraceEnvChecked = []()
		{
#ifdef _WIN32
			char buf[8] = {};
			if (GetEnvironmentVariableA("COSMIC_WINDOW_TRACE", buf, sizeof(buf)) > 0 && buf[0] == '1')
				s_TraceEnabled = true;
#else
			if (const char* v = std::getenv("COSMIC_WINDOW_TRACE"); v && v[0] == '1')
				s_TraceEnabled = true;
#endif
			return true;
		}();
		(void)s_TraceEnvChecked;

#ifdef _WIN32
		// W3 A/B override: COSMIC_FULLSCREEN_COMPAT=exact|oversize picks the
		// fullscreen sizing strategy for this run without a rebuild (default is
		// OversizeByOne — see FullscreenCompatMode in Window.h).
		{
			char cbuf[16] = {};
			if (GetEnvironmentVariableA("COSMIC_FULLSCREEN_COMPAT", cbuf, sizeof(cbuf)) > 0)
			{
				if (cbuf[0] == 'e' || cbuf[0] == 'E')
					m_FullscreenCompatMode = FullscreenCompatMode::ExactCover;
				else if (cbuf[0] == 'o' || cbuf[0] == 'O')
					m_FullscreenCompatMode = FullscreenCompatMode::OversizeByOne;
				CS_CORE_INFO("Window: COSMIC_FULLSCREEN_COMPAT override -> {}.",
					m_FullscreenCompatMode == FullscreenCompatMode::OversizeByOne ? "OversizeByOne" : "ExactCover");
			}
		}
#endif

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

		// No timer may outlive the HWND, and no frame callback may fire into a
		// dying Application.
		m_ModalFrameCallback = nullptr;
		EndModalFramePump();

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

		// W1: when tracing, time the present. A swap that takes far longer than
		// a vsync interval is the signature of driver throttling of an occluded/
		// demoted swapchain (hypothesis H-B2 in docs/plans/09-windowing-plan.md).
		if (s_TraceEnabled)
		{
			const double t0 = glfwGetTime();
			m_Context->SwapBuffers();
			const double ms = (glfwGetTime() - t0) * 1000.0;
			if (ms > 25.0)
				CS_CORE_TRACE("[WinTrace] SwapBuffers took {:.1f} ms (possible occlusion throttle)", ms);
			return;
		}

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
		// The modal pump timer is dispatched through our subclass WndProc —
		// kill it before the subclass is removed.
		EndModalFramePump();

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
	// Modal frame pump (W4 — responsive rendering during the Win32 modal
	// move/size loop). See docs/design/responsive-rendering-and-pause.md.
	// =========================================================================

	void Window::SetModalRenderingEnabled(bool enabled)
	{
		m_ModalRenderingEnabled = enabled;
		if (!enabled)
			EndModalFramePump(); // if mid-drag, stop pumping immediately
	}

	void Window::BeginModalFramePump()
	{
#ifdef _WIN32
		if (!m_ModalRenderingEnabled || m_InModalLoop || !m_Handle)
			return;

		HWND hwnd = glfwGetWin32Window(m_Handle);
		if (!hwnd)
			return;

		m_InModalLoop = true;
		// USER_TIMER_MINIMUM (~10 ms, floored near ~15 ms by the scheduler) is
		// plenty — VSync caps the effective rate anyway.
		SetTimer(hwnd, static_cast<UINT_PTR>(kModalFrameTimerId), USER_TIMER_MINIMUM, nullptr);
		CS_WINTRACE("Modal frame pump started");
#endif
	}

	void Window::EndModalFramePump()
	{
#ifdef _WIN32
		if (!m_InModalLoop)
			return;

		m_InModalLoop = false;
		if (m_Handle)
		{
			if (HWND hwnd = glfwGetWin32Window(m_Handle))
				KillTimer(hwnd, static_cast<UINT_PTR>(kModalFrameTimerId));
		}
		CS_WINTRACE("Modal frame pump stopped");
#endif
	}

	void Window::ModalFrameTick()
	{
		if (m_ModalFrameCallback)
			m_ModalFrameCallback();
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

		// W2: present a correctly-sized frame within the same toggle. The
		// SetWindowPos above already dispatched WM_SIZE synchronously (GLFW size
		// callback → WindowResizeEvent → FBO resize), so the engine state is at
		// the new size — render and swap NOW instead of up to a frame later,
		// which is what DWM otherwise fills with stale/black content.
		ModalFrameTick();
	}

	void Window::SetFullscreenCompatMode(FullscreenCompatMode mode)
	{
		if (m_FullscreenCompatMode == mode)
			return;

		m_FullscreenCompatMode = mode;
		CS_CORE_INFO("Window: fullscreen compat mode = {}.",
			mode == FullscreenCompatMode::OversizeByOne ? "OversizeByOne" : "ExactCover");

		// Live re-apply so the A/B experiment can flip modes while fullscreen.
		if (m_Fullscreen && m_Handle)
		{
			ReassertFullscreenCoverWin32();
			ModalFrameTick();
		}
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

			// 1. Save the current windowed placement. W5.1: if the window is
			//    maximized, GetWindowRect returns the ZOOMED rect — restoring
			//    that as a normal window produces a pseudo-maximized floating
			//    window. Save the *normal* (restored) rect from the window
			//    placement plus the maximize flag, and re-maximize on exit.
			m_SavedMaximized = IsZoomed(hwnd) != 0;

			WINDOWPLACEMENT wp = {};
			wp.length = sizeof(wp);
			if (m_SavedMaximized && GetWindowPlacement(hwnd, &wp))
			{
				// Note: rcNormalPosition is in workspace coordinates; identical
				// to screen coordinates unless the taskbar docks left/top.
				m_SavedX      = wp.rcNormalPosition.left;
				m_SavedY      = wp.rcNormalPosition.top;
				m_SavedWidth  = wp.rcNormalPosition.right  - wp.rcNormalPosition.left;
				m_SavedHeight = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
			}
			else
			{
				RECT winRect = {};
				GetWindowRect(hwnd, &winRect);
				m_SavedX = winRect.left;
				m_SavedY = winRect.top;
				m_SavedWidth = winRect.right - winRect.left;
				m_SavedHeight = winRect.bottom - winRect.top;
			}

			CS_WINTRACE("Fullscreen enter: saved rect ({},{}) {}x{} maximized={}",
				m_SavedX, m_SavedY, m_SavedWidth, m_SavedHeight, m_SavedMaximized);

			// 2. Strip window decorations via style bits.
			//    We keep WS_VISIBLE so the window does not flash hidden.
			LONG style = GetWindowLong(hwnd, GWL_STYLE);
			style &= ~WS_OVERLAPPEDWINDOW;
			SetWindowLong(hwnd, GWL_STYLE, style);
			CS_WINTRACE("Fullscreen enter: stripped WS_OVERLAPPEDWINDOW (style 0x{:X})", static_cast<unsigned long>(style));

			// 3. Stretch the window to cover the current monitor (exact cover,
			//    or +1 px height in OversizeByOne compat mode — W3). Shared with
			//    the display-change path.
			ReassertFullscreenCoverWin32();
		}
		else
		{
			// ---- Exiting fullscreen ----

			// 1. Restore window decorations.
			LONG style = GetWindowLong(hwnd, GWL_STYLE);
			style |= WS_OVERLAPPEDWINDOW;
			SetWindowLong(hwnd, GWL_STYLE, style);
			CS_WINTRACE("Fullscreen exit: restored WS_OVERLAPPEDWINDOW (style 0x{:X})", static_cast<unsigned long>(style));

			// 2. W5.2: the saved rect may be stale — the monitor could have been
			//    unplugged or the resolution changed while fullscreen. Clamp to
			//    the work area of the nearest monitor so the window always comes
			//    back reachable.
			RECT restore = { m_SavedX, m_SavedY, m_SavedX + m_SavedWidth, m_SavedY + m_SavedHeight };
			HMONITOR mon = MonitorFromRect(&restore, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = {};
			mi.cbSize = sizeof(mi);
			if (mon && GetMonitorInfoW(mon, &mi))
			{
				const RECT& wa = mi.rcWork;
				int w = std::min<int>(m_SavedWidth,  wa.right  - wa.left);
				int h = std::min<int>(m_SavedHeight, wa.bottom - wa.top);
				int x = std::clamp<int>(m_SavedX, wa.left, wa.right  - w);
				int y = std::clamp<int>(m_SavedY, wa.top,  wa.bottom - h);
				if (x != m_SavedX || y != m_SavedY || w != m_SavedWidth || h != m_SavedHeight)
				{
					CS_WINTRACE("Fullscreen exit: clamped restore rect ({},{}) {}x{} -> ({},{}) {}x{}",
						m_SavedX, m_SavedY, m_SavedWidth, m_SavedHeight, x, y, w, h);
					m_SavedX = x; m_SavedY = y; m_SavedWidth = w; m_SavedHeight = h;
				}
			}

			// 3. Restore saved position and size. SWP_NOCOPYBITS: never blit
			//    stale fullscreen-sized pixels into the smaller rect (W2/H-A3).
			SetWindowPos(hwnd, HWND_TOP,
				m_SavedX, m_SavedY,
				m_SavedWidth, m_SavedHeight,
				SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

			// 4. W5.1: the window was maximized when it entered fullscreen —
			//    put it back to maximized (the normal rect restored above is
			//    what a later un-maximize returns to).
			if (m_SavedMaximized)
			{
				ShowWindow(hwnd, SW_MAXIMIZE);
				CS_WINTRACE("Fullscreen exit: re-maximized");
			}

			CS_CORE_INFO("Window: Restored windowed mode ({}x{} @ {},{}){}.",
				m_SavedWidth, m_SavedHeight, m_SavedX, m_SavedY,
				m_SavedMaximized ? " [maximized]" : "");
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
	// Fullscreen cover (shared: enter path, W3 compat re-apply, W5.3
	// display/DPI change re-assertion)
	// =========================================================================

	void Window::ReassertFullscreenCoverWin32()
	{
#ifdef _WIN32
		if (!m_Fullscreen || !m_Handle)
			return;

		HWND hwnd = glfwGetWin32Window(m_Handle);
		if (!hwnd)
			return;

		// Find the monitor the window centre currently lives on.
		GLFWmonitor* monitor = FindCurrentMonitor();
		if (!monitor)
			monitor = glfwGetPrimaryMonitor();
		if (!monitor)
			return;

		int monX = 0, monY = 0;
		glfwGetMonitorPos(monitor, &monX, &monY);
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		if (!mode)
			return;

		// W3: OversizeByOne makes the window 1 px taller than the monitor so
		// DWM never classifies it as fullscreen (no independent-flip promotion,
		// so capture overlays composite normally). The taskbar still hides —
		// that comes from the stripped style, not the exact size.
		const int extraH = (m_FullscreenCompatMode == FullscreenCompatMode::OversizeByOne) ? 1 : 0;

		// SWP_FRAMECHANGED forces the style change to be applied.
		// No HWND_TOPMOST — we stay in the normal z-order stack.
		// SWP_NOCOPYBITS (W2/H-A3): without it, SetWindowPos may blit the old
		// windowed-size client pixels into the new rect before our first
		// correctly-sized present, which reads as a stretched/garbage flash.
		SetWindowPos(hwnd, HWND_TOP,
			monX, monY,
			mode->width, mode->height + extraH,
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOCOPYBITS);

		CS_CORE_INFO("Window: Entered borderless fullscreen ({}x{} @ {},{}){}.",
			mode->width, mode->height + extraH, monX, monY,
			extraH ? " [compat: oversize-by-one]" : "");
#endif
	}

	// =========================================================================
	// Monitor detection
	// =========================================================================

	GLFWmonitor* Window::FindCurrentMonitor() const
	{
		// W5.4 test note: using the window CENTRE (not the top-left corner) is
		// deliberate — a window straddling two monitors goes fullscreen on the
		// one holding the majority of it, and the saved rect restores onto that
		// same monitor. Verified manually with the dual-monitor checklist in
		// docs/plans/09-windowing-plan.md (W5).
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