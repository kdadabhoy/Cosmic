#include "DinoFlightLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoFlightLayer::DinoFlightLayer(Cosmic::Ref<Cosmic::Texture2D> texture)
		: m_Texture(texture), m_CameraController(1280.0f / 720.0f, true)
	{
	}

	void DinoFlightLayer::OnUpdate(float ts)
	{
		m_DinoPos.x += m_FlightSpeed * ts;
		m_DinoPos.y += (m_FlightSpeed * m_FlightSlope) * ts;

		if (m_ChaosMode)
		{
			float jitter = (((float)rand() / RAND_MAX) - 0.5f) * 0.2f;
			m_DinoPos.y += jitter;
		}

		m_FlightPath.push_back(m_DinoPos);
		if (m_FlightPath.size() > 500) m_FlightPath.erase(m_FlightPath.begin());

		if (m_CameraFollow)
			m_CameraController.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });

		m_CameraController.OnUpdate(ts);
	}

	void DinoFlightLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Infinite Grid Logic
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

		// Flight Path Trail
		if (m_FlightPath.size() > 1)
		{
			for (size_t i = 0; i < m_FlightPath.size() - 1; i++)
				Cosmic::Renderer2D::DrawLine(m_FlightPath[i], m_FlightPath[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, m_Texture);

		Cosmic::Renderer2D::EndScene();
	}

	void DinoFlightLayer::OnImGuiRender()
	{
		ImGui::Checkbox("Camera Follow", &m_CameraFollow);
		ImGui::Checkbox("Chaos Mode", &m_ChaosMode);
		ImGui::DragFloat("Flight Speed", &m_FlightSpeed, 0.1f, 0.0f, 20.0f);
		ImGui::DragFloat("Flight Slope", &m_FlightSlope, 0.05f, -2.0f, 2.0f);

		if (ImGui::Button("Clear Flight Path")) m_FlightPath.clear();
	}
}