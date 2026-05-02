#include "DinoProject.h"
#include <imgui.h>

namespace Workspace
{
	DinoProject::DinoProject()
	{
		m_DinoTexture = Cosmic::Texture2D::Create("assets/shaders/Texture.png");
		m_RunSim = std::make_unique<DinoRunLayer>(m_DinoTexture);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_DinoTexture);
		m_StressSim = std::make_unique<DinoStressLayer>(m_DinoTexture);
		m_ActiveSim = m_RunSim.get();
	}

	void DinoProject::OnUpdate(float ts)
	{
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + ts * 0.05f;

		// Hotkeys for switching
		if (Cosmic::Input::IsKeyPressed(KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim)
			m_ActiveSim->OnUpdate(ts);
	}

	void DinoProject::OnEvent(Cosmic::Event& e)
	{
		if (m_ActiveSim)
			m_ActiveSim->OnEvent(e);
	}

	void DinoProject::OnRender()
	{
		// We no longer bind the Framebuffer here because 
		// WorkspaceLayer::OnUpdate handles the binding for us!
		if (m_ActiveSim)
			m_ActiveSim->OnRender();
	}

	void DinoProject::SetViewportSize(float w, float h)
	{
		m_RunSim->SetViewportSize(w, h);
		m_FlightSim->SetViewportSize(w, h);
		m_StressSim->SetViewportSize(w, h);
	}

	void DinoProject::OnImGuiRender()
	{
		// This appears inside the "Project Inspector" window of WorkspaceLayer
		ImGui::Text("Dino Simulator Suite");
		ImGui::Text("FPS: %.0f", 1.0f / m_SmoothedDeltaTime);
		ImGui::Separator();

		if (ImGui::Button("Runner [R]")) m_ActiveSim = m_RunSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Flight [F]")) m_ActiveSim = m_FlightSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Stress [T]")) m_ActiveSim = m_StressSim.get();

		ImGui::Separator();

		// Render the active sub-sim's UI
		if (m_ActiveSim)
			m_ActiveSim->OnImGuiRender();
	}
}