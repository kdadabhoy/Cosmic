#include "ShowcaseStressLayer.h"
#include <imgui.h>
#include <cmath>

namespace Showcase
{
	ShowcaseStressLayer::ShowcaseStressLayer(
		Cosmic::Ref<Cosmic::Scene> scene,
		Cosmic::Ref<Cosmic::Material> materialA,
		Cosmic::Ref<Cosmic::Material> materialB)
		: Cosmic::Layer("ShowcaseStressLayer")
		, m_Scene(scene)
		, m_MaterialA(materialA)
		, m_MaterialB(materialB)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	void ShowcaseStressLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(3.5f);
		m_Camera.SetZoomLimits(0.5f, 30.0f);
		m_Camera.SetZoomSpeed(0.5f);

		RebuildGrid();
	}

	void ShowcaseStressLayer::OnDetach()
	{
		for (auto& e : m_GridEntities)
		{
			if (e)
				m_Scene->DestroyEntity(e);
		}
		m_GridEntities.clear();
	}

	void ShowcaseStressLayer::RebuildGrid()
	{
		for (auto& e : m_GridEntities)
		{
			if (e)
				m_Scene->DestroyEntity(e);
		}
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

		CS_INFO("ShowcaseStressLayer: Component grid allocation completed via {0} entities.", total);
	}

	// =========================================================================
	// Deterministic Simulation Steps
	// =========================================================================
	void ShowcaseStressLayer::OnFixedUpdate(float deltaFixedTime)
	{
		// ARCHITECTURE FIX: Manual time accumulation removed.
		// We preserve only pure execution logic tracking counters here.
		++m_UpdateTicks;
	}

	// =========================================================================
	// Frame Graphics Render Pass
	// =========================================================================
	void ShowcaseStressLayer::OnUpdate(float ts)
	{
		m_Camera.OnUpdate(ts);

		// VISUAL FIX: Move kinematic visual animations out of OnFixedUpdate into OnUpdate.
		// By querying Layer::GetLocalTime(), updates match variable hardware monitor refresh rates perfectly.
		if (m_Animate)
		{
			float currentTimelineTime = Cosmic::Layer::GetLocalTime();
			for (auto& ent : m_GridEntities)
			{
				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				t.Rotation.z = currentTimelineTime * 30.0f;
			}
		}

		Cosmic::Renderer2D::ResetStats();
		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		m_Scene->OnRender();

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseStressLayer::OnImGuiRender()
	{
		ImGui::Begin("Simulation Inspection Window");
		ImGui::Text("--- ECS Massive Grid Stress Target ---");
		ImGui::Separator();

		int oldRadius = m_GridRadius;
		ImGui::SliderInt("Grid Cell Radius", &m_GridRadius, 5, 60);
		if (m_GridRadius != oldRadius)
		{
			RebuildGrid();
		}

		size_t side = static_cast<size_t>(m_GridRadius * 2);
		ImGui::Text("Active Entities: %zu  [%zu x %zu]", side * side, side, side);

		float oldSpacing = m_CellSpacing;
		ImGui::SliderFloat("Cell Matrix Spacing", &m_CellSpacing, 0.05f, 0.5f, "%.3f");
		if (std::abs(m_CellSpacing - oldSpacing) > 0.001f)
		{
			RebuildGrid();
		}

		ImGui::Separator();
		ImGui::Checkbox("Animate Transformations", &m_Animate);

		if (!m_Animate)
		{
			if (ImGui::Button("Reset Static Rotations"))
			{
				for (auto& e : m_GridEntities)
				{
					e.GetComponent<Cosmic::TransformComponent>().Rotation.z = 0.0f;
				}
			}
		}

		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("--- Engine Renderer2D Metrics ---");
		ImGui::Text("Draw Pipeline Flushes: %u", stats.DrawCalls);
		ImGui::Text("Rendered Quads:        %u", stats.QuadCount);
		ImGui::Text("Vertex Buffer Usage:   %u", stats.GetTotalVertexCount());
		ImGui::Text("Index Buffer Usage:    %u", stats.GetTotalIndexCount());

		// ARCHITECTURE FIX: Display pure system ticks along with native engine layer time
		ImGui::Text("Fixed Step Iterations: %u steps", m_UpdateTicks);
		ImGui::Text("Timeline Sync Phase:   %.2fs", Cosmic::Layer::GetLocalTime());
		ImGui::Spacing();

		ImGui::TextWrapped("System Architecture Notice: Entities are grouped automatically into localized asset/material buckets. Alternate layouts using 2 materials will trigger a pipeline flush only when batch array bounds or allocation thresholds are exceeded.");

		if (ImGui::Button("Reset Framework Fixed Counters"))
		{
			m_UpdateTicks = 0;
		}

		ImGui::End();
	}

	void ShowcaseStressLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(GLCORE_BIND_EVENT_FN(ShowcaseStressLayer::OnWindowResize));
	}

	bool ShowcaseStressLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}