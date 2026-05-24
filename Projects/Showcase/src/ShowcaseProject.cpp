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
		// Default to the first registered simulation sub-layer mode
		m_ActiveModeIndex = 0;
	}

	// =========================================================================
	// Lifecycle Management
	// =========================================================================

	void ShowcaseProject::OnAttach()
	{
		CS_INFO("ShowcaseProject: Composite Root attached. Initializing asset environment...");

		// 1. Establish Virtual File System context for the Showcase workspace.
		//    This mounts the core project directory prefix maps.
		Cosmic::FileSystem::SetActiveProject("Showcase");

		std::string virtualFirePath = "project://shaders/FireShader.glsl";
		std::string fireShaderPath = Cosmic::FileSystem::Resolve(virtualFirePath);

		std::filesystem::path resolvedPath(fireShaderPath);
		m_ShaderDir = resolvedPath.parent_path().string();

		// Gracefully abort initialization if asset generation prerequisites fail validation
		if (!std::filesystem::exists(fireShaderPath))
		{
			CS_ERROR("ShowcaseProject: Critical error. VFS failed to resolve asset path: {}", fireShaderPath);
			return;
		}

		// 2. Create foundational graphic runtime resources.
		m_Scene = Cosmic::Scene::Create();
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);
		m_DinoMaterial = Cosmic::Material::Create(fireShader, "DinoMaterial");

		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Color", glm::vec4(1.0f));
			CS_INFO("ShowcaseProject: DinoMaterial bound to registry address: 0x{0:x}", (uintptr_t)m_DinoMaterial.get());
		}

		// 3. Allocate specialized child simulations directly as managed sub-layers.
		if (m_DinoMaterial)
		{
			// Populate the sub-layer execution vector with concrete simulation instances
			m_Modes.push_back(std::make_shared<ShowcaseFlightLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseRunLayer>(m_Scene, m_DinoMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseShaderLayer>(m_ShaderDir));

			// Build alternative material resources using the dynamically discovered VFS root path
			auto alternativeShader = Cosmic::Shader::Create(m_ShaderDir + "/FireShader.glsl");
			auto alternativeMaterial = Cosmic::Material::Create(alternativeShader, "AlternativeMaterial");

			if (alternativeMaterial)
			{
				alternativeMaterial->Set("u_Color", glm::vec4(0.2f, 0.7f, 1.0f, 1.0f));
			}

			m_Modes.push_back(std::make_shared<ShowcaseStressLayer>(m_Scene, m_DinoMaterial, alternativeMaterial));

			// Standard internal lifecycle cascade: Boot and mount all registered sub-layers
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

		// Safely spin down sub-layers before flushing ownership wrappers
		for (auto& mode : m_Modes)
		{
			mode->OnDetach();
		}

		m_Modes.clear();
		m_DinoMaterial.reset();
		m_Scene.reset();

		CS_INFO("ShowcaseProject: Clean workspace shutdown finalized.");
	}

	// =========================================================================
	// System Core Ticks
	// =========================================================================

	void ShowcaseProject::OnUpdate(float ts)
	{
		if (m_Modes.empty()) return;

		// Protect shader clock math against zero-delta pausing hitches
		float dt = ts > 0.0f ? ts : 0.001f;
		static float s_AccumulatedTime = 0.0f;
		s_AccumulatedTime += dt;

		// Inject elapsed delta coordinates into materials for global dynamic effects
		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Time", s_AccumulatedTime);
		}

		// Isolate system update ticks exclusively to the chosen active sub-layer
		m_Modes[m_ActiveModeIndex]->OnUpdate(ts);
	}

	void ShowcaseProject::OnImGuiRender()
	{
		// Diagnostic Fallback: Render error notice overlay if resources failed to mount
		if (m_Modes.empty())
		{
			ImGui::Begin("Cosmic Workspace Manager");
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "CRITICAL: Workspace failed to mount context or verify assets.");
			ImGui::Text("Check your engine console log output window for disk search details.");
			ImGui::End();
			return;
		}

		// 1. Render Root Project Management Hub Controls
		ImGui::Begin("Cosmic Workspace Manager");

		// Display dropdown selector linked directly to native layer string markers
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

		// Render global material uniform parameters
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

		// 2. Pass UI rendering pipelines onwards down to the focused sub-layer
		m_Modes[m_ActiveModeIndex]->OnImGuiRender();
	}

	void ShowcaseProject::OnEvent(Cosmic::Event& e)
	{
		// Interrupt and halt propagation if layers are unassigned or event is marked handled
		if (m_Modes.empty() || e.Handled) return;

		// Propagate system events exclusively down into the active subsystem scope
		m_Modes[m_ActiveModeIndex]->OnEvent(e);
	}
}

// =============================================================================
// C-Linkage Dynamic Library Entry Points (Required for Hot-Reloading Host)
// =============================================================================
extern "C"
{
	// Synchronize rendering contexts across dll context boundaries
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	// Factory hook called by engine module loader to mount the plugin entry layer
	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Showcase::ShowcaseProject();
	}
}