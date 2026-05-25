#include "ShowcaseShaderLayer.h"
#include <imgui.h>
#include <algorithm>
#include <filesystem>

namespace Showcase
{
	ShowcaseShaderLayer::ShowcaseShaderLayer(const std::string& shaderDirectory)
		: Cosmic::Layer("ShowcaseShaderLayer")
		, m_ShaderDirectory(shaderDirectory)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	void ShowcaseShaderLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(1.0f);
		m_Camera.SetZoomLimits(0.1f, 10.0f);

		ScanShaderDirectory();

		if (!m_ShaderPaths.empty())
		{
			LoadShader(m_ShaderPaths[0]);
		}
	}

	void ShowcaseShaderLayer::OnDetach()
	{
		m_Material.reset();
		m_ShaderPaths.clear();
		m_ShaderNames.clear();
	}

	void ShowcaseShaderLayer::ScanShaderDirectory()
	{
		m_ShaderPaths.clear();
		m_ShaderNames.clear();

		namespace fs = std::filesystem;
		if (!fs::exists(m_ShaderDirectory))
		{
			CS_WARN("ShowcaseShaderLayer: Target workspace shader pool missing at: '{0}'", m_ShaderDirectory);
			return;
		}

		for (const auto& entry : fs::directory_iterator(m_ShaderDirectory))
		{
			if (!entry.is_regular_file()) continue;
			auto ext = entry.path().extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			if (ext != ".glsl") continue;

			m_ShaderPaths.push_back(entry.path().string());
			m_ShaderNames.push_back(entry.path().filename().string());
		}

		std::sort(m_ShaderPaths.begin(), m_ShaderPaths.end());
		std::sort(m_ShaderNames.begin(), m_ShaderNames.end());

		CS_INFO("ShowcaseShaderLayer: VFS indexed {0} standalone workspace shaders.", m_ShaderPaths.size());
	}

	void ShowcaseShaderLayer::LoadShader(const std::string& filepath)
	{
		m_LoadError = false;
		m_ErrorMsg.clear();

		auto it = std::find(m_ShaderPaths.begin(), m_ShaderPaths.end(), filepath);
		m_SelectedIndex = (it != m_ShaderPaths.end()) ? static_cast<int>(std::distance(m_ShaderPaths.begin(), it)) : -1;

		auto shader = Cosmic::Shader::Create(filepath);
		if (!shader)
		{
			m_LoadError = true;
			m_ErrorMsg = "Shader::Create context allocation failure for: " + filepath;
			CS_ERROR("ShowcaseShaderLayer: {0}", m_ErrorMsg);
			m_Material.reset();
			return;
		}

		m_Material = Cosmic::Material::Create(shader, "ShaderBrowserMaterial");
		if (m_Material)
		{
			m_Material->Set("u_Color", glm::vec4(1.0f));

			// ARCHITECTURE FIX: Called instance method to fetch this layer's individual timeline clock
			m_Material->Set("u_Time", GetLocalTime());
			CS_INFO("ShowcaseShaderLayer: Pipeline bound to compilation node '{0}'.", std::filesystem::path(filepath).filename().string());
		}
	}

	// =========================================================================
	// Deterministic Simulation / Timeline Management
	// =========================================================================
	void ShowcaseShaderLayer::OnFixedUpdate(float deltaFixedTime)
	{
		// CLEAN ENGINE ARCHITECTURE:
		// Manual clock tracking accumulation is removed. 
		// Time updates are stream-fed via WorkspaceLayer directly into base class timelines.
	}

	// =========================================================================
	// Frame Graphics Render Pass
	// =========================================================================
	// =========================================================================
	// Frame Graphics Render Pass
	// =========================================================================
	void ShowcaseShaderLayer::OnUpdate(float ts)
	{
		// --- ARCHITECTURAL FIX: Dynamic Canvas Layout Synchronization ---
		// Fetch active resolution properties directly from the host application's 
		// primary framebuffers. This guarantees that screen-to-world bounds and aspect 
		// ratios never become uninitialized or stretched during initial load frames.
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float activeWidth = static_cast<float>(fb->GetWidth());
		float activeHeight = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != activeWidth || m_ViewportSize.y != activeHeight)
		{
			m_ViewportSize = { activeWidth, activeHeight };
			m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		}
		// -----------------------------------------------------------------

		// Step smooth camera interpolation parameters forward
		m_Camera.OnUpdate(ts);

		// ARCHITECTURE FIX: Called instance method to fetch this layer's individual timeline clock
		if (m_Material)
		{
			m_Material->Set("u_Time", GetLocalTime());
		}

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		if (m_Material && !m_LoadError)
		{
			// FEATURE IMPLEMENTATION: Handle Fill Screen Toggle & Locked Aspect Constraints
			if (m_FillScreen)
			{
				// Compute exactly what is needed to cover the orthographic viewing projection space completely
				float aspect = m_ViewportSize.x / m_ViewportSize.y;
				float zoom = m_Camera.GetZoomLevel();
				Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f * aspect * zoom, 2.0f * zoom }, m_Material);
			}
			else if (m_LockAspect)
			{
				// Constrain the render canvas strictly to the user's targeted custom ratio (e.g., 16:9 box)
				Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { m_TargetAspect * 2.0f, 2.0f }, m_Material);
			}
			else
			{
				// Default behavior: Scale matching the exact layout viewport aspect ratio
				float aspect = m_ViewportSize.x / m_ViewportSize.y;
				Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 2.0f * aspect, 2.0f }, m_Material);
			}
		}
		else
		{
			Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, 0.0f }, { 4.0f, 4.0f }, { 0.08f, 0.08f, 0.1f, 1.0f });
		}

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseShaderLayer::OnImGuiRender()
	{
		ImGui::Begin("Simulation Inspection Window");
		ImGui::Text("--- Shader Browser Node Matrix ---");
		ImGui::Separator();

		ImGui::TextWrapped("Scanning System Target: %s", m_ShaderDirectory.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Rescan Pool (F5)")) ScanShaderDirectory();

		ImGui::Spacing();

		// --- ASPECT RATIO & FULLSCREEN MANAGEMENT PANEL ---
		if (ImGui::CollapsingHeader("Display Topology Configuration", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Fill Screen Projection Space (Ignore Bounds)", &m_FillScreen);

			if (m_FillScreen) ImGui::BeginDisabled();
			ImGui::Checkbox("Lock Custom Aspect Ratio", &m_LockAspect);
			if (m_LockAspect && !m_FillScreen)
			{
				ImGui::SliderFloat("Aspect Value", &m_TargetAspect, 0.5f, 3.0f, "Ratio: %.2f");
				if (ImGui::Button("Force 16:9")) m_TargetAspect = 16.0f / 9.0f;
				ImGui::SameLine();
				if (ImGui::Button("Force 4:3"))  m_TargetAspect = 4.0f / 3.0f;
				ImGui::SameLine();
				if (ImGui::Button("Force 1:1"))  m_TargetAspect = 1.0f;
			}
			if (m_FillScreen) ImGui::EndDisabled();
		}
		ImGui::Separator();
		ImGui::Spacing();

		if (m_ShaderPaths.empty())
		{
			ImGui::TextColored({ 1.0f, 0.5f, 0.2f, 1.0f }, "No valid .glsl context definitions found in targeted project tree.");
			ImGui::End();
			return;
		}

		ImGui::Text("Available Pipeline Modules:");
		ImGui::BeginChild("ShaderList", { 0.0f, 150.0f }, true); // Shrunk height slightly to fit new aspect panels
		for (int i = 0; i < static_cast<int>(m_ShaderNames.size()); ++i)
		{
			bool selected = (i == m_SelectedIndex);
			if (selected) ImGui::PushStyleColor(ImGuiCol_Text, { 0.4f, 1.0f, 0.4f, 1.0f });

			if (ImGui::Selectable(m_ShaderNames[i].c_str(), selected))
			{
				LoadShader(m_ShaderPaths[i]);
			}

			if (selected) ImGui::PopStyleColor();
		}
		ImGui::EndChild();

		ImGui::Spacing();

		bool canReload = (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_ShaderPaths.size()));
		if (!canReload) ImGui::BeginDisabled();
		if (ImGui::Button("Hot-Reload Compilation (R)")) LoadShader(m_ShaderPaths[m_SelectedIndex]);
		if (!canReload) ImGui::EndDisabled();

		if (m_LoadError)
		{
			ImGui::Spacing();
			ImGui::TextColored({ 1.0f, 0.2f, 0.2f, 1.0f }, "GPU Compilation Backtrace Error:");
			ImGui::TextWrapped("%s", m_ErrorMsg.c_str());
		}
		else if (m_SelectedIndex >= 0)
		{
			ImGui::Spacing();
			ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "Active Buffer: %s", m_ShaderNames[m_SelectedIndex].c_str());

			// ARCHITECTURE FIX: Called instance method to fetch this layer's individual timeline clock
			ImGui::Text("Shader Clock Phase: %.2fs", GetLocalTime());

			if (m_Material)
			{
				glm::vec4 tint = m_Material->GetVector("u_Color");
				if (ImGui::ColorEdit4("Uniform Color Overlay", &tint.x))
				{
					m_Material->Set("u_Color", tint);
				}
			}
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Hotkeys: R = reload buffer  |  F5 = rescan project paths  |  Scroll Wheel = camera zoom");
		ImGui::End();
	}

	void ShowcaseShaderLayer::OnEvent(Cosmic::Event& e)
	{
		// Pass down updates uniformly into the camera matrix pipeline
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);

		// MODERN C++ LAMBDA REFACTOR: Eliminates standard template library macro plumbing
		dispatcher.Dispatch<Cosmic::KeyPressedEvent>([this](Cosmic::KeyPressedEvent& event) { return OnKeyPressed(event); });
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>([this](Cosmic::WindowResizeEvent& event) { return OnWindowResize(event); });
	}

	bool ShowcaseShaderLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
	{
		if (e.GetRepeatCount() > 0) return false;

		if (e.GetKeyCode() == CS_KEY_R)
		{
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_ShaderPaths.size()))
			{
				CS_INFO("ShowcaseShaderLayer: Hot-reloading shader via hotkey tracking...");
				LoadShader(m_ShaderPaths[m_SelectedIndex]);
				return true;
			}
		}

		if (e.GetKeyCode() == CS_KEY_F5)
		{
			ScanShaderDirectory();
			return true;
		}

		return false;
	}

	bool ShowcaseShaderLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}