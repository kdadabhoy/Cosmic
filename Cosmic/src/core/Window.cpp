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