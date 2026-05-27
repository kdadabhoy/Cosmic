#pragma once

#include <Cosmic.h>
#include "BallPhysicsSystem.h"
#include "Components.h"
#include <random>

namespace Workspace
{
	class TemplateParallelSimLayer : public Cosmic::Layer
	{
	public:
		explicit TemplateParallelSimLayer(Cosmic::Ref<Cosmic::Scene> scene);
		virtual ~TemplateParallelSimLayer() override = default;

		virtual void OnAttach()                          override;
		virtual void OnDetach()                          override;
		virtual void OnUpdate(float ts)                  override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender()                     override;
		virtual void OnEvent(Cosmic::Event& e)           override;

	private:
		void SpawnBall(glm::vec2 position, glm::vec2 velocity);
		void ClearBalls();
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		Cosmic::Ref<Cosmic::Scene>           m_Scene;
		Cosmic::OrthographicCameraController m_Camera;
		glm::vec2                            m_ViewportSize = { 1280.0f, 720.0f };

		// Handle to our backend parallel processing execution system
		BallPhysicsSystem* m_PhysicsSystem = nullptr;
		Cosmic::Ref<Cosmic::Shader>          m_SpecularCircleShader = nullptr;

		int      m_SpawnCount = 8;
		uint32_t m_FixedTicks = 0;
		std::mt19937 m_Rng{ std::random_device{}() };
	};
}