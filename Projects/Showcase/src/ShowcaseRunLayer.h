#pragma once

#include <Cosmic.h>
#include <vector>
#include <random>

namespace Showcase
{
	struct RunnerDinoComponent
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
		ShowcaseRunLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> dinoMaterial);
		virtual ~ShowcaseRunLayer() override = default;

		// Native engine lifecycle mappings
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		void Reset();
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;
		Cosmic::OrthographicCameraController m_Camera;

		Cosmic::Entity m_DinoEntity;
		std::vector<Cosmic::Entity> m_Obstacles;

		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 1.8f;
		bool m_GameOver = false;

		std::mt19937 m_Rng{ std::random_device{}() };
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };

		static constexpr float k_GroundY = -0.8f;
		static constexpr float k_Gravity = -16.0f;
		static constexpr float k_JumpV = 6.2f;

		float m_AccumulatedTime = 0.0f;
	};
}