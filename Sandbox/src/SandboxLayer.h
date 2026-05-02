#pragma once
#include "Cosmic.h"
#include "GameScene.h"

namespace Cosmic
{
	class SandboxLayer : public Layer
	{
	public:
		SandboxLayer();
		virtual void OnAttach() override;
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnImGuiRender() override;
	private:
		void SelectScene(int index);
		Scope<GameScene> m_ActiveScene;
		int m_CurrentMode = 0;
		glm::vec2 m_ViewportSize = { 0, 0 };
	};
}