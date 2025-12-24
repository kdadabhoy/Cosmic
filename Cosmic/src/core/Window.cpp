#include "core/Window.h"
#include "events/ApplicationEvent.h"
#include "events/KeyEvent.h"
#include "events/MouseEvent.h"
#include <iostream>



namespace Cosmic
{

	Window::Window(int width, int height, const std::string& title)
		: handle(nullptr)
	{

		if (!glfwInit()) 
		{
			return;
		}

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

		handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

		if (handle == NULL) 
		{
			std::cout << "Failed to Create GLFW Window" << std::endl;
			glfwTerminate();
			return;
		}

		glfwMakeContextCurrent(handle);

		if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
		{
			std::cout << "GLAD Initialization Failed" << std::endl;
			return;
		}

		m_Data.Title = title;
		m_Data.Width = width;
		m_Data.Height = height;

		glfwSetWindowUserPointer(handle, &m_Data);


		// --- Window Resize Callback ---
		glfwSetFramebufferSizeCallback(handle, [](GLFWwindow* window, int width, int height) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;

			WindowResizeEvent event(width, height);
			data.EventCallback(event);
			});


		// --- Window Close Callback ---
		glfwSetWindowCloseCallback(handle, [](GLFWwindow* window) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.EventCallback(event);
			});


		// --- Key Callback ---
		glfwSetKeyCallback(handle, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
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







		// --- Mouse Button Callback ---
		glfwSetMouseButtonCallback(handle, [](GLFWwindow* window, int button, int action, int mods) {
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







		// --- Mouse Scroll Callback ---
		glfwSetScrollCallback(handle, [](GLFWwindow* window, double xOffset, double yOffset) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseScrolledEvent event((float)xOffset, (float)yOffset);
			data.EventCallback(event);
			});





		// --- Mouse Move Callback ---
		glfwSetCursorPosCallback(handle, [](GLFWwindow* window, double xPos, double yPos) {
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseMovedEvent event((float)xPos, (float)yPos);
			data.EventCallback(event);
			});
	}

	/////////////////////////////////////////////////////////////////////////////////

	Window::~Window()
	{
		glfwDestroyWindow(handle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool Window::shouldClose() const
	{
		return glfwWindowShouldClose(handle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::swapBuffers()
	{
		glfwSwapBuffers(handle);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::pollEvents()
	{
		glfwPollEvents();
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::getSize(int* width, int* height) const
	{
		glfwGetFramebufferSize(handle, width, height);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Window::setVSync(bool enabled)
	{
		if (enabled)
			glfwSwapInterval(1);
		else
			glfwSwapInterval(0);
	}


}