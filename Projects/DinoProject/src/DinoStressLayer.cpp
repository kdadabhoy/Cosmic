#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_Cam(1280.0f / 720.0f, true)
	{
	}

	void DinoStressLayer::OnUpdate(float ts)
	{
		m_Cam.OnUpdate(ts);
	}

	void DinoStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_FixedUpdateCount++;
	}

	void DinoStressLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());

		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
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

		if (ImGui::Button("Reset Tick Counter")) m_FixedUpdateCount = 0;
	}
}