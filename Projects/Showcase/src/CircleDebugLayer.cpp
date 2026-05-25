#include "CircleDebugLayer.h"
#include <imgui.h>

namespace Showcase
{
	CircleDebugLayer::CircleDebugLayer()
		: Cosmic::Layer("CircleDebugLayer")
		, m_CameraController(1280.0f / 720.0f, false)
	{
	}

	void CircleDebugLayer::OnAttach()
	{
		CS_INFO("CircleDebugLayer: Attached sandbox context matching working system patterns.");
		m_CameraController.SetZoomLevel(3.0f);
	}

	void CircleDebugLayer::OnDetach()
	{
		CS_INFO("CircleDebugLayer: Detached sandbox context.");
	}

	void CircleDebugLayer::OnUpdate(float ts)
	{
		// 1. Viewport Synchronization (Matching working layers pattern perfectly)
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float activeWidth = static_cast<float>(fb->GetWidth());
		float activeHeight = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != activeWidth || m_ViewportSize.y != activeHeight)
		{
			m_ViewportSize = { activeWidth, activeHeight };
			m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		}

		m_CameraController.OnUpdate(ts);

		// 2. Render Batch Submission Sequence
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Guideline Backdrop Grid for reference tracking
		for (float x = -5.0f; x <= 5.0f; x += 1.0f)
		{
			Cosmic::Renderer2D::DrawLine({ x, -5.0f, -0.1f }, { x, 5.0f, -0.1f }, { 0.2f, 0.2f, 0.25f, 1.0f });
		}
		for (float y = -5.0f; y <= 5.0f; y += 1.0f)
		{
			Cosmic::Renderer2D::DrawLine({ -5.0f, y, -0.1f }, { 5.0f, y, -0.1f }, { 0.2f, 0.2f, 0.25f, 1.0f });
		}

		// Defensive normalization fallback helper
		glm::vec4 safeDiskColor = m_DiskColor;
		if (safeDiskColor.r > 1.0f || safeDiskColor.g > 1.0f || safeDiskColor.b > 1.0f || safeDiskColor.a > 1.0f)
			safeDiskColor /= 255.0f;

		glm::vec4 safeRingColor = m_RingColor;
		if (safeRingColor.r > 1.0f || safeRingColor.g > 1.0f || safeRingColor.b > 1.0f || safeRingColor.a > 1.0f)
			safeRingColor /= 255.0f;

		// Left Side Primitive: A Solid/Feathered Disk
		Cosmic::Renderer2D::DrawCircle(
			{ -1.5f, 0.0f, 0.0f },    // Position
			{ 2.0f, 2.0f },           // Size Vector
			safeDiskColor,            // Normalized Tint Color
			m_DiskThickness,          // Thickness property
			m_DiskFade                // Fade property
		);

		// Right Side Primitive: The Tracker / Radar Ring
		Cosmic::Renderer2D::DrawCircle(
			{ 1.5f, 0.0f, 0.0f },     // Position
			{ 2.0f, 2.0f },           // Size Vector
			safeRingColor,            // Normalized Tint Color
			m_RingThickness,          // Thickness property
			m_RingFade                // Fade property
		);

		Cosmic::Renderer2D::EndScene();
	}

	void CircleDebugLayer::OnImGuiRender()
	{
		ImGui::Begin("Renderer2D Circle Isolation Sandbox");

		// Live Telemetry Monitoring via exposed Renderer Stats
		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Batch Hardware Telemetry:");
		ImGui::Text("  Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("  Staged Quads: %u", stats.QuadCount);
		ImGui::Text("  Total Vertices: %u", stats.GetTotalVertexCount());
		ImGui::Separator();

		// Left Primitive Custom Controller Widget
		if (ImGui::CollapsingHeader("LEFT PRIMITIVE: FILLED DISK", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::ColorEdit4("Disk Color Picker", &m_DiskColor.x);
			ImGui::SliderFloat("Disk Thickness Offset", &m_DiskThickness, 0.01f, 1.0f, "%.3f");
			ImGui::SliderFloat("Disk Edge Feathering", &m_DiskFade, 0.001f, 1.0f, "%.4f");
		}

		ImGui::Spacing();

		// Right Primitive Custom Controller Widget
		if (ImGui::CollapsingHeader("RIGHT PRIMITIVE: PROCEDURAL HOLLOW RING", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::ColorEdit4("Ring Color Picker", &m_RingColor.x);
			ImGui::SliderFloat("Ring Wall Thickness", &m_RingThickness, 0.01f, 1.0f, "%.3f");
			ImGui::SliderFloat("Ring Edge Anti-Aliasing", &m_RingFade, 0.001f, 0.2f, "%.4f");
		}

		ImGui::End();
	}

	void CircleDebugLayer::OnEvent(Cosmic::Event& e)
	{
		m_CameraController.OnEvent(e);
	}
}