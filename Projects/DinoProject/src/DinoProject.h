#pragma once
#include <Cosmic.h>
#include <memory>
#include "ISimulationMode.h"

namespace Workspace
{
	class DinoProject : public Cosmic::Layer
	{
	public:
		DinoProject();
		virtual ~DinoProject() = default;

		virtual void OnAttach() override {}
		virtual void OnDetach() override;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		Cosmic::Ref<Cosmic::Texture2D> m_DinoTexture;
		Cosmic::Ref<Cosmic::Material>  m_DinoMaterial;

		// The master scene container context used across your simulation modes
		Cosmic::Ref<Cosmic::Scene>     m_Scene;

		std::unique_ptr<ISimulationMode> m_RunSim;
		std::unique_ptr<ISimulationMode> m_FlightSim;
		std::unique_ptr<ISimulationMode> m_StressSim;

		ISimulationMode* m_ActiveSim = nullptr;
		float m_SmoothedDeltaTime = 0.0f;
	};
}