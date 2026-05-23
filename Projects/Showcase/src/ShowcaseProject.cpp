#include "ShowcaseProject.h"
#include "ShowcaseFlightLayer.h"
#include "ShowcaseRunLayer.h"
#include "ShowcaseShaderLayer.h"
#include "ShowcaseStressLayer.h"
#include <imgui.h>
#include <implot.h>
#include <filesystem>
#include <Cosmic.h>

namespace Showcase
{
	ShowcaseProject::ShowcaseProject()
		: Cosmic::Layer("ShowcaseProject")
	{
		// 1. Establish Virtual File System context for the Showcase workspace
		Cosmic::FileSystem::SetActiveProject("Showcase");

		CS_INFO("ShowcaseProject: Initializing simulation layers via Virtual File System...");

		std::string virtualFirePath = "project://shaders/FireShader.glsl";
		std::string fireShaderPath = Cosmic::FileSystem::Resolve(virtualFirePath);

		std::filesystem::path resolvedPath(fireShaderPath);
		m_ShaderDir = resolvedPath.parent_path().string();

		auto VerifyAsset = [](const std::string& path, const std::string& name) -> bool
			{
				if (!std::filesystem::exists(path))
				{
					CS_ERROR("ShowcaseProject: FAILED to find {0} at resolved path: '{1}'", name, path);
					return false;
				}
				uintmax_t fileSize = std::filesystem::file_size(path);
				CS_INFO("ShowcaseProject: Verified asset '{0}' ({1} bytes)", name, fileSize);
				return true;
			};

		bool assetsValid = VerifyAsset(fireShaderPath, "Fire Shader");

		if (!assetsValid)
		{
			CS_ERROR("ShowcaseProject: Critical error during asset verification. Aborting structural simulation allocation!");
			return;
		}

		m_Scene = Cosmic::Scene::Create();
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);
		m_DinoMaterial = Cosmic::Material::Create(fireShader, "DinoMaterial");

		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
			CS_INFO("ShowcaseProject: DinoMaterial bound to registry address: 0x{0:x}", (uintptr_t)m_DinoMaterial.get());
		}

		// =========================================================================
		// INITIALIZE MODES DIRECTLY IN CONSTRUCTOR
		// =========================================================================
		if (m_DinoMaterial)
		{
			m_Modes.push_back(std::make_shared<ShowcaseFlightLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseRunLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseShaderLayer>(m_ShaderDir));

			auto fallBackShader = Cosmic::Shader::Create(m_ShaderDir + "/FireShader.glsl");
			auto alternativeMaterial = Cosmic::Material::Create(fallBackShader, "AlternativeMaterial");
			if (alternativeMaterial) alternativeMaterial->Set("u_Color", glm::vec4(0.2f, 0.7f, 1.0f, 1.0f));

			m_Modes.push_back(std::make_shared<ShowcaseStressLayer>(m_Scene, m_DinoMaterial, alternativeMaterial));

			for (auto& mode : m_Modes)
			{
				mode->SetViewportSize(1280.0f, 720.0f);
			}
		}
	}

	void ShowcaseProject::OnAttach()
	{
		// Safe to leave empty, or add runtime-specific hooks here later
	}

	void ShowcaseProject::OnDetach()
	{
		CS_WARN("ShowcaseProject: Detach lifecycle triggered. Releasing resource handlers...");
		m_Modes.clear();
		m_DinoMaterial.reset();
		m_Scene.reset();
		CS_INFO("ShowcaseProject: All simulation contexts cleanly destroyed.");
	}

	void ShowcaseProject::OnUpdate(float ts)
	{
		if (m_Modes.empty()) return;

		// Track running application timestamps for uniform modifications
		float dt = ts > 0.0f ? ts : 0.001f;
		static float s_AccumulatedTime = 0.0f;
		s_AccumulatedTime += dt;

		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Time", s_AccumulatedTime);
		}

		// Run updates on our simulations
		m_Modes[m_ActiveModeIndex]->OnUpdate(ts);
		m_Modes[m_ActiveModeIndex]->OnFixedUpdate(ts);
		m_Modes[m_ActiveModeIndex]->OnRender();
	}

	void ShowcaseProject::OnImGuiRender()
	{
		if (m_Modes.empty())
		{
			ImGui::Begin("Cosmic Module: Showcase Manager");
			ImGui::Text("Waiting for simulation modes to initialize or verify assets...");
			ImGui::End();
			return;
		}

		ImGui::Begin("Cosmic Module: Showcase Manager");

		if (ImGui::BeginCombo("Active Simulation", m_Modes[m_ActiveModeIndex]->GetName().c_str()))
		{
			for (size_t i = 0; i < m_Modes.size(); ++i)
			{
				bool isSelected = (m_ActiveModeIndex == i);
				if (ImGui::Selectable(m_Modes[i]->GetName().c_str(), isSelected))
				{
					m_ActiveModeIndex = static_cast<int>(i);
				}
			}
			ImGui::EndCombo();
		}

		if (m_DinoMaterial && ImGui::CollapsingHeader("Global Fire Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_DinoMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Flame Tint", &color.x))
			{
				m_DinoMaterial->Set("u_Color", color);
			}
		}

		ImGui::Separator();
		ImGui::Spacing();

		m_Modes[m_ActiveModeIndex]->OnImGuiRender();

		ImGui::End();
	}

	void ShowcaseProject::OnEvent(Cosmic::Event& e)
	{
		if (m_Modes.empty()) return;

		m_Modes[m_ActiveModeIndex]->OnEvent(e);

		if (e.GetEventType() == Cosmic::EventType::WindowResize)
		{
			auto& resizeEvent = static_cast<Cosmic::WindowResizeEvent&>(e);
			float w = static_cast<float>(resizeEvent.GetWidth());
			float h = static_cast<float>(resizeEvent.GetHeight());

			for (auto& mode : m_Modes)
			{
				mode->SetViewportSize(w, h);
			}
		}
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
		return new Showcase::ShowcaseProject();
	}
}