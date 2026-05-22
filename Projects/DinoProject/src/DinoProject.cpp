#include "DinoProject.h"
#include "DinoRunLayer.h"
#include "DinoFlightLayer.h"
#include "DinoStressLayer.h"
#include "DinoShaderTestLayer.h" // Include your new shader sandbox compilation header
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

		// Restored back to native distinct pipeline shader profiles
		std::string shaderPath = "assets/projects/DinoProject/shaders/DebugTexture.glsl";
		std::string fireShaderPath = "assets/projects/DinoProject/shaders/FireShader.glsl";

		// shaderTest
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/GloriousLineAlgorithm.glsl";
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/Octagrams.glsl";
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/FractalPyramid.glsl";
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/CyberFuji.glsl";
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/Mandelbulb.glsl";
		//std::string shaderTestPath = "assets/projects/DinoProject/shaders/Cloud.glsl";
		std::string shaderTestPath = "assets/projects/DinoProject/shaders/Space.glsl";




		auto VerifyAsset = [](const std::string& path, const std::string& name) -> bool
			{
				if (!std::filesystem::exists(path))
				{
					CS_ERROR("DinoProject: FAILED to find {0} at '{1}'", name, path);
					return false;
				}
				uintmax_t fileSize = std::filesystem::file_size(path);
				CS_INFO("DinoProject: Verified asset '{0}' ({1} bytes)", name, fileSize);
				return true;
			};

		bool assetsValid = VerifyAsset(dinoPath, "Dino Texture") &&
			VerifyAsset(shaderPath, "Debug Shader") &&
			VerifyAsset(fireShaderPath, "Fire Shader") &&
			VerifyAsset(shaderTestPath, "Inputted Test Shader");

		if (!assetsValid)
		{
			CS_ERROR("DinoProject: Critical error during asset verification. Aborting structural simulation allocation!");
			return;
		}

		m_DinoTexture = Cosmic::Texture2D::Create(dinoPath);
		CS_TRACE("DinoProject: Native Texture2D allocated handle ID: {0}", m_DinoTexture->GetRendererID());

		auto debugShader = Cosmic::Shader::Create(shaderPath);
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);
		auto shaderTestShader = Cosmic::Shader::Create(shaderTestPath);

		m_DinoMaterial = Cosmic::Material::Create(debugShader, "DinoMaterial");
		if (m_DinoMaterial && m_DinoTexture)
		{
			m_DinoMaterial->Set("Texture", m_DinoTexture);
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
			CS_INFO("DinoProject: DinoMaterial bound to registry address: 0x{0:x}", (uintptr_t)m_DinoMaterial.get());
		}

		m_FireMaterial = Cosmic::Material::Create(fireShader, "FireMaterial");
		if (m_FireMaterial)
		{
			m_FireMaterial->Set("u_Color", glm::vec4(1.0f, 0.5f, 0.2f, 1.0f));
		}

		// Allocate new procedural material context tracking container
		m_ShaderTestMaterial = Cosmic::Material::Create(shaderTestShader, "ShaderTestMaterial");

		m_Scene = Cosmic::Scene::Create();
		CS_TRACE("DinoProject: Master scene runtime context created.");

		m_RunSim = std::make_unique<DinoRunLayer>(m_Scene, m_DinoMaterial);
		m_FlightSim = std::make_unique<DinoFlightLayer>(m_Scene, m_DinoMaterial);
		m_StressSim = std::make_unique<DinoStressLayer>(m_Scene);

		// Initialize the structural sandbox layout mode container
		m_ShaderTestSim = std::make_unique<DinoShaderTestLayer>(m_Scene, m_ShaderTestMaterial);

		auto stressLayerPtr = static_cast<DinoStressLayer*>(m_StressSim.get());
		if (stressLayerPtr)
		{
			stressLayerPtr->SetMaterials(m_FireMaterial, m_DinoMaterial);
		}

		m_ActiveSim = m_RunSim.get();
		CS_INFO("DinoProject: All sub-simulation layers bound. Simulation root fully operational.");
	}

	void DinoProject::OnDetach()
	{
		CS_WARN("DinoProject: Detach lifecycle triggered. Releasing resource handlers...");

		m_ShaderTestSim.reset();
		m_RunSim.reset();
		m_FlightSim.reset();
		m_StressSim.reset();

		m_ShaderTestMaterial.reset();
		m_FireMaterial.reset();
		m_DinoMaterial.reset();
		m_Scene.reset();

		CS_INFO("DinoProject: All simulation contexts cleanly destroyed.");
	}

	void DinoProject::OnUpdate(float ts)
	{
		float dt = ts > 0.0f ? ts : 0.001f;
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + dt * 0.05f;

		static float s_AccumulatedTime = 0.0f;
		static float s_LogHeartbeatTimer = 0.0f;
		s_AccumulatedTime += dt;
		s_LogHeartbeatTimer += dt;

		if (s_LogHeartbeatTimer >= 5.0f)
		{
			CS_TRACE("Telemetry: RunTime: {0:.2f}s | FPS: {1:.1f} ({2:.3f} ms/frame)",
				s_AccumulatedTime, 1.0f / m_SmoothedDeltaTime, m_SmoothedDeltaTime * 1000.0f);
			s_LogHeartbeatTimer = 0.0f;
		}

		if (m_FireMaterial)
		{
			m_FireMaterial->Set("u_Time", s_AccumulatedTime);
		}

		// Update global clock updates inside the automated preprocessor uniform hook bounds
		if (m_ShaderTestMaterial)
		{
			m_ShaderTestMaterial->Set("u_Time", s_AccumulatedTime);
		}

		auto previousSim = m_ActiveSim;

		if (Cosmic::Input::IsKeyPressed(CS_KEY_R)) m_ActiveSim = m_RunSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_F)) m_ActiveSim = m_FlightSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_T)) m_ActiveSim = m_StressSim.get();
		if (Cosmic::Input::IsKeyPressed(CS_KEY_G)) m_ActiveSim = m_ShaderTestSim.get(); // Hook up G hotkey

		if (m_ActiveSim != previousSim && m_ActiveSim != nullptr)
		{
			CS_INFO("DinoProject: Active context modified. Swapped target layer pointer over to pipeline state: '{0}' at timestamp {1:.2f}s",
				m_ActiveSim->GetName(), s_AccumulatedTime);
		}

		if (m_ActiveSim)
		{
			m_ActiveSim->OnUpdate(ts);
			m_ActiveSim->OnRender();
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
			CS_TRACE("DinoProject: Catching resize notification cascade ({0}, {1})", re.GetWidth(), re.GetHeight());

			if (m_RunSim)        m_RunSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
			if (m_FlightSim)     m_FlightSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
			if (m_StressSim)     m_StressSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
			if (m_ShaderTestSim) m_ShaderTestSim->SetViewportSize((float)re.GetWidth(), (float)re.GetHeight());
		}
	}

	void DinoProject::OnImGuiRender()
	{
		ImGui::Begin("Dino Project Controller");

		ImGui::Text("Dino Simulator Suite");
		ImGui::Text("FPS: %.0f (%.3f ms)", 1.0f / m_SmoothedDeltaTime, m_SmoothedDeltaTime * 1000.0f);


	
		if (m_ShaderTestMaterial && ImGui::CollapsingHeader("Shader Sandbox Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Fetch the current color uniform from your shader test material
			glm::vec4 shaderColor = m_ShaderTestMaterial->GetVector("u_Color");

			// Create the ColorEdit4 UI control
			if (ImGui::ColorEdit4("Shader Tint", &shaderColor.x))
			{
				// Update the material uniform when the UI changes
				m_ShaderTestMaterial->Set("u_Color", shaderColor);
				CS_TRACE("DinoProject: Shader Sandbox tint uniform updated -> ({0}, {1}, {2}, {3})",
					shaderColor.r, shaderColor.g, shaderColor.b, shaderColor.a);
			}
		}

		if (m_DinoMaterial && ImGui::CollapsingHeader("Global Dino Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_DinoMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Dino Tint", &color.x))
			{
				m_DinoMaterial->Set("u_Color", color);
				CS_TRACE("DinoProject: Dynamic color tint uniform updated -> ({0}, {1}, {2}, {3})", color.r, color.g, color.b, color.a);
			}
		}

		if (m_FireMaterial && ImGui::CollapsingHeader("Global Fire Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 fireColor = m_FireMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Flame Tint", &fireColor.x))
			{
				m_FireMaterial->Set("u_Color", fireColor);
				CS_TRACE("DinoProject: Flame uniform color tint tracker updated -> ({0}, {1}, {2}, {3})", fireColor.r, fireColor.g, fireColor.b, fireColor.a);
			}
		}


		ImGui::Separator();
		ImGui::Text("Switch Mode:");

		auto previousSim = m_ActiveSim;

		if (ImGui::Button("Runner [R]"))  m_ActiveSim = m_RunSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Flight [F]"))  m_ActiveSim = m_FlightSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Stress [T]"))  m_ActiveSim = m_StressSim.get();
		ImGui::SameLine();
		if (ImGui::Button("Shader Sandbox [G]")) m_ActiveSim = m_ShaderTestSim.get();

		if (m_ActiveSim != previousSim && m_ActiveSim != nullptr)
		{
			CS_INFO("DinoProject: Active context modified via UI. Swapped target layer pointer over to pipeline state: '{0}'",
				m_ActiveSim->GetName());
		}

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