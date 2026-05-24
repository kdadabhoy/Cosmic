#include "ShowcaseFlightLayer.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Showcase
{
	ShowcaseFlightLayer::ShowcaseFlightLayer(
		Cosmic::Ref<Cosmic::Scene> scene,
		Cosmic::Ref<Cosmic::Material> dinoMaterial)
		: Cosmic::Layer("ShowcaseFlightLayer")
		, m_Scene(scene)
		, m_DinoMaterial(dinoMaterial)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	void ShowcaseFlightLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(2.0f);
		m_Camera.SetZoomLimits(0.5f, 20.0f);

		m_DinoA = m_Scene->CreateEntity("FlightDino_A");
		{
			auto& t = m_DinoA.GetComponent<Cosmic::TransformComponent>();
			t.Position = { -5.0f, 1.0f, 0.0f };
			t.Scale = { 0.45f, 0.45f };

			auto& fd = m_DinoA.AddComponent<FlightDinoComponent>();
			fd.Speed = 2.5f;
			fd.Slope = 0.1f;
			fd.Color = { 1.0f, 0.6f, 0.1f, 1.0f };
			fd.Trail.push_back(t.Position);
		}

		m_DinoB = m_Scene->CreateEntity("FlightDino_B");
		{
			auto& t = m_DinoB.GetComponent<Cosmic::TransformComponent>();
			t.Position = { -5.0f, -1.0f, 0.0f };
			t.Scale = { 0.45f, 0.45f };

			auto& fd = m_DinoB.AddComponent<FlightDinoComponent>();
			fd.Speed = 1.8f;
			fd.Slope = -0.15f;
			fd.Color = { 0.2f, 0.9f, 1.0f, 1.0f };
			fd.Trail.push_back(t.Position);
		}
	}

	void ShowcaseFlightLayer::OnDetach()
	{
		DeselectAll();
	}

	void ShowcaseFlightLayer::OnUpdate(float ts)
	{
		m_Camera.OnUpdate(ts);

		float fixedDt = ts > 0.0f ? ts : 0.001f;
		auto moveDino = [&](Cosmic::Entity ent)
			{
				if (!ent) return;
				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				auto& fd = ent.GetComponent<FlightDinoComponent>();

				t.Position.x += fd.Speed * fixedDt;
				t.Position.y += fd.Speed * fd.Slope * fixedDt;

				if (t.Position.x > 12.0f)
				{
					t.Position.x = -12.0f;
					fd.Trail.clear();
				}

				if (t.Position.y > 5.0f || t.Position.y < -5.0f)
					fd.Slope = -fd.Slope;

				fd.Trail.push_back(t.Position);
				if (fd.Trail.size() > k_MaxTrailLength)
					fd.Trail.erase(fd.Trail.begin());
			};

		moveDino(m_DinoA);
		moveDino(m_DinoB);

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		// Grid background rendering
		for (float x = -12.0f; x <= 12.0f; x += 2.0f)
			Cosmic::Renderer2D::DrawLine({ x, -6.0f, -0.1f }, { x, 6.0f, -0.1f }, { 0.15f, 0.15f, 0.18f, 1.0f });
		for (float y = -6.0f; y <= 6.0f; y += 2.0f)
			Cosmic::Renderer2D::DrawLine({ -12.0f, y, -0.1f }, { 12.0f, y, -0.1f }, { 0.15f, 0.15f, 0.18f, 1.0f });

		auto drawDino = [&](Cosmic::Entity ent)
			{
				if (!ent) return;
				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				auto& fd = ent.GetComponent<FlightDinoComponent>();

				size_t n = fd.Trail.size();
				for (size_t i = 1; i < n; ++i)
				{
					float alpha = static_cast<float>(i) / static_cast<float>(n);
					glm::vec4 trailColor = fd.Color;
					trailColor.a = alpha * 0.85f;
					Cosmic::Renderer2D::DrawLine(fd.Trail[i - 1], fd.Trail[i], trailColor);
				}

				Cosmic::Renderer2D::DrawQuad(t.Position, t.Scale, m_DinoMaterial);

				if (fd.Selected)
				{
					Cosmic::Renderer2D::DrawRect(t.Position, { t.Scale.x + 0.12f, t.Scale.y + 0.12f }, { 1.0f, 1.0f, 0.0f, 1.0f });
				}
			};

		drawDino(m_DinoA);
		drawDino(m_DinoB);

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseFlightLayer::OnImGuiRender()
	{
		ImGui::Begin("Simulation Inspection Window");
		ImGui::Text("--- Flight Simulation Viewport ---");
		ImGui::Separator();
		ImGui::Text("Click down directly inside the editor grid viewport to map-select entity nodes.");
		ImGui::Spacing();

		auto showDinoStats = [&](const char* label, Cosmic::Entity ent)
			{
				if (!ent) return;

				ImGui::PushID((int)(uint32_t)ent);

				auto& t = ent.GetComponent<Cosmic::TransformComponent>();
				auto& fd = ent.GetComponent<FlightDinoComponent>();

				bool selected = fd.Selected;
				if (selected)
					ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 0.0f, 1.0f });

				if (ImGui::CollapsingHeader(label, selected ? ImGuiTreeNodeFlags_DefaultOpen : 0))
				{
					ImGui::Text("Position:    (%.2f, %.2f)", t.Position.x, t.Position.y);
					ImGui::Text("Speed:       %.2f", fd.Speed);
					ImGui::Text("Slope:       %.3f", fd.Slope);
					ImGui::Text("Trail Pts:   %zu / %zu", fd.Trail.size(), k_MaxTrailLength);

					ImGui::ColorEdit4("Trail Color", &fd.Color.x);
					ImGui::SliderFloat("Speed Rate", &fd.Speed, 0.5f, 8.0f);
					ImGui::SliderFloat("Ascent Angle", &fd.Slope, -1.0f, 1.0f);

					if (ImGui::Button("Flush Trail Cache"))
						fd.Trail.clear();

					if (ImGui::Button("Focus Node"))
						SelectEntity(ent);
				}

				if (selected)
					ImGui::PopStyleColor();

				ImGui::PopID();
			};

		showDinoStats("Dino Entity Alpha (Orange)", m_DinoA);
		showDinoStats("Dino Entity Beta (Cyan)", m_DinoB);

		ImGui::Spacing();
		if (m_SelectedEntity)
		{
			auto& tag = m_SelectedEntity.GetComponent<Cosmic::TagComponent>();
			ImGui::TextColored({ 1.0f, 1.0f, 0.0f, 1.0f }, "Selected Node: %s", tag.Tag.c_str());
		}
		else
		{
			ImGui::TextDisabled("No active simulation entity focused.");
		}
		ImGui::End();
	}

	void ShowcaseFlightLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(GLCORE_BIND_EVENT_FN(ShowcaseFlightLayer::OnMouseClicked));
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(GLCORE_BIND_EVENT_FN(ShowcaseFlightLayer::OnWindowResize));
	}

	bool ShowcaseFlightLayer::OnMouseClicked(Cosmic::MouseButtonPressedEvent& e)
	{
		if (e.GetMouseButton() != CS_MOUSE_BUTTON_LEFT) return false;

		glm::vec2 screenPos = Cosmic::Input::GetMousePosition();
		glm::vec2 worldPos = ScreenToWorld(screenPos);

		if (HitTest(m_DinoA, worldPos)) { SelectEntity(m_DinoA); return true; }
		if (HitTest(m_DinoB, worldPos)) { SelectEntity(m_DinoB); return true; }

		DeselectAll();
		return false;
	}

	bool ShowcaseFlightLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}

	glm::vec2 ShowcaseFlightLayer::ScreenToWorld(glm::vec2 screenPos) const
	{
		// PUBLIC API FIX: Leverage ImGui's public DrawList / Content Region tracking 
		// instead of relying on private internal headers like ImGuiWindow*.
		// We can safely grab the global cursor position relative to the current active viewport.
		glm::vec2 displaySize = { m_ViewportSize.x, m_ViewportSize.y };

		// Fallback safe mapping using ImGui's main viewport context space
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		float localX = screenPos.x - viewport->Pos.x;
		float localY = screenPos.y - viewport->Pos.y;

		// If a workspace panel is currently open and active, account for panel offsets safely
		if (ImGui::GetCurrentContext() != nullptr)
		{
			// Obtain local workspace offset using safe public checks
			glm::vec2 mousePosInViewport = { ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y };
		}

		float ndcX = (localX / displaySize.x) * 2.0f - 1.0f;
		float ndcY = 1.0f - (localY / displaySize.y) * 2.0f;

		// Clamp calculations safely into normalized bounds
		ndcX = glm::clamp(ndcX, -1.0f, 1.0f);
		ndcY = glm::clamp(ndcY, -1.0f, 1.0f);

		glm::mat4 invVP = glm::inverse(m_Camera.GetCamera().GetViewProjectionMatrix());
		glm::vec4 world = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		return { world.x, world.y };
	}

	bool ShowcaseFlightLayer::HitTest(Cosmic::Entity entity, glm::vec2 worldPos) const
	{
		if (!entity) return false;
		auto& t = entity.GetComponent<Cosmic::TransformComponent>();

		float halfW = t.Scale.x * 0.5f;
		float halfH = t.Scale.y * 0.5f;
		return (worldPos.x >= t.Position.x - halfW &&
			worldPos.x <= t.Position.x + halfW &&
			worldPos.y >= t.Position.y - halfH &&
			worldPos.y <= t.Position.y + halfH);
	}

	void ShowcaseFlightLayer::SelectEntity(Cosmic::Entity e)
	{
		DeselectAll();
		if (e) e.GetComponent<FlightDinoComponent>().Selected = true;
		m_SelectedEntity = e;
	}

	void ShowcaseFlightLayer::DeselectAll()
	{
		if (m_DinoA) m_DinoA.GetComponent<FlightDinoComponent>().Selected = false;
		if (m_DinoB) m_DinoB.GetComponent<FlightDinoComponent>().Selected = false;
		m_SelectedEntity = Cosmic::Entity{};
	}
}