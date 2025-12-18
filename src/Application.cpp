#include "Application.h"
#include <iostream>





Application::Application() 
	: isRunning(false)
{}





Application::~Application()
{
	shutdown();
}








void Application::shutdown()
{
	if (!isRunning) {
		return;
	}

	// Add any other necessary deletion handling

	window.reset();
	isRunning = false;
}







bool Application::initialize() {

	window = std::make_unique<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);
	if (!window->getHandle()) {
		std::cout << "Failed to Create a Window" << std::endl;
		return false;
	}

	isRunning = true;
	return true;
}




void Application::run() {

	while (isRunning && !window->shouldClose()) {
		window->pollEvents();

		// imgui/rendering stuff

		window->swapBuffers();
	}

}