#include "TemplateSimLayer.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <filesystem> // Just for path validation

namespace Workspace
{
	TemplateSimLayer::TemplateSimLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: Cosmic::Layer("Physics Simulation")
		, m_Scene(scene)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(3.0f);
		m_Camera.SetZoomLimits(0.5f, 20.0f);
		m_Camera.SetManualMovementEnabled(false);

		// ---------------------------------------------------------------------
		// VFS SHADER RESOLUTION & LOADING
		// ---------------------------------------------------------------------
		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/ClientSpecularCircle.glsl");
		if (std::filesystem::exists(shaderPath))
		{
			m_SpecularCircleShader = Cosmic::Shader::Create(shaderPath);
			CS_INFO("TemplateSimLayer: Successfully loaded custom specular shader via VFS.");
		}
		else
		{
			CS_WARN("TemplateSimLayer: Custom shader missing at '{0}'. Defaulting to engine fallback.", shaderPath);
			m_SpecularCircleShader = nullptr; // Explicit fallback
		}

		// Spawn an initial set of balls
		SpawnBall({ -2.0f,  2.0f }, { 3.2f, -1.5f });
		SpawnBall({ 2.0f,  2.5f }, { -2.8f, -0.8f });
		SpawnBall({ -1.5f,  3.0f }, { 1.0f, -3.0f });
		SpawnBall({ 0.5f,  3.5f }, { -1.5f, -2.0f });

