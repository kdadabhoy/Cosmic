#include "core/Input.h"
#include "core/Application.h"
#include <GLFW/glfw3.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * IsKeyPressed
	 *
	 * Polls the current state of a specific keyboard key.
	 * This reaches into the Application singleton to retrieve the active window
	 * handle, ensuring we are querying the correct OS window context.
	 *
	 * Returns true if the key is currently held down or being repeated by the OS.
	 */
	bool Input::IsKeyPressed(int keycode)
	{
		auto* windowHandle = Application::Get().GetWindow().GetHandle();
		auto state = glfwGetKey(windowHandle, keycode);

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * IsMouseButtonPressed
	 *
	 * Polls the current state of a specific mouse button.
	 * Similar to keyboard polling, this utilizes the GLFW window handle to check
	 * if the button is currently in a pressed state.
	 */
	bool Input::IsMouseButtonPressed(int button)
	{
		auto* windowHandle = Application::Get().GetWindow().GetHandle();
		auto state = glfwGetMouseButton(windowHandle, button);

		return state == GLFW_PRESS;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * GetMousePosition
	 *
	 * Queries the underlying OS via GLFW for the current cursor coordinates relative
	 * to the top-left corner of the window client area.
	 *
	 * Returns a glm::vec2 containing the (x, y) coordinates as floats.
	 */
	glm::vec2 Input::GetMousePosition()
	{
		auto* windowHandle = Application::Get().GetWindow().GetHandle();
		double xpos, ypos;
		glfwGetCursorPos(windowHandle, &xpos, &ypos);

		return { (float)xpos, (float)ypos };
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * GetMouseX
	 *
	 * Helper method to extract only the horizontal component of the cursor position.
	 */
	float Input::GetMouseX()
	{
		return GetMousePosition().x;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * GetMouseY
	 *
	 * Helper method to extract only the vertical component of the cursor position.
	 */
	float Input::GetMouseY()
	{
		return GetMousePosition().y;
	}

	/////////////////////////////////////////////////////////////////////////////////
}