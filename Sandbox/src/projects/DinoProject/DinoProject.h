#pragma once
#include "../../Simulation.h"
#include "DinoRunLayer.h"
#include "DinoFlightLayer.h"
#include "DinoStressLayer.h"
#include <memory>

namespace Workspace
{
	class DinoProject : public Simulation
	{
	public:
		DinoProject();
		virtual ~DinoProject() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;
		virtual void SetViewportSize(float w, float h) override;

	private:
		Cosmic::Ref<Cosmic::Texture2D> m_DinoTexture;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial; // New Material

		std::unique_ptr<DinoRunLayer> m_RunSim;
		std::unique_ptr<DinoFlightLayer> m_FlightSim;
		std::unique_ptr<DinoStressLayer> m_StressSim;

		Simulation* m_ActiveSim = nullptr;
		float m_SmoothedDeltaTime = 0.0f;
	};
}