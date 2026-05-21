#include "DinoStressLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoStressLayer::DinoStressLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: m_Scene(scene), m_CamController(1280.0f / 720.0f, true)
	{
		m_CamController.SetZoomLimits(0.04f, 10.0f);
		m_CamController.SetZoomSpeed(0.01f);
	}

	void DinoStressLayer::SetMaterials(Cosmic::Ref<Cosmic::Material> fireMaterial, Cosmic::Ref<Cosmic::Material> dinoMaterial)
	{
		m_CachedFireMaterial = fireMaterial;
		m_CachedDinoMaterial = dinoMaterial;

		// Regenerate or update the grid once we have valid assets to apply
		RegenerateGrid();
	}

	void DinoStressLayer::RegenerateGrid()
	{
		// 1. Calculate our absolute bounding target dimensions
		size_t targetCount = static_cast<size_t>(m_GridSize * 2) * static_cast<size_t>(m_GridSize * 2);

		// 2. If downsizing, explicitly remove the trailing entities from the core ECS world context first
		if (m_GridEntities.size() > targetCount)
		{
			for (size_t i = targetCount; i < m_GridEntities.size(); i++)
			{
				m_Scene->DestroyEntity(m_GridEntities[i]);
			}
		}

		// 3. Force resize the underlying storage vector. 
		// If expanding, this fills new slots with null/default handles to be populated below.
		m_GridEntities.resize(targetCount);

		// 4. Repopulate and transform the layout grid cleanly
		size_t index = 0;
		for (int x = -m_GridSize; x < m_GridSize; x++)
		{
			for (int y = -m_GridSize; y < m_GridSize; y++)
			{
				Cosmic::Entity entity = m_GridEntities[index];

				// If the handle is null (meaning the vector just expanded to fit this slot), 
				// instantiate the new entity identity and store it.
				if (!entity)
				{
					entity = m_Scene->CreateEntity("StressCell");
					entity.AddComponent<Cosmic::SpriteRendererComponent>();
					m_GridEntities[index] = entity;
				}

				// Apply spatial positions based on grid coordinate updates
				auto& t = entity.GetComponent<Cosmic::TransformComponent>();
				t.Position = { x * 0.1f, y * 0.1f, 0.0f };
				t.Scale = { 0.08f, 0.08f };

				// Assign materials directly to the ECS component structure properties
				auto& sprite = entity.GetComponent<Cosmic::SpriteRendererComponent>();
				sprite.ActiveMaterial = (index % 2 == 0) ? m_CachedFireMaterial : m_CachedDinoMaterial;

				index++;
			}
		}
	}

	void DinoStressLayer::OnUpdate(float ts)
	{
		Cosmic::Renderer2D::ResetStats();
		m_CamController.OnUpdate(ts);
		m_Time += ts;
	}

	void DinoStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_FixedUpdateCount++;
	}

	void DinoStressLayer::OnRender()
	{
		// 1. Establish camera matrices
		Cosmic::Renderer2D::BeginScene(m_CamController.GetCamera());

		// 2. TRUE ECS RENDERING: Pass drawing completely off to your native scene system view loops!
		if (m_Scene)
		{
			m_Scene->OnRender();
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

		// --- CAMERA CONFIGURATION MANAGEMENT PANEL ---
		ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "CAMERA PROPERTIES");

		// 1. Zoom Speed Slider
		float speed = m_CamController.GetZoomSpeed();
		if (ImGui::SliderFloat("Zoom Speed", &speed, 0.01f, 1.0f, "%.2f"))
		{
			m_CamController.SetZoomSpeed(speed);
		}

		// 2. Zoom Min/Max Range Sliders
		static float localLimits[2] = { 0.05f, 10.0f }; // Starts matching your constructor properties
		if (ImGui::DragFloat2("Zoom Limits (Min/Max)", localLimits, 0.01f, 0.01f, 20.0f, "%.2f"))
		{
			// Input sanity checkpoint: Prevent bounds inversion anomalies
			if (localLimits[0] < localLimits[1])
			{
				m_CamController.SetZoomLimits(localLimits[0], localLimits[1]);
			}
		}

		// Quick readout of active rendering scaling values
		ImGui::Text("Current Zoom State Scale: %.3f", m_CamController.GetZoomLevel());

		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Renderer2D Stats:");
		ImGui::Text("Draw Calls: %d", stats.DrawCalls);
		ImGui::Text("Quads: %d", stats.QuadCount);

		if (ImGui::Button("Reset Tick Counter")) m_FixedUpdateCount = 0;

		ImGui::End();
	}
}