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

		// 1. Attempt to load the custom specular circle shader via VFS.
		//    Falls back gracefully to the engine's built-in circle shader if missing.
		std::string shaderPath = Cosmic::FileSystem::Resolve("project://shaders/ClientSpecularCircleInstance.glsl");
		if (std::filesystem::exists(shaderPath))
		{
			m_SpecularCircleShader = Cosmic::Shader::Create(shaderPath);
			CS_INFO("TemplateParallelSimLayer: Loaded custom instanced specular shader via VFS.");
		}
		else
		{
			CS_WARN("TemplateParallelSimLayer: Custom shader missing at '{0}'. Using engine fallback.", shaderPath);
			m_SpecularCircleShader = nullptr;
		}

		// 2. Register BallPhysicsSystem into the scene.
		//    The scene owns the system; we borrow a raw pointer for ImGui parameter access.
		m_PhysicsSystem = &m_Scene->AddSystem<BallPhysicsSystem>();

		// 3. Spawn a small initial set of balls so there is something to look at on attach.
		SpawnBall({ -2.0f,  2.0f }, { 3.2f, -1.5f });
		SpawnBall({ 2.0f,  2.5f }, { -2.8f, -0.8f });
		SpawnBall({ -1.5f,  3.0f }, { 1.0f, -3.0f });
		SpawnBall({ 0.5f,  3.5f }, { -1.5f, -2.0f });

		CS_INFO("TemplateParallelSimLayer: Attached. Initial ball count: 4.");
	}

	void TemplateParallelSimLayer::OnDetach()
	{
		ClearBalls();
		m_SpecularCircleShader.reset();

		// The physics system is owned by the scene and will be cleaned up when
		// the scene drops scope. We just null our borrow pointer.
		m_PhysicsSystem = nullptr;

		CS_INFO("TemplateParallelSimLayer: Detached.");
	}

	// =========================================================================
	// OnFixedUpdate
	// =========================================================================
	// The scene's parallel system hooks (OnFixedPrepare → OnFixedParallelExecute
	// → OnFixedMerge) are driven by Scene::OnFixedUpdate. This layer calls that
	// once per fixed tick so the BallPhysicsSystem runs its full pipeline.
	// The layer itself does NOT manipulate PhysicsBody state directly.
	// =========================================================================
	void TemplateParallelSimLayer::OnFixedUpdate(float dt)
	{
		if (dt <= 0.0f) return; // Guard: pause / rewind — freeze simulation

		++m_FixedTicks;
		m_Scene->OnFixedUpdate(dt);
	}

	// =========================================================================
	// OnUpdate
	// =========================================================================
	// Variable-rate rendering pass. Reads TransformComponent.Position and
	// BallComponent.Color / Radius to draw circles. TransformComponent has
	// already been synced from PhysicsBody.Position by the merge pass, so
	// the render loop is completely decoupled from simulation internals.
	// =========================================================================
	void TemplateParallelSimLayer::OnUpdate(float ts)
	{
		// 1. Sync viewport from the active framebuffer.
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			m_Camera.OnResize(w, h);
		}

		m_Camera.OnUpdate(ts);

		// 2. Drive Scene::OnUpdate for variable-rate systems (none here currently,
		//    but keeps the call chain consistent with the engine convention).
		m_Scene->OnUpdate(ts);

		// 3. Retrieve current simulation bounds for arena geometry.
		const float bx = m_PhysicsSystem ? m_PhysicsSystem->BoundsX : 6.0f;
		const float by = m_PhysicsSystem ? m_PhysicsSystem->BoundsY : 4.0f;

		// 4. Render.
		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		// --- Arena backdrop ---
		Cosmic::Renderer2D::DrawQuad(
			{ 0.0f, 0.0f, -0.3f },
			{ bx * 2.0f + 0.1f, by * 2.0f + 0.1f },
			{ 0.06f, 0.06f, 0.09f, 1.0f }
		);

		// --- Soft floor shadow ---
		Cosmic::Renderer2D::DrawCircle(
			{ 0.0f, -by + 0.02f, -0.26f },
			{ bx * 2.0f, 0.8f },
			{ 0.0f, 0.0f, 0.0f, 0.35f },
			1.0f, 0.2f
		);

		// --- Arena border ---
		Cosmic::Renderer2D::DrawRect(
			{ 0.0f, 0.0f, -0.25f },
			{ bx * 2.0f + 0.05f, by * 2.0f + 0.05f },
			{ 0.3f, 0.6f, 0.9f, 0.8f }
		);

		// --- Interior reference grid ---
		{
			const glm::vec4 gc = { 0.12f, 0.12f, 0.18f, 1.0f };
			for (float x = -bx; x <= bx; x += 1.0f)
				Cosmic::Renderer2D::DrawLine({ x, -by, -0.2f }, { x,  by, -0.2f }, gc);
			for (float y = -by; y <= by; y += 1.0f)
				Cosmic::Renderer2D::DrawLine({ -bx, y, -0.2f }, { bx,  y, -0.2f }, gc);
		}


		/// =========================================================================
		// --- Ball entities Rendering Path ---
		// =========================================================================
		auto view = m_Scene->View<Cosmic::TransformComponent, BallComponent>();

		if (view.begin() != view.end())
		{
			// Allocate a contiguous block of stack/heap memory to hold our instances.
			std::vector<Cosmic::Renderer2D::InstanceCircleData> instanceBuffer;

			// size_hint() returns the size of the smallest component pool in the view,
			// which is a perfect safe upper bound for reserving vector capacity!
			instanceBuffer.reserve(view.size_hint());

			// 2. Linear iteration pass to pack entity component fields straight into our data structs
			for (auto entity : view)
			{
				const auto& t = view.get<Cosmic::TransformComponent>(entity);
				const auto& b = view.get<BallComponent>(entity);

				// Range protection: normalize ball color if components fall into a 0-255 boundary format
				glm::vec4 ballColor = b.Color;
				if (ballColor.r > 1.0f || ballColor.g > 1.0f || ballColor.b > 1.0f || ballColor.a > 1.0f)
				{
					ballColor /= 255.0f;
				}

				// Push raw data parameters without performing any matrix mathematics on the CPU
				instanceBuffer.push_back({
					t.Position,                                 // Center position (X, Y, Z)
					glm::vec2(b.Radius * 2.0f, b.Radius * 2.0f), // Sizing bounds (Width, Height dimensions)
					ballColor,                                  // Normalized color properties
					1.0f,                                       // Thickness (1.0f = solid filled circle)
					0.02f                                       // Fade (Edge anti-aliasing profile value)
					});
			}

			// 3. Blast the entire vector of circles across the PCIe bus in a single call!
			// Only draw if we actually packed elements into the buffer.
			if (!instanceBuffer.empty())
			{
				Cosmic::Renderer2D::DrawInstancedCircles(
					instanceBuffer.data(),
					static_cast<uint32_t>(instanceBuffer.size()),
					m_SpecularCircleShader
				);
			}
		}

		Cosmic::Renderer2D::EndScene();
	}

	// =========================================================================
	// OnImGuiRender
	// =========================================================================
	void TemplateParallelSimLayer::OnImGuiRender()
	{
		ImGui::Begin("Project Inspector Bottom");

		ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Layer: Parallel Physics (Approach B)");
		ImGui::Separator();
		ImGui::Spacing();

		// --- Telemetry ---
		auto view = m_Scene->View<BallComponent>();
		ImGui::Text("Active Bodies:   %zu", view.size());
		ImGui::Text("Fixed Ticks:     %u", m_FixedTicks);
		ImGui::Spacing();

		// --- NEW: Parallel System Profiler ---
		if (m_PhysicsSystem && ImGui::CollapsingHeader("Parallel System Profiler", ImGuiTreeNodeFlags_DefaultOpen))
		{
			float prep = m_PhysicsSystem->TimePrepareMs;
			float exec = m_PhysicsSystem->TimeExecuteMs;
			float merge = m_PhysicsSystem->TimeMergeMs;
			float total = prep + exec + merge;

			ImGui::Text("Total Pipeline Time: %.3f ms", total);
			ImGui::Spacing();

			if (total > 0.0f)
			{
				char label[64];

				// 1. Prepare Pass Progress Bar (Single Threaded Copy)
				float prepPct = prep / total;
				sprintf_s(label, "Prepare: %.3f ms (%.1f%%)", prep, prepPct * 100.0f);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.9f, 0.4f, 0.4f, 1.0f)); // Soft Red
				ImGui::ProgressBar(prepPct, ImVec2(-1, 0), label);
				ImGui::PopStyleColor();

				// 2. Execute Pass Progress Bar (Multithreaded Compute)
				float execPct = exec / total;
				sprintf_s(label, "Execute: %.3f ms (%.1f%%)", exec, execPct * 100.0f);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.8f, 0.4f, 1.0f)); // Soft Green
				ImGui::ProgressBar(execPct, ImVec2(-1, 0), label);
				ImGui::PopStyleColor();

				// 3. Merge Pass Progress Bar (Single Threaded Sync)
				float mergePct = merge / total;
				sprintf_s(label, "Merge:   %.3f ms (%.1f%%)", merge, mergePct * 100.0f);
				ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.6f, 0.9f, 1.0f)); // Soft Blue
				ImGui::ProgressBar(mergePct, ImVec2(-1, 0), label);
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextDisabled("No active physics steps recorded yet.");
			}
			ImGui::Spacing();
		}


		// --- Simulation parameters (wired directly into BallPhysicsSystem) ---
		if (m_PhysicsSystem && ImGui::CollapsingHeader("Simulation Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderFloat("Gravity", &m_PhysicsSystem->Gravity, -20.0f, 0.0f, "%.1f m/s²");
			ImGui::SliderFloat("Damping", &m_PhysicsSystem->Damping, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Bounds X", &m_PhysicsSystem->BoundsX, 2.0f, 10.0f, "%.1f");
			ImGui::SliderFloat("Bounds Y", &m_PhysicsSystem->BoundsY, 1.5f, 8.0f, "%.1f");

			ImGui::Spacing();
			ImGui::TextDisabled("Workers: %u  |  Cores: %u",
				Cosmic::JobSystem::Get().GetWorkerCount(),
				Cosmic::JobSystem::Get().GetCoreCount());
		}

		// --- Spawn controls ---
		if (ImGui::CollapsingHeader("Spawn Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::SliderInt("Spawn Count", &m_SpawnCount, 1, 500);

			if (ImGui::Button("Spawn Balls"))
			{
				const float bx = m_PhysicsSystem ? m_PhysicsSystem->BoundsX : 6.0f;
				const float by = m_PhysicsSystem ? m_PhysicsSystem->BoundsY : 4.0f;

				std::uniform_real_distribution<float> xDist(-bx * 0.8f, bx * 0.8f);
				std::uniform_real_distribution<float> vyDist(-3.0f, -0.5f); // Bias downward
				std::uniform_real_distribution<float> vxDist(-4.0f, 4.0f);
				std::uniform_real_distribution<float> rDist(0.15f, 0.38f);
				std::uniform_real_distribution<float> cDist(0.4f, 1.0f);

				for (int i = 0; i < m_SpawnCount; ++i)
				{
					const float r = rDist(m_Rng);
					SpawnBall(
						{ xDist(m_Rng), by * 0.65f },
						{ vxDist(m_Rng), vyDist(m_Rng) },
						r,
						{ cDist(m_Rng), cDist(m_Rng), cDist(m_Rng), 1.0f }
					);
				}
			}

			ImGui::SameLine();

			if (ImGui::Button("Clear All"))
				ClearBalls();

			ImGui::SameLine();

			if (ImGui::Button("Reset Counters"))
				m_FixedTicks = 0;
		}

		// --- Renderer stats ---
		ImGui::Spacing();
		ImGui::Separator();
		const auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Quads:      %u", stats.QuadCount);
		ImGui::Text("Vertices:   %u", stats.GetTotalVertexCount());

		ImGui::End();

		Cosmic::Renderer2D::ResetStats();
	}

	// =========================================================================
	// OnEvent
	// =========================================================================
	void TemplateParallelSimLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	// =========================================================================
	// Private helpers
	// =========================================================================

	void TemplateParallelSimLayer::SpawnBall(glm::vec2 position, glm::vec2 velocity,
		float radius, glm::vec4 color)
	{
		Cosmic::Entity ent = m_Scene->CreateEntity("Ball");

		// --- TransformComponent (already added by CreateEntity) ---
		auto& t = ent.GetComponent<Cosmic::TransformComponent>();
		t.Position = { position.x, position.y, 0.0f };

		// --- BallComponent: visual/identity data for the render pass ---
		auto& b = ent.AddComponent<BallComponent>();
		b.Radius = radius;
		b.Mass = radius * radius * 3.14159f; // proportional to area
		b.Color = color;

		// Ensure the color is at least minimally visible.
		const float maxC = std::max({ b.Color.r, b.Color.g, b.Color.b });
		if (maxC < 0.35f)
		{
			b.Color.r += 0.4f;
			b.Color.g += 0.3f;
			b.Color.b += 0.5f;
		}

		// --- PhysicsBody: dedicated simulation component (Approach B) ---
		// Seed position from the desired spawn location so the physics system
		// sees the correct starting state on its first Prepare pass.
		auto& body = ent.AddComponent<PhysicsBody>();
		body.Position = position;
		body.Velocity = velocity;
		body.Radius = radius;
		body.Mass = b.Mass;
		body.Restitution = 1.0f;
		body.LinearDrag = 1.0f;
	}

	void TemplateParallelSimLayer::ClearBalls()
	{
		// Collect matching entities first to avoid invalidating the view iterator
		// during destruction.
		auto view = m_Scene->View<BallComponent>();
		std::vector<entt::entity> targets(view.begin(), view.end());

		for (auto entity : targets)
		{
			m_Scene->DestroyEntity({ entity, m_Scene.get() });
		}

		m_FixedTicks = 0;
		CS_INFO("TemplateParallelSimLayer: Cleared all ball entities.");
	}

	bool TemplateParallelSimLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = {
			static_cast<float>(e.GetWidth()),
			static_cast<float>(e.GetHeight())
		};
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false; // Allow the event to propagate to other systems
	}

} // namespace Workspace