#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_Cam(16.0f / 9.0f)
	{
	}

	void DinoStressLayer::OnUpdate(float ts) { m_Cam.OnUpdate(ts); }

	void DinoStressLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());

		// Stress test the Material batching path
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				// Uses the Material overload of DrawQuad
				Cosmic::Renderer2D::DrawQuad({ x * 0.1f, y * 0.1f, 0.0f }, { 0.08f, 0.08f }, m_Material);
			}
		}

		Cosmic::Renderer2D::EndScene();
	}

	void DinoStressLayer::OnImGuiRender()
	{
		ImGui::Text("STRESS TEST ACTIVE");
		ImGui::DragInt("Grid Intensity", &m_GridSize, 1, 10, 150);

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);
		ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
	}
}