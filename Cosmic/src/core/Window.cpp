#include "core/Window.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"

#include <GLFW/glfw3.h>
#include "platform/opengl/OpenGLContext.h" // Needed to create the context
#include <iostream>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	Window::Window(int width, int height, const std::string& title)
		: m_Context(nullptr), m_Handle(nullptr)
	{
		if (!glfwInit())
		{
			std::cout << "Could not initialize GLFW!" << std::endl;
			return;
		}

		// We still set these for now since we are in OpenGL, 
		// but eventually, these hints might move to the Context.
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		m_Handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

		// Initialize the Graphics Context
		m_Context = new OpenGLContext(m_Handle);
		m_Context->Init();

		m_Data.Title = title;
		m_Data.Width = width;
		m_Data.Height = height;

		glfwSetWindowUserPointer(m_Handle, &m_Data);

		// --- Window Resize Callback ---
		glfwSetWindowSizeCallback(m_Handle, [](GLFWwindow* window, int width, int height)
			{
				WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
				data.Width = width;
				data.Height = height;

				WindowResizeEvent event(width, height);
				data.EventCallback(event);
			});




		// Note: Other callbacks for Key, Mouse, etc., stay in the other file
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

	/////////////////////////////////////////////////////////////////////////////////

	Window::~Window()
	{
		delete m_Context;
		glfwDestroyWindow(m_Handle);
		glfwTerminate();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::SwapBuffers()
	{
		m_Context->SwapBuffers();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::PollEvents()
	{
		glfwPollEvents();
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool Window::ShouldClose() const
	{
		return glfwWindowShouldClose(m_Handle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::SetVSync(bool enabled)
	{
		if (enabled)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);

		m_Data.VSync = enabled;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::GetSize(int* width, int* height) const
	{
		glfwGetFramebufferSize(m_Handle, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

}


