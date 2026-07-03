#include <glad/glad.h>
#include "platform/opengl/OpenGLContext.h"
#include "core/Core.h"
#include "core/Log.h"
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
		CS_CORE_ASSERT(status, "Failed to initialize GLAD — OpenGL function pointers not loaded.");

		// The engine requires 4.5 core (Window.cpp context hints, #version 450 shaders,
		// S4.7 compute) — log what the driver actually handed us so a mismatch is visible.
		CS_CORE_INFO("OpenGL {}.{} — {}", GLVersion.major, GLVersion.minor,
		             reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
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

	/**
	 * HasCurrentContext
	 * * Reports whether an OpenGL context is current on the calling thread.
	 * * GL resource destructors call this before issuing glDelete* so that a
	 * shutdown/abort path that destroys the window (and context) before the normal
	 * cleanup runs cannot fault inside the driver. glfwGetCurrentContext() returns
	 * null after the context is destroyed — and remains safe to call even after
	 * glfwTerminate (it just returns null) — so this is a reliable "can I safely
	 * issue GL commands right now?" check.
	 */
	bool OpenGLContext::HasCurrentContext()
	{
		return glfwGetCurrentContext() != nullptr;
	}

	/////////////////////////////////////////////////////////////////////////////////
}