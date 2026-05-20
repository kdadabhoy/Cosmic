#pragma once
#include <Cosmic.h>

namespace Workspace
{
	class ISimulationMode
	{
	public:
		virtual ~ISimulationMode() = default;

		virtual void OnUpdate(float ts) {}
		virtual void OnFixedUpdate(float deltaFixedTime) {}
		virtual void OnRender() {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Cosmic::Event& e) {}
		virtual void SetViewportSize(float w, float h) {}
	};
}