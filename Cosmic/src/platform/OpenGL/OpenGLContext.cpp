#include "platform/opengl/OpenGLContext.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 * * Stores the GLFW window handle. Note that this constructor does not perform
	 * any API calls; the actual hardware binding is deferred to the Init() method.
	 */
	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		: m_WindowHandle(windowHandle)
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Init
	 * * Binds the OpenGL API to the window and thread.
	 * * 1. glfwMakeContextCurrent: Tells the OS/Driver that all subsequent OpenGL
	 * commands from this thread should target m_WindowHandle.
	 * 2. gladLoadGLLoader: Queries the driver for the memory addresses of OpenGL
	 * functions (like glDrawArrays). This is essential because these addresses
	 * vary by GPU vendor and driver version.
	 */
	void OpenGLContext::Init()
	{
		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

		// Note: status check is critical here to ensure GLAD loaded correctly
		// before any rendering commands are issued.
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SwapBuffers
	 * * Triggers the hardware flip.
	 * * In a double-buffered environment, the renderer draws to a hidden "back"
	 * buffer. This function instructs GLFW to swap that back buffer with the
	 * visible "front" buffer, presenting the completed frame to the user.
	 */
	void OpenGLContext::SwapBuffers()
	{
		glfwSwapBuffers(m_WindowHandle);
	}

	/////////////////////////////////////////////////////////////////////////////////
}