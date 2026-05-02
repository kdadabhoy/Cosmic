#pragma once
#include "../../Simulation.h"

namespace Workspace
{
	class DinoRunLayer : public Simulation
	{
	public:
		DinoRunLayer() : m_Cam(1280.0f / 720.0f) { m_DinoPos = { -1.0f, -0.5f, 0.0f }; }

		virtual void OnUpdate(float ts) override
		{
			m_Cam.OnUpdate(ts);
			// Jumping Logic
			if (Cosmic::Input::IsKeyPressed(KEY_SPACE) && m_Grounded) { m_VelocityY = 5.0f; m_Grounded = false; }
			if (!m_Grounded)
			{
				m_VelocityY -= 12.0f * ts;
				m_DinoPos.y += m_VelocityY * ts;
			}
			if (m_DinoPos.y <= -0.5f) { m_DinoPos.y = -0.5f; m_Grounded = true; }
		}

		virtual void OnRender() override
		{
			Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());
			Cosmic::Renderer2D::DrawQuad({ 0.0f, -0.8f }, { 4.0f, 0.2f }, { 0.2f, 0.2f, 0.2f, 1.0f }); // Floor
			Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, { 0.8f, 0.2f, 0.2f, 1.0f });    // Dino
			Cosmic::Renderer2D::EndScene();
		}

		virtual void OnImGuiRender() override { ImGui::Text("Dino is %s", m_Grounded ? "Grounded" : "Jumping"); }
		virtual void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	private:
		Cosmic::OrthographicCameraController m_Cam;
		glm::vec3 m_DinoPos;
		float m_VelocityY = 0.0f;
		bool m_Grounded = true;
	};
}