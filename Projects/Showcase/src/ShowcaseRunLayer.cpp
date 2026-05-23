#include "ShowcaseRunLayer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Showcase
{
	ShowcaseRunLayer::ShowcaseRunLayer(
		Cosmic::Ref<Cosmic::Scene> scene,
		Cosmic::Ref<Cosmic::Material> dinoMaterial)
		: Cosmic::Layer("ShowcaseRunLayer")
		, m_Scene(scene)
		, m_DinoMaterial(dinoMaterial)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	void ShowcaseRunLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(1.5f);
		m_Camera.SetZoomLimits(0.5f, 5.0f);
		m_Camera.SetTranslationSpeed(4.0f);

		m_DinoEntity = m_Scene->CreateEntity("RunnerDino");
		auto& t = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		t.Scale = { 0.45f, 0.45f };

		m_DinoEntity.AddComponent<RunnerDinoComponent>();
		Reset();
	}

	void ShowcaseRunLayer::OnDetach()
	{
		for (auto& obs : m_Obstacles)
		{
			// FIX: Use native Cosmic::Entity operator bool handle verification
			if (obs)
				m_Scene->DestroyEntity(obs);
		}
		m_Obstacles.clear();

		// FIX: Verify local dino handle integrity before destruction call
		if (m_DinoEntity)
			m_Scene->DestroyEntity(m_DinoEntity);
	}

	void ShowcaseRunLayer::Reset()
	{
		for (auto& obs : m_Obstacles)
		{
			// FIX: Verify handle integrity via operator bool
			if (obs)
				m_Scene->DestroyEntity(obs);
		}
		m_Obstacles.clear();

		auto& t = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		auto& d = m_DinoEntity.GetComponent<RunnerDinoComponent>();
		t.Position = { -1.5f, k_GroundY + 0.225f, 0.0f };

		float prevHigh = d.HighScore;
		d = RunnerDinoComponent{};
		d.HighScore = prevHigh;

		m_SpawnTimer = 0.0f;
		m_NextSpawnTime = 1.8f;
		m_GameOver = false;

		CS_INFO("ShowcaseRunLayer: Simulation state reset accomplished.");
	}

	void ShowcaseRunLayer::OnUpdate(float ts)
	{
		m_Camera.OnUpdate(ts);
		float fixedDt = ts > 0.0f ? ts : 0.001f;

		if (!m_GameOver)
		{
			auto& dinoT = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
			auto& dinoD = m_DinoEntity.GetComponent<RunnerDinoComponent>();

			dinoD.Score += fixedDt * 10.0f * dinoD.SpeedMultiplier;
			dinoD.SpeedMultiplier += fixedDt * 0.01f;
			dinoD.HighScore = std::max(dinoD.HighScore, dinoD.Score);

			if (!dinoD.IsGrounded)
			{
				dinoD.VelocityY += k_Gravity * fixedDt;
				dinoT.Position.y += dinoD.VelocityY * fixedDt;
			}

			float groundRestY = k_GroundY + dinoT.Scale.y * 0.5f;
			if (dinoT.Position.y <= groundRestY)
			{
				dinoT.Position.y = groundRestY;
				dinoD.VelocityY = 0.0f;
				dinoD.IsGrounded = true;
			}

			m_SpawnTimer += fixedDt;
			if (m_SpawnTimer >= m_NextSpawnTime)
			{
				m_SpawnTimer = 0.0f;

				std::uniform_real_distribution<float> heightDist(0.3f, 0.9f);
				std::uniform_real_distribution<float> gapDist(1.4f, 2.6f);
				float h = heightDist(m_Rng);

				auto obs = m_Scene->CreateEntity("Obstacle");
				auto& t = obs.GetComponent<Cosmic::TransformComponent>();
				t.Position = { 3.5f, k_GroundY + h * 0.5f, 0.0f };
				t.Scale = { 0.3f, h };

				auto& oc = obs.AddComponent<ObstacleComponent>();
				oc.Speed = 3.5f * dinoD.SpeedMultiplier;
				oc.Width = t.Scale.x;
				oc.Height = h;

				m_Obstacles.push_back(obs);
				m_NextSpawnTime = gapDist(m_Rng) / dinoD.SpeedMultiplier;
			}

			const float dinoHalfW = dinoT.Scale.x * 0.45f;
			const float dinoHalfH = dinoT.Scale.y * 0.45f;

			for (auto& obs : m_Obstacles)
			{
				auto& ot = obs.GetComponent<Cosmic::TransformComponent>();
				auto& oc = obs.GetComponent<ObstacleComponent>();
				ot.Position.x -= oc.Speed * fixedDt;

				float dx = std::abs(dinoT.Position.x - ot.Position.x);
				float dy = std::abs(dinoT.Position.y - ot.Position.y);
				if (dx < (dinoHalfW + oc.Width * 0.5f) && dy < (dinoHalfH + oc.Height * 0.5f))
				{
					m_GameOver = true;
					CS_INFO("ShowcaseRunLayer: Collision registered! Score: {0:.0f}", dinoD.Score);
				}
			}

			m_Obstacles.erase(
				std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
					[this](Cosmic::Entity ent) mutable
					{
						float x = ent.GetComponent<Cosmic::TransformComponent>().Position.x;
						if (x < -4.0f)
						{
							m_Scene->DestroyEntity(ent);
							return true;
						}
						return false;
					}),
				m_Obstacles.end());
		}

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		Cosmic::Renderer2D::DrawQuad({ 0.0f, k_GroundY - 0.05f, -0.1f }, { 20.0f, 0.12f }, { 0.35f, 0.35f, 0.38f, 1.0f });

		auto& dinoT = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		Cosmic::Renderer2D::DrawQuad(dinoT.Position, dinoT.Scale, m_DinoMaterial);

		for (auto& obs : m_Obstacles)
		{
			auto& ot = obs.GetComponent<Cosmic::TransformComponent>();
			Cosmic::Renderer2D::DrawQuad(ot.Position, ot.Scale, { 0.85f, 0.2f, 0.2f, 1.0f });
		}

		if (m_GameOver)
		{
			Cosmic::Renderer2D::DrawLine({ -10.0f, dinoT.Position.y, 0.0f }, { 10.0f, dinoT.Position.y, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
		}

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseRunLayer::OnImGuiRender()
	{
		auto& d = m_DinoEntity.GetComponent<RunnerDinoComponent>();

		ImGui::Begin("Simulation Inspection Window");
		ImGui::Text("--- Runner Simulation Panel ---");
		ImGui::Separator();

		ImGui::Text("Active Score:   %.0f", d.Score);
		ImGui::Text("Personal Best:  %.0f", d.HighScore);
		ImGui::Text("Game Speed:     %.2fx", d.SpeedMultiplier);
		ImGui::Text("Live Entities:  %zu active obstacles", m_Obstacles.size());
		ImGui::Spacing();

		if (m_GameOver)
		{
			ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "CRITICAL COLLISION: ENGINE GAME OVER");
			ImGui::Text("Press [Space / Up Arrow] or activate Reset below to cycle context.");
		}
		else
		{
			ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "Status: Runner System Nominal");
			ImGui::Text("Controls: Space/Up = Jump | R = Quick Reset");
		}

		ImGui::Spacing();
		if (ImGui::Button("Reset Game Run"))
			Reset();

		ImGui::End();
	}

	void ShowcaseRunLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::KeyPressedEvent>(GLCORE_BIND_EVENT_FN(ShowcaseRunLayer::OnKeyPressed));
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(GLCORE_BIND_EVENT_FN(ShowcaseRunLayer::OnWindowResize));
	}

	bool ShowcaseRunLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
	{
		if (e.GetRepeatCount() > 0) return false;

		if (e.GetKeyCode() == CS_KEY_SPACE || e.GetKeyCode() == CS_KEY_UP)
		{
			if (m_GameOver)
			{
				Reset();
				return true;
			}

			auto& d = m_DinoEntity.GetComponent<RunnerDinoComponent>();
			if (d.IsGrounded)
			{
				d.VelocityY = k_JumpV;
				d.IsGrounded = false;
				return true;
			}
		}

		if (e.GetKeyCode() == CS_KEY_R)
		{
			Reset();
			return true;
		}

		return false;
	}

	bool ShowcaseRunLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}