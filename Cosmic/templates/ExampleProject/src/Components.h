#pragma once
#include <glm/glm.hpp>
#include <Cosmic.h>

namespace Workspace
{
	struct BallComponent
	{
		glm::vec2 Velocity = { 0.0f, 0.0f };
		float     Radius = 0.2f;
		float     Mass = 1.0f;
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};
}

// Ensure type registry safety for cross-DLL component pools
CS_REGISTER_COMPONENT(Workspace::BallComponent)