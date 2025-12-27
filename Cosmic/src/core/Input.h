#pragma once

#include <glm/glm.hpp>

namespace Cosmic
{
	class Input
	{
	public:
		// Static methods so we can call them like Input::IsKeyPressed(key)
		static bool			IsKeyPressed(int keycode);
		static bool			IsMouseButtonPressed(int button);

		static glm::vec2	GetMousePosition();
		static float		GetMouseX();
		static float		GetMouseY();
	};
}