#pragma once
#include "Cosmic.h"

namespace Cosmic
{
	class GameScene
	{
	public:
		virtual ~GameScene() = default;
		virtual void OnUpdate(float ts) = 0;
		virtual void OnRender() = 0;
		virtual void OnImGuiRender() = 0;
		virtual void SetViewportSize(float width, float height) = 0;
	};
}