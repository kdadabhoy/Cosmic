#include "DinoProject.h"
#include "DinoRunLayer.h"
#include "DinoFlightLayer.h"
#include "DinoStressLayer.h"
#include <imgui.h>
#include <implot.h>
#include <filesystem>
#include <Cosmic.h>

namespace Workspace
{
	DinoProject::DinoProject()
		: Layer("DinoProject")
	{
		CS_INFO("DinoProject: Initializing simulation layers...");

		std::string dinoPath = "assets/projects/DinoProject/Dino.png";
		std::string shaderPath = "assets/projects/DinoProject/shaders/DebugTexture.glsl";
		std::string fireShaderPath = "assets/projects/DinoProject/shaders/FireShader.glsl";

		// Asset Verification Helper to ensure assets exist before loading
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

		bool assetsLoaded = VerifyAsset(dinoPath, "Dino Texture") &&
			VerifyAsset(shaderPath, "Debug Shader") &&
			VerifyAsset(fireShaderPath, "Fire Shader");

		// --- Resource Loading ---
		m_DinoTexture = Cosmic::Texture2D::Create(dinoPath);

		auto debugShader = Cosmic::Shader::Create(shaderPath);
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);

		// Crucial: Initialize the sampler array slots inside the shader context layout 
		if (debugShader)
		{
			debugShader->Bind();
			int32_t samplers[32];
			for (uint32_t i = 0; i < 32; i++) samplers[i] = i;
			debugShader->SetIntArray("u_Textures", samplers, 32);
		}

		m_DinoMaterial = Cosmic::Material::Create(debugShader, "DinoMaterial");
		if (m_DinoMaterial)
		{
			// Fix: Redundantly bind keys to catch whatever string literal your Renderer2D core expects
			m_DinoMaterial->Set("u_Texture", m_DinoTexture);
			m_DinoMaterial->Set("u_Textures", m_DinoTexture); // Add this fallback key!
			m_DinoMaterial->Set("Texture", m_DinoTexture);    // Add this fallback key!

			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
		}

		m_FireMaterial = Cosmic::Material::Create(fireShader, "FireMaterial");

		// --- Long-Term Static Factory Instantiation ---
		m_Scene = Cosmic::Scene::Create();

		// Instantiate layers passing down our shared master scene smart pointer context
		m_RunSim = std::make_unique<DinoRunLayer>(m_Scene, m_DinoMaterial);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_Scene, m_DinoMaterial);
		m_StressSim = std::make_unique<DinoStressLayer>(m_Scene, m_FireMaterial); // Swapped to fire material!

		// Default Active Layer Configuration
		m_ActiveSim = m_RunSim.get();
		CS_INFO("DinoProject: All systems operational.");
	}

	void DinoProject::OnDetach()
	{
		// Clean up and release owned unique smart pointer simulation contexts safely
		m_RunSim.reset();
		m_FlightSim.reset();
		m_StressSim.reset();
		m_FireMaterial.reset();
		m_DinoMaterial.reset();
		m_Scene.reset();
	}

	void DinoProject::OnUpdate(float ts)
	{
		// 1. Prevent division by zero if timescale freezes or hits a hard stutter step frame
		float dt = ts > 0.0f ? ts : 0.001f;
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + dt * 0.05f;

		// 2. Accumulate continuous frame ticks over time (ADD THIS LINE IF MISSING)
		static float s_AccumulatedTime = 0.0f;
		s_AccumulatedTime += dt; // Use smoothed or raw timestep delta

		// 3. Upload the current timeline clock to the Fire Material uniform cache
		if (m_FireMaterial)
		{
			m_FireMaterial->Set("u_Time", s_AccumulatedTime);
		}

		// 4. Handle active simulation mode quick switching hotkeys
		if (Cosmic::Input::IsKeyPressed(CS_KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_T)) m_ActiveSim = m_StressSim.get();

		if (m_ActiveSim)
		{
			m_ActiveSim->OnUpdate(ts);
			m_ActiveSim->OnRender(); // Route system parameters to Renderer2D batchers
		}
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