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

// 2. Platform Specific Windows block (Ordered perfectly to eliminate redefinition)
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

// 3. Main library includes (Includes Windows backend nicely now)
#include <GLFW/glfw3.h>

// 4. Native extensions (Loaded only after standard GLFW declarations are mapped)
#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

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

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		// Keep window visible even when focus changes; avoids an iconify on Alt+Tab
		// out of fullscreen.
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);

		m_Handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
		if (!m_Handle)
		{
			CS_CORE_CRITICAL("Window: glfwCreateWindow failed!");
			glfwTerminate();
			return;
		}

		m_Context = new OpenGLContext(m_Handle);
		m_Context->Init();

		m_Data.Title = title;
		m_Data.Width = static_cast<unsigned int>(width);
		m_Data.Height = static_cast<unsigned int>(height);
		m_Data.Self = this;

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

		// Null out the GLFW user pointer so any lingering callbacks can't
		// dereference our now-dying WindowData.
		if (m_Handle)
			glfwSetWindowUserPointer(m_Handle, nullptr);

		delete m_Context;
		m_Context = nullptr;

		if (m_Handle)
		{
			glfwDestroyWindow(m_Handle);
			m_Handle = nullptr;
		}

		glfwTerminate();
	}

	// =========================================================================
	// Frame
	// =========================================================================

	void Window::PollEvents() { glfwPollEvents(); }
	void Window::SwapBuffers() { m_Context->SwapBuffers(); }
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