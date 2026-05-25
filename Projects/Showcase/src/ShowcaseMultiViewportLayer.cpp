#include "ShowcaseMultiViewportLayer.h"
#include <imgui.h>
#include <cmath>

namespace Showcase
{
	// =========================================================================
	// Constructor
	// =========================================================================

	ShowcaseMultiViewportLayer::ShowcaseMultiViewportLayer()
		: Cosmic::Layer("ShowcaseMultiViewportLayer")
		, m_CamMain(1280.0f / 720.0f, false)
		, m_CamOverhead(1280.0f / 720.0f, false)
		, m_CamSlowMo(1280.0f / 720.0f, false)
		, m_CamDebug(1280.0f / 720.0f, false)
	{
	}

	// =========================================================================
	// Lifecycle
	// =========================================================================

	void ShowcaseMultiViewportLayer::OnAttach()
	{
		CS_INFO("ShowcaseMultiViewportLayer: Attaching 4-camera viewport showcase.");

		// Main camera: close-up follow, moderate zoom
		m_CamMain.SetZoomLevel(2.5f);
		m_CamMain.SetZoomLimits(0.5f, 20.0f);
		m_CamMain.SetManualMovementEnabled(false); // auto-pan driven in OnUpdate

		// Overhead camera: wide bird's-eye view of the whole scene
		m_CamOverhead.SetZoomLevel(8.0f);
		m_CamOverhead.SetZoomLimits(2.0f, 30.0f);
		m_CamOverhead.SetManualMovementEnabled(false);

		// Slow-mo tinted camera: same position as main but with a distinct blue tint applied at draw time
		m_CamSlowMo.SetZoomLevel(3.0f);
		m_CamSlowMo.SetZoomLimits(0.5f, 20.0f);
		m_CamSlowMo.SetManualMovementEnabled(false);

		// Debug camera: slightly pulled back, shows wireframe bounding boxes
		m_CamDebug.SetZoomLevel(5.0f);
		m_CamDebug.SetZoomLimits(1.0f, 20.0f);
		m_CamDebug.SetManualMovementEnabled(false);

		m_Scene = Cosmic::Scene::Create();
		BuildScene();
	}

	void ShowcaseMultiViewportLayer::OnDetach()
	{
		CS_INFO("ShowcaseMultiViewportLayer: Detaching.");
		CleanupScene();
		m_Scene.reset();
	}

	// =========================================================================
	// Scene Helpers
	// =========================================================================

	void ShowcaseMultiViewportLayer::BuildScene()
	{
		CleanupScene();
		m_Objects.clear();

		// Central "star" anchor
		{
			MultiViewObject star;
			star.IsStar = true;
			star.Color = { 1.0f, 0.9f, 0.3f, 1.0f };
			star.Radius = 0.0f;

			star.EntityHandle = m_Scene->CreateEntity("Star");
			auto& t = star.EntityHandle.GetComponent<Cosmic::TransformComponent>();
			t.Position = { 0.0f, 0.0f, 0.0f };
			t.Scale = { 0.6f, 0.6f };
			m_Objects.push_back(star);
		}

		// Orbital bodies — evenly spaced phase offsets
		const glm::vec4 palette[] = {
			{ 0.3f, 0.6f, 1.0f, 1.0f },  // blue
			{ 1.0f, 0.4f, 0.3f, 1.0f },  // red
			{ 0.4f, 1.0f, 0.5f, 1.0f },  // green
			{ 1.0f, 0.7f, 0.2f, 1.0f },  // orange
			{ 0.8f, 0.3f, 1.0f, 1.0f },  // purple
			{ 0.2f, 1.0f, 1.0f, 1.0f },  // cyan
		};
		const int paletteCount = 6;

		for (int i = 0; i < m_ObjectCount; ++i)
		{
			MultiViewObject obj;
			obj.IsStar = false;
			obj.Color = palette[i % paletteCount];
			obj.Radius = m_OrbitScale * (0.5f + 0.5f * (float)(i + 1) / (float)m_ObjectCount);
			obj.OrbitSpeed = 0.4f + 0.15f * (float)i;
			obj.OrbitPhase = (float)i * (2.0f * 3.14159265f / (float)m_ObjectCount);
			obj.RotationVel = 45.0f + 30.0f * (float)i;

			std::string name = "Orb_" + std::to_string(i);
			obj.EntityHandle = m_Scene->CreateEntity(name);
			auto& t = obj.EntityHandle.GetComponent<Cosmic::TransformComponent>();
			t.Scale = { 0.25f + 0.08f * (float)i, 0.25f + 0.08f * (float)i };

			m_Objects.push_back(obj);
		}
	}

