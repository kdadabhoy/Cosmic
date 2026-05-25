#include "ShowcaseProject.h"
#include "ShowcaseFlightLayer.h"
#include "ShowcaseRunLayer.h"
#include "ShowcaseShaderLayer.h"
#include "ShowcaseStressLayer.h"
#include "ShowcaseDinoLayer.h"
#include "CircleDebugLayer.h" 

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

		Cosmic::FileSystem::SetActiveProject("Showcase");

		std::string virtualFirePath = "project://shaders/FireShader.glsl";
		std::string fireShaderPath = Cosmic::FileSystem::Resolve(virtualFirePath);

		std::filesystem::path resolvedPath(fireShaderPath);
		m_ShaderDir = resolvedPath.parent_path().string();

		if (!std::filesystem::exists(fireShaderPath))
		{
			CS_ERROR("ShowcaseProject: Critical error. VFS failed to resolve asset path: {}", fireShaderPath);
			return;
		}

		m_Scene = Cosmic::Scene::Create();
		auto fireShader = Cosmic::Shader::Create(fireShaderPath);
		m_FlameMaterial = Cosmic::Material::Create(fireShader, "DinoMaterial");

		if (m_FlameMaterial)
		{
			m_FlameMaterial->Set("u_Color", glm::vec4(1.0f));
		}

		if (m_FlameMaterial)
		{
			m_Modes.push_back(std::make_shared<CircleDebugLayer>());
			m_Modes.push_back(std::make_shared<ShowcaseFlightLayer>(m_Scene, m_FlameMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseRunLayer>(m_Scene, m_FlameMaterial));
			m_Modes.push_back(std::make_shared<ShowcaseShaderLayer>(m_ShaderDir));
			m_Modes.push_back(std::make_shared<ShowcaseDinoLayer>(m_Scene));


			auto alternativeShader = Cosmic::Shader::Create(m_ShaderDir + "/DebugTexture.glsl");
			auto alternativeMaterial = Cosmic::Material::Create(alternativeShader, "AlternativeMaterial");

			if (alternativeMaterial)
			{
				alternativeMaterial->Set("u_Color", glm::vec4(0.2f, 0.7f, 1.0f, 1.0f));
			}

			m_Modes.push_back(std::make_shared<ShowcaseStressLayer>(m_Scene, m_FlameMaterial, alternativeMaterial));

			for (auto& mode : m_Modes)
			{
				mode->OnAttach();
			}
		}
	}

	void ShowcaseProject::OnDetach()
	{
		for (auto& mode : m_Modes)
		{
			mode->OnDetach();
		}
		m_Modes.clear();
		m_FlameMaterial.reset();
		m_Scene.reset();
	}

	void ShowcaseProject::OnUpdate(float ts)
	{
		if (m_Modes.empty()) return;

		// CLEAN ENGINE ARCHITECTURE: 
		// We pass the incoming timestep down directly. The engine loop and WorkspaceLayer
		// have already scaled 'ts' perfectly before it hits us.
		auto& activeMode = m_Modes[m_ActiveModeIndex];
		activeMode->UpdateLayerTime(ts);

		// CLEAN ARCHITECTURE FIX: 
		// Query the localized synchronised timeline from the active mode layer 
		// to feed materials instead of fetching engine global absolute time.
		if (m_FlameMaterial)
		{
			m_FlameMaterial->Set("u_Time", activeMode->GetLocalTime());
		}

		activeMode->OnUpdate(ts);
	}

	void ShowcaseProject::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_Modes.empty()) return;

		// No manual scaling math hacks here anymore! 
		m_Modes[m_ActiveModeIndex]->OnFixedUpdate(deltaFixedTime);
	}

	void ShowcaseProject::OnImGuiRender()
	{
		if (m_Modes.empty()) return;

		ImGui::Begin("Cosmic Workspace Manager");

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

		// FIXED DYNAMIC DLL STATE LINKAGE:
		// We query the core engine application singleton directly rather than relying on 
		// local DLL memory space. This immediately couples the slider mechanics to the main loops.
		ImGui::Spacing();
		float hostTimeScale = Cosmic::Application::Get().GetTimeScale();

		// Range extended to negative floats (-2.0f) to allow full rewinding support
		if (ImGui::SliderFloat("Simulation TimeScale", &hostTimeScale, -2.0f, 3.0f, "%.2fx"))
		{
			Cosmic::Application::Get().SetTimeScale(hostTimeScale);
		}

		// State Notification Overlays for Playback Manipulation Status
		if (hostTimeScale == 0.0f)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "⏸ SIMULATION SYSTEM PAUSED");
		}
		else if (hostTimeScale < 0.0f)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.64f, 0.0f, 1.0f), "⏪ REWINDING TIMELINE CONTEXT");
		}
		else if (hostTimeScale > 1.0f)
		{
			ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "⏩ FAST-FORWARDING ACTIVE SIMULATION");
		}
		else
		{
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "▶ Standard Hardware Synchronized Playback");
		}

		ImGui::Separator();

		if (m_FlameMaterial && ImGui::CollapsingHeader("Global Fire Material Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			glm::vec4 color = m_FlameMaterial->GetVector("u_Color");
			if (ImGui::ColorEdit4("Flame Tint", &color.x))
			{
				m_FlameMaterial->Set("u_Color", color);
			}
		}

		ImGui::Separator();
		ImGui::End();

		m_Modes[m_ActiveModeIndex]->OnImGuiRender();
	}

	void ShowcaseProject::OnEvent(Cosmic::Event& e)
	{
		if (m_Modes.empty()) return;

		// 1. CRITICAL INFRASTRUCTURE EVENT BROADCAST
		// Window size and viewport updates must be sent to ALL layers uniformly. 
		// If an inactive layer misses a resize event, its camera controller projection matrix
		// will become stretched and corrupted when the user switches to it later.
		if (e.IsInCategory(Cosmic::EventCategoryApplication))
		{
			for (auto& mode : m_Modes)
			{
				if (mode)
				{
					mode->OnEvent(e);
				}
			}
			return; // Layout synchronization complete
		}

		// 2. INPUT ISOLATION FILTERING
		// If the event has been intercepted/consumed by ImGui blocking layers downstream, or 
		// if it's an isolated input (Key/Mouse), dispatch it exclusively to the active simulation panel.
		if (e.Handled) return;

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