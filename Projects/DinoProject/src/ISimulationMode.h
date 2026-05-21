#pragma once
#include <Cosmic.h>
#include <vector>
#include <glm/glm.hpp>

namespace Workspace
{
	// ============================================================================
	// CUSTOM SIMULATION ECS COMPONENTS (Private to Dino Project DLL Assembly)
	// ============================================================================

	// Tracks a runner obstacle's movement, bounding, and visual properties
	struct RunnerObstacleComponent
	{
		glm::vec2 Size;
		glm::vec4 Color;
		float Speed = 2.0f;
	};

	// Tracks trail histories, velocities, and tracking flags for flight simulations
	struct FlightTrailComponent
	{
		std::vector<glm::vec3> Path;
		float FlightSpeed = 2.0f;
		float FlightSlope = 0.0f;
		bool ChaosMode = false;
		bool CameraFollow = true;
	};


	// ============================================================================
	// SIMULATION INTERFACE BASE CLASS
	// ============================================================================
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