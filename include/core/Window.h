#ifndef WINDOW_H
#define WINDOW_H
#include <glad/glad.h> // It is important that GLAD is included before GLFW
#include <GLFW/glfw3.h>
#include <string>



class Window {
public:
	Window(int width, int height, const std::string& title);
	~Window();

	GLFWwindow* getHandle() const;


	// Accessors

	// GLFW Wrapper functions:
	bool shouldClose() const;					  // glfwWindowShouldClose()
	void swapBuffers();							  // glfwSwapBuffers()
	void pollEvents();							  // glfwPollEvents() 
	void getSize(int* width, int* height) const;  // glfwGetFramebufferSize()
	void setVSync(bool enabled);				  // glfwSwapInterval()


private:
	GLFWwindow* handle;
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


