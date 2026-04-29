#pragma once

#include "Cosmic.h"
#include "camera/OrthographicCameraController.h"
#include <vector>
#include <random>

namespace Cosmic
{
	enum class SceneMode { DinoRunner = 0, FlightSim = 1 };

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
		void ResetCamera();


	private:
		std::vector<glm::vec3> m_FlightPath;
		const size_t m_MaxPathPoints = 500; // Keep performance stable

	private:
		OrthographicCameraController m_CameraController;
		Ref<Texture2D> m_Texture;

		SceneMode m_CurrentMode = SceneMode::DinoRunner;

		// --- Common State ---
		glm::vec3 m_DinoPos = { -1.0f, -0.5f, 0.0f };
		float m_DinoRotation = 0.0f;
		float m_SmoothedDeltaTime = 0.016f;
		bool m_ShowStats = true;

		// --- Dino Runner Specifics ---
		float m_VelocityY = 0.0f;
		bool m_IsGrounded = true;
		float m_Score = 0.0f;
		std::vector<Obstacle> m_Obstacles;
		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 2.0f;
		bool m_StressTestMode = false;

		// --- Flight Sim Specifics ---
		float m_FlightSpeed = 2.0f;
		float m_FlightSlope = 1.0f;
		bool m_CameraFollow = true;

		// --- Input Debouncing ---
		bool m_TKeyPressed = false;
		bool m_F1KeyPressed = false;
		bool m_CKeyPressed = false;
		bool m_FKeyPressed = false;

		// --- UI State Tracking ---
		float m_MinZ = 0.25f, m_MaxZ = 10.0f;
		float m_MinBoundX = -1000.0f, m_MaxBoundX = 1000.0f;
		float m_MinBoundY = -1000.0f, m_MaxBoundY = 1000.0f;

		std::mt19937 m_RandomEngine;
	};
}