#include "DinoFlightLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoFlightLayer::DinoFlightLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_CameraController(1280.0f / 720.0f, true)
	{
	}

	/**
	 * OnUpdate
	 * * Handles per-frame visual updates, such as camera controller smoothing.
	 */
	void DinoFlightLayer::OnUpdate(float ts)
	{
		// Camera follow should still happen in update for smooth visual tracking
		if (m_CameraFollow)
			m_CameraController.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });

		m_CameraController.OnUpdate(ts);
	}

	/**
	 * OnFixedUpdate
	 * * THE PHYSICS HUB: This handles the deterministic movement of the Dino.
	 * By performing these calculations here, the flight path remains consistent
	 * regardless of CPU load or stuttering frame rates.
	 */
	void DinoFlightLayer::OnFixedUpdate(float deltaFixedTime)
	{
		// Move based on fixed time interval
		m_DinoPos.x += m_FlightSpeed * deltaFixedTime;
		m_DinoPos.y += (m_FlightSpeed * m_FlightSlope) * deltaFixedTime;

		if (m_ChaosMode)
			m_DinoPos.y += (((float)rand() / RAND_MAX) - 0.5f) * 0.2f;

		// Record history for the trail
		m_FlightPath.push_back(m_DinoPos);
		if (m_FlightPath.size() > 500)
			m_FlightPath.erase(m_FlightPath.begin());
	}

	/**
	 * OnRender
	 * * Visualizes the simulation state captured in the fixed update.
	 */
	void DinoFlightLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// 1. Background Grid (Procedural)
		float startX = floor(m_DinoPos.x) - 10;
		float startY = floor(m_DinoPos.y) - 10;
		for (float x = startX; x < startX + 20; x += 1.0f)
		{
			for (float y = startY; y < startY + 20; y += 1.0f)
			{
				bool isEven = (int(floor(x)) + int(floor(y))) % 2 == 0;
				Cosmic::Renderer2D::DrawQuad({ x, y, -0.1f }, { 1.0f, 1.0f },
					isEven ? glm::vec4(0.2f, 0.2f, 0.25f, 1.0f) : glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
			}
		}

		// 2. Flight Path Trail
		if (m_FlightPath.size() > 1)
		{
			for (size_t i = 0; i < m_FlightPath.size() - 1; i++)
				Cosmic::Renderer2D::DrawLine(m_FlightPath[i], m_FlightPath[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		// 3. Dino Sprite (Material System)
		Cosmic::Renderer2D::DrawRotatedQuad(m_DinoPos, { 0.5f, 0.5f }, m_FlightSlope * 0.5f, m_Material);

		Cosmic::Renderer2D::EndScene();
	}

	void DinoFlightLayer::OnImGuiRender()
	{
		ImGui::Text("Flight Controls");
		ImGui::Checkbox("Camera Follow", &m_CameraFollow);
		ImGui::DragFloat("Flight Speed", &m_FlightSpeed, 0.1f);
		ImGui::SliderFloat("Flight Slope", &m_FlightSlope, -1.0f, 1.0f);
		ImGui::Checkbox("Chaos Mode", &m_ChaosMode);

		if (ImGui::Button("Reset Dino Position")) m_DinoPos = { 0.0f, 0.0f, 0.0f };
		if (ImGui::Button("Clear Flight Path")) m_FlightPath.clear();
	}
}