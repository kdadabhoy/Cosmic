#pragma once
#include <Cosmic.h>
#include <vector>
#include <random>

namespace Workspace
{
	// ============================================================================
	// Simulation component — registered cross-DLL safe via CS_REGISTER_COMPONENT
	// ============================================================================
	struct BallComponent
	{
		glm::vec2 Velocity = { 0.0f, 0.0f };
		float     Radius = 0.2f;
		float     Mass = 1.0f;
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	// ============================================================================
	// TemplateSimLayer
	//
	// Demonstrates:
	//   - ECS entity creation with AddComponent / GetComponent
	//   - OnFixedUpdate for deterministic physics (gravity, wall bounce)
	//   - OnUpdate for smooth visual rendering
	//   - SDF circles as the primary primitive
	//   - Timeline guards (pause / rewind via dt <= 0)
	//   - ImGui runtime controls: spawn, clear, gravity, damping
	//   - CS_REGISTER_COMPONENT for DLL-safe component type IDs
	// ============================================================================

	class TemplateSimLayer : public Cosmic::Layer
	{
	public:
		explicit TemplateSimLayer(Cosmic::Ref<Cosmic::Scene> scene);
		virtual ~TemplateSimLayer() override = default;

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
		// Scene / entities
		Cosmic::Ref<Cosmic::Scene>       m_Scene;
		std::vector<Cosmic::Entity>      m_Balls;

		// Camera
		Cosmic::OrthographicCameraController m_Camera;
		glm::vec2                            m_ViewportSize = { 1280.0f, 720.0f };

		// Simulation parameters
		float m_Gravity = -9.8f;
		float m_Damping = 0.78f;
		float m_BoundsX = 5.0f;
		float m_BoundsY = 4.0f;
		int   m_SpawnCount = 8;

		// Stats
		uint32_t m_FixedTicks = 0;

		// RNG
		std::mt19937 m_Rng{ std::random_device{}() };
	};
}

// DLL-safe component registration — must live at file scope in the header
CS_REGISTER_COMPONENT(Workspace::BallComponent)