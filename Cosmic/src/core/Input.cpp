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
	// Gamepad / Joystick (E7)
	//
	// GLFW's joystick API is window-independent, but Application must exist so
	// GLFW itself is initialized (the Window constructor runs glfwInit). Slots
	// map directly onto GLFW_JOYSTICK_1..16.
	/////////////////////////////////////////////////////////////////////////////////

	bool Input::IsGamepadConnected(int gamepad)
	{
		if (gamepad < 0 || gamepad > GLFW_JOYSTICK_LAST)
			return false;
		return glfwJoystickPresent(GLFW_JOYSTICK_1 + gamepad) == GLFW_TRUE;
	}

	/**
	 * GetGamepadAxis
	 *
	 * Mapped devices (glfwJoystickIsGamepad): standardized SDL-style layout —
	 * pass CS_GAMEPAD_AXIS_*. Unmapped devices (RC transmitters, sim yokes):
	 * raw glfwGetJoystickAxes indexing, so the same call keeps working; the
	 * axis order is whatever the device reports.
	 */
	float Input::GetGamepadAxis(int axis, int gamepad)
	{
		if (!IsGamepadConnected(gamepad) || axis < 0)
			return 0.0f;

		const int jid = GLFW_JOYSTICK_1 + gamepad;

		if (glfwJoystickIsGamepad(jid))
		{
			GLFWgamepadstate state;
			if (glfwGetGamepadState(jid, &state) && axis <= GLFW_GAMEPAD_AXIS_LAST)
				return state.axes[axis];
			return 0.0f;
		}

		int count = 0;
		const float* axes = glfwGetJoystickAxes(jid, &count);
		if (axes && axis < count)
			return axes[axis];
		return 0.0f;
	}

	/**
	 * IsGamepadButtonPressed
	 *
	 * Mapped layout (CS_GAMEPAD_BUTTON_*) when available, raw button index
	 * otherwise — mirrors GetGamepadAxis.
	 */
	bool Input::IsGamepadButtonPressed(int button, int gamepad)
	{
		if (!IsGamepadConnected(gamepad) || button < 0)
			return false;

		const int jid = GLFW_JOYSTICK_1 + gamepad;

		if (glfwJoystickIsGamepad(jid))
		{
			GLFWgamepadstate state;
			if (glfwGetGamepadState(jid, &state) && button <= GLFW_GAMEPAD_BUTTON_LAST)
				return state.buttons[button] == GLFW_PRESS;
			return false;
		}

		int count = 0;
		const unsigned char* buttons = glfwGetJoystickButtons(jid, &count);
		if (buttons && button < count)
			return buttons[button] == GLFW_PRESS;
		return false;
	}

	int Input::GetGamepadAxisCount(int gamepad)
	{
		if (!IsGamepadConnected(gamepad))
			return 0;
		int count = 0;
		glfwGetJoystickAxes(GLFW_JOYSTICK_1 + gamepad, &count);
		return count;
	}

	int Input::GetGamepadButtonCount(int gamepad)
	{
		if (!IsGamepadConnected(gamepad))
			return 0;
		int count = 0;
		glfwGetJoystickButtons(GLFW_JOYSTICK_1 + gamepad, &count);
		return count;
	}

	std::string Input::GetGamepadName(int gamepad)
	{
		if (!IsGamepadConnected(gamepad))
			return "";

		const int jid = GLFW_JOYSTICK_1 + gamepad;

		// Prefer the mapping-database name for mapped pads
		if (glfwJoystickIsGamepad(jid))
		{
			const char* name = glfwGetGamepadName(jid);
			if (name)
				return name;
		}

		const char* name = glfwGetJoystickName(jid);
		return name ? name : "";
	}

	/////////////////////////////////////////////////////////////////////////////////
}