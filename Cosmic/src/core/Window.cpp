#include "core/Window.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"
#include <GLFW/glfw3.h>
#include "platform/opengl/OpenGLContext.h"
#include <iostream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Constructor
	 * Initializes the GLFW library, configures window hints for the OpenGL Core Profile,
	 * creates the native window handle, and establishes the Graphics Context.
	 * It also sets up the "User Pointer" bridge and registers all hardware callbacks.
	 */
	Window::Window(int width, int height, const std::string& title)
		: m_Context(nullptr), m_Handle(nullptr)
	{
		
		// 1. Initialize GLFW
		if (!glfwInit())
		{
			// Note: Using std::cout here as a fallback if the Engine Logger isn't ready
			std::cout << "Cosmic: Could not initialize GLFW!" << std::endl;
			return;
		}


		// 2. Set Window Hints (Targeting OpenGL 3.3 Core)
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);


		// 3. Create the Native Window
		m_Handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);


		// 4. Initialize Graphics Context (API-Specific)
		m_Context = new OpenGLContext(m_Handle);
		m_Context->Init();


		// 5. Store metadata and bridge to GLFW
		m_Data.Title = title;
		m_Data.Width = width;
		m_Data.Height = height;


		// Link our WindowData struct to the GLFW handle so callbacks can access it
		glfwSetWindowUserPointer(m_Handle, &m_Data);

		// -----------------------------------------------------------------
		// Hardware Callbacks (Mapping Native OS events to Cosmic Events)
		// -----------------------------------------------------------------


		// Window Resize Callback
		glfwSetWindowSizeCallback(m_Handle, [](GLFWwindow* window, int width, int height)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				data.Width = width;
				data.Height = height;

				WindowResizeEvent event(width, height);
				data.EventCallback(event);
			});


		// Mouse Scroll Callback
		glfwSetScrollCallback(m_Handle, [](GLFWwindow* window, double xOffset, double yOffset)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				MouseScrolledEvent event((float)xOffset, (float)yOffset);
				data.EventCallback(event);
			});


		// Mouse Button Callback
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


		// Mouse Movement Callback
		glfwSetCursorPosCallback(m_Handle, [](GLFWwindow* window, double xPos, double yPos)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				MouseMovedEvent event((float)xPos, (float)yPos);
				data.EventCallback(event);
			});


		// Keyboard Input Callback
		glfwSetKeyCallback(m_Handle, [](GLFWwindow* window, int key, int scancode, int action, int mods)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				switch (action)
				{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0); // 0 = First press
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
					KeyPressedEvent event(key, 1); // 1 = Repeat/Hold
					data.EventCallback(event);
					break;
				}
				}
			});


		// Text Input Callback (For ImGui/Text Fields)
		glfwSetCharCallback(m_Handle, [](GLFWwindow* window, unsigned int keycode)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

				KeyTypedEvent event(keycode);
				data.EventCallback(event);
			});
	}


	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Destructor
	 * Cleans up the graphics context and destroys the window handle to prevent leaks.
	 */
	Window::~Window()
	{
		delete m_Context;
		glfwDestroyWindow(m_Handle);
		glfwTerminate();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Delegates the buffer swap to the graphics context.
	 */
	void Window::SwapBuffers()
	{
		m_Context->SwapBuffers();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Event Polling
	 * Instructs GLFW to check the OS for any pending hardware events.
	 */
	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Returns true if the window has been flagged for closure.
	 */
	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Sets the swap interval to sync with the monitor's refresh rate.
	 */
	void Window::SetVSync(bool enabled)
	{
		if (enabled)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Data.VSync = enabled;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Retrieves the current pixel width and height from the GLFW framebuffer.
	 */
	void Window::GetSize(int* width, int* height) const
	{
		glfwGetFramebufferSize(m_Handle, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

}