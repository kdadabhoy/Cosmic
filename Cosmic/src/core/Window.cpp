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
    #include <Windows.h>
#endif

namespace Cosmic
{
	Window::Window(int width, int height, const std::string& title)
		: m_Context(nullptr), m_Handle(nullptr)
	{
		if (!glfwInit())
		{
			std::cout << "Cosmic: Could not initialize GLFW!" << std::endl;
			return;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		// Prevent DWM minimized flash behaviors
		glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
		
		#ifdef _WIN32
			glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
		#endif

		m_Handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

		m_Context = new OpenGLContext(m_Handle);
		m_Context->Init();

		m_Data.Title = title;
		m_Data.Width = width;
		m_Data.Height = height;
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
					if (data.Fullscreen && data.WindowInstancePtr)
					{
						data.WindowInstancePtr->ReassertFullscreenTopology();
					}
				}
				else
				{
					// If we lose focus, unlock the cursor so the developer can multi-task
					#ifdef _WIN32
						ClipCursor(NULL);
					#endif
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
					case GLFW_PRESS:   { MouseButtonPressedEvent event(button);  data.EventCallback(event); break; }
					case GLFW_RELEASE: { MouseButtonReleasedEvent event(button); data.EventCallback(event); break; }
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

				if (data.FullscreenOverride && data.FullscreenOverride(key, action, mods))
					return;

				if (key == CS_KEY_F11 && action == 1) // GLFW_PRESS
				{
					if (data.WindowInstancePtr)
					{
						data.WindowInstancePtr->SetFullscreen(!data.Fullscreen);
					}
					return;
				}

				switch (action)
				{
					case GLFW_PRESS:   { KeyPressedEvent event(key, 0); data.EventCallback(event); break; }
					case GLFW_RELEASE: { KeyReleasedEvent event(key);   data.EventCallback(event); break; }
					case GLFW_REPEAT:  { KeyPressedEvent event(key, 1); data.EventCallback(event); break; }
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
		#ifdef _WIN32
			ClipCursor(NULL); // Free cursor bounds safely on teardown
		#endif
		delete m_Context;
		glfwDestroyWindow(m_Handle);
		glfwTerminate();
	}

	void Window::SwapBuffers() { m_Context->SwapBuffers(); }
	void Window::PollEvents()  { glfwPollEvents(); }
	bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Handle); }

	void Window::SetVSync(bool enabled)
	{
		m_Data.VSync = enabled;
		if (m_Handle) glfwSwapInterval(enabled ? 1 : 0);
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
			// 1. Cache the windowed properties so we can revert back to them cleanly
			glfwGetWindowPos(m_Handle, &m_Data.WindowedX, &m_Data.WindowedY);
			glfwGetWindowSize(m_Handle, (int*)&m_Data.WindowedWidth, (int*)&m_Data.WindowedHeight);

			// 2. Identify the target monitor based on the window center coordinate (Raylib's style)
			int monitorCount;
			GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
			GLFWmonitor* targetMonitor = glfwGetPrimaryMonitor();

			int windowCenterX = m_Data.WindowedX + (m_Data.WindowedWidth / 2);
			int windowCenterY = m_Data.WindowedY + (m_Data.WindowedHeight / 2);

			for (int i = 0; i < monitorCount; i++)
			{
				int mx, my;
				glfwGetMonitorPos(monitors[i], &mx, &my);
				const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
				
				if ((windowCenterX >= mx && windowCenterX < mx + mode->width) &&
					(windowCenterY >= my && windowCenterY < my + mode->height))
				{
					targetMonitor = monitors[i];
					break;
				}
			}

			const GLFWvidmode* mode = glfwGetVideoMode(targetMonitor);
			int monitorX, monitorY;
			glfwGetMonitorPos(targetMonitor, &monitorX, &monitorY);

			// 3. Set properties inside standard Windowed Mode (Passes NULL as monitor pointer)
			// This tells the DWM that we are just a standard window layout, completely preventing black screen glitches.
			glfwSetWindowAttrib(m_Handle, GLFW_DECORATED, GLFW_FALSE);
			glfwSetWindowMonitor(m_Handle, NULL, monitorX, monitorY, mode->width, mode->height, mode->refreshRate);

			// 4. Pin layout explicitly above desktop layers to defend against overlapping windows or taskbars
			#ifdef _WIN32
				HWND hwnd = glfwGetWin32Window(m_Handle);
				SetWindowPos(hwnd, HWND_TOPMOST, monitorX, monitorY, mode->width, mode->height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);

				// Traps the cursor precisely within this monitor grid so it can't escape to an extended display,
				// while leaving the cursor shape fully intact and visible for ImGui panels.
				RECT clipRect = { monitorX, monitorY, monitorX + mode->width, monitorY + mode->height };
				ClipCursor(&clipRect);
			#endif
		}
		else
		{
			// Restore standard desktop layout aesthetics cleanly
			#ifdef _WIN32
				ClipCursor(NULL); // Give back standard desktop mouse access
				HWND hwnd = glfwGetWin32Window(m_Handle);
				SetWindowPos(hwnd, HWND_NOTOPMOST, m_Data.WindowedX, m_Data.WindowedY, m_Data.WindowedWidth, m_Data.WindowedHeight, SWP_FRAMECHANGED);
			#endif

			glfwSetWindowAttrib(m_Handle, GLFW_DECORATED, GLFW_TRUE);
			glfwSetWindowMonitor(m_Handle, NULL, m_Data.WindowedX, m_Data.WindowedY, m_Data.WindowedWidth, m_Data.WindowedHeight, 0);
		}

		glfwSwapInterval(m_Data.VSync ? 1 : 0);
	}

	void Window::ReassertFullscreenTopology()
	{
		#ifdef _WIN32
			if (m_Data.Fullscreen && m_Handle)
			{
				HWND hwnd = glfwGetWin32Window(m_Handle);
				
				// Re-verify monitor dimensions to accommodate dynamic display arrangement adjustments
				int width, height;
				glfwGetWindowSize(m_Handle, &width, &height);
				
				int mx, my;
				glfwGetWindowPos(m_Handle, &mx, &my);

				SetWindowPos(hwnd, HWND_TOPMOST, mx, my, width, height, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
				
				RECT clipRect = { mx, my, mx + width, my + height };
				ClipCursor(&clipRect);
			}
		#endif
	}
}