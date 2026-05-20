#pragma once
#include "ISimulationMode.h"
#include <vector>

namespace Workspace
{
	class DinoFlightLayer : public ISimulationMode
	{
	public:
		DinoFlightLayer(Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoFlightLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override { m_CameraController.OnEvent(e); }
		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }

	private:
		Cosmic::Ref<Cosmic::Material> m_Material;
		Cosmic::OrthographicCameraController m_CameraController;

		glm::vec3 m_DinoPos = { 0.0f, 0.0f, 0.0f };
		std::vector<glm::vec3> m_FlightPath;

		float m_FlightSpeed = 2.0f;
		float m_FlightSlope = 0.0f;
		bool m_ChaosMode = false;
		bool m_CameraFollow = true;
	};
}