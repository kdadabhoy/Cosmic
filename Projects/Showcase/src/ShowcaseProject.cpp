#include "ShowcaseProject.h"
#include "ShowcaseFlightLayer.h"
#include "ShowcaseRunLayer.h"
#include "ShowcaseShaderLayer.h"
#include "ShowcaseStressLayer.h"
#include <imgui.h>
#include <implot.h>
#include <filesystem>

namespace Showcase
{
	ShowcaseProject::ShowcaseProject()
		: Cosmic::Layer("ShowcaseProject")
	{
		m_ActiveModeIndex = 0;
	}

	void ShowcaseProject::OnAttach()
	{
		CS_INFO("ShowcaseProject: Composite Root attached. Initializing asset environment...");

		// 1. Establish Virtual File System context for the Showcase workspace
		Cosmic::FileSystem::SetActiveProject("Showcase");

		std::string virtualFirePath = "project://shaders/FireShader.glsl";
		std::string fireShaderPath = Cosmic::FileSystem::Resolve(virtualFirePath);

		// FALLBACK DIAGNOSTIC: If the VFS path resolution fails due to Working Directory offsets,
		// search for the local application development runtime assets directly.
		if (!std::filesystem::exists(fireShaderPath))
		{
			CS_WARN("ShowcaseProject: VFS path failed to resolve target [{}]. Attempting relative local sandbox fallback...", fireShaderPath);

			std::vector<std::string> fallbackPaths = {
				"assets/projects/Showcase/shaders/FireShader.glsl",
				"assets/shaders/FireShader.glsl",
				"../assets/shaders/FireShader.glsl"
			};

			for (const auto& path : fallbackPaths)
			{
				if (std::filesystem::exists(path))
				{
					fireShaderPath = path;
					CS_INFO("ShowcaseProject: Successfully localized fallback shader asset path at: {}", fireShaderPath);
					break;
				}
			}
		}

		std::filesystem::path resolvedPath(fireShaderPath);
		m_ShaderDir = resolvedPath.parent_path().string();

		if (!std::filesystem::exists(fireShaderPath))
		{
			CS_ERROR("ShowcaseProject: Critical error during asset verification. Aborting workspace allocation!");
			CS_ERROR("ShowcaseProject: Looked everywhere. Ensure assets folder exists at your runtime working directory: {}",
				std::filesystem::current_path().string());
			return; // Leaves m_Modes empty, triggering diagnostic window view safely
		}

		// 2. Create foundational graphic runtime resources
		m_Scene = Cosmic::Scene::Create();
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);
		m_DinoMaterial = Cosmic::Material::Create(fireShader, "DinoMaterial");

		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
			CS_INFO("ShowcaseProject: DinoMaterial bound to registry address: 0x{0:x}", (uintptr_t)m_DinoMaterial.get());
		}

		// 3. Allocate specialized child simulations directly as engine layers
		if (m_DinoMaterial)
		{
			// Add modes safely to vector
			m_Modes.push_back(std::make_shared<ShowcaseFlightLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseRunLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseShaderLayer>(m_ShaderDir));

			auto fallBackShader = Cosmic::Shader::Create(m_ShaderDir + "/FireShader.glsl");
			auto alternativeMaterial = Cosmic::Material::Create(fallBackShader, "AlternativeMaterial");
			if (alternativeMaterial)
				alternativeMaterial->Set("u_Color", glm::vec4(0.2f, 0.7f, 1.0f, 1.0f));

			m_Modes.push_back(std::make_shared<ShowcaseStressLayer>(m_Scene, m_DinoMaterial, alternativeMaterial));

			// Standard lifecycle cascade: Let the engine layers configure themselves
			for (auto& mode : m_Modes)
			{
				mode->OnAttach();
			}

			CS_INFO("ShowcaseProject: Successfully instantiated and attached {0} simulation layers.", m_Modes.size());
		}
	}

	void ShowcaseProject::OnDetach()
	{
		CS_WARN("ShowcaseProject: Detach lifecycle triggered. Cascading destruction down the layer array...");

		for (auto& mode : m_Modes)
		{
			mode->OnDetach();
		}

		m_Modes.clear();
		m_DinoMaterial.reset();
		m_Scene.reset();
		CS_INFO("ShowcaseProject: Clean workspace shutdown finalized.");
	}

	void ShowcaseProject::OnUpdate(float ts)
	{
		if (m_Modes.empty()) return;

		float dt = ts > 0.0f ? ts : 0.001f;
		static float s_AccumulatedTime = 0.0f;
		s_AccumulatedTime += dt;

		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Time", s_AccumulatedTime);
		}

		// Isolate update processing to the explicitly selected sub-layer
		m_Modes[m_ActiveModeIndex]->OnUpdate(ts);
	}

	void ShowcaseProject::OnImGuiRender()
	{
		if (m_Modes.empty())
		{
			ImGui::Begin("Cosmic Workspace Manager");
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "CRITICAL: Workspace failed to mount context or verify assets.");
			ImGui::Text("Check your engine console log output window for disk search details.");
			ImGui::End();
			return;
		}

		ImGui::Begin("Cosmic Workspace Manager");

		// Fetch standard layer debug name natively via .GetName()
		if (ImGui::BeginCombo("Active Layer Module", m_Modes[m_ActiveModeIndex]->GetName().c_str()))
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

		if (m_DinoMaterial && ImGui::CollapsingHeader("Global Fire Material Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_DinoMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Flame Tint", &color.x))
			{
				m_DinoMaterial->Set("u_Color", color);
			}
		}

		ImGui::Separator();
		ImGui::Spacing();
		ImGui::End();

		// Forward ImGui context pipeline commands into the target layer
		m_Modes[m_ActiveModeIndex]->OnImGuiRender();
	}

	void ShowcaseProject::OnEvent(Cosmic::Event& e)
	{
		if (m_Modes.empty() || e.Handled) return;

		// Propagate active inputs exclusively into the chosen system layer
		m_Modes[m_ActiveModeIndex]->OnEvent(e);
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