#pragma once
#include "ISimulationMode.h"
#include <vector>

namespace Workspace
{
	class DinoFlightLayer : public ISimulationMode
	{
	public:
		DinoFlightLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoFlightLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override { m_CameraController.OnEvent(e); }
		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_Material;
		Cosmic::OrthographicCameraController m_CameraController;

		Cosmic::Entity m_FlightDino;
	};
}