		CS_INFO("TemplateSimLayer: Attached with {} balls.", m_Balls.size());
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnDetach()
	{
		ClearBalls();
		m_SpecularCircleShader.reset(); // Clean up GPU context pointer
		CS_INFO("TemplateSimLayer: Detached.");
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnFixedUpdate(float dt)
	{
		if (dt <= 0.0f) return;

		++m_FixedTicks;

		for (auto& ent : m_Balls)
		{
			if (!ent) continue;

			auto& t = ent.GetComponent<Cosmic::TransformComponent>();
			auto& b = ent.GetComponent<BallComponent>();

			b.Velocity.y += m_Gravity * dt;

			t.Position.x += b.Velocity.x * dt;
			t.Position.y += b.Velocity.y * dt;

			if (t.Position.x + b.Radius > m_BoundsX)
			{
				t.Position.x = m_BoundsX - b.Radius;
				b.Velocity.x = -b.Velocity.x * m_Damping;
			}
			else if (t.Position.x - b.Radius < -m_BoundsX)
			{
				t.Position.x = -m_BoundsX + b.Radius;
				b.Velocity.x = -b.Velocity.x * m_Damping;
			}

			if (t.Position.y - b.Radius < -m_BoundsY)
			{
				t.Position.y = -m_BoundsY + b.Radius;
				b.Velocity.y = -b.Velocity.y * m_Damping;
				b.Velocity.x *= m_Damping;
			}
			else if (t.Position.y + b.Radius > m_BoundsY)
			{
				t.Position.y = m_BoundsY - b.Radius;
				b.Velocity.y = -b.Velocity.y * m_Damping;
			}
		}
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnUpdate(float ts)
	{
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			m_Camera.OnResize(w, h);
		}

		m_Camera.OnUpdate(ts);

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		// Background arena
		Cosmic::Renderer2D::DrawQuad(
			{ 0.0f, 0.0f, -0.3f },
			{ m_BoundsX * 2.0f + 0.1f, m_BoundsY * 2.0f + 0.1f },
			{ 0.06f, 0.06f, 0.09f, 1.0f }
		);

		// Shared soft floor shadow quad (Left as nullptr so it uses the standard engine shader)
		Cosmic::Renderer2D::DrawCircle(
			{ 0.0f, -m_BoundsY + 0.02f, -0.26f },
			{ m_BoundsX * 2.0f, 0.8f },
			{ 0.0f, 0.0f, 0.0f, 0.35f },
			1.0f, 0.2f
		);

		// Arena border
		Cosmic::Renderer2D::DrawRect(
			{ 0.0f, 0.0f, -0.25f },
			{ m_BoundsX * 2.0f + 0.05f, m_BoundsY * 2.0f + 0.05f },
			{ 0.3f, 0.6f, 0.9f, 0.8f }
		);

		// Grid inside arena
		{
			const glm::vec4 gc = { 0.12f, 0.12f, 0.18f, 1.0f };
			for (float x = -m_BoundsX; x <= m_BoundsX; x += 1.0f)
				Cosmic::Renderer2D::DrawLine({ x, -m_BoundsY, -0.2f }, { x, m_BoundsY, -0.2f }, gc);
			for (float y = -m_BoundsY; y <= m_BoundsY; y += 1.0f)
				Cosmic::Renderer2D::DrawLine({ -m_BoundsX, y, -0.2f }, { m_BoundsX, y, -0.2f }, gc);
		}

		// Physics Rendering Loop
		for (auto& ent : m_Balls)
		{
			if (!ent) continue;

			const auto& t = ent.GetComponent<Cosmic::TransformComponent>();
			const auto& b = ent.GetComponent<BallComponent>();

			// -----------------------------------------------------------------
			// SPECULAR SHADER PIPELINE EXECUTION
			// -----------------------------------------------------------------
			// We pass our compiled m_SpecularCircleShader here. If it failed to load
			// and is nullptr, the engine gracefully utilizes the default unlit fallback shader.
			Cosmic::Renderer2D::DrawCircle(
				t.Position,
				{ b.Radius * 2.0f, b.Radius * 2.0f },
				b.Color,
				1.0f, 0.02f,
				m_SpecularCircleShader // <--- Pass custom pipeline here
			);
		}

		Cosmic::Renderer2D::EndScene();
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Bottom");

		ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Layer: Physics Simulation");
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::Text("Active Balls:  %zu", m_Balls.size());
		ImGui::Text("Fixed Ticks:   %u", m_FixedTicks);
		ImGui::Text("Layer Time:    %.2fs", GetLocalTime());
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Simulation Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Gravity", &m_Gravity, -20.0f, 0.0f, "%.1f m/s²");
			ImGui::SliderFloat("Damping", &m_Damping, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Bounds X", &m_BoundsX, 2.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("BoundsY", &m_BoundsY, 1.5f, 8.0f, "%.1f");
		}

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderInt("Count", &m_SpawnCount, 1, 20);

			if (ImGui::Button("Spawn Balls"))
			{
				std::uniform_real_distribution<float> xDist(-m_BoundsX * 0.8f, m_BoundsX * 0.8f);
				std::uniform_real_distribution<float> vDist(-5.0f, 5.0f);
				std::uniform_real_distribution<float> rDist(0.15f, 0.4f);
				std::uniform_real_distribution<float> colDist(0.0f, 1.0f);

				for (int i = 0; i < m_SpawnCount; ++i)
				{
					float r = rDist(m_Rng);
					glm::vec2 pos = { xDist(m_Rng), m_BoundsY * 0.5f };
					glm::vec2 vel = { vDist(m_Rng), vDist(m_Rng) };

					auto& ball = m_Balls.emplace_back(m_Scene->CreateEntity("Ball"));
					auto& t = ball.GetComponent<Cosmic::TransformComponent>();
					t.Position = { pos.x, pos.y, 0.0f };

					auto& b = ball.AddComponent<BallComponent>();
					b.Velocity = vel;
					b.Radius = r;
					b.Mass = r * r * 3.14159f;
					b.Color = { colDist(m_Rng), colDist(m_Rng), colDist(m_Rng), 1.0f };

					float maxC = std::max({ b.Color.r, b.Color.g, b.Color.b });
					if (maxC < 0.4f) { b.Color.r += 0.4f; b.Color.g += 0.3f; b.Color.b += 0.5f; }
				}
			}

			ImGui::SameLine();
			if (ImGui::Button("Clear All"))
				ClearBalls();

			if (ImGui::Button("Reset Counters"))
				m_FixedTicks = 0;
		}

		ImGui::Spacing();
		ImGui::Separator();
		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Quads:      %u", stats.QuadCount);

		ImGui::End();

		Cosmic::Renderer2D::ResetStats();
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::SpawnBall(glm::vec2 position, glm::vec2 velocity)
	{
		std::uniform_real_distribution<float> rDist(0.18f, 0.32f);
		std::uniform_real_distribution<float> cDist(0.4f, 1.0f);

		auto ent = m_Scene->CreateEntity("Ball");
		auto& t = ent.GetComponent<Cosmic::TransformComponent>();
		t.Position = { position.x, position.y, 0.0f };

		auto& b = ent.AddComponent<BallComponent>();
		b.Velocity = velocity;
		b.Radius = rDist(m_Rng);
		b.Mass = b.Radius * b.Radius * 3.14159f;
		b.Color = { cDist(m_Rng), cDist(m_Rng), cDist(m_Rng), 1.0f };

		m_Balls.push_back(ent);
	}

	// -------------------------------------------------------------------------
	void TemplateSimLayer::ClearBalls()
	{
		for (auto& ent : m_Balls)
		{
			if (ent && m_Scene)
				m_Scene->DestroyEntity(ent);
		}
		m_Balls.clear();
		m_FixedTicks = 0;
	}

	// -------------------------------------------------------------------------
	bool TemplateSimLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}