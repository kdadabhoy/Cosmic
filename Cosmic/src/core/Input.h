#pragma once

// Input.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Input class is a static utility that provides global access to the current
 * state of input devices. It abstracts the polling mechanisms of GLFW, allowing
 * any subsystem to check the status of keys, mouse buttons, and cursor positions
 * without maintaining a direct reference to the Window handle.
 *
 * Unlike the Event system which is reactive, the Input class is proactive; it
 * is intended for use within update loops where the current state of a device
 * is required for continuous logic (e.g., character movement or camera rotation).
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. static bool IsKeyPressed(int keycode)
 *    Pre:  The Application singleton and its Window must be initialized.
 *    Post: Returns true if the key corresponding to the keycode is held down.
 *
 * 2. static bool IsMouseButtonPressed(int button)
 *    Post: Returns true if the specified mouse button is currently depressed.
 *
 * 3. static glm::vec2 GetMousePosition()
 *    Post: Returns the (x, y) screen coordinates of the cursor as a GLM vector.
 *
 * 4. static float GetMouseX() / GetMouseY()
 *    Post: Returns the specific individual component of the mouse position.
 */

#include <glm/glm.hpp>

namespace Cosmic
{
	class Input
	{
	public:
		////////////////////////////////
		// Keyboard Queries
		///////////////////////////////

		static bool			IsKeyPressed(int keycode);


		////////////////////////////////
		// Mouse Queries
		///////////////////////////////

		static bool			IsMouseButtonPressed(int button);
		static glm::vec2	GetMousePosition();
		static float		GetMouseX();
		static float		GetMouseY();
	};
}