#include "core/Window.h"
#include "events/WindowEvent.h"
#include <iostream>




Window::Window(int width, int height, const std::string& title)
	: handle(nullptr)
{
	
	if (!glfwInit()) {
		return;
	}

	// OpenGL 3.3 (Major = 3, Minor = 3)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	handle = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);

	if (handle == NULL) {
		std::cout << "Failed to Create GLFW Window" << std::endl;
		glfwTerminate();
		return;
	}

	glfwMakeContextCurrent(handle);


	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "GLAD Initialization Failed" << std::endl;
		return;
	}







	// --- NEW: Event Setup ---
	m_Data.Title = title;
	m_Data.Width = width;
	m_Data.Height = height;

	// Attach our m_Data struct to this GLFW window instance
	glfwSetWindowUserPointer(handle, &m_Data);

	// Set GLFW callbacks
	glfwSetFramebufferSizeCallback(handle, [](GLFWwindow* window, int width, int height) {
		// Retrieve our data struct from the GLFW user pointer
		WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
		data.Width = width;
		data.Height = height;

		// Create the abstract Event and shout it back to the Application
		WindowResizeEvent event(width, height);
		data.EventCallback(event);
		});

	glfwSetWindowCloseCallback(handle, [](GLFWwindow* window) {
		WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

		// Create and dispatch the event to the Application
		WindowCloseEvent event;
		data.EventCallback(event);
		});

}







Window::~Window() 
{
	glfwDestroyWindow(handle);
}








GLFWwindow* Window::getHandle() const 
{
	return handle;
}








/*
	*******************************
	*** GLFW Wrapper functions: ***
	********************************
*/


bool Window::shouldClose() const 
{
	return glfwWindowShouldClose(handle);
}





void Window::swapBuffers()
{ 
	glfwSwapBuffers(handle);
	return;
}





void Window::pollEvents()
{ 
	glfwPollEvents();
	return;
}





void Window::getSize(int* width, int* height) const 
{ 
	glfwGetFramebufferSize(handle, width, height);
	return;
}





void Window::setVSync(bool enabled)
{
	if (enabled) {
		glfwSwapInterval(1);
	} else {
		glfwSwapInterval(0);
	}
}