	void ShowcaseMultiViewportLayer::CleanupScene()
	{
		if (!m_Scene) return;
		for (auto& obj : m_Objects)
		{
			if (obj.EntityHandle)
				m_Scene->DestroyEntity(obj.EntityHandle);
		}
		m_Objects.clear();
	}

	// =========================================================================
	// Fixed Update — Deterministic Orbital Simulation
	// =========================================================================

	void ShowcaseMultiViewportLayer::OnFixedUpdate(float deltaFixedTime)
	{
		if (deltaFixedTime <= 0.0f) return;

		// Advance orbital positions for each non-star body
		for (auto& obj : m_Objects)
		{
			if (obj.IsStar || !obj.EntityHandle) continue;

			auto& t = obj.EntityHandle.GetComponent<Cosmic::TransformComponent>();

			// Circular orbit around origin
			float angle = obj.OrbitPhase + m_Time * obj.OrbitSpeed;
			t.Position.x = obj.Radius * std::cos(angle);
			t.Position.y = obj.Radius * std::sin(angle) * 0.5f; // flatten into ellipse
			t.Position.z = 0.0f;

			// Spin in place
			t.Rotation.z += obj.RotationVel * deltaFixedTime;
		}
	}

	// =========================================================================
	// Update — Cameras, Time, Render Passes
	// =========================================================================

	void ShowcaseMultiViewportLayer::OnUpdate(float ts)
	{
		m_Time = GetLocalTime();

		// -----------------------------------------------------------------------
		// Sync viewport from framebuffer
		// -----------------------------------------------------------------------
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			float aspect = (w * 0.5f) / (h * 0.5f); // each quadrant is half-width, half-height

			m_CamMain.OnResize(w * 0.5f, h * 0.5f);
			m_CamOverhead.OnResize(w * 0.5f, h * 0.5f);
			m_CamSlowMo.OnResize(w * 0.5f, h * 0.5f);
			m_CamDebug.OnResize(w * 0.5f, h * 0.5f);
		}

		// -----------------------------------------------------------------------
		// Animate main camera — slow auto-pan around the scene centre
		// -----------------------------------------------------------------------
		if (m_AnimateCam)
		{
			m_CamMainAngle += ts * 0.12f;
			float cx = std::cos(m_CamMainAngle) * 1.5f;
			float cy = std::sin(m_CamMainAngle) * 0.6f;
			m_CamMain.SetPosition({ cx, cy, 0.0f });

			// Slow-mo camera mirrors main position with a slight lag
			float lagAngle = m_CamMainAngle - 0.4f;
			m_CamSlowMo.SetPosition({ std::cos(lagAngle) * 1.5f, std::sin(lagAngle) * 0.6f, 0.0f });
		}

		// Overhead and debug cameras stay fixed at origin
		m_CamOverhead.SetPosition({ 0.0f, 0.0f, 0.0f });
		m_CamDebug.SetPosition({ 0.0f, 0.0f, 0.0f });

		m_CamMain.OnUpdate(ts);
		m_CamOverhead.OnUpdate(ts);
		m_CamSlowMo.OnUpdate(ts);
		m_CamDebug.OnUpdate(ts);

		// -----------------------------------------------------------------------
		// Compute quadrant bounds (pixel-space, origin at BOTTOM-LEFT for OpenGL)
		//
		// Full viewport:
		//   w x h pixels total
		// Quadrant layout:
		//   TL = (0,    h/2, w/2, h/2)
		//   TR = (w/2,  h/2, w/2, h/2)
		//   BL = (0,    0,   w/2, h/2)
		//   BR = (w/2,  0,   w/2, h/2)
		//
		// glViewport(x, y, width, height) — y=0 is bottom of window
		// -----------------------------------------------------------------------
		float hw = w * 0.5f;
		float hh = h * 0.5f;

		glm::vec4 boundsTopLeft = { 0.0f, hh,   hw, hh };
		glm::vec4 boundsTopRight = { hw,   hh,   hw, hh };
		glm::vec4 boundsBottomLeft = { 0.0f, 0.0f, hw, hh };
		glm::vec4 boundsBottomRight = { hw,   0.0f, hw, hh };

		Cosmic::Renderer2D::ResetStats();

