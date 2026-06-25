#include "TemplateRenderLayer.h"
#include <imgui.h>
#include <cmath>

namespace Workspace
{
	TemplateRenderLayer::TemplateRenderLayer(Cosmic::Ref<Cosmic::Material> sharedMaterial)
		: Cosmic::Layer("Render Showcase")
		, m_Camera(1280.0f / 720.0f, false)
		, m_Material(sharedMaterial)
	{
	}

	// -------------------------------------------------------------------------
	void TemplateRenderLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(2.5f);
		m_Camera.SetZoomLimits(0.5f, 15.0f);
		m_Camera.SetManualMovementEnabled(true);

		CS_INFO("TemplateRenderLayer: Attached.");
	}

	// -------------------------------------------------------------------------
	void TemplateRenderLayer::OnDetach()
	{
		CS_INFO("TemplateRenderLayer: Detached.");
	}

	// -------------------------------------------------------------------------
	void TemplateRenderLayer::OnUpdate(float ts)
	{
		// --- Framebuffer size sync (always read from the FBO, never the OS window) ---
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			m_Camera.OnResize(w, h);
		}

		m_Camera.OnUpdate(ts);

		// --- Render pass ---
		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		// Background grid
		if (m_ShowGrid)
		{
			const glm::vec4 gridColor = { 0.12f, 0.12f, 0.15f, 1.0f };
			for (float x = -8.0f; x <= 8.0f; x += 1.0f)
				Cosmic::Renderer2D::DrawLine({ x, -5.0f, -0.2f }, { x,  5.0f, -0.2f }, gridColor);
			for (float y = -5.0f; y <= 5.0f; y += 1.0f)
				Cosmic::Renderer2D::DrawLine({ -8.0f, y, -0.2f }, { 8.0f, y, -0.2f }, gridColor);
		}

		// Decorative pulsing SDF rings (driven by local layer time via GetLocalTime())
		if (m_ShowRings)
		{
			// GetLocalTime() is already accumulated by the manager before OnUpdate is called.
			// It responds to both global TimeScale AND this layer's own TimeScale.
			float t = GetLocalTime();

			// Outer glow disk
			float pulse = 1.0f + std::sin(t * m_PulseSpeed) * m_PulseAmp;
			Cosmic::Renderer2D::DrawCircle(
				{ 0.0f, 0.0f, -0.1f },
				{ 6.0f * pulse, 6.0f * pulse },
				{ m_Tint.r * 0.15f, m_Tint.g * 0.15f, m_Tint.b * 0.15f, 0.35f },
				1.0f, 0.25f
			);

			// Mid ring
			Cosmic::Renderer2D::DrawCircle(
				{ 0.0f, 0.0f, -0.09f },
				{ 4.5f, 4.5f },
				{ m_Tint.r, m_Tint.g, m_Tint.b, 0.30f },
				m_RingThick, 0.005f
			);

			// Inner crisp ring
			float innerPulse = 1.0f + std::sin(t * m_PulseSpeed * 1.6f + 1.0f) * m_PulseAmp;
			Cosmic::Renderer2D::DrawCircle(
				{ 0.0f, 0.0f, -0.08f },
				{ 2.8f * innerPulse, 2.8f * innerPulse },
				{ m_Tint.r, m_Tint.g, m_Tint.b, 0.55f },
				m_RingThick * 0.5f, 0.004f
			);
		}

		// Central material quad — renders the shader
		if (m_Material)
		{
			Cosmic::Renderer2D::DrawQuad(
				{ 0.0f, 0.0f, 0.0f },
				{ 2.0f, 2.0f },
				m_Material
			);

			// Wireframe bounding rect
			Cosmic::Renderer2D::DrawRect(
				{ 0.0f, 0.0f, 0.01f },
				{ 2.05f, 2.05f },
				{ m_Tint.r, m_Tint.g, m_Tint.b, 0.5f }
			);
		}
		else
		{
			// Fallback flat-color quad if no shader was loaded
			Cosmic::Renderer2D::DrawQuad(
				{ 0.0f, 0.0f, 0.0f },
				{ 2.0f, 2.0f },
				m_Tint
			);
		}

		// Corner accent lines
		{
			const glm::vec4 ac = { m_Tint.r, m_Tint.g, m_Tint.b, 0.8f };
			const float ext = 1.5f;
			Cosmic::Renderer2D::DrawLine({ -ext, -ext, 0.02f }, { ext, -ext, 0.02f }, ac);
			Cosmic::Renderer2D::DrawLine({ ext, -ext, 0.02f }, { ext,  ext, 0.02f }, ac);
			Cosmic::Renderer2D::DrawLine({ ext,  ext, 0.02f }, { -ext,  ext, 0.02f }, ac);
			Cosmic::Renderer2D::DrawLine({ -ext,  ext, 0.02f }, { -ext, -ext, 0.02f }, ac);
		}

		Cosmic::Renderer2D::EndScene();
	}

	// -------------------------------------------------------------------------
	void TemplateRenderLayer::OnImGuiRender()
	{
		// -------------------------------------------------------------------------
		// MIDDLE SIDEBAR LAYER: Mounts cleanly right under the Master panel
		// -------------------------------------------------------------------------
		ImGui::Begin("Project Inspector Mid");

		ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "Layer: Render Showcase");
		ImGui::Separator();
		ImGui::Spacing();

		// Shader time display
		ImGui::Text("Shader Clock: %.3fs", GetLocalTime());

		ImGui::Spacing();
		ImGui::Separator();

		// Visual toggles
		if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Show Grid", &m_ShowGrid);
			ImGui::Checkbox("Show Rings", &m_ShowRings);
		}

		// Material tint
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::ColorEdit4("Tint##layer", &m_Tint.x))
			{
				if (m_Material)
					m_Material->Set("u_Color", m_Tint);
			}
		}

		// Ring parameters
		if (m_ShowRings && ImGui::CollapsingHeader("Ring Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Pulse Speed", &m_PulseSpeed, 0.0f, 8.0f, "%.1f Hz");
			ImGui::SliderFloat("Pulse Amplitude", &m_PulseAmp, 0.0f, 0.5f, "%.2f");
			ImGui::SliderFloat("Ring Thickness", &m_RingThick, 0.005f, 0.15f, "%.3f");
		}

		// Camera info
		if (ImGui::CollapsingHeader("Camera"))
		{
			auto pos = m_Camera.GetPosition();
			ImGui::Text("Position: (%.2f, %.2f)", pos.x, pos.y);
			ImGui::Text("Zoom:     %.2f", m_Camera.GetZoomLevel());
			ImGui::TextDisabled("WASD = pan  |  Scroll = zoom");
		}

		// Renderer stats
		ImGui::Spacing();
		ImGui::Separator();
		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Quads:      %u", stats.QuadCount);

		ImGui::End(); // End "Project Inspector Mid"

		Cosmic::Renderer2D::ResetStats();
	}

	// -------------------------------------------------------------------------
	void TemplateRenderLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	bool TemplateRenderLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}