#include "TemplateParallelSimLayer.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace Workspace
{
	TemplateParallelSimLayer::TemplateParallelSimLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: Cosmic::Layer("Physics Simulation UI - Multithreading")
		, m_Scene(scene)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	void TemplateParallelSimLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(3.0f);
		m_Camera.SetZoomLimits(0.5f, 20.0f);
		m_Camera.SetManualMovementEnabled(true);

		// 1. Resolve custom shader asset via virtual file system
		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/ClientSpecularCircle.glsl");
		if (std::filesystem::exists(shaderPath))
		{
			m_SpecularCircleShader = Cosmic::Shader::Create(shaderPath);
			CS_INFO("TemplateParallelSimLayer: Loaded custom specular shader via VFS.");
		}
		else
		{
			CS_WARN("TemplateParallelSimLayer: Custom shader missing at '{0}'. Using engine fallback.", shaderPath);
			m_SpecularCircleShader = nullptr;
		}

		// 2. Allocate and register our Parallel system into the active engine scene lifecycle
		m_PhysicsSystem = &m_Scene->AddSystem<BallPhysicsSystem>();

		// Spawn initial elements
		SpawnBall({ -2.0f,  2.0f }, { 3.2f, -1.5f });
		SpawnBall({ 2.0f,  2.5f }, { -2.8f, -0.8f });
		SpawnBall({ -1.5f,  3.0f }, { 1.0f, -3.0f });
		SpawnBall({ 0.5f,  3.5f }, { -1.5f, -2.0f });
	}

	void TemplateParallelSimLayer::OnDetach()
	{
		ClearBalls();
		m_SpecularCircleShader.reset();
		m_PhysicsSystem = nullptr; // Cleaned up implicitly when m_Scene drops scope
		CS_INFO("TemplateParallelSimLayer: Detached.");
	}

	void TemplateParallelSimLayer::OnFixedUpdate(float dt)
	{
		if (dt <= 0.0f) return;

		++m_FixedTicks;

		// FIX: Hand off the fixed timestep to the scene so it can tick BallPhysicsSystem!
		if (m_Scene)
		{
			m_Scene->OnFixedUpdate(dt);
		}
	}

	void TemplateParallelSimLayer::OnUpdate(float ts)
	{
		// FIX: Hand off the variable frame time to the scene to update variable systems if any exist
		if (m_Scene)
		{
			m_Scene->OnUpdate(ts);
		}

		// Grab the active physics system safely
		BallPhysicsSystem* physicsSystem = m_Scene->GetSystem<BallPhysicsSystem>();

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

		// Use the retrieved instance safely
		float bx = physicsSystem ? physicsSystem->BoundsX : 5.0f;
		float by = physicsSystem ? physicsSystem->BoundsY : 4.0f;

		// Render environment backdrop geometry
		Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.3f }, { bx * 2.0f + 0.1f, by * 2.0f + 0.1f }, { 0.06f, 0.06f, 0.09f, 1.0f });
		Cosmic::Renderer2D::DrawCircle({ 0.0f, -by + 0.02f, -0.26f }, { bx * 2.0f, 0.8f }, { 0.0f, 0.0f, 0.0f, 0.35f }, 1.0f, 0.2f);
		Cosmic::Renderer2D::DrawRect({ 0.0f, 0.0f, -0.25f }, { bx * 2.0f + 0.05f, by * 2.0f + 0.05f }, { 0.3f, 0.6f, 0.9f, 0.8f });

		// Draw interior simulation grid guidelines
		{
			const glm::vec4 gc = { 0.12f, 0.12f, 0.18f, 1.0f };
			for (float x = -bx; x <= bx; x += 1.0f)
				Cosmic::Renderer2D::DrawLine({ x, -by, -0.2f }, { x, by, -0.2f }, gc);
			for (float y = -by; y <= by; y += 1.0f)
				Cosmic::Renderer2D::DrawLine({ -bx, y, -0.2f }, { bx, y, -0.2f }, gc);
		}

		// Pure Scene View Iteration: Thread-safe reading on main loop thread
		auto view = m_Scene->View<Cosmic::TransformComponent, BallComponent>();
		for (auto entity : view)
		{
			const auto& t = view.get<Cosmic::TransformComponent>(entity);
			const auto& b = view.get<BallComponent>(entity);

			Cosmic::Renderer2D::DrawCircle(
				t.Position,
				{ b.Radius * 2.0f, b.Radius * 2.0f },
				b.Color,
				1.0f, 0.02f,
				m_SpecularCircleShader // Custom pipeline verification
			);
		}

		Cosmic::Renderer2D::EndScene();
	}

	void TemplateParallelSimLayer::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Bottom");

		ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Layer: Parallel Physics Simulation");
		ImGui::Separator();

		BallPhysicsSystem* physicsSystem = m_Scene->GetSystem<BallPhysicsSystem>();

		auto view = m_Scene->View<BallComponent>();
		ImGui::Text("Active Balls:  %zu", view.size());
		ImGui::Text("Fixed Ticks:   %u", m_FixedTicks);
		ImGui::Spacing();

		if (physicsSystem && ImGui::CollapsingHeader("Simulation Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			// Modifying these now writes directly into the true polymorphic pointer processing jobs!
			ImGui::SliderFloat("Gravity", &physicsSystem->Gravity, -20.0f, 0.0f, "%.1f m/s²");
			ImGui::SliderFloat("Damping", &physicsSystem->Damping, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Bounds X", &physicsSystem->BoundsX, 2.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Bounds Y", &physicsSystem->BoundsY, 1.5f, 8.0f, "%.1f");
		}

		if (ImGui::CollapsingHeader("Spawn Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderInt("Count", &m_SpawnCount, 1, 50);

			if (ImGui::Button("Spawn Balls"))
			{
				float bx = physicsSystem ? physicsSystem->BoundsX : 5.0f;
				float by = physicsSystem ? physicsSystem->BoundsY : 4.0f;

				std::uniform_real_distribution<float> xDist(-bx * 0.8f, bx * 0.8f);
				std::uniform_real_distribution<float> vDist(-5.0f, 5.0f);
				std::uniform_real_distribution<float> rDist(0.15f, 0.4f);
				std::uniform_real_distribution<float> colDist(0.0f, 1.0f);

				for (int i = 0; i < m_SpawnCount; ++i)
				{
					float r = rDist(m_Rng);
					glm::vec2 pos = { xDist(m_Rng), by * 0.5f };
					glm::vec2 vel = { vDist(m_Rng), vDist(m_Rng) };
					SpawnBall(pos, vel);
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

	void TemplateParallelSimLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	void TemplateParallelSimLayer::SpawnBall(glm::vec2 position, glm::vec2 velocity)
	{
		std::uniform_real_distribution<float> rDist(0.18f, 0.32f);
		std::uniform_real_distribution<float> cDist(0.4f, 1.0f);

		Cosmic::Entity ent = m_Scene->CreateEntity("Ball");
		auto& t = ent.GetComponent<Cosmic::TransformComponent>();
		t.Position = { position.x, position.y, 0.0f };

		auto& b = ent.AddComponent<BallComponent>();
		b.Velocity = velocity;
		b.Radius = rDist(m_Rng);
		b.Mass = b.Radius * b.Radius * 3.14159f;
		b.Color = { cDist(m_Rng), cDist(m_Rng), cDist(m_Rng), 1.0f };

		float maxC = std::max({ b.Color.r, b.Color.g, b.Color.b });
		if (maxC < 0.4f) { b.Color.r += 0.4f; b.Color.g += 0.3f; b.Color.b += 0.5f; }
	}

	void TemplateParallelSimLayer::ClearBalls()
	{
		auto view = m_Scene->View<BallComponent>();

		// Collect matching entities to avoid mutation invalidation during direct iteration
		std::vector<entt::entity> targets(view.begin(), view.end());

		for (auto entity : targets)
		{
			if (m_Scene)
				m_Scene->DestroyEntity({ entity, m_Scene.get() });
		}

		m_FixedTicks = 0;
	}

	bool TemplateParallelSimLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}