		// ===================================================================
		// QUADRANT 1 — TOP-LEFT: Main Close-Up Camera
		// ===================================================================
		{
			Cosmic::RenderPass pass(m_CamMain.GetCamera(), boundsTopLeft);
			DrawGrid({ 0.12f, 0.12f, 0.15f, 1.0f }, 1.0f, 12.0f);
			DrawSceneContent(false, { 1.0f, 1.0f, 1.0f, 1.0f });
		}

		// ===================================================================
		// QUADRANT 2 — TOP-RIGHT: Overhead Overview Camera
		// ===================================================================
		{
			Cosmic::RenderPass pass(m_CamOverhead.GetCamera(), boundsTopRight);
			DrawGrid({ 0.1f, 0.1f, 0.12f, 1.0f }, 2.0f, 20.0f);
			DrawSceneContent(false, { 1.0f, 1.0f, 1.0f, 1.0f });

			// Draw orbital path rings as reference circles
			for (const auto& obj : m_Objects)
			{
				if (obj.IsStar) continue;
				Cosmic::Renderer2D::DrawCircle(
					{ 0.0f, 0.0f, -0.05f },
					{ obj.Radius * 2.0f, obj.Radius * 1.0f },
					{ obj.Color.r, obj.Color.g, obj.Color.b, 0.15f },
					0.02f, 0.005f
				);
			}
		}

