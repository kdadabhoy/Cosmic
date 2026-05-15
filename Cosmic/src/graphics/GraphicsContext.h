#pragma once

// GraphicsContext.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The GraphicsContext is an abstract interface that defines the contract for
 * managing a graphics API's lifecycle within a window. It serves as the
 * foundation for hardware abstraction, allowing the engine to initialize
 * specific rendering backends (like OpenGL, DirectX, or Vulkan) and handle
 * frame buffer synchronization (swapping buffers).
 * 
 * * Documentation Notes:
 * - Platform Independence: Every supported platform or API must derive its own
 * concrete Context class from this interface.
 * - Initialization: Handles the "binding" of the graphics API to the OS-level window.
 * - Double Buffering: The SwapBuffers() call is the primary mechanism for
 * presenting the rendered frame to the screen.
 * 
 * * Public Function Prototypes (Pre and Post Conditions):
 * * 1. virtual ~GraphicsContext()
 * Pre:  A derived context instance exists.
 * Post: Context-specific resources are released.
 * 
 * * 2. virtual void Init() = 0
 * Pre:  A valid window handle has been provided to the derived constructor.
 * Post: The graphics API is loaded, the context is made current, and
 * internal drivers (like GLAD) are initialized.
 * 
 * * 3. virtual void SwapBuffers() = 0
 * Pre:  The context has been initialized and a frame has been rendered.
 * Post: The back buffer and front buffer are swapped, displaying the
 * rendered image to the user.
 */

namespace Cosmic
{
	class GraphicsContext
	{
	public:
		////////////////////////////////
		// Destructor
		///////////////////////////////
		virtual ~GraphicsContext() = default;

		////////////////////////////////
		// Lifecycle & Frame Control
		///////////////////////////////

		/**
		 * @brief Initializes the underlying graphics API for the window.
		 */
		virtual void	Init() = 0;

		/**
		 * @brief Swaps the front and back buffers to display the rendered frame.
		 */
		virtual void	SwapBuffers() = 0;
	};

}