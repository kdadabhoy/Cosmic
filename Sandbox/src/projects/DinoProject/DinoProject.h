#pragma once
#include "../../Simulation.h"
#include "DinoRunLayer.h"
#include "DinoFlightLayer.h"
#include "StressTestLayer.h"

namespace Workspace
{

	class DinoProject : public Simulation
	{
	public:
		DinoProject()
		{
			m_RunSim = std::make_unique<DinoRunLayer>();
			m_FlightSim = std::make_unique<DinoFlightLayer>();
			m_StressSim = std::make_unique<StressTestLayer>();

			m_ActiveSim = m_RunSim.get(); // Default
		}

		virtual void OnUpdate(float ts) override { m_ActiveSim->OnUpdate(ts); }


		virtual void OnRender() override
		{
			// 1. Wipe the "Project Canvas" before drawing the new frame.
			// This uses your legacy helper: SetClearColor + Clear.
			Cosmic::RenderCommand::Clear(0.1f, 0.1f, 0.1f);

			// 2. Now draw whichever sim is active (Runner, Flight, or Stress)
			if (m_ActiveSim)
			{
				m_ActiveSim->OnRender();
			}
		}


		virtual void OnImGuiRender() override
		{
			ImGui::Text("DINO MISSION CONTROL");
			if (ImGui::Button("Launch Runner")) m_ActiveSim = m_RunSim.get();
			ImGui::SameLine();
			if (ImGui::Button("Launch Flight")) m_ActiveSim = m_FlightSim.get();
			ImGui::SameLine();
			if (ImGui::Button("Launch Stress Test")) m_ActiveSim = m_StressSim.get();

			ImGui::Separator();
			m_ActiveSim->OnImGuiRender();
		}

		virtual void SetViewportSize(float w, float h) override
		{
			m_RunSim->SetViewportSize(w, h);
			m_FlightSim->SetViewportSize(w, h);
			m_StressSim->SetViewportSize(w, h);
		}

	private:
		std::unique_ptr<DinoRunLayer> m_RunSim;
		std::unique_ptr<DinoFlightLayer> m_FlightSim;
		std::unique_ptr<StressTestLayer> m_StressSim;
		Simulation* m_ActiveSim = nullptr;
	};
}