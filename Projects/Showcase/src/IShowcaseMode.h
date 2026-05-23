#pragma once
// IShowcaseMode.h
// Base interface for all CosmicShowcase simulation modes.

#include <Cosmic.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace Showcase
{
	// ============================================================
	// Custom ECS Components — private to CosmicShowcase DLL
	// ============================================================

	/// Physics state for the runner dino.
	struct RunnerDinoComponent
	{
		float VelocityY = 0.0f;
		bool  IsGrounded = true;
		float Score = 0.0f;
		float HighScore = 0.0f;
		float SpeedMultiplier = 1.0f; // increases over time
	};

	/// Marks an entity as a ground obstacle in the runner.
	struct ObstacleComponent
	{
		float Speed = 3.0f;
		float Width = 0.3f;
		float Height = 0.5f;
	};

	/// State for one of the two flight dinos.
	struct FlightDinoComponent
	{
		std::vector<glm::vec3> Trail;    // world positions, newest at back
		float    Speed = 2.0f;
		float    Slope = 0.0f;      // dy/dx ratio
		bool     Selected = false;
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
	};

	// ============================================================
	// Simulation Mode Interface
	// ============================================================

	class IShowcaseMode
	{
	public:
		virtual ~IShowcaseMode() = default;

		virtual const std::string& GetName()  const = 0;

		virtual void OnUpdate(float ts) {}
		virtual void OnFixedUpdate(float fixedDt) {}
		virtual void OnRender() {}
		virtual void OnImGuiRender() {}
		virtual void OnEvent(Cosmic::Event& e) {}

		// Called on every resize, including inactive sims
		virtual void SetViewportSize(float w, float h) {}
	};

} // namespace Showcase