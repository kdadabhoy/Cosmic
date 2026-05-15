#include "DinoProject.h"
#include <imgui.h>
#include "core/Log.h" // Using logging system

namespace Workspace
{
	DinoProject::DinoProject()
	{
		Cosmic::FileSystem::SetActiveProject("DinoProject");

		// --- Path Resolution ---
		std::string dinoPath = Cosmic::FileSystem::Resolve("project://Dino.png");
		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/DebugTexture.glsl");

		// --- Resource Loading ---
		m_DinoTexture = Cosmic::Texture2D::Create(dinoPath);
		auto debugShader = Cosmic::Shader::Create(shaderPath);
		m_DinoMaterial = Cosmic::Material::Create(debugShader, "DinoMaterial");

		// --- Material Setup ---
		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Texture", m_DinoTexture);
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
		}

		// --- Simulation Initialization ---
		m_RunSim = std::make_unique<DinoRunLayer>(m_DinoMaterial);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_DinoMaterial);
		m_StressSim = std::make_unique<DinoStressLayer>(m_DinoMaterial);

		m_ActiveSim = m_RunSim.get();

		// Log resource status using the engine's core logger
		CS_CORE_INFO("DinoProject: Resolved Texture Path: {0}", dinoPath);
		CS_CORE_INFO("DinoProject: Resolved Shader Path: {0}", shaderPath);

		if (!std::filesystem::exists(dinoPath))
			CS_CORE_ERROR("DinoProject: Dino.png NOT FOUND at {0}", dinoPath);

		if (!std::filesystem::exists(shaderPath))
			CS_CORE_ERROR("DinoProject: DebugTexture.glsl NOT FOUND at {0}", shaderPath);
	}

	void DinoProject::OnUpdate(float ts)
	{
		// Smooth delta time for the UI FPS counter
		float dt = ts > 0.0f ? ts : 0.001f;
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + dt * 0.05f;

		// Real-time Input Handling (Polling)
		if (Cosmic::Input::IsKeyPressed(CS_KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim)
			m_ActiveSim->OnUpdate(ts);
	}

	/**
	 * OnFixedUpdate
	 * * THE PASSTHROUGH: Forwards the stable physics heartbeat to the active
	 * simulation layer. This ensures that whichever mode we are in (Run,
	 * Flight, or Stress) receives the constant-time signal.
	 */
	void DinoProject::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_ActiveSim)
			m_ActiveSim->OnFixedUpdate(deltaFixedTime);
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

		// --- Global Material Editor ---
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

		// Render the active sub-layer's specific UI
		if (m_ActiveSim)
			m_ActiveSim->OnImGuiRender();

		ImGui::End();
	}
}