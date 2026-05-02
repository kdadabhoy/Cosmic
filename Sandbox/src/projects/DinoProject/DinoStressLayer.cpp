#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Texture2D> tex) : m_Tex(tex), m_Cam(16.0f / 9.0f) {}

	void DinoStressLayer::OnUpdate(float ts) { m_Cam.OnUpdate(ts); }

	void DinoStressLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				Cosmic::Renderer2D::DrawQuad({ x * 0.1f, y * 0.1f }, { 0.08f, 0.08f }, m_Tex);
			}
		}
		Cosmic::Renderer2D::EndScene();
	}

	void DinoStressLayer::OnImGuiRender()
	{
		ImGui::Text("STRESS TEST ACTIVE");
		ImGui::DragInt("Grid Intensity", &m_GridSize, 1, 10, 100);
	}
}