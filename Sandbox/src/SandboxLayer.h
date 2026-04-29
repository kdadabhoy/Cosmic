#pragma once

#include "Cosmic.h"
#include "camera/OrthographicCameraController.h" // New Include
#include <vector>
#include <random>

namespace Cosmic
{
	struct Obstacle
	{
		glm::vec3 Position;
		glm::vec2 Size;
		glm::vec4 Color;
	};

	class SandboxLayer : public Layer
	{
	public:
		SandboxLayer();
		virtual ~SandboxLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;

	private:
		void OnRender();
		void ResetGame();

	private:
		// Replaced unique_ptr<OrthographicCamera> with the controller
		OrthographicCameraController m_CameraController;
		Ref<Texture2D> m_Texture;

		// --- Gameplay State ---
		glm::vec3 m_DinoPos = { -1.0f, -0.5f, 0.0f };
		float m_DinoRotation = 0.0f;
		float m_VelocityY = 0.0f;
		bool m_IsGrounded = true;
		float m_Score = 0.0f;

		std::vector<Obstacle> m_Obstacles;
		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 2.0f;

		// --- Input & Engine Toggles ---
		bool m_ShowStats = true;
		bool m_StressTestMode = false;

		// Debouncing flags
		bool m_TKeyPressed = false;
		bool m_F1KeyPressed = false;

		float m_SmoothedDeltaTime = 0.016f;

		// Randomization
		std::mt19937 m_RandomEngine;
	};
}