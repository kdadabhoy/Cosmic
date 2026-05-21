#include "DinoFlightLayer.h"
#include <imgui.h>
#include <cmath>

namespace Workspace
{
	DinoFlightLayer::DinoFlightLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material)
		: m_Scene(scene), m_Material(material), m_CameraController(1280.0f / 720.0f, true)
	{
		m_FlightDino = m_Scene->CreateEntity("Flight Dino");
		// TransformComponent added by default in Scene::CreateEntity

		auto& trail = m_FlightDino.AddComponent<FlightTrailComponent>();
		trail.Path.push_back({ 0.0f, 0.0f, 0.0f });
	}

	void DinoFlightLayer::OnUpdate(float ts)
	{
		auto& trans = m_FlightDino.GetComponent<Cosmic::TransformComponent>();
		auto& trail = m_FlightDino.GetComponent<FlightTrailComponent>();

		if (trail.CameraFollow)
			m_CameraController.SetPosition({ trans.Position.x, trans.Position.y, 0.0f });

		m_CameraController.OnUpdate(ts);
	}

	void DinoFlightLayer::OnFixedUpdate(float deltaFixedTime)
	{
		auto& trans = m_FlightDino.GetComponent<Cosmic::TransformComponent>();
		auto& trail = m_FlightDino.GetComponent<FlightTrailComponent>();

		trans.Position.x += trail.FlightSpeed * deltaFixedTime;
		trans.Position.y += (trail.FlightSpeed * trail.FlightSlope) * deltaFixedTime;

		if (trail.ChaosMode)
			trans.Position.y += (((float)rand() / RAND_MAX) - 0.5f) * 0.2f;

		trail.Path.push_back(trans.Position);
		if (trail.Path.size() > 500)
			trail.Path.erase(trail.Path.begin());
	}

	void DinoFlightLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		auto& trans = m_FlightDino.GetComponent<Cosmic::TransformComponent>();
		auto& trail = m_FlightDino.GetComponent<FlightTrailComponent>();

		// Background Checkerboard Grid
		float startX = floor(trans.Position.x) - 10.0f;
		float startY = floor(trans.Position.y) - 10.0f;
		for (float x = startX; x < startX + 20.0f; x += 1.0f)
		{
			for (float y = startY; y < startY + 20.0f; y += 1.0f)
			{
				bool isEven = (int(floor(x)) + int(floor(y))) % 2 == 0;
				Cosmic::Renderer2D::DrawQuad({ x, y, -0.1f }, { 1.0f, 1.0f },
					isEven ? glm::vec4(0.2f, 0.2f, 0.25f, 1.0f) : glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
			}
		}

		// Flight Trail Lines
		if (trail.Path.size() > 1)
		{
			for (size_t i = 0; i < trail.Path.size() - 1; i++)
				Cosmic::Renderer2D::DrawLine(trail.Path[i], trail.Path[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		// Animated Rotated Render Quad
		Cosmic::Renderer2D::DrawRotatedQuad(trans.Position, trans.Scale, trans.Rotation.z + (trail.FlightSlope * 0.5f), m_Material);
		Cosmic::Renderer2D::EndScene();
	}

	void DinoFlightLayer::OnImGuiRender()
	{
		auto& trans = m_FlightDino.GetComponent<Cosmic::TransformComponent>();
		auto& trail = m_FlightDino.GetComponent<FlightTrailComponent>();

		ImGui::Text("Flight Controls (ECS Data Bound)");
		ImGui::Checkbox("Camera Follow", &trail.CameraFollow);
		ImGui::DragFloat("Flight Speed", &trail.FlightSpeed, 0.1f);
		ImGui::SliderFloat("Flight Slope", &trail.FlightSlope, -1.0f, 1.0f);
		ImGui::Checkbox("Chaos Mode", &trail.ChaosMode);

		if (ImGui::Button("Reset Dino Position")) trans.Position = { 0.0f, 0.0f, 0.0f };
		if (ImGui::Button("Clear Flight Path")) trail.Path.clear();
	}
}