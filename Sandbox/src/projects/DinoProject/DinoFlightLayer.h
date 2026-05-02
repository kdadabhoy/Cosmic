#pragma once
#include "../../Simulation.h"
#include <vector>

namespace Workspace
{
	class DinoFlightLayer : public Simulation
	{
	public:
		DinoFlightLayer(Cosmic::Ref<Cosmic::Texture2D> texture);

		virtual void OnUpdate(float ts) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }

	private:
		Cosmic::Ref<Cosmic::Texture2D> m_Texture;
		Cosmic::OrthographicCameraController m_CameraController;

		glm::vec3 m_DinoPos = { 0.0f, 0.0f, 0.0f };
		std::vector<glm::vec3> m_FlightPath;

		float m_FlightSpeed = 2.0f;
		float m_FlightSlope = 0.0f;
		bool m_ChaosMode = false;
		bool m_CameraFollow = true;
	};
}