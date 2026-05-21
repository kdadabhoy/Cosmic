#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> fireMaterial, Cosmic::Ref<Cosmic::Material> dinoMaterial)
		: m_Scene(scene), m_FireMaterial(fireMaterial), m_DinoMaterial(dinoMaterial),
		m_CamController(1280.0f / 720.0f, true) // Updated initializer
	{
		m_CamController.SetZoomLimits(0.05f, 10.0f);
		RegenerateGrid();
	}

	void DinoStressLayer::RegenerateGrid()
	{
		size_t targetCount = static_cast<size_t>(m_GridSize * 2) * static_cast<size_t>(m_GridSize * 2);

		if (m_GridEntities.size() > targetCount)
		{
			for (size_t i = targetCount; i < m_GridEntities.size(); i++)
			{
				m_Scene->DestroyEntity(m_GridEntities[i]);
			}
			m_GridEntities.resize(targetCount);
		}

		size_t index = 0;
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				Cosmic::Entity entity;

				if (index < m_GridEntities.size())
				{
					entity = m_GridEntities[index];
				}
				else
				{
					entity = m_Scene->CreateEntity("StressCell");
					m_GridEntities.push_back(entity);
				}

				auto& t = entity.GetComponent<Cosmic::TransformComponent>();
				t.Position = { x * 0.1f, y * 0.1f, 0.0f };
				t.Scale = { 0.08f, 0.08f };

				index++;
			}
		}
	}

	void DinoStressLayer::OnUpdate(float ts)
	{
		Cosmic::Renderer2D::ResetStats();
		m_CamController.OnUpdate(ts); // FIX: Polls input layout transforms smoothly now
		m_Time += ts;

		if (m_FireMaterial)
		{
			m_FireMaterial->Set("u_Time", m_Time);
		}
	}

	void DinoStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_FixedUpdateCount++;
	}

	void DinoStressLayer::OnRender()
	{
		// FIX: Pull camera data matrix out from the controller state wrapper
		Cosmic::Renderer2D::BeginScene(m_CamController.GetCamera());

		size_t index = 0;
		for (auto& entity : m_GridEntities)
		{
			auto& t = entity.GetComponent<Cosmic::TransformComponent>();

			Cosmic::Ref<Cosmic::Material> targetMaterial = (index % 2 == 0) ? m_FireMaterial : m_DinoMaterial;

			if (targetMaterial)
			{
				Cosmic::Renderer2D::DrawQuad(t.Position, t.Scale, targetMaterial);
			}

			index++;
		}

		Cosmic::Renderer2D::EndScene();
	}

	void DinoStressLayer::OnImGuiRender()
	{
		ImGui::Begin("Cosmic Engine Stress Test");

		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "ECS MULTI-ENTITY STRESS TEST");
		ImGui::Separator();

		int oldSize = m_GridSize;
		if (ImGui::DragInt("Grid Intensity", &m_GridSize, 0.5f, 5, 60))
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

		ImGui::End();
	}
}