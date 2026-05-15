#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_Cam(16.0f / 9.0f)
	{
	}

	void DinoStressLayer::OnUpdate(float ts)
	{
		m_Cam.OnUpdate(ts);
	}

	/**
	 * OnFixedUpdate
	 * * In the Stress Layer, we use this to verify the engine's fixed-time
	 * pulse is consistent even when the GPU is under heavy load.
	 */
	void DinoStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_FixedUpdateCount++;
	}

	void DinoStressLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());

		// Stress test the Material batching path
		// Total quads rendered: (GridSize * 2) ^ 2
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				// Every quad here shares the same Material Ref, 
				// allowing the Batch Renderer to draw them in a single call.
				Cosmic::Renderer2D::DrawQuad({ x * 0.1f, y * 0.1f, 0.0f }, { 0.08f, 0.08f }, m_Material);
			}
		}

		Cosmic::Renderer2D::EndScene();
	}

	void DinoStressLayer::OnImGuiRender()
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "STRESS TEST ACTIVE");
		ImGui::Separator();

		ImGui::DragInt("Grid Intensity", &m_GridSize, 1, 10, 150);
		ImGui::Text("Fixed Ticks Processed: %u", m_FixedUpdateCount);

		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);
		ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
		ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

		if (ImGui::Button("Reset Tick Counter")) m_FixedUpdateCount = 0;
	}
}