		// ===================================================================
		// QUADRANT 3 — BOTTOM-LEFT: Slow-Mo Tinted Camera
		// ===================================================================
		{
			Cosmic::RenderPass pass(m_CamSlowMo.GetCamera(), boundsBottomLeft);
			// Cold blue background tint drawn as a full-screen quad
			Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.5f }, { 100.0f, 100.0f }, { 0.04f, 0.06f, 0.14f, 1.0f });
			DrawGrid({ 0.1f, 0.15f, 0.3f, 1.0f }, 1.0f, 12.0f);
			// Render objects with a blue tint to visually distinguish this pass
			DrawSceneContent(false, { 0.5f, 0.7f, 1.0f, 1.0f });
		}

		// ===================================================================
		// QUADRANT 4 — BOTTOM-RIGHT: Debug / Wireframe Camera
		// ===================================================================
		{
			Cosmic::RenderPass pass(m_CamDebug.GetCamera(), boundsBottomRight);
			// Dark grid
			DrawGrid({ 0.08f, 0.08f, 0.08f, 1.0f }, 1.0f, 20.0f);
			// Draw wireframe bounding rectangles instead of solid quads
			DrawSceneContent(true, { 1.0f, 1.0f, 1.0f, 1.0f });

			// Draw velocity vectors as lines originating from each orbiter
			for (const auto& obj : m_Objects)
			{
				if (obj.IsStar || !obj.EntityHandle) continue;
				auto& t = obj.EntityHandle.GetComponent<Cosmic::TransformComponent>();

				// Tangent direction = perpendicular to radial direction
				float angle = std::atan2(t.Position.y, t.Position.x);
				float tangX = -std::sin(angle) * 0.8f;
				float tangY = std::cos(angle) * 0.4f;

				glm::vec3 start = t.Position;
				glm::vec3 end = { t.Position.x + tangX, t.Position.y + tangY, t.Position.z };
				Cosmic::Renderer2D::DrawLine(start, end, { 1.0f, 1.0f, 0.2f, 0.8f });
			}
		}
	}

	// =========================================================================
	// Draw Helpers
	// =========================================================================

	void ShowcaseMultiViewportLayer::DrawGrid(const glm::vec4& color, float spacing, float extent)
	{
		for (float x = -extent; x <= extent; x += spacing)
			Cosmic::Renderer2D::DrawLine({ x, -extent, -0.2f }, { x,  extent, -0.2f }, color);
		for (float y = -extent; y <= extent; y += spacing)
			Cosmic::Renderer2D::DrawLine({ -extent, y, -0.2f }, { extent, y, -0.2f }, color);
	}

	void ShowcaseMultiViewportLayer::DrawSceneContent(bool wireframe, const glm::vec4& tint)
	{
		for (const auto& obj : m_Objects)
		{
			if (!obj.EntityHandle) continue;
			auto& t = obj.EntityHandle.GetComponent<Cosmic::TransformComponent>();

			glm::vec4 drawColor = {
				obj.Color.r * tint.r,
				obj.Color.g * tint.g,
				obj.Color.b * tint.b,
				obj.Color.a * tint.a
			};

			if (wireframe)
			{
				// Draw bounding rect wireframe
				Cosmic::Renderer2D::DrawRect(t.Position, t.Scale, drawColor);

				// Also draw a small dot at the centre
				Cosmic::Renderer2D::DrawCircle(
					t.Position,
					{ t.Scale.x * 0.3f, t.Scale.y * 0.3f },
					drawColor,
					1.0f,
					0.05f
				);
			}
			else
			{
				if (obj.IsStar)
				{
					// Star: solid SDF circle
					Cosmic::Renderer2D::DrawCircle(
						t.Position,
						t.Scale,
						drawColor,
						1.0f,   // solid disk
						0.1f
					);
				}
				else
				{
					// Orbiters: rotated square
					Cosmic::Renderer2D::DrawRotatedQuad(
						t.Position,
						t.Scale,
						glm::radians(t.Rotation.z),
						drawColor
					);
				}
			}
		}
	}

	// =========================================================================
	// ImGui Inspector
	// =========================================================================

	void ShowcaseMultiViewportLayer::OnImGuiRender()
	{
		ImGui::Begin("Simulation Inspection Window");
		ImGui::Text("--- RenderPass Multi-Viewport Showcase ---");
		ImGui::Separator();

		ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "4 Independent Camera Passes per Frame");
		ImGui::Spacing();

		ImGui::Text("┌ TOP-LEFT    Main close-up camera (auto-panning)");
		ImGui::Text("├ TOP-RIGHT   Overhead overview + orbit path rings");
		ImGui::Text("├ BOTTOM-LEFT Tinted slow-motion mirror of main");
		ImGui::Text("└ BOT-RIGHT   Debug wireframe + velocity vectors");

		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Draw Calls this frame: %u", stats.DrawCalls);
		ImGui::Text("Total Quads:           %u", stats.QuadCount);
		ImGui::Text("Total Vertices:        %u", stats.GetTotalVertexCount());

		ImGui::Separator();

		if (ImGui::CollapsingHeader("Scene Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool rebuild = false;

			int prevCount = m_ObjectCount;
			ImGui::SliderInt("Orbiter Count", &m_ObjectCount, 1, 12);
			if (m_ObjectCount != prevCount) rebuild = true;

			float prevScale = m_OrbitScale;
			ImGui::SliderFloat("Orbit Radius Scale", &m_OrbitScale, 1.0f, 8.0f, "%.2f");
			if (std::abs(m_OrbitScale - prevScale) > 0.01f) rebuild = true;

			if (rebuild)
				BuildScene();

			ImGui::Checkbox("Animate Main Camera", &m_AnimateCam);
			ImGui::Checkbox("Show Camera Labels", &m_ShowCamLabels);
		}

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Camera Zoom Controls"))
		{
			float z;

			z = m_CamMain.GetZoomLevel();
			if (ImGui::SliderFloat("Main Zoom", &z, 0.5f, 10.0f, "%.2f")) m_CamMain.SetZoomLevel(z);

			z = m_CamOverhead.GetZoomLevel();
			if (ImGui::SliderFloat("Overhead Zoom", &z, 2.0f, 30.0f, "%.2f")) m_CamOverhead.SetZoomLevel(z);

			z = m_CamSlowMo.GetZoomLevel();
			if (ImGui::SliderFloat("SlowMo Zoom", &z, 0.5f, 10.0f, "%.2f")) m_CamSlowMo.SetZoomLevel(z);

			z = m_CamDebug.GetZoomLevel();
			if (ImGui::SliderFloat("Debug Zoom", &z, 1.0f, 20.0f, "%.2f")) m_CamDebug.SetZoomLevel(z);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::TextWrapped(
			"Architecture Note: Each quadrant is an independent RenderPass RAII scope. "
			"Geometry submitted inside one scope is batch-flushed before the next pass "
			"begins, guaranteeing that Camera A's VP matrix never contaminates Camera B's "
			"vertices. All four cameras observe the same Scene entity data each frame."
		);

		ImGui::End();
	}

	// =========================================================================
	// Event Handling
	// =========================================================================

	void ShowcaseMultiViewportLayer::OnEvent(Cosmic::Event& e)
	{
		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>([this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	bool ShowcaseMultiViewportLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		float w = static_cast<float>(e.GetWidth());
		float h = static_cast<float>(e.GetHeight());
		m_ViewportSize = { w, h };

		float halfW = w * 0.5f;
		float halfH = h * 0.5f;
		m_CamMain.OnResize(halfW, halfH);
		m_CamOverhead.OnResize(halfW, halfH);
		m_CamSlowMo.OnResize(halfW, halfH);
		m_CamDebug.OnResize(halfW, halfH);

		return false;
	}

} // namespace Showcase