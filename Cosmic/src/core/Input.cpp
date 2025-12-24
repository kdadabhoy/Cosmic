#include "core/Input.h"
#include "core/Application.h"
#include <GLFW/glfw3.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////
	bool Input::IsKeyPressed(int keycode)
	{
		// Reach into the singleton Application to get the GLFW window handle
		auto* windowHandle = Application::Get().GetWindow().getHandle();
		auto state = glfwGetKey(windowHandle, keycode);

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool Input::IsMouseButtonPressed(int button)
	{
		auto* windowHandle = Application::Get().GetWindow().getHandle();
		auto state = glfwGetMouseButton(windowHandle, button);

		return state == GLFW_PRESS;
	}

	/////////////////////////////////////////////////////////////////////////////////

	glm::vec2 Input::GetMousePosition()
	{
		auto* windowHandle = Application::Get().GetWindow().getHandle();
		double xpos, ypos;
		glfwGetCursorPos(windowHandle, &xpos, &ypos);

		return { (float)xpos, (float)ypos };
	}

	/////////////////////////////////////////////////////////////////////////////////

	float Input::GetMouseX()
	{
		return GetMousePosition().x;
	}

	/////////////////////////////////////////////////////////////////////////////////

	float Input::GetMouseY()
	{
		return GetMousePosition().y;
	}
}