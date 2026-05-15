#pragma once

// OpenGLContext.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The OpenGLContext is the concrete implementation of the GraphicsContext interface
 * specifically for the OpenGL API. It manages the lifecycle of an OpenGL rendering
 * context, ensuring that the GPU is correctly bound to a window and that the
 * function pointers for modern OpenGL commands are loaded.
 * 
 * 
 * Documentation Notes:
 * - Window Integration: It requires a valid GLFWwindow handle to establish
 * the connection between the OS windowing system and the graphics hardware.
 * 
 * - Loading Mechanism: Uses GLAD to retrieve API function addresses from the
 * graphics driver during the initialization phase.
 * 
 * - Presentation: Implements the final SwapBuffers call to present the
 * off-screen back buffer to the user's display.
 * 
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. OpenGLContext(GLFWwindow* windowHandle)
 * Pre:  A valid, non-null GLFWwindow handle is provided.
 * Post: The context object is created and linked to the specified window.
 * 
 * 2. void Init() override
 * Pre:  The window handle provided during construction remains valid.
 * Post: The OpenGL context is made current on the executing thread, and
 * GLAD is initialized to load API functions.
 * 
 * 3. void SwapBuffers() override
 * Pre:  The context has been successfully initialized.
 * Post: The front and back buffers of the linked window are swapped.
 */

#include "graphics/GraphicsContext.h"

struct GLFWwindow;

namespace Cosmic
{
	class OpenGLContext : public GraphicsContext
	{
	public:
		////////////////////////////////
		// Life Cycle
		///////////////////////////////

		OpenGLContext(GLFWwindow* windowHandle);

		virtual void	Init() override;
		virtual void	SwapBuffers() override;

	private:
		GLFWwindow* m_WindowHandle;
	};
}