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

		// WASD/Arrow panning updates without breaking smooth scroll-wheel zooming metrics.
		m_Camera.SetManualMovementEnabled(false);

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
			if (obs)
				m_Scene->DestroyEntity(obs);
		}
		m_Obstacles.clear();

		if (m_DinoEntity)
			m_Scene->DestroyEntity(m_DinoEntity);
	}

	void ShowcaseRunLayer::Reset()
	{
		for (auto& obs : m_Obstacles)
		{
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

	void ShowcaseRunLayer::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_GameOver) return;

		// FIX: Handle timeline scales less than or equal to zero safely
		if (deltaFixedTime == 0.0f) return; // Paused -> Nothing to simulate

		if (deltaFixedTime < 0.0f)
		{
			// Simple Rewind Behavior fallback: Run the matrix operations opposite positions
			// (Note: For flawless physics rewinding, a structural state-history buffer is recommended)
			auto& dinoT = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
			auto& dinoD = m_DinoEntity.GetComponent<RunnerDinoComponent>();

			for (auto& obs : m_Obstacles)
			{
				auto& ot = obs.GetComponent<Cosmic::TransformComponent>();
				auto& oc = obs.GetComponent<ObstacleComponent>();
				ot.Position.x += oc.Speed * std::abs(deltaFixedTime); // Push obstacles backward out of view
			}
			return;
		}

		auto& dinoT = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		auto& dinoD = m_DinoEntity.GetComponent<RunnerDinoComponent>();

		// 1. Progress score tracking metrics (Game Speed scales difficulty smoothly automatically)
		dinoD.Score += deltaFixedTime * 10.0f * dinoD.SpeedMultiplier;
		dinoD.SpeedMultiplier += deltaFixedTime * 0.01f;
		dinoD.HighScore = std::max(dinoD.HighScore, dinoD.Score);

		// 2. Handle kinematic physics integrations
		if (!dinoD.IsGrounded)
		{
			dinoD.VelocityY += k_Gravity * deltaFixedTime;
			dinoT.Position.y += dinoD.VelocityY * deltaFixedTime;
		}

		float groundRestY = k_GroundY + dinoT.Scale.y * 0.5f;
		if (dinoT.Position.y <= groundRestY)
		{
			dinoT.Position.y = groundRestY;
			dinoD.VelocityY = 0.0f;
			dinoD.IsGrounded = true;
		}

		// 3. Procedural entity generation manager
		m_SpawnTimer += deltaFixedTime;
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
			oc.Speed = m_BaseObstacleSpeed * dinoD.SpeedMultiplier; // Multiplied by pure game difficulty metrics
			oc.Width = t.Scale.x;
			oc.Height = h;

			m_Obstacles.push_back(obs);
			m_NextSpawnTime = gapDist(m_Rng) / dinoD.SpeedMultiplier;
		}

		// 4. Evaluate collision bounds state transformations
		const float dinoHalfW = dinoT.Scale.x * 0.45f;
		const float dinoHalfH = dinoT.Scale.y * 0.45f;

		for (auto& obs : m_Obstacles)
		{
			auto& ot = obs.GetComponent<Cosmic::TransformComponent>();
			auto& oc = obs.GetComponent<ObstacleComponent>();
			ot.Position.x -= oc.Speed * deltaFixedTime;

			float dx = std::abs(dinoT.Position.x - ot.Position.x);
			float dy = std::abs(dinoT.Position.y - ot.Position.y);
			if (dx < (dinoHalfW + oc.Width * 0.5f) && dy < (dinoHalfH + oc.Height * 0.5f))
			{
				m_GameOver = true;
				CS_INFO("ShowcaseRunLayer: Collision registered! Score: {0:.0f}", dinoD.Score);
			}
		}

		// 5. Garbage collect off-screen obstacle instances
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

	void ShowcaseRunLayer::OnUpdate(float ts)
	{
		// --- ARCHITECTURAL FIX: Dynamic Canvas Layout Synchronization ---
		// Fetch active resolution properties directly from the host application's 
		// primary framebuffers. This guarantees that orthographic view matrix bounds
		// never become stretched, corrupted, or uninitialized during initial load frames.
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float activeWidth = static_cast<float>(fb->GetWidth());
		float activeHeight = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != activeWidth || m_ViewportSize.y != activeHeight)
		{
			m_ViewportSize = { activeWidth, activeHeight };
			m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		}
		// -----------------------------------------------------------------

		// Step smooth camera zoom interpolation parameters forward
		m_Camera.OnUpdate(ts);

		// --- CAMERA FOCUS TRACKING OVERRIDE ---
		// Update the view matrix to automatically track the flame runner runner dino.
		if (m_DinoEntity)
		{
			auto& dinoTransform = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
			glm::vec3 cameraTarget = m_Camera.GetPosition();

			// Center tracking directly onto player positions.
			// (Tip: Add + 1.0f to cameraTarget.x if you want the dino positioned further left for framing oncoming obstacles)
			cameraTarget.x = dinoTransform.Position.x;
			cameraTarget.y = dinoTransform.Position.y;

			m_Camera.SetPosition(cameraTarget);
		}
		// -----------------------------------------------------------------

		// TIMELINE UPDATE FIX: Use this layer instance's local time tracking context
		if (m_DinoMaterial)
		{
			m_DinoMaterial->Set("u_Time", GetLocalTime());
		}

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		// Draw world layout base floor
		Cosmic::Renderer2D::DrawQuad({ 0.0f, k_GroundY - 0.05f, -0.1f }, { 20.0f, 0.12f }, { 0.35f, 0.35f, 0.38f, 1.0f });

		// Draw player node
		auto& dinoT = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		Cosmic::Renderer2D::DrawQuad(dinoT.Position, dinoT.Scale, m_DinoMaterial);

		// Draw procedural obstacle array nodes
		for (auto& obs : m_Obstacles)
		{
			auto& ot = obs.GetComponent<Cosmic::TransformComponent>();
			Cosmic::Renderer2D::DrawQuad(ot.Position, ot.Scale, { 0.85f, 0.2f, 0.2f, 1.0f });
		}

		// Draw debug fail line if collision state flags are tripped
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
		ImGui::Text("Game Difficulty: %.2fx", d.SpeedMultiplier);
		ImGui::Text("Live Entities:  %zu active obstacles", m_Obstacles.size());
		ImGui::Spacing();

		if (m_GameOver)
		{
			ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "CRITICAL COLLISION: ENGINE GAME OVER");
		}
		else
		{
			ImGui::TextColored({ 0.3f, 1.0f, 0.3f, 1.0f }, "Status: Runner System Nominal");
		}

		ImGui::Separator();
		ImGui::Spacing();

		// Completely customized run-logic adjustment slider
		if (ImGui::CollapsingHeader("Runner Settings", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (ImGui::SliderFloat("Base Obstacle Velocity", &m_BaseObstacleSpeed, 1.0f, 12.0f, "%.1f m/s"))
			{
				for (auto& obs : m_Obstacles)
				{
					if (obs && obs.HasComponent<ObstacleComponent>())
					{
						obs.GetComponent<ObstacleComponent>().Speed = m_BaseObstacleSpeed * d.SpeedMultiplier;
					}
				}
			}
		}

		ImGui::Spacing();
		if (ImGui::Button("Reset Game Run"))
			Reset();

		ImGui::End();
	}

	void ShowcaseRunLayer::OnEvent(Cosmic::Event& e)
	{
		// Pass down events uniformly into the camera controller matrix pipeline
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);

		// MODERN C++ LAMBDA REFACTOR: Eliminates legacy template binding macros
		dispatcher.Dispatch<Cosmic::KeyPressedEvent>([this](Cosmic::KeyPressedEvent& event) { return OnKeyPressed(event); });
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>([this](Cosmic::WindowResizeEvent& event) { return OnWindowResize(event); });
	}

	bool ShowcaseRunLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
	{
		if (e.GetRepeatCount() > 0) return false;

		// TIMELINE UPDATE FIX: Map checking behavior to this layer's clock context speed scale
		if (GetTimeScale() <= 0.0f)
		{
			// Allow pressing R or SPACE to reset a Game Over screen even while paused
			if (m_GameOver && (e.GetKeyCode() == CS_KEY_SPACE || e.GetKeyCode() == CS_KEY_UP || e.GetKeyCode() == CS_KEY_R))
			{
				Reset();
				return true;
			}
			return false;
		}

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