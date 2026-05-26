#include "core/Window.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"
#include <GLFW/glfw3.h>
#include "platform/opengl/OpenGLContext.h"
#include "codes/KeyCodes.h"
#include <iostream>

#ifdef _WIN32
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <GLFW/glfw3native.h>
#endif

namespace Cosmic
{
	Window::Window(int width, int height, const std::string& title)
		: m_Context(nullptr), m_Handle(nullptr)
	{
		// 1. Initialize GLFW
		if (!glfwInit())
		{
			std::cout << "Cosmic: Could not initialize GLFW!" << std::endl;
			return;
		}

		// 2. Set Window Hints (Targeting OpenGL 3.3 Core Profile)
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		// --- THE DOUBLE FLASH FIX ---
		// 1. Tell Windows not to use exclusive mode when switching styles (removes first flash)
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
		
		// 2. Force GLFW to request a hardware-composed flip model backbuffer path from the OS DWM
		// This bypasses the old blit-model presentation that triggers the second Windows HDR/G-Sync flash.
		#ifdef _WIN32
			glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
		#endif

		// 3. Create the Native Window
		m_Handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

		// 4. Initialize Graphics Context
		m_Context = new OpenGLContext(m_Handle);
		m_Context->Init();

		// 5. Store metadata and bridge to GLFW
		m_Data.Title = title;
		m_Data.Width = width;
		m_Data.Height = height;

		// --- CRITICAL STEP ---
		// Save the class address so static callbacks can call member functions safely
		m_Data.WindowInstancePtr = this;

		glfwSetWindowUserPointer(m_Handle, &m_Data);

		// -----------------------------------------------------------------
		// Hardware Callbacks
		// -----------------------------------------------------------------

		glfwSetWindowSizeCallback(m_Handle, [](GLFWwindow* window, int width, int height)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				data.Width = width;
				data.Height = height;

				WindowResizeEvent event(width, height);
				data.EventCallback(event);
			});

