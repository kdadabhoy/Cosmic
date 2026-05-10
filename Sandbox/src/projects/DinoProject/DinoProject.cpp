#include "DinoProject.h"
#include <imgui.h>

namespace Workspace
{
	DinoProject::DinoProject()
	{
		Cosmic::FileSystem::SetActiveProject("DinoProject");
		std::string dinoPath = Cosmic::FileSystem::Resolve("project://Dino.png");
		m_DinoTexture = Cosmic::Texture2D::Create(dinoPath);

		// 1. Create a specific material for the Dino
		// We use the standard texture shader but can customize parameters
		auto textureShader = Cosmic::Shader::Create("assets/shaders/Texture.glsl");
		m_DinoMaterial = Cosmic::Material::Create(textureShader, "DinoMaterial");
		m_DinoMaterial->Set("u_Texture", m_DinoTexture);
		m_DinoMaterial->Set("u_Color", glm::vec4(1.0f)); // Default white tint

		m_RunSim = std::make_unique<DinoRunLayer>(m_DinoMaterial);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_DinoMaterial);
		m_StressSim = std::make_unique<DinoStressLayer>(m_DinoMaterial);
		m_ActiveSim = m_RunSim.get();
	}

	void DinoProject::OnUpdate(float ts)
	{
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + ts * 0.05f;

		if (Cosmic::Input::IsKeyPressed(KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim) m_ActiveSim->OnUpdate(ts);
	}

	void DinoProject::OnRender() { if (m_ActiveSim) m_ActiveSim->OnRender(); }
	void DinoProject::OnEvent(Cosmic::Event& e) { if (m_ActiveSim) m_ActiveSim->OnEvent(e); }
	void DinoProject::SetViewportSize(float w, float h)
	{
		m_RunSim->SetViewportSize(w, h);
		m_FlightSim->SetViewportSize(w, h);
		m_StressSim->SetViewportSize(w, h);
	}

	void DinoProject::OnImGuiRender()
	{
		ImGui::Text("Dino Simulator Suite");
		ImGui::Text("FPS: %.0f", 1.0f / m_SmoothedDeltaTime);

		// --- Material Editor UI ---
		if (ImGui::CollapsingHeader("Global Dino Material"))
		{
			glm::vec4 color = m_DinoMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Dino Tint", &color.x))
				m_DinoMaterial->Set("u_Color", color);
		}

		ImGui::Separator();
		if (ImGui::Button("Runner [R]")) m_ActiveSim = m_RunSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Flight [F]")) m_ActiveSim = m_FlightSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Stress [T]")) m_ActiveSim = m_StressSim.get();
		ImGui::Separator();

		if (m_ActiveSim) m_ActiveSim->OnImGuiRender();
	}
}