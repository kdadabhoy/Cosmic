#include "DinoProject.h"
#include <imgui.h>
#include <iostream>

namespace Workspace
{
	DinoProject::DinoProject()
	{
		Cosmic::FileSystem::SetActiveProject("DinoProject");

		// --- Path Resolution ---
		// Based on your FileSystem.h logic:
		// "project://Dino.png" -> "assets/DinoProject/Dino.png"
		std::string dinoPath = Cosmic::FileSystem::Resolve("project://Dino.png");

		// "project://shaders/DebugTexture.glsl" -> "assets/DinoProject/shaders/DebugTexture.glsl"
		// Note: We removed "assets/" from the middle because FileSystem/CMake handles it.
		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/DebugTexture.glsl");

		// --- Resource Loading ---
		m_DinoTexture = Cosmic::Texture2D::Create(dinoPath);

		auto debugShader = Cosmic::Shader::Create(shaderPath);
		m_DinoMaterial = Cosmic::Material::Create(debugShader, "DinoMaterial");

		// --- Material Setup ---
		if (m_DinoMaterial)
		{
			// Ensure these uniform names match your DebugTexture.glsl exactly!
			m_DinoMaterial->Set("u_Texture", m_DinoTexture);
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
		}

		// --- Simulation Initialization ---
		m_RunSim = std::make_unique<DinoRunLayer>(m_DinoMaterial);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_DinoMaterial);
		m_StressSim = std::make_unique<DinoStressLayer>(m_DinoMaterial);

		m_ActiveSim = m_RunSim.get();


		// Add these to DinoProject.cpp constructor
		std::cout << "[DEBUG] Resolved Texture Path: " << dinoPath << std::endl;
		std::cout << "[DEBUG] Resolved Shader Path: " << shaderPath << std::endl;

		if (!std::filesystem::exists(dinoPath))
			std::cout << "[ERROR] Dino.png NOT FOUND at resolved path!" << std::endl;

		if (!std::filesystem::exists(shaderPath))
			std::cout << "[ERROR] DebugTexture.glsl NOT FOUND at resolved path!" << std::endl;

	}

	void DinoProject::OnUpdate(float ts)
	{
		// Prevent division by zero on first frame
		float dt = ts > 0.0f ? ts : 0.001f;
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + dt * 0.05f;

		// Input Handling
		if (Cosmic::Input::IsKeyPressed(KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim)
			m_ActiveSim->OnUpdate(ts);
	}

	void DinoProject::OnRender()
	{
		if (m_ActiveSim)
			m_ActiveSim->OnRender();
	}

	void DinoProject::OnEvent(Cosmic::Event& e)
	{
		if (m_ActiveSim)
			m_ActiveSim->OnEvent(e);
	}

	void DinoProject::SetViewportSize(float w, float h)
	{
		if (m_RunSim)    m_RunSim->SetViewportSize(w, h);
		if (m_FlightSim) m_FlightSim->SetViewportSize(w, h);
		if (m_StressSim) m_StressSim->SetViewportSize(w, h);
	}

	void DinoProject::OnImGuiRender()
	{
		ImGui::Begin("Dino Project Controller");

		ImGui::Text("Dino Simulator Suite");
		ImGui::Text("FPS: %.0f (%.3f ms)", 1.0f / m_SmoothedDeltaTime, m_SmoothedDeltaTime * 1000.0f);

		// --- Material Editor UI ---
		if (m_DinoMaterial && ImGui::CollapsingHeader("Global Dino Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_DinoMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Dino Tint", &color.x))
				m_DinoMaterial->Set("u_Color", color);
		}

		ImGui::Separator();
		ImGui::Text("Switch Mode:");
		if (ImGui::Button("Runner [R]")) m_ActiveSim = m_RunSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Flight [F]")) m_ActiveSim = m_FlightSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Stress [T]")) m_ActiveSim = m_StressSim.get();

		ImGui::Separator();

		if (m_ActiveSim)
			m_ActiveSim->OnImGuiRender();

		ImGui::End();
	}
}