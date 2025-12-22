#ifndef WINDOW_H
#define WINDOW_H

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include <functional>
#include "events/Event.h"

class Window {
public:
	using EventCallbackFn = std::function<void(Event&)>;

	Window(int width, int height, const std::string& title);
	~Window();

	// --- Core Window Functionality ---
	void pollEvents();
	void swapBuffers();


	// --- Getters ---
	inline unsigned int GetWidth() const { return m_Data.Width; }
	inline unsigned int GetHeight() const { return m_Data.Height; }
	inline GLFWwindow* getHandle() const { return handle; }


	// --- Window Attributes ---
	void setEventCallback(const EventCallbackFn& callback) { m_Data.EventCallback = callback; }
	void setVSync(bool enabled);
	bool IsVSync() const { return m_Data.VSync; }

	bool shouldClose() const;
	void getSize(int* width, int* height) const;

private:
	GLFWwindow* handle;

	// This struct is passed to GLFW's "User Pointer" so 
	// static callbacks can talk back to our Window class.
	struct WindowData {
		std::string Title;
		unsigned int Width;
		unsigned int Height;
		bool VSync;

		EventCallbackFn EventCallback;
	};

	WindowData m_Data;
};

#endif






/*

Documentation:

	Note: It is important that GLAD is included before GLFW



	Window(int width, int height, const std::string& title);
		- Creates a window and makes it current
		- Returns an error if the window does not open
		- Uses OpenGL 3.3
		- Initializes GLAD




	~Window()
		- Calls glfwDestroyWindow() to kill the specific window




	GLFWwindow* getHandle() const;
		- Returns a pointer to the window's handle
		- More useful than one might think




	**************************************
	GLFW Wrapper functions:
		- For naming and ease of use




		bool shouldClose() const;
			- A wrapper for glfwWindowShouldClose()
				- WindowShouldClose() Answers: "Did we press the X button?"




		void swapBuffers() const;
			- Call it at the bottom of the render loop
			- A wrapper for glfwSwapBuffers()
				- SwapBuffers() essentially puts what you
				  want to appear (the new frame) on the screen
					- Bc of Dual Buffer System (to prevent screen tearing)



		void pollEvents() const;
			- Call it at the top of the render loop
			- A wrapper for glfwPollEvents()
				- PollEvents() essentially handles the input
					- Without it... your window will freeze




		void getSize(int* width, int* height) const;
			- A wrapper for glfwGetFramebufferSize()
			- width and height are passed by pointer
				- Passed by pointer to make it clear we are modifying them
	

		void setVSync(bool enabled);
			- A wrapper for glfwSwapInterval()
			- VSync (Vertical Synchronization) syncs the monitor and games refresh rate
				- This helps eliminate screen tearing and reduces hardware strain



*/


