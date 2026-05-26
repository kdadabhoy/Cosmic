#include "MyProject.h"
#include "TemplateRenderLayer.h"
#include "TemplateSimLayer.h"
#include <imgui.h>
#include <filesystem>

namespace Workspace
{
	MyProject::MyProject()
		: Cosmic::Layer("MyProject")
	{
	}

	// -------------------------------------------------------------------------
	void MyProject::OnAttach()
	{
		CS_INFO("MyProject: Attaching root manager layer.");

		// 1. Resolve asset paths through the VFS
		Cosmic::FileSystem::SetActiveProject("MyProject");

		// Redirect logging outputs into the localized project workspace subfolder (still lives in build)
		std::string physicalLogPath = Cosmic::FileSystem::Resolve("project://logs");
		Cosmic::Log::SetLogDirectory(physicalLogPath);

		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/TemplateShader.glsl");
		std::filesystem::path resolvedPath(shaderPath);
		m_ShaderDir = resolvedPath.parent_path().string();

		// 2. Create the shared scene (passed by Ref<> — never duplicated)
		m_Scene = Cosmic::Scene::Create();

		// 3. Build the shared material, fall back gracefully if shader is missing
		if (std::filesystem::exists(shaderPath))
		{
			auto shader = Cosmic::Shader::Create(shaderPath);
			m_SharedMaterial = Cosmic::Material::Create(shader, "TemplateMaterial");
			if (m_SharedMaterial)
			{
				m_SharedMaterial->Set("u_Color", glm::vec4(1.0f, 0.6f, 0.2f, 1.0f));
			}
		}
		else
		{
			CS_WARN("MyProject: Shader not found at '{0}'. Rendering layers will use fallback geometry.", shaderPath);
		}

		// 4. Construct child layers — NOT pushed onto the engine LayerStack
		m_Modes.push_back(std::make_shared<TemplateRenderLayer>(m_SharedMaterial));
		m_Modes.push_back(std::make_shared<TemplateSimLayer>(m_Scene));

		// 5. Attach each child layer so it can load its own GPU resources
		for (auto& mode : m_Modes)
		{
			mode->OnAttach();
		}

		// ---------------------------------------------------------------------
		// 6. Client DLL Fullscreen Hotkey Override
		// ---------------------------------------------------------------------
		// Fetch the exported Engine instance and register a custom bypass lambda
		auto& window = Cosmic::Application::Get().GetWindow();

		window.SetFullscreenHotkeyOverride([](int key, int action, int mods) -> bool
			{
				// Example Custom Override: Listen for Alt + Enter (GLFW tokens 257 & 0x0002)
				// instead of the engine's default F11 key behavior
				if (key == 257 && action == 1 && (mods & 0x0002)) // 257 = GLFW_KEY_ENTER, 1 = GLFW_PRESS, 0x0002 = GLFW_MOD_ALT
				{
					auto& app = Cosmic::Application::Get();
					app.GetWindow().SetFullscreen(!app.GetWindow().IsFullscreen());

					return true; // Match found! Abort the engine's default key actions
				}

				return false; // Not our key combo, let the engine handle it normally
			});

		CS_INFO("MyProject: {} child layers attached. Fullscreen override active.", m_Modes.size());

		
	}

	// -------------------------------------------------------------------------
	void MyProject::OnDetach()
	{
		for (auto& mode : m_Modes)
		{
			mode->OnDetach();
		}
		m_Modes.clear();

		m_SharedMaterial.reset();
		m_Scene.reset();

		CS_INFO("MyProject: Detached and cleaned up.");

		// Optional Safety Measure: Reset logging back to engine defaults when leaving workspace
		Cosmic::Log::SetLogDirectory("logs");
	}

	// -------------------------------------------------------------------------
	void MyProject::OnUpdate(float ts)
	{
		if (m_Modes.empty()) return;

		auto& activeMode = m_Modes[m_ActiveModeIndex];

		// Drive the active mode's local clock — this is what GetLocalTime() returns.
		// The incoming 'ts' is already scaled by the engine (global TimeScale applied).
		activeMode->UpdateLayerTime(ts);

		// Feed the shared material's time from the active mode's local clock.
		// This means pause/rewind affects the shader automatically.
		if (m_SharedMaterial)
		{
			m_SharedMaterial->Set("u_Time", activeMode->GetLocalTime());
		}

		activeMode->OnUpdate(ts);
	}

