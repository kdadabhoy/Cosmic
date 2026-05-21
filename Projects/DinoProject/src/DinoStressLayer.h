#pragma once

#include <Cosmic.h>
#include <vector>
#include "ISimulationMode.h"

namespace Workspace
{
	class DinoStressLayer : public ISimulationMode
	{
	public:
		DinoStressLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> fireMaterial, Cosmic::Ref<Cosmic::Material> dinoMaterial);
		virtual ~DinoStressLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;

		// Hook this up so events can flow directly down to the controller
		virtual void OnEvent(Cosmic::Event& e) override { m_CamController.OnEvent(e); }

		virtual const std::string& GetName() const override { static std::string name = "DinoStressLayer"; return name; }
		virtual void SetViewportSize(float width, float height) override {}

	private:
		void RegenerateGrid();

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_FireMaterial;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;

		// FIX: Use the Controller here, not the raw camera
		Cosmic::OrthographicCameraController m_CamController;

		std::vector<Cosmic::Entity> m_GridEntities;
		int m_GridSize = 25;

		float m_Time = 0.0f;
		uint32_t m_FixedUpdateCount = 0;
	};
}