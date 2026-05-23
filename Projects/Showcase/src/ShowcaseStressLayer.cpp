#include "ShowcaseStressLayer.h"
#include <imgui.h>
#include <cmath>

namespace Showcase
{
	ShowcaseStressLayer::ShowcaseStressLayer(
		Cosmic::Ref<Cosmic::Scene>    scene,
		Cosmic::Ref<Cosmic::Material> materialA,
		Cosmic::Ref<Cosmic::Material> materialB)
		: m_Scene(scene)
		, m_MaterialA(materialA)
		, m_MaterialB(materialB)
		, m_Camera(1280.0f / 720.0f, false)
	{
		m_Camera.SetZoomLevel(3.5f);
		m_Camera.SetZoomLimits(0.5f, 30.0f);
		m_Camera.SetZoomSpeed(0.5f);

		RebuildGrid();
	}

	void ShowcaseStressLayer::RebuildGrid()
	{
		for (auto& e : m_GridEntities)
			m_Scene->DestroyEntity(e);
		m_GridEntities.clear();

		size_t side = static_cast<size_t>(m_GridRadius * 2);
		size_t total = side * side;
		m_GridEntities.reserve(total);

		size_t index = 0;
		for (int x = -m_GridRadius; x < m_GridRadius; ++x)
		{
			for (int y = -m_GridRadius; y < m_GridRadius; ++y)
			{
				auto ent = m_Scene->CreateEntity("StressCell");
				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				t.Position = { x * m_CellSpacing, y * m_CellSpacing, 0.0f };
				t.Scale = { m_CellSpacing * 0.85f, m_CellSpacing * 0.85f };

				auto& sprite = ent.AddComponent<Cosmic::SpriteRendererComponent>();
				sprite.ActiveMaterial = (index % 2 == 0) ? m_MaterialA : m_MaterialB;

				m_GridEntities.push_back(ent);
				++index;
			}
		}

		CS_INFO("ShowcaseStressLayer: Grid rebuilt — {} entities.", total);
	}

	void ShowcaseStressLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
	}

	void ShowcaseStressLayer::OnUpdate(float ts)
	{
		m_Camera.OnUpdate(ts);
		m_Time += ts;

		if (m_Animate)
		{
			for (auto& ent : m_GridEntities)
			{
				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				t.Rotation.z = m_Time * 30.0f;
			}
		}
	}

	void ShowcaseStressLayer::OnFixedUpdate(float fixedDt)
	{
		++m_FixedTicks;
	}

	void ShowcaseStressLayer::OnRender()
	{
		Cosmic::Renderer2D::ResetStats();
		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		m_Scene->OnRender();

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseStressLayer::OnImGuiRender()
	{
		ImGui::Text("--- ECS Stress Test ---");
		ImGui::Separator();

		int oldRadius = m_GridRadius;
		ImGui::SliderInt("Grid Radius", &m_GridRadius, 5, 60);
		if (m_GridRadius != oldRadius)
			RebuildGrid();

		size_t side = static_cast<size_t>(m_GridRadius * 2);
		ImGui::Text("Entities:    %zu  (%zu x %zu)", side * side, side, side);

		float oldSpacing = m_CellSpacing;
		ImGui::SliderFloat("Cell Spacing", &m_CellSpacing, 0.05f, 0.5f, "%.3f");
		if (std::abs(m_CellSpacing - oldSpacing) > 0.001f)
			RebuildGrid();

		ImGui::Separator();
		ImGui::Checkbox("Animate (Rotate)", &m_Animate);

		if (!m_Animate)
		{
			if (ImGui::Button("Reset Rotations"))
			{
				for (auto& e : m_GridEntities)
					e.GetComponent<Cosmic::TransformComponent>().Rotation.z = 0.0f;
			}
		}

		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("--- Renderer2D Stats ---");
		ImGui::Text("Draw Calls:  %u", stats.DrawCalls);
		ImGui::Text("Quads:       %u", stats.QuadCount);
		ImGui::Text("Vertices:    %u", stats.GetTotalVertexCount());
		ImGui::Text("Indices:     %u", stats.GetTotalIndexCount());
		ImGui::Text("Fixed Ticks: %u", m_FixedTicks);
		ImGui::Spacing();

		ImGui::TextWrapped("Entities are sorted into material buckets before drawing. With 2 materials in alternating order, draw calls minimize via flushes when overflow boundaries hit.");

		if (ImGui::Button("Reset Tick Counter"))
			m_FixedTicks = 0;
	}
}