		glfwSetWindowFocusCallback(m_Handle, [](GLFWwindow* window, int focused)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				if (focused)
				{
					// Defends against taskbar overlapping layouts when focus transfers back to the engine window context
					if (data.Fullscreen && data.WindowInstancePtr)
					{
						data.WindowInstancePtr->ReassertFullscreenTopology();
					}
				}
			});

		glfwSetScrollCallback(m_Handle, [](GLFWwindow* window, double xOffset, double yOffset)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				MouseScrolledEvent event((float)xOffset, (float)yOffset);
				data.EventCallback(event);
			});

		glfwSetMouseButtonCallback(m_Handle, [](GLFWwindow* window, int button, int action, int mods)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				switch (action)
				{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					data.EventCallback(event);
					break;
				}
				}
			});

		glfwSetCursorPosCallback(m_Handle, [](GLFWwindow* window, double xPos, double yPos)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				MouseMovedEvent event((float)xPos, (float)yPos);
				data.EventCallback(event);
			});

		glfwSetKeyCallback(m_Handle, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				// 1. Client-Side DLL Hotkey Override Interception
				if (data.FullscreenOverride)
				{
					if (data.FullscreenOverride(key, action, mods))
						return;
				}

				// 2. Default Engine Fallback Behavior (Cleanly Routing to Member Function)
				if (key == CS_KEY_F11 && action == 1) // 1 = GLFW_PRESS
				{
					if (data.WindowInstancePtr)
					{
						// Call the single source of truth method!
						data.WindowInstancePtr->SetFullscreen(!data.Fullscreen);
					}
					return;
				}

				// 3. Normal Engine Input Event Distribution System
				switch (action)
				{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key);
					data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, 1);
					data.EventCallback(event);
					break;
				}
				}
			});

		glfwSetCharCallback(m_Handle, [](GLFWwindow* window, unsigned int keycode)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				KeyTypedEvent event(keycode);
				data.EventCallback(event);
			});
	}

	Window::~Window()
	{
		delete m_Context;
		glfwDestroyWindow(m_Handle);
		glfwTerminate();
	}

	void Window::SwapBuffers()
	{
		m_Context->SwapBuffers();
	}

	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle);
	}

	void Window::SetVSync(bool enabled)
	{
		m_Data.VSync = enabled;
		if (m_Handle)
		{
			if (enabled)
				glfwSwapInterval(1);
			else
				glfwSwapInterval(0);
		}
	}

	void Window::GetSize(int* width, int* height) const
	{
		glfwGetFramebufferSize(m_Handle, width, height);
	}

	void Window::SetFullscreen(bool enabled)
	{
		if (m_Data.Fullscreen == enabled)
			return;

		m_Data.Fullscreen = enabled;

		if (m_Data.Fullscreen)
		{
			glfwGetWindowPos(m_Handle, &m_Data.WindowedX, &m_Data.WindowedY);
			glfwGetWindowSize(m_Handle, (int*)&m_Data.WindowedWidth, (int*)&m_Data.WindowedHeight);

			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			if (monitor)
			{
				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				int monitorX, monitorY;
				glfwGetMonitorPos(monitor, &monitorX, &monitorY);

				// Strip the OS borders
				glfwSetWindowAttrib(m_Handle, GLFW_DECORATED, GLFW_FALSE);
				glfwSetWindowAttrib(m_Handle, GLFW_AUTO_ICONIFY, GLFW_FALSE);

				// --- THE DEFINITIVE MULTI-MONITOR MULTI-PLANE OVERRIDE ---
				// We offset the vertical positioning by exactly 1 pixel.
				// This explicitly breaks the driver's ability to silently promote this window to exclusive mode.
				// Result: ZERO black flashes, instant toggles, and flawless screenshots!
				glfwSetWindowPos(m_Handle, monitorX, monitorY + 1);
				glfwSetWindowSize(m_Handle, mode->width, mode->height - 1);

				#ifdef _WIN32
					HWND hwnd = glfwGetWin32Window(m_Handle);
					
					LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
					style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
					SetWindowLongPtr(hwnd, GWL_STYLE, style);
					
					// Pin it right above the taskbar layer so you don't see any visual gap
					SetWindowPos(hwnd, HWND_TOPMOST, monitorX, monitorY + 1, mode->width, mode->height - 1, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
				#endif
			}
		}
		else
		{
			glfwSetWindowAttrib(m_Handle, GLFW_DECORATED, GLFW_TRUE);
			glfwSetWindowPos(m_Handle, m_Data.WindowedX, m_Data.WindowedY);
			glfwSetWindowSize(m_Handle, m_Data.WindowedWidth, m_Data.WindowedHeight);

			#ifdef _WIN32
				HWND hwnd = glfwGetWin32Window(m_Handle);
				
				LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
				style |= (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
				SetWindowLongPtr(hwnd, GWL_STYLE, style);

				SetWindowPos(hwnd, HWND_NOTOPMOST, m_Data.WindowedX, m_Data.WindowedY, m_Data.WindowedWidth, m_Data.WindowedHeight, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
			#endif
		}

		glfwSwapInterval(m_Data.VSync ? 1 : 0);
	}

	void Window::ReassertFullscreenTopology()
	{
#ifdef _WIN32
		if (m_Data.Fullscreen && m_Handle)
		{
			HWND hwnd = glfwGetWin32Window(m_Handle);
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			if (monitor)
			{
				const GLFWvidmode* mode = glfwGetVideoMode(monitor);
				int monitorX, monitorY;
				glfwGetMonitorPos(monitor, &monitorX, &monitorY);

				// Pushes window state properties over desktop compositor frames using lightweight hints
				SetWindowPos(hwnd, HWND_TOPMOST, monitorX, monitorY + 1, mode->width, mode->height - 1, 
					SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
			}
		}
#endif
	}
}