	// -------------------------------------------------------------------------
	void MyProject::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_Modes.empty()) return;

		// No manual scaling — the engine already applied global TimeScale before
		// this arrives. The active mode must guard against dt <= 0 itself.
		m_Modes[m_ActiveModeIndex]->OnFixedUpdate(deltaFixedTime);
	}

	// -------------------------------------------------------------------------
	void MyProject::OnImGuiRender()
	{
		if (m_Modes.empty()) return;

		// Root manager panel — mode selector + global controls
		ImGui::Begin("MyProject");

		ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "Template Project — Root Manager");
		ImGui::Separator();
		ImGui::Spacing();

		// Mode selector combo
		if (ImGui::BeginCombo("Active Layer", m_Modes[m_ActiveModeIndex]->GetName().c_str()))
		{
			for (int i = 0; i < static_cast<int>(m_Modes.size()); ++i)
			{
				bool selected = (m_ActiveModeIndex == i);
				if (ImGui::Selectable(m_Modes[i]->GetName().c_str(), selected))
					m_ActiveModeIndex = i;
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Global time scale — drives the engine singleton directly
		float hostScale = Cosmic::Application::Get().GetTimeScale();
		if (ImGui::SliderFloat("Global TimeScale", &hostScale, -2.0f, 3.0f, "%.2fx"))
			Cosmic::Application::Get().SetTimeScale(hostScale);

		if (hostScale == 0.0f)
			ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "  PAUSED");
		else if (hostScale < 0.0f)
			ImGui::TextColored({ 1.0f, 0.7f, 0.0f, 1.0f }, "  REWINDING");
		else if (hostScale > 1.0f)
			ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "  FAST FORWARD");
		else
			ImGui::TextColored({ 0.6f, 0.6f, 0.6f, 1.0f }, "  Normal playback");

		ImGui::Spacing();

		// Per-layer local time scale (independent of global)
		auto& active = m_Modes[m_ActiveModeIndex];
		float localScale = active->GetTimeScale();
		if (ImGui::SliderFloat("Layer TimeScale", &localScale, 0.0f, 3.0f, "%.2fx"))
			active->SetTimeScale(localScale);

		ImGui::Spacing();
		ImGui::Separator();

		// Shared material color override
		if (m_SharedMaterial)
		{
			if (ImGui::CollapsingHeader("Shared Material"))
			{
				glm::vec4 col = m_SharedMaterial->GetVector("u_Color");
				if (ImGui::ColorEdit4("Tint##shared", &col.x))
					m_SharedMaterial->Set("u_Color", col);
			}
		}

		// Frame stats
		ImGui::Spacing();
		ImGui::Separator();
		float fps = ImGui::GetIO().Framerate;
		ImVec4 fpsCol = fps >= 60.f ? ImVec4(0.2f, 1.f, 0.2f, 1.f) : fps >= 30.f ? ImVec4(1.f, 0.8f, 0.2f, 1.f) : ImVec4(1.f, 0.3f, 0.3f, 1.f);
		ImGui::TextColored(fpsCol, "FPS: %.1f  (%.2f ms)", fps, 1000.f / fps);
		ImGui::Text("Active Layer Time: %.2fs", active->GetLocalTime());

		ImGui::End();

		// Let the active mode render its own inspector panel
		m_Modes[m_ActiveModeIndex]->OnImGuiRender();
	}

	// -------------------------------------------------------------------------
	void MyProject::OnEvent(Cosmic::Event& e)
	{
		if (m_Modes.empty()) return;

		// Application events (resize, etc.) must broadcast to ALL modes so that
		// inactive cameras don't accumulate stale projection matrices.
		if (e.IsInCategory(Cosmic::EventCategoryApplication))
		{
			for (auto& mode : m_Modes)
				mode->OnEvent(e);
			return;
		}

		// Input events go only to the active mode
		if (e.Handled) return;
		m_Modes[m_ActiveModeIndex]->OnEvent(e);
	}
}

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
// =============================================================================
extern "C"
{
	__declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
	{
		ImGui::SetCurrentContext(context.ImGuiCtx);
		ImPlot::SetCurrentContext(context.ImPlotCtx);
	}

	__declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
	{
		return new Workspace::MyProject();
	}
}