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
		// Calculate how many entities we actually need
		size_t targetCount = static_cast<size_t>(m_GridSize * 2) * static_cast<size_t>(m_GridSize * 2);

		// Optimization: Trim excess entities if the grid size decreased
		if (m_GridEntities.size() > targetCount)
		{
			for (size_t i = targetCount; i < m_GridEntities.size(); i++)
			{
				m_Scene->DestroyEntity(m_GridEntities[i]);
			}
			m_GridEntities.resize(targetCount);
		}

		// Optimization: Re-use existing entities and only create new ones if needed
		size_t index = 0;
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				Cosmic::Entity entity;

				if (index < m_GridEntities.size())
				{
					// Re-use existing entity
					entity = m_GridEntities[index];
				}
				else
				{
					// Allocate new entity
					entity = m_Scene->CreateEntity("StressCell");
					m_GridEntities.push_back(entity);
				}

				auto& t = entity.GetComponent<Cosmic::TransformComponent>();
				t.Position = { x * 0.1f, y * 0.1f, 0.0f };
				t.Scale = { 0.08f, 0.08f }; // Safe 2D Vector scaling

				index++;
			}
		}
	}

	void DinoStressLayer::OnUpdate(float ts)
	{
		// CRITICAL FIX: Reset rendering metrics at the start of the frame loop, 
		// ensuring telemetry is accurate regardless of ImGui visibility states.
		Cosmic::Renderer2D::ResetStats();

		m_Cam.OnUpdate(ts);

		// Accumulate running engine time
		m_Time += ts;

		// Update the uniform inside the material's cache so it uploads during OnRender -> Flush
		if (m_Material)
		{
			m_Material->Set("u_Time", m_Time);
		}
	}

	void DinoStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_FixedUpdateCount++;
	}

	void DinoStressLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_Cam.GetCamera());

		// Batch submit our grid matrix structures
		for (auto& entity : m_GridEntities)
		{
			auto& t = entity.GetComponent<Cosmic::TransformComponent>();

			// Explicitly passing glm::vec3 position and glm::vec2 scale
			Cosmic::Renderer2D::DrawQuad(t.Position, t.Scale, m_Material);
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

		// Extract compiled batching statistics
		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);

		// NOTE: Cosmic::Renderer2D::ResetStats() removed from here to prevent state loss.

		if (ImGui::Button("Reset Tick Counter")) m_FixedUpdateCount = 0;

		ImGui::End();
	}
}