#include "DinoProject.h"
#include "DinoRunLayer.h"
#include "DinoFlightLayer.h"
#include "DinoStressLayer.h"
#include <imgui.h>
#include <filesystem>
#include <Cosmic.h>

namespace Workspace
{
	DinoProject::DinoProject()
		: Layer("DinoProject")
	{
		CS_INFO("DinoProject: Initializing simulation layers...");

		// Define paths relative to the executable output
		std::string dinoPath = "assets/projects/DinoProject/Dino.png";
		std::string shaderPath = "assets/projects/DinoProject/shaders/DebugTexture.glsl";

		// Asset Verification Helper
		auto VerifyAsset = [](const std::string& path, const std::string& name) -> bool
			{
				if (!std::filesystem::exists(path))
				{
					CS_ERROR("DinoProject: FAILED to find {0} at '{1}'", name, path);
					return false;
				}
				CS_INFO("DinoProject: Successfully loaded {0}", name);
				return true;
			};

		// Validate assets exist before attempting to bind to GPU
		bool assetsLoaded = VerifyAsset(dinoPath, "Dino Texture") &&
			VerifyAsset(shaderPath, "Debug Shader");

		// --- Resource Loading ---
		// Cosmic::Texture2D and Cosmic::Shader are now available via Cosmic.h
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

		// Set Default
		m_ActiveSim = m_RunSim.get();

		CS_INFO("DinoProject: All systems operational.");
	}

	void DinoProject::OnDetach()
	{
		m_RunSim.reset();
		m_FlightSim.reset();
		m_StressSim.reset();
	}

	void DinoProject::OnUpdate(float ts)
	{
		float dt = ts > 0.0f ? ts : 0.001f;
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + dt * 0.05f;

		// Real-time Input Polling Mode Switches
		if (Cosmic::Input::IsKeyPressed(CS_KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim)
			m_ActiveSim->OnUpdate(ts);

		// Core Rendering Pass
		if (m_ActiveSim)
			m_ActiveSim->OnRender();
	}

	void DinoProject::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_ActiveSim)
			m_ActiveSim->OnFixedUpdate(deltaFixedTime);
	}

	void DinoProject::OnEvent(Cosmic::Event& e)
	{
		if (m_ActiveSim)
			m_ActiveSim->OnEvent(e);

		// Handle window viewport resizing triggers dynamically
		if (e.GetEventType() == Cosmic::EventType::WindowResize)
		{
			auto& re = (Cosmic::WindowResizeEvent&)e;
			if (m_RunSim)    m_RunSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
			if (m_FlightSim) m_FlightSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
			if (m_StressSim) m_StressSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
		}
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

// --- DLL Linkages ---
extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Workspace::DinoProject();
	}
}