#pragma once
#include "../../Simulation.h"

namespace Workspace
{
	class StressTestLayer : public Simulation
	{
	public:
		StressTestLayer() : m_Cam(1280.0f / 720.0f) {}

		virtual void OnUpdate(float ts) override { m_Cam.OnUpdate(ts); }

		virtual void OnRender() override
		{
			Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());
			for (float y = -1.0f; y < 1.0f; y += 0.05f)
				for (float x = -1.0f; x < 1.0f; x += 0.05f)
					Cosmic::Renderer2D::DrawQuad({ x, y }, { 0.04f, 0.04f }, { (x + 1) * 0.5f, (y + 1) * 0.5f, 0.8f, 1.0f });
			Cosmic::Renderer2D::EndScene();
		}

		virtual void OnImGuiRender() override
		{
			auto stats = Cosmic::Renderer2D::GetStats();
			ImGui::Text("Quads: %d", stats.QuadCount);
			ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		}

		virtual void SetViewportSize(float w, float h) override { m_Cam.OnResize(w, h); }

	private:
		Cosmic::OrthographicCameraController m_Cam;
	};
}