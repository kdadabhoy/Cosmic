#pragma once

#include "core/Core.h"

namespace Cosmic
{
	class Scene;

	class COSMIC_API System
	{
	public:
		virtual ~System() = default;

		virtual void OnUpdate(Scene& scene, float deltaTime) {}
		virtual void OnFixedUpdate(Scene& scene, float deltaTime) {}
	};
}