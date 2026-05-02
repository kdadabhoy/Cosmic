#pragma once
#include "../../Simulation.h"
#include <vector>

namespace Workspace
{
	class DinoFlightLayer : public Simulation
	{
	public:
		DinoFlightLayer() : m_Cam(1280.0f / 720.0f) {}

		virtual void OnUpdate(float ts) override
		{
			m_Cam.OnUpdate(ts);
			m_DinoPos.x += 2.0f * ts;
			m_DinoPos.y += sin(ImGui::GetTime()) * ts;
			m_Path.push_back(m_DinoPos);
			if (m_Path.size() > 200) m_Path.erase(m_Path.begin());
			m_Cam.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });
		}

		virtual void OnRender() override
		{
			Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());
			for (size_t i = 0; i < m_Path.size() - 1; i++)
				Cosmic::Renderer2D::DrawLine(m_Path[i], m_Path[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
			Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.4f, 0.4f }, { 0.2f, 0.8f, 0.2f, 1.0f });
			Cosmic::Renderer2D::EndScene();
		}

		virtual void OnImGuiRender() override { ImGui::Text("Flight Path Nodes: %zu", m_Path.size()); }
		virtual void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	private:
		Cosmic::OrthographicCameraController m_Cam;
		glm::vec3 m_DinoPos{ 0.0f };
		std::vector<glm::vec3> m_Path;
	};
}