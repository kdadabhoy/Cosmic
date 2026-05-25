#pragma once
// ShowcaseRunLayer.h

#include <Cosmic.h>
#include <vector>
#include <random>

namespace Showcase
{
	struct RunnerFlameComponent
	{
		float Score = 0.0f;
		float HighScore = 0.0f;
		float SpeedMultiplier = 1.0f;
		float VelocityY = 0.0f;
		bool IsGrounded = false;
	};

	struct ObstacleComponent
	{
		float Speed = 3.5f;
		float Width = 0.3f;
		float Height = 1.0f;
	};

	class ShowcaseRunLayer : public Cosmic::Layer
	{
	public:
		ShowcaseRunLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> flameMaterial);
		virtual ~ShowcaseRunLayer() override = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		void Reset();
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_FlameMaterial;
		Cosmic::OrthographicCameraController m_Camera;

		Cosmic::Entity m_FlameEntity;
		std::vector<Cosmic::Entity> m_Obstacles;

		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 1.8f;
		bool m_GameOver = false;

		std::mt19937 m_Rng{ std::random_device{}() };
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		static constexpr float k_GroundY = -0.8f;
		static constexpr float k_Gravity = -16.0f;
		static constexpr float k_JumpV = 6.2f;

		float m_BaseObstacleSpeed = 3.5f;
	};
}

// CRITICAL STEP 2 ENGINE SYNCHRONIZATION:
// Register the components safely across the executable and DLL plugin boundary.
CS_REGISTER_COMPONENT(Showcase::RunnerFlameComponent)
CS_REGISTER_COMPONENT(Showcase::ObstacleComponent)