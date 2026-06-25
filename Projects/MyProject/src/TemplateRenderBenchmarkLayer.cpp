#include "TemplateRenderBenchmarkLayer.h"

#include <imgui.h>
#include <implot.h>
#include <algorithm>
#include <cmath>

namespace Workspace
{
	// =========================================================================
	// Constructor
	// =========================================================================
	TemplateRenderBenchmarkLayer::TemplateRenderBenchmarkLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: Cosmic::Layer("Render Benchmark Layer")
		, m_Scene(scene)
		, m_Camera(1280.0f / 720.0f, false)
	{
	}

	// =========================================================================
	// OnAttach
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnAttach()
	{
		m_Camera.SetZoomLevel(4.0f);
		m_Camera.SetZoomLimits(0.5f, 25.0f);
		m_Camera.SetManualMovementEnabled(true);

		m_LastFrameTime = Clock::now();

		// Seed with a small default crowd so the scene isn't empty on first load.
		SpawnBatch(24);

		CS_INFO("TemplateRenderBenchmarkLayer: Attached. Workers available: {0}",
			Cosmic::JobSystem::Get().GetWorkerCount());
	}

	// =========================================================================
	// OnDetach
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnDetach()
	{
		ClearBalls();
		CS_INFO("TemplateRenderBenchmarkLayer: Detached.");
	}

	// =========================================================================
	// OnFixedUpdate
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnFixedUpdate(float dt)
	{
		if (dt <= 0.0f) return;
		++m_FixedTicks;

		auto t0 = Clock::now();

		if (m_PhysicsMode == PhysicsMode::MultiThreaded)
			PhysicsMultiThreaded(dt);
		else
			PhysicsSingleThreaded(dt);

		m_LastPhysicsMs = ElapsedMs(t0, Clock::now());
	}

	// =========================================================================
	// OnUpdate
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnUpdate(float ts)
	{
		// --- Frame time ---
		auto now = Clock::now();
		m_FrameTimeMs = ElapsedMs(m_LastFrameTime, now);
		m_LastFrameTime = now;

		// --- Viewport sync ---
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());
		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			m_Camera.OnResize(w, h);
		}

		m_Camera.OnUpdate(ts);

		// --- Render ---
		auto t0 = Clock::now();

		Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());

		RenderArena();

		if (m_RenderMode == RenderMode::Instanced)
			RenderInstanced();
		else
			RenderBatched();

		Cosmic::Renderer2D::EndScene();

		m_LastRenderMs = ElapsedMs(t0, Clock::now());
	}

	// =========================================================================
	// OnImGuiRender
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnImGuiRender()
	{

		ImGui::Begin("Project Inspector Mid");

		ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Render Benchmark Layer");
		ImGui::Separator();
		ImGui::Spacing();

		ImGuiModePanel();
		ImGui::Spacing();

		ImGuiSpawnPanel();
		ImGui::Spacing();

		ImGuiPhysicsPanel();
		ImGui::Spacing();

		ImGuiPerfPanel();

		ImGui::End();

		Cosmic::Renderer2D::ResetStats();
	}

	// =========================================================================
	// OnEvent
	// =========================================================================
	void TemplateRenderBenchmarkLayer::OnEvent(Cosmic::Event& e)
	{
		m_Camera.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	// =========================================================================
	// Physics — Single-threaded
	// =========================================================================
	void TemplateRenderBenchmarkLayer::PhysicsSingleThreaded(float dt)
	{
		// Reuse the same double-buffer storage so timings stay comparable.
		// Prepare: snapshot EnTT components into the buffer.
		auto& reg = m_Scene->GetRegistry();
		auto group = reg.group<BenchmarkPhysicsBody>(entt::get<Cosmic::TransformComponent>);
		const size_t count = group.size();

		if (m_PhysicsBuffer.Count() != count)
		{
			m_PhysicsBuffer.Resize(count);
			m_EntitySlots.clear();
			m_EntitySlots.reserve(count);
		}

		m_EntitySlots.clear();
		size_t slot = 0;
		for (auto entity : group)
		{
			m_PhysicsBuffer.WriteAt(slot) = group.get<BenchmarkPhysicsBody>(entity);
			m_EntitySlots.push_back(entity);
			++slot;
		}
		m_PhysicsBuffer.Swap(); // promote write → read

		// Execute: integrate on main thread, no parallelism.
		const BenchmarkPhysicsBody* src = m_PhysicsBuffer.GetReadBuffer();
		BenchmarkPhysicsBody* dst = m_PhysicsBuffer.GetWriteBuffer();

		const float grav = m_Gravity;
		const float damp = m_Damping;
		const float bx = m_BoundsX;
		const float by = m_BoundsY;

		for (size_t i = 0; i < count; ++i)
			IntegrateBody(src[i], dst[i], dt, grav, damp, bx, by);

		m_PhysicsBuffer.Swap(); // promote results

		// Merge: write back to EnTT TransformComponent.
		const BenchmarkPhysicsBody* result = m_PhysicsBuffer.GetReadBuffer();
		for (size_t i = 0; i < m_EntitySlots.size(); ++i)
		{
			auto& t = reg.get<Cosmic::TransformComponent>(m_EntitySlots[i]);
			t.Position = { result[i].Position.x, result[i].Position.y, t.Position.z };

			// Also keep PhysicsBody in sync so Prepare is consistent next tick.
			auto& body = reg.get<BenchmarkPhysicsBody>(m_EntitySlots[i]);
			body = result[i];
		}
	}

	// =========================================================================
	// Physics — Multi-threaded
	// =========================================================================
	void TemplateRenderBenchmarkLayer::PhysicsMultiThreaded(float dt)
	{
		auto& reg = m_Scene->GetRegistry();

		// ---------------------------------------------------------------
		// PASS B — Prepare  (main thread)
		// ---------------------------------------------------------------
		auto prepStart = Clock::now();

		auto group = reg.group<BenchmarkPhysicsBody>(entt::get<Cosmic::TransformComponent>);
		const size_t count = group.size();

		if (m_PhysicsBuffer.Count() != count)
		{
			m_PhysicsBuffer.Resize(count);
			m_EntitySlots.clear();
			m_EntitySlots.reserve(count);
		}

		m_EntitySlots.clear();
		size_t slot = 0;
		for (auto entity : group)
		{
			m_PhysicsBuffer.WriteAt(slot) = group.get<BenchmarkPhysicsBody>(entity);
			m_EntitySlots.push_back(entity);
			++slot;
		}
		m_PhysicsBuffer.Swap(); // promote write → read

		m_PrepareMs = ElapsedMs(prepStart, Clock::now());

		// ---------------------------------------------------------------
		// PASS C — Parallel Execute  (worker threads)
		// ---------------------------------------------------------------
		auto execStart = Clock::now();

		const BenchmarkPhysicsBody* src = m_PhysicsBuffer.GetReadBuffer();
		BenchmarkPhysicsBody* dst = m_PhysicsBuffer.GetWriteBuffer();

		// Capture simulation parameters by value — safe for cross-thread capture.
		const float grav = m_Gravity;
		const float damp = m_Damping;
		const float bx = m_BoundsX;
		const float by = m_BoundsY;

		// ParallelFor internally calls WaitIdle before returning.
		// Each worker receives a contiguous sub-range [begin, end).
		Cosmic::ParallelFor(count,
			[src, dst, grav, damp, bx, by, dt](size_t begin, size_t end)
			{
				for (size_t i = begin; i < end; ++i)
					IntegrateBody(src[i], dst[i], dt, grav, damp, bx, by);
			});

		// Workers are done — safe to swap.
		m_PhysicsBuffer.Swap();

		m_ExecuteMs = ElapsedMs(execStart, Clock::now());

		// ---------------------------------------------------------------
		// PASS D — Merge  (main thread)
		// ---------------------------------------------------------------
		auto mergeStart = Clock::now();

		const BenchmarkPhysicsBody* result = m_PhysicsBuffer.GetReadBuffer();
		for (size_t i = 0; i < m_EntitySlots.size(); ++i)
		{
			auto& t = reg.get<Cosmic::TransformComponent>(m_EntitySlots[i]);
			auto& body = reg.get<BenchmarkPhysicsBody>(m_EntitySlots[i]);

			t.Position = { result[i].Position.x, result[i].Position.y, t.Position.z };
			body = result[i];
		}

		m_MergeMs = ElapsedMs(mergeStart, Clock::now());
	}

	// =========================================================================
	// IntegrateBody — shared integration kernel (static)
	// =========================================================================
	void TemplateRenderBenchmarkLayer::IntegrateBody(const BenchmarkPhysicsBody& src,
		BenchmarkPhysicsBody& dst,
		float dt,
		float gravity,
		float damping,
		float boundsX,
		float boundsY)
	{
		BenchmarkPhysicsBody b = src;

		// Gravity
		b.Velocity.y += gravity * dt;

		// Air drag
		float drag = 1.0f - (damping * b.LinearDrag * dt);
		b.Velocity *= glm::clamp(drag, 0.0f, 1.0f);

		// Integrate position
		b.Position += b.Velocity * dt;

		// Bounce retention coefficient
		const float retention = glm::clamp(b.Restitution - damping, 0.0f, 1.0f);

		// X bounds
		if (b.Position.x + b.Radius > boundsX)
		{
			b.Position.x = boundsX - b.Radius;
			b.Velocity.x = -b.Velocity.x * retention;
		}
		else if (b.Position.x - b.Radius < -boundsX)
		{
			b.Position.x = -boundsX + b.Radius;
			b.Velocity.x = -b.Velocity.x * retention;
		}

		// Y bounds — floor
		if (b.Position.y - b.Radius < -boundsY)
		{
			b.Position.y = -boundsY + b.Radius;
			b.Velocity.y = -b.Velocity.y * retention;
			b.Velocity.x *= (1.0f - damping * 0.5f); // floor friction
		}
		// Y bounds — ceiling
		else if (b.Position.y + b.Radius > boundsY)
		{
			b.Position.y = boundsY - b.Radius;
			b.Velocity.y = -b.Velocity.y * retention;
		}

		dst = b;
	}

	// =========================================================================
	// RenderArena
	// =========================================================================
	void TemplateRenderBenchmarkLayer::RenderArena()
	{
		const float bx = m_BoundsX;
		const float by = m_BoundsY;

		// Background
		Cosmic::Renderer2D::DrawQuad(
			{ 0.0f, 0.0f, -0.3f },
			{ bx * 2.0f + 0.1f, by * 2.0f + 0.1f },
			{ 0.06f, 0.06f, 0.09f, 1.0f });

		// Floor shadow
		Cosmic::Renderer2D::DrawCircle(
			{ 0.0f, -by + 0.02f, -0.26f },
			{ bx * 2.0f, 0.7f },
			{ 0.0f, 0.0f, 0.0f, 0.30f },
			1.0f, 0.2f);

		// Border
		Cosmic::Renderer2D::DrawRect(
			{ 0.0f, 0.0f, -0.25f },
			{ bx * 2.0f + 0.05f, by * 2.0f + 0.05f },
			{ 0.3f, 0.6f, 0.9f, 0.8f });

		// Reference grid
		const glm::vec4 gc = { 0.12f, 0.12f, 0.18f, 1.0f };
		for (float x = -bx; x <= bx; x += 1.0f)
			Cosmic::Renderer2D::DrawLine({ x, -by, -0.2f }, { x,  by, -0.2f }, gc);
		for (float y = -by; y <= by; y += 1.0f)
			Cosmic::Renderer2D::DrawLine({ -bx, y, -0.2f }, { bx,  y, -0.2f }, gc);
	}

	// =========================================================================
	// RenderBatched  — one DrawQuad / DrawCircle per entity
	// =========================================================================
	void TemplateRenderBenchmarkLayer::RenderBatched()
	{
		if (m_ShapeMode == ShapeMode::Quads)
		{
			auto view = m_Scene->View<Cosmic::TransformComponent, BenchmarkBallComponent>();
			for (auto entity : view)
			{
				const auto& t = view.get<Cosmic::TransformComponent>(entity);
				const auto& b = view.get<BenchmarkBallComponent>(entity);

				Cosmic::Renderer2D::DrawQuad(
					t.Position,
					{ b.Radius * 2.0f, b.Radius * 2.0f },
					b.Color);
			}
		}
		else // Circles
		{
			auto view = m_Scene->View<Cosmic::TransformComponent, BenchmarkBallComponent>();
			for (auto entity : view)
			{
				const auto& t = view.get<Cosmic::TransformComponent>(entity);
				const auto& b = view.get<BenchmarkBallComponent>(entity);

				Cosmic::Renderer2D::DrawCircle(
					t.Position,
					{ b.Radius * 2.0f, b.Radius * 2.0f },
					b.Color,
					1.0f, 0.02f);
			}
		}
	}

	// =========================================================================
	// RenderInstanced — pack all entities, single GPU draw call
	// =========================================================================
	void TemplateRenderBenchmarkLayer::RenderInstanced()
	{
		auto view = m_Scene->View<Cosmic::TransformComponent, BenchmarkBallComponent>();
		const size_t count = static_cast<size_t>(view.size_hint());

		if (m_ShapeMode == ShapeMode::Quads)
		{
			std::vector<Cosmic::Renderer2D::InstanceQuadData> buf;
			buf.reserve(count);

			for (auto entity : view)
			{
				const auto& t = view.get<Cosmic::TransformComponent>(entity);
				const auto& b = view.get<BenchmarkBallComponent>(entity);

				buf.push_back({
					t.Position,
					{ b.Radius * 2.0f, b.Radius * 2.0f },
					b.Color,
					{ 0.0f, 0.0f },   // TexCoordOffset — solid color
					{ 1.0f, 1.0f },   // TexCoordScale  — solid color
					0.0f,             // TexIndex        — white texture slot
					1.0f              // TilingFactor
					});
			}

			if (!buf.empty())
				Cosmic::Renderer2D::DrawInstancedQuads(buf.data(),
					static_cast<uint32_t>(buf.size()));
		}
		else // Circles
		{
			std::vector<Cosmic::Renderer2D::InstanceCircleData> buf;
			buf.reserve(count);

			for (auto entity : view)
			{
				const auto& t = view.get<Cosmic::TransformComponent>(entity);
				const auto& b = view.get<BenchmarkBallComponent>(entity);

				buf.push_back({
					t.Position,
					{ b.Radius * 2.0f, b.Radius * 2.0f },
					b.Color,
					1.0f,   // Thickness
					0.02f   // Fade
					});
			}

			if (!buf.empty())
				Cosmic::Renderer2D::DrawInstancedCircles(buf.data(),
					static_cast<uint32_t>(buf.size()));
		}
	}

	// =========================================================================
	// Spawn helpers
	// =========================================================================
	void TemplateRenderBenchmarkLayer::SpawnBall(glm::vec2 position, glm::vec2 velocity,
		float radius, glm::vec4 color)
	{
		Cosmic::Entity ent = m_Scene->CreateEntity("BenchBall");

		auto& t = ent.GetComponent<Cosmic::TransformComponent>();
		t.Position = { position.x, position.y, 0.0f };

		auto& ball = ent.AddComponent<BenchmarkBallComponent>();
		ball.Radius = radius;
		ball.Color = color;

		// Ensure visible
		const float maxC = std::max({ color.r, color.g, color.b });
		if (maxC < 0.35f) { ball.Color.r += 0.4f; ball.Color.g += 0.3f; ball.Color.b += 0.5f; }

		auto& body = ent.AddComponent<BenchmarkPhysicsBody>();
		body.Position = position;
		body.Velocity = velocity;
		body.Radius = radius;
		body.Mass = radius * radius * 3.14159f;
		body.Restitution = 0.9f;
		body.LinearDrag = 1.0f;

		m_Balls.push_back(ent);
		m_TargetCount = static_cast<int>(m_Balls.size());
	}

	void TemplateRenderBenchmarkLayer::SpawnBatch(int count)
	{
		std::uniform_real_distribution<float> xDist(-m_BoundsX * 0.85f, m_BoundsX * 0.85f);
		std::uniform_real_distribution<float> yDist(0.0f, m_BoundsY * 0.75f);
		std::uniform_real_distribution<float> vDist(-4.5f, 4.5f);
		std::uniform_real_distribution<float> rDist(0.14f, 0.38f);
		std::uniform_real_distribution<float> cDist(0.3f, 1.0f);

		for (int i = 0; i < count; ++i)
		{
			SpawnBall(
				{ xDist(m_Rng), yDist(m_Rng) },
				{ vDist(m_Rng), vDist(m_Rng) },
				rDist(m_Rng),
				{ cDist(m_Rng), cDist(m_Rng), cDist(m_Rng), 1.0f });
		}
	}

	void TemplateRenderBenchmarkLayer::ClearBalls()
	{
		// Collect to avoid iterator invalidation mid-destroy.
		auto view = m_Scene->View<BenchmarkBallComponent>();
		std::vector<entt::entity> targets(view.begin(), view.end());
		for (auto e : targets)
			m_Scene->DestroyEntity({ e, m_Scene.get() });

		m_Balls.clear();
		m_TargetCount = 0;
		m_FixedTicks = 0;
		m_PhysicsBuffer.Resize(0);
		m_EntitySlots.clear();

		CS_INFO("TemplateRenderBenchmarkLayer: Cleared all benchmark entities.");
	}

	void TemplateRenderBenchmarkLayer::RebuildToCount(int targetCount)
	{
		const int current = static_cast<int>(m_Balls.size());
		if (targetCount > current)
		{
			SpawnBatch(targetCount - current);
		}
		else if (targetCount < current)
		{
			// Destroy the tail.
			const int toRemove = current - targetCount;
			for (int i = 0; i < toRemove; ++i)
			{
				Cosmic::Entity& back = m_Balls.back();
				m_Scene->DestroyEntity(back);
				m_Balls.pop_back();
			}
			m_TargetCount = static_cast<int>(m_Balls.size());
		}
	}

	// =========================================================================
	// Event
	// =========================================================================
	bool TemplateRenderBenchmarkLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = {
			static_cast<float>(e.GetWidth()),
			static_cast<float>(e.GetHeight())
		};
		m_Camera.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}

	// =========================================================================
	// ImGui — Mode Panel
	// =========================================================================
	void TemplateRenderBenchmarkLayer::ImGuiModePanel()
	{
		if (!ImGui::CollapsingHeader("Render & Physics Mode", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// --- Render mode ---
		ImGui::Text("Render Mode:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);

		int renderMode = static_cast<int>(m_RenderMode);
		if (ImGui::RadioButton("Batched##R", &renderMode, 0)) m_RenderMode = RenderMode::Batched;
		ImGui::SameLine();
		if (ImGui::RadioButton("Instanced##R", &renderMode, 1)) m_RenderMode = RenderMode::Instanced;

		// Mode description hint
		if (m_RenderMode == RenderMode::Batched)
			ImGui::TextDisabled("  One DrawCall per entity (CPU vertex packing)");
		else
			ImGui::TextDisabled("  One glDrawElementsInstanced (GPU per-instance stream)");

		ImGui::Spacing();

		// --- Shape ---
		ImGui::Text("Shape:");
		ImGui::SameLine();
		int shapeMode = static_cast<int>(m_ShapeMode);
		if (ImGui::RadioButton("Quads##S", &shapeMode, 0)) m_ShapeMode = ShapeMode::Quads;
		ImGui::SameLine();
		if (ImGui::RadioButton("Circles##S", &shapeMode, 1)) m_ShapeMode = ShapeMode::Circles;

		ImGui::Spacing();

		// --- Physics mode ---
		ImGui::Text("Physics Mode:");
		ImGui::SameLine();
		int physMode = static_cast<int>(m_PhysicsMode);
		if (ImGui::RadioButton("Single-Threaded##P", &physMode, 0))
			m_PhysicsMode = PhysicsMode::SingleThreaded;
		ImGui::SameLine();
		if (ImGui::RadioButton("Multi-Threaded##P", &physMode, 1))
			m_PhysicsMode = PhysicsMode::MultiThreaded;

		if (m_PhysicsMode == PhysicsMode::MultiThreaded)
		{
			ImGui::TextColored({ 0.4f, 1.0f, 0.5f, 1.0f },
				"  Workers: %u  |  Cores: %u",
				Cosmic::JobSystem::Get().GetWorkerCount(),
				Cosmic::JobSystem::Get().GetCoreCount());
		}
		else
		{
			ImGui::TextDisabled("  Integration runs on the main thread.");
		}
	}

	// =========================================================================
	// ImGui — Spawn Panel
	// =========================================================================
	void TemplateRenderBenchmarkLayer::ImGuiSpawnPanel()
	{
		if (!ImGui::CollapsingHeader("Entity Spawning", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::Text("Active entities: %zu", m_Balls.size());
		ImGui::Spacing();

		// Live count slider — adjust entity count without clearing
		int liveTarget = static_cast<int>(m_Balls.size());
		if (ImGui::SliderInt("Live Count", &liveTarget, k_MinEntities, k_MaxEntities))
			RebuildToCount(liveTarget);

		ImGui::Spacing();
		ImGui::SliderInt("Batch Spawn Count", &m_SpawnCount, 1, 500);

		if (ImGui::Button("Spawn Batch"))
			SpawnBatch(m_SpawnCount);

		ImGui::SameLine();
		if (ImGui::Button("Clear All"))
			ClearBalls();

		ImGui::SameLine();
		if (ImGui::Button("Reset Counters"))
			m_FixedTicks = 0;

		ImGui::Spacing();

		// Quick preset buttons
		ImGui::Text("Quick Presets:");
		const int presets[] = { 100, 500, 1000, 5000, 10000, 20000 };
		for (int p : presets)
		{
			char label[32];
			snprintf(label, sizeof(label), "%dk", p / 1000 ? p / 1000 : 0);
			if (p < 1000)
				snprintf(label, sizeof(label), "%d", p);
			else
				snprintf(label, sizeof(label), "%dk", p / 1000);

			if (ImGui::Button(label))
				RebuildToCount(p);
			ImGui::SameLine();
		}
		ImGui::NewLine();
	}

	// =========================================================================
	// ImGui — Physics Panel
	// =========================================================================
	void TemplateRenderBenchmarkLayer::ImGuiPhysicsPanel()
	{
		if (!ImGui::CollapsingHeader("Simulation Parameters", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		ImGui::SliderFloat("Gravity", &m_Gravity, -20.0f, 0.0f, "%.1f m/s²");
		ImGui::SliderFloat("Damping", &m_Damping, 0.0f, 1.0f, "%.3f");
		ImGui::SliderFloat("Bounds X", &m_BoundsX, 2.0f, 15.0f, "%.1f");
		ImGui::SliderFloat("Bounds Y", &m_BoundsY, 1.5f, 9.0f, "%.1f");

		ImGui::Spacing();
		ImGui::Text("Fixed Ticks: %u", m_FixedTicks);
	}

	// =========================================================================
	// ImGui — Performance Panel
	// =========================================================================
	void TemplateRenderBenchmarkLayer::ImGuiPerfPanel()
	{
		if (!ImGui::CollapsingHeader("Performance Telemetry", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		// --- Frame ---
		ImGui::Text("Frame Time:       %.3f ms  (%.1f FPS)",
			m_FrameTimeMs,
			m_FrameTimeMs > 0.0f ? 1000.0f / m_FrameTimeMs : 0.0f);

		// --- Render ---
		const char* renderLabel = (m_RenderMode == RenderMode::Instanced) ? "Instanced" : "Batched";
		const char* shapeLabel = (m_ShapeMode == ShapeMode::Circles) ? "Circles" : "Quads";
		ImGui::Text("Render (%s %s): %.3f ms", renderLabel, shapeLabel, m_LastRenderMs);

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("  Draw Calls:     %u", stats.DrawCalls);
		ImGui::Text("  Quads/Circles:  %u", stats.QuadCount);

		ImGui::Spacing();

		// --- Physics ---
		const char* physLabel = (m_PhysicsMode == PhysicsMode::MultiThreaded)
			? "Multi-Threaded" : "Single-Threaded";
		ImGui::Text("Physics (%s): %.3f ms", physLabel, m_LastPhysicsMs);

		if (m_PhysicsMode == PhysicsMode::MultiThreaded)
		{
			const float total = m_PrepareMs + m_ExecuteMs + m_MergeMs;

			ImGui::Text("  Prepare:  %.3f ms", m_PrepareMs);
			ImGui::Text("  Execute:  %.3f ms  (%.1f%% of total)",
				m_ExecuteMs,
				total > 0.0f ? (m_ExecuteMs / total) * 100.0f : 0.0f);
			ImGui::Text("  Merge:    %.3f ms", m_MergeMs);
			ImGui::Text("  Total:    %.3f ms", total);

			ImGui::Spacing();

			// Bar chart of the three phases using ImGui draw list
			if (total > 0.0f)
			{
				const float barW = ImGui::GetContentRegionAvail().x - 10.0f;
				const float barH = 16.0f;
				const float gap = 2.0f;
				ImDrawList* dl = ImGui::GetWindowDrawList();
				ImVec2 cursor = ImGui::GetCursorScreenPos();

				auto DrawBar = [&](float fraction, ImVec4 col, const char* label, float ms)
					{
						float w = barW * fraction;
						dl->AddRectFilled(cursor,
							{ cursor.x + w, cursor.y + barH },
							IM_COL32(
								(int)(col.x * 255),
								(int)(col.y * 255),
								(int)(col.z * 255),
								(int)(col.w * 255)));
						dl->AddText({ cursor.x + 4.0f, cursor.y + 2.0f },
							IM_COL32(255, 255, 255, 220),
							label);
						char msStr[32];
						snprintf(msStr, sizeof(msStr), "%.3f ms", ms);
						dl->AddText({ cursor.x + w - 60.0f, cursor.y + 2.0f },
							IM_COL32(255, 255, 255, 160),
							msStr);
						cursor.y += barH + gap;
						ImGui::Dummy({ barW, barH + gap });
					};

				DrawBar(m_PrepareMs / total, { 0.4f, 0.7f, 1.0f, 0.9f }, "Prepare", m_PrepareMs);
				DrawBar(m_ExecuteMs / total, { 0.3f, 1.0f, 0.4f, 0.9f }, "Execute", m_ExecuteMs);
				DrawBar(m_MergeMs / total, { 1.0f, 0.7f, 0.3f, 0.9f }, "Merge", m_MergeMs);
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		// --- Renderer2D stats label reminder ---
		ImGui::TextDisabled("Batched: many draw calls, one per entity batch-flush.");
		ImGui::TextDisabled("Instanced: 1 draw call covers all entities (GPU instancing).");
	}

} // namespace Workspace