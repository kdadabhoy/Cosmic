#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material)
		: m_Scene(scene), m_Material(material), m_Cam(1280.0f / 720.0f, true)
	{
		RegenerateGrid();
	}

	void DinoStressLayer::RegenerateGrid()
	{
		for (auto& ent : m_GridEntities)
			m_Scene->DestroyEntity(ent);
		m_GridEntities.clear();

		// Generate entities into the scene container
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				auto entity = m_Scene->CreateEntity("StressCell");
				auto& t = entity.GetComponent<Cosmic::TransformComponent>(); // Scene automatically creates this
				t.Position = { x * 0.1f, y * 0.1f, 0.0f };
				t.Scale = { 0.08f, 0.08f }; // Scale is a 2D vector

				m_GridEntities.push_back(entity);
			}
		}
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

		// Pull data back out of the layout components for processing
		for (auto& entity : m_GridEntities)
		{
			auto& t = entity.GetComponent<Cosmic::TransformComponent>();
			Cosmic::Renderer2D::DrawQuad(t.Position, t.Scale, m_Material);
		}

		Cosmic::Renderer2D::EndScene();
	}

	void DinoStressLayer::OnImGuiRender()
	{
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "ECS MULTI-ENTITY STRESS TEST");
		ImGui::Separator();

		int oldSize = m_GridSize;
		if (ImGui::DragInt("Grid Intensity", &m_GridSize, 1, 5, 60))
		{
			if (m_GridSize != oldSize)
				RegenerateGrid();
		}

		ImGui::Text("Total Live Scene Entities: %zu", m_GridEntities.size());
		ImGui::Text("Fixed Ticks Processed: %u", m_FixedUpdateCount);

		ImGui::Separator();
		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);

		if (ImGui::Button("Reset Tick Counter")) m_FixedUpdateCount = 0;
	}
}