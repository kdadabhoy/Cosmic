#include "DinoShaderTestLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoShaderTestLayer::DinoShaderTestLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material)
		: m_FullscreenMaterial(material), m_CameraController(1280.0f / 720.0f, true)
	{
		// 1. Expanded zoom limits for a wider view
		m_CameraController.SetZoomLimits(0.01f, 50.0f);
		m_CameraController.SetZoomSpeed(0.1f);
	}

	void DinoShaderTestLayer::OnEvent(Cosmic::Event& e)
	{
		// 2. Route events to the controller so it can react to mouse scrolling
		m_CameraController.OnEvent(e);
	}

	void DinoShaderTestLayer::SetViewportSize(float width, float height)
	{
		m_ViewportSize = { width, height };
		m_CameraController.OnResize(width, height);
	}

	void DinoShaderTestLayer::OnUpdate(float ts)
	{
		m_CameraController.OnUpdate(ts);
		Cosmic::Renderer2D::UpdateTimeline(ts, (uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
	}

	void DinoShaderTestLayer::OnRender()
	{
		float aspectRatio = m_ViewportSize.x / m_ViewportSize.y;
		// Scale the quad to match the screen's aspect ratio
		float quadWidth = 20.0f * aspectRatio;
		float quadHeight = 20.0f;

		// BeginScene uses the camera updated by the controller's zoom/pan state
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		if (m_FullscreenMaterial)
		{
			Cosmic::Renderer2D::DrawQuad(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec2(quadWidth, quadHeight), m_FullscreenMaterial);
		}

		Cosmic::Renderer2D::EndScene();
	}

	void DinoShaderTestLayer::OnImGuiRender()
	{
		ImGui::Begin("Shader Camera Control");

		// 3. UI to reflect the expanded range
		float zoom = m_CameraController.GetZoomLevel();
		if (ImGui::SliderFloat("Zoom Level", &zoom, 0.01f, 50.0f))
		{
			m_CameraController.SetZoomLevel(zoom);
		}

		ImGui::Text("Current Zoom: %.2f", m_CameraController.GetZoomLevel());
		ImGui::End();
	}
}