#include "DinoFlightLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoFlightLayer::DinoFlightLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_CameraController(1280.0f / 720.0f, true)
	{
	}

	void DinoFlightLayer::OnUpdate(float ts)
	{
		m_DinoPos.x += m_FlightSpeed * ts;
		m_DinoPos.y += (m_FlightSpeed * m_FlightSlope) * ts;

		if (m_ChaosMode)
			m_DinoPos.y += (((float)rand() / RAND_MAX) - 0.5f) * 0.2f;

		m_FlightPath.push_back(m_DinoPos);
		if (m_FlightPath.size() > 500) m_FlightPath.erase(m_FlightPath.begin());

		if (m_CameraFollow)
			m_CameraController.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });

		m_CameraController.OnUpdate(ts);
	}

	void DinoFlightLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Background Grid (Default Material)
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

		// Trail (Lines)
		if (m_FlightPath.size() > 1)
		{
			for (size_t i = 0; i < m_FlightPath.size() - 1; i++)
				Cosmic::Renderer2D::DrawLine(m_FlightPath[i], m_FlightPath[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		// Dino (Material System)
		// This will trigger a Flush because we switch from DefaultMaterial to m_Material
		Cosmic::Renderer2D::DrawRotatedQuad(m_DinoPos, { 0.5f, 0.5f }, m_FlightSlope * 0.5f, m_Material);

		Cosmic::Renderer2D::EndScene();
	}

	void DinoFlightLayer::OnImGuiRender()
	{
		ImGui::Checkbox("Camera Follow", &m_CameraFollow);
		ImGui::DragFloat("Flight Speed", &m_FlightSpeed, 0.1f);
		if (ImGui::Button("Clear Path")) m_FlightPath.clear();
	}
}