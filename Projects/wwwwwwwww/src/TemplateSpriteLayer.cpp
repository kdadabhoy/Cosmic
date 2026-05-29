#include "TemplateSpriteLayer.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace Workspace
{
	// ============================================================================
	// Construction
	// ============================================================================

	TemplateSpriteLayer::TemplateSpriteLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: Cosmic::Layer("Sprite & Multi-Camera")
		, m_Scene(scene)
		, m_CamTL(1280.f / 720.f, false)
		, m_CamTR(1280.f / 720.f, false)
		, m_CamBL(1280.f / 720.f, false)
		, m_CamBR(1280.f / 720.f, false)
	{
	}

	// ============================================================================
	// Lifecycle
	// ============================================================================

	void TemplateSpriteLayer::OnAttach()
	{
		CS_INFO("TemplateSpriteLayer: Attaching.");

		// --- Camera configuration ---
		// TL: close-up follow camera — auto-driven, no manual WASD
		m_CamTL.SetZoomLevel(2.0f);
		m_CamTL.SetZoomLimits(0.5f, 15.0f);
		m_CamTL.SetManualMovementEnabled(false);

		// TR: wide bird's-eye overview — user can scroll to zoom
		m_CamTR.SetZoomLevel(7.0f);
		m_CamTR.SetZoomLimits(2.0f, 30.0f);
		m_CamTR.SetManualMovementEnabled(false);

		// BL: blue-tinted slow-mo mirror of TL — auto-driven
		m_CamBL.SetZoomLevel(2.5f);
		m_CamBL.SetZoomLimits(0.5f, 15.0f);
		m_CamBL.SetManualMovementEnabled(false);

		// BR: debug camera — slightly wider than TL
		m_CamBR.SetZoomLevel(4.5f);
		m_CamBR.SetZoomLimits(1.0f, 20.0f);
		m_CamBR.SetManualMovementEnabled(false);

		// --- Atlas loading via VFS ---
		// Sprites must be placed at:
		//   assets/projects/wwwwwwwww/sprites/DinoSprites - <name>.png
		// after the CMake POST_BUILD asset sync runs.
		std::vector<std::string> vfsPaths = {
			"project://sprites/DinoSprites - doux.png",  // 0 = Blue
			"project://sprites/DinoSprites - mort.png",  // 1 = Red
			"project://sprites/DinoSprites - tard.png",  // 2 = Yellow
			"project://sprites/DinoSprites - vita.png",  // 3 = Green
		};
		m_AtlasNames = { "Blue (Doux)", "Red (Mort)", "Yellow (Tard)", "Green (Vita)" };

		for (const auto& vfsPath : vfsPaths)
		{
			std::string resolved = Cosmic::FileSystem::Resolve(vfsPath);
			auto tex = Cosmic::Texture2D::Create(resolved);
			if (tex)
			{
				m_Atlases.push_back(tex);
			}
			else
			{
				CS_ERROR("TemplateSpriteLayer: Failed to load atlas '{0}'", vfsPath);
				m_Atlases.push_back(nullptr);
			}
		}

		BuildEntities();
	}

	void TemplateSpriteLayer::OnDetach()
	{
		CleanupEntities();
		m_Atlases.clear();
		CS_INFO("TemplateSpriteLayer: Detached.");
	}

	// ============================================================================
	// Entity construction helpers
	// ============================================================================

	void TemplateSpriteLayer::BuildEntities()
	{
		CleanupEntities();

		// Four dinos arranged in a loose arc so the overhead camera can see them all
		struct DinoConfig
		{
			std::string name;
			int         atlasIndex;
			glm::vec3   homePos;
			float       walkSpeed;
			float       phaseOffset;
			glm::vec2   scale;
		};

		const std::vector<DinoConfig> configs = {
			{ "Player (Blue)",   0, {  0.0f,  0.0f, 0.0f }, 3.5f, 0.0f,  { 1.4f, 1.4f } },
			{ "Buddy (Red)",     1, { -2.5f, -1.5f, 0.0f }, 2.8f, 1.05f, { 1.1f, 1.1f } },
			{ "Buddy (Yellow)",  2, {  2.5f, -1.5f, 0.0f }, 3.1f, 2.09f, { 1.1f, 1.1f } },
			{ "Buddy (Green)",   3, {  0.0f, -3.0f, 0.0f }, 2.5f, 3.14f, { 1.1f, 1.1f } },
		};

		m_Entities.resize(configs.size());
		m_AtlasIndex.resize(configs.size());
		m_SubTextures.resize(configs.size());
		m_AnimCoords.resize(configs.size(), static_cast<float>(m_RunStartCol));

		for (size_t i = 0; i < configs.size(); ++i)
		{
			const auto& cfg = configs[i];
			m_AtlasIndex[i] = cfg.atlasIndex;

			auto ent = m_Scene->CreateEntity(cfg.name);

			auto& t = ent.GetComponent<Cosmic::TransformComponent>();
			t.Position = cfg.homePos;
			t.Scale = cfg.scale;

			// No SpriteRendererComponent — all rendering is done manually via
			// SubTexture2D DrawQuad calls inside each RenderPass block in OnUpdate.
			// Scene::OnRender is intentionally not called by this layer; using it
			// with no ActiveMaterial set would draw white fallback quads over every
			// sprite in every quadrant.

			auto& dino = ent.AddComponent<DinoCharacterComponent>();
			dino.WalkSpeed = cfg.walkSpeed * m_GlobalMoveSpeed;
			dino.BobFrequency = 2.5f;
			dino.BobAmplitude = 0.18f;
			dino.PhaseOffset = cfg.phaseOffset;
			dino.HomePosition = cfg.homePos;

			m_Entities[i] = ent;
			UpdateSubTexture(static_cast<int>(i));
		}

		CS_INFO("TemplateSpriteLayer: Built {} dino entities.", configs.size());
	}

	void TemplateSpriteLayer::CleanupEntities()
	{
		for (auto& ent : m_Entities)
		{
			if (ent && m_Scene)
				m_Scene->DestroyEntity(ent);
		}
		m_Entities.clear();
		m_SubTextures.clear();
		m_AtlasIndex.clear();
		m_AnimCoords.clear();
	}

	void TemplateSpriteLayer::UpdateSubTexture(int idx)
	{
		int atlasIdx = m_AtlasIndex[idx];
		if (atlasIdx < 0 || atlasIdx >= static_cast<int>(m_Atlases.size()) || !m_Atlases[atlasIdx])
		{
			m_SubTextures[idx] = nullptr;
			return;
		}

		m_SubTextures[idx] = Cosmic::SubTexture2D::CreateFromCoords(
			m_Atlases[atlasIdx],
			{ m_AnimCoords[idx], 0.0f },
			k_CellSize
		);
	}

	// ============================================================================
	// Fixed Update — deterministic animation clock + position bob
	// ============================================================================

	void TemplateSpriteLayer::OnFixedUpdate(float dt)
	{
		if (dt <= 0.0f) return; // guard pause / rewind

		// Advance shared run-cycle frame timer
		m_FrameTimer += dt;
		if (m_FrameTimer >= m_FrameDuration)
		{
			m_FrameTimer = 0.0f;
			m_CurrentRunCol = (m_CurrentRunCol + 1) % m_RunFrameCount;

			// Update all dinos to the new column
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				m_AnimCoords[i] = static_cast<float>(m_RunStartCol + m_CurrentRunCol);
				UpdateSubTexture(i);
			}
		}

		// Sinusoidal bob — each dino has its own phase so they move independently
		float t = GetLocalTime();
		for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
		{
			if (!m_Entities[i]) continue;
			auto& trans = m_Entities[i].GetComponent<Cosmic::TransformComponent>();
			auto& dino = m_Entities[i].GetComponent<DinoCharacterComponent>();

			// Horizontal patrol around home position
			float cycle = t * dino.WalkSpeed * 0.4f + dino.PhaseOffset;
			trans.Position.x = dino.HomePosition.x + std::sin(cycle) * 1.8f;

			// Vertical bob
			float bob = std::sin(t * dino.BobFrequency + dino.PhaseOffset) * dino.BobAmplitude;
			trans.Position.y = dino.HomePosition.y + bob;

			// Face the direction of travel
			dino.FacingLeft = (std::cos(cycle) < 0.0f);
		}
	}

	// ============================================================================
	// Update — cameras, rendering
	// ============================================================================

	void TemplateSpriteLayer::OnUpdate(float ts)
	{
		m_LocalTime = GetLocalTime();

		// -----------------------------------------------------------------------
		// Step 1: Sync viewport from framebuffer
		// -----------------------------------------------------------------------
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float w = static_cast<float>(fb->GetWidth());
		float h = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != w || m_ViewportSize.y != h)
		{
			m_ViewportSize = { w, h };
			float hw = w * 0.5f;
			float hh = h * 0.5f;
			// Each camera is resized to QUADRANT dimensions, not full framebuffer.
			// Passing full-window dimensions to a half-window camera stretches the image.
			m_CamTL.OnResize(hw, hh);
			m_CamTR.OnResize(hw, hh);
			m_CamBL.OnResize(hw, hh);
			m_CamBR.OnResize(hw, hh);
		}

		// -----------------------------------------------------------------------
		// Step 2: Drive camera positions BEFORE any RenderPass is constructed.
		// The VP matrix is snapshotted at construction time, so positions must be
		// set first.
		// -----------------------------------------------------------------------

		// TL camera follows the selected dino
		if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Entities.size()))
		{
			auto& followTrans = m_Entities[m_SelectedIndex]
				.GetComponent<Cosmic::TransformComponent>();
			m_CamTL.SetPosition({ followTrans.Position.x, followTrans.Position.y, 0.f });

			// BL mirrors TL with a slight lag
			float lagX = followTrans.Position.x * 0.92f;
			float lagY = followTrans.Position.y;
			m_CamBL.SetPosition({ lagX, lagY, 0.f });
		}

		// TR and BR stay centred on the scene origin
		m_CamTR.SetPosition({ 0.f, 0.f, 0.f });
		m_CamBR.SetPosition({ 0.f, 0.f, 0.f });

		m_CamTL.OnUpdate(ts);
		m_CamTR.OnUpdate(ts);
		m_CamBL.OnUpdate(ts);
		m_CamBR.OnUpdate(ts);

		// -----------------------------------------------------------------------
		// Step 3: Compute quadrant pixel bounds.
		// OpenGL y=0 is the BOTTOM of the framebuffer.
		//
		//   TL: x=0,   y=hh,  w=hw, h=hh
		//   TR: x=hw,  y=hh,  w=hw, h=hh
		//   BL: x=0,   y=0,   w=hw, h=hh
		//   BR: x=hw,  y=0,   w=hw, h=hh
		// -----------------------------------------------------------------------
		float hw = w * 0.5f;
		float hh = h * 0.5f;

		Cosmic::Renderer2D::ResetStats();

		// -----------------------------------------------------------------------
		// QUADRANT 1 — TOP-LEFT: close-up follow camera
		// Pure manual rendering — SubTexture2D DrawQuad calls only.
		// -----------------------------------------------------------------------
		{
			Cosmic::RenderPass passTL(m_CamTL.GetCamera(), { 0.f, hh, hw, hh });

			if (m_ShowGrid)
				DrawGrid({ 0.10f, 0.10f, 0.14f, 1.0f }, 1.0f, 10.0f);

			if (m_ShowRings)
				DrawTrackingRings();

			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i] || !m_SubTextures[i]) continue;
				auto& t = m_Entities[i].GetComponent<Cosmic::TransformComponent>();
				auto& d = m_Entities[i].GetComponent<DinoCharacterComponent>();

				glm::vec2 drawScale = {
					t.Scale.x * (d.FacingLeft ? -1.0f : 1.0f),
					t.Scale.y
				};
				Cosmic::Renderer2D::DrawQuad(t.Position, drawScale, m_SubTextures[i]);
			}
		}

		// -----------------------------------------------------------------------
		// QUADRANT 2 — TOP-RIGHT: overhead overview
		// All atlas sprites + orbital path circles
		// -----------------------------------------------------------------------
		{
			Cosmic::RenderPass passTR(m_CamTR.GetCamera(), { hw, hh, hw, hh });

			if (m_ShowGrid)
				DrawGrid({ 0.08f, 0.08f, 0.10f, 1.0f }, 2.0f, 20.0f);

			// Draw home-position markers as faint SDF circles
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i]) continue;
				auto& d = m_Entities[i].GetComponent<DinoCharacterComponent>();
				Cosmic::Renderer2D::DrawCircle(
					{ d.HomePosition.x, d.HomePosition.y, -0.1f },
					{ 3.6f, 3.6f },
					{ 0.3f, 0.6f, 1.0f, 0.06f },
					0.015f, 0.004f
				);
			}

			// Atlas sprites at full scale
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i] || !m_SubTextures[i]) continue;
				auto& t = m_Entities[i].GetComponent<Cosmic::TransformComponent>();
				auto& d = m_Entities[i].GetComponent<DinoCharacterComponent>();

				glm::vec2 drawScale = {
					t.Scale.x * (d.FacingLeft ? -1.0f : 1.0f),
					t.Scale.y
				};
				Cosmic::Renderer2D::DrawQuad(t.Position, drawScale, m_SubTextures[i]);
			}

			// Highlight selected dino with a bright rect
			if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Entities.size()))
			{
				auto& t = m_Entities[m_SelectedIndex].GetComponent<Cosmic::TransformComponent>();
				Cosmic::Renderer2D::DrawRect(
					{ t.Position.x, t.Position.y, 0.05f },
					{ t.Scale.x + 0.15f, t.Scale.y + 0.15f },
					{ 1.0f, 1.0f, 0.2f, 1.0f }
				);
			}
		}

		// -----------------------------------------------------------------------
		// QUADRANT 3 — BOTTOM-LEFT: blue-tinted slow-mo mirror
		// -----------------------------------------------------------------------
		{
			Cosmic::RenderPass passBL(m_CamBL.GetCamera(), { 0.f, 0.f, hw, hh });

			// Cold blue background tint
			Cosmic::Renderer2D::DrawQuad(
				{ 0.f, 0.f, -0.5f }, { 200.f, 200.f },
				{ 0.03f, 0.05f, 0.12f, 1.0f }
			);

			if (m_ShowGrid)
				DrawGrid({ 0.08f, 0.12f, 0.28f, 1.0f }, 1.0f, 10.0f);

			// Atlas sprites with a blue colour tint
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i] || !m_SubTextures[i]) continue;
				auto& t = m_Entities[i].GetComponent<Cosmic::TransformComponent>();
				auto& d = m_Entities[i].GetComponent<DinoCharacterComponent>();

				glm::vec2 drawScale = {
					t.Scale.x * (d.FacingLeft ? -1.0f : 1.0f),
					t.Scale.y
				};
				Cosmic::Renderer2D::DrawQuad(
					t.Position, drawScale, m_SubTextures[i],
					{ 0.55f, 0.72f, 1.0f, 1.0f }
				);
			}
		}

		// -----------------------------------------------------------------------
		// QUADRANT 4 — BOTTOM-RIGHT: debug wireframe + bounding boxes
		// -----------------------------------------------------------------------
		{
			Cosmic::RenderPass passBR(m_CamBR.GetCamera(), { hw, 0.f, hw, hh });

			DrawGrid({ 0.07f, 0.07f, 0.07f, 1.0f }, 1.0f, 20.0f);

			if (m_ShowDebugBounds)
				DrawDebugOverlay();

			// Velocity direction arrows
			float t = m_LocalTime;
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i]) continue;
				auto& trans = m_Entities[i].GetComponent<Cosmic::TransformComponent>();
				auto& d = m_Entities[i].GetComponent<DinoCharacterComponent>();

				float cycle = t * d.WalkSpeed * 0.4f + d.PhaseOffset;
				float velX = std::cos(cycle) * 0.8f;
				float velY = std::cos(t * d.BobFrequency + d.PhaseOffset) * d.BobAmplitude * d.BobFrequency * 0.3f;

				glm::vec3 start = trans.Position;
				glm::vec3 end = { trans.Position.x + velX, trans.Position.y + velY, trans.Position.z };
				Cosmic::Renderer2D::DrawLine(start, end, { 1.0f, 1.0f, 0.2f, 0.85f });
			}
		}
	}

	// ============================================================================
	// Draw helpers
	// ============================================================================

	void TemplateSpriteLayer::DrawGrid(const glm::vec4& color, float spacing, float extent)
	{
		for (float x = -extent; x <= extent; x += spacing)
			Cosmic::Renderer2D::DrawLine({ x, -extent, -0.3f }, { x,  extent, -0.3f }, color);
		for (float y = -extent; y <= extent; y += spacing)
			Cosmic::Renderer2D::DrawLine({ -extent, y, -0.3f }, { extent, y, -0.3f }, color);
	}

	void TemplateSpriteLayer::DrawDebugOverlay()
	{
		for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
		{
			if (!m_Entities[i]) continue;
			auto& t = m_Entities[i].GetComponent<Cosmic::TransformComponent>();

			glm::vec4 color = (i == m_SelectedIndex)
				? glm::vec4{ 1.0f, 1.0f, 0.2f, 1.0f }
			: glm::vec4{ 0.4f, 0.8f, 0.4f, 0.8f };

			Cosmic::Renderer2D::DrawRect(
				{ t.Position.x, t.Position.y, 0.01f },
				{ t.Scale.x, t.Scale.y },
				color
			);

			Cosmic::Renderer2D::DrawCircle(
				{ t.Position.x, t.Position.y, 0.02f },
				{ t.Scale.x * 0.25f, t.Scale.y * 0.25f },
				color, 1.0f, 0.08f
			);
		}
	}

	void TemplateSpriteLayer::DrawTrackingRings()
	{
		if (m_SelectedIndex < 0 || m_SelectedIndex >= static_cast<int>(m_Entities.size())) return;

		auto& t = m_Entities[m_SelectedIndex].GetComponent<Cosmic::TransformComponent>();
		float pulse = 1.0f + std::sin(m_LocalTime * m_RingPulseSpeed) * m_RingPulseAmp;

		glm::vec3 ringPos = t.Position;
		ringPos.y -= t.Scale.y * 0.52f;
		ringPos.z -= 0.05f;

		Cosmic::Renderer2D::DrawCircle(
			ringPos,
			{ t.Scale.x * 1.6f * pulse, t.Scale.y * 0.55f * pulse },
			{ 0.2f, 0.9f, 0.5f, 0.18f },
			1.0f, 0.25f
		);
		Cosmic::Renderer2D::DrawCircle(
			ringPos,
			{ t.Scale.x * 1.3f, t.Scale.y * 0.42f },
			{ 0.2f, 0.95f, 0.55f, 0.70f },
			0.06f, 0.005f
		);
	}

	// ============================================================================
	// ImGui
	// ============================================================================

	void TemplateSpriteLayer::OnImGuiRender()
	{
		// -------------------------------------------------------------------------
		// MIDDLE SIDEBAR LAYER: Mounts cleanly right under the Master panel
		// -------------------------------------------------------------------------
		ImGui::Begin("Project Inspector Mid");

		ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "Layer: Sprite & Multi-Camera");
		ImGui::Separator();
		ImGui::Spacing();

		ImGui::TextDisabled("Quadrant layout:");
		ImGui::Text("  TL  Close-up follow camera");
		ImGui::Text("  TR  Overhead overview + orbit markers");
		ImGui::Text("  BL  Blue-tinted slow-mo mirror");
		ImGui::Text("  BR  Debug wireframe + velocity vectors");
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Entity Selection", ImGuiTreeNodeFlags_DefaultOpen))
		{
			for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
			{
				if (!m_Entities[i]) continue;
				auto& tag = m_Entities[i].GetComponent<Cosmic::TagComponent>();
				bool  sel = (i == m_SelectedIndex);

				if (sel) ImGui::PushStyleColor(ImGuiCol_Text, { 1.0f, 1.0f, 0.3f, 1.0f });
				if (ImGui::Selectable(tag.Tag.c_str(), sel))
					m_SelectedIndex = i;
				if (sel) ImGui::PopStyleColor();
			}
		}

		ImGui::Spacing();

		if (m_SelectedIndex >= 0 && m_SelectedIndex < static_cast<int>(m_Entities.size())
			&& m_Entities[m_SelectedIndex])
		{
			if (ImGui::CollapsingHeader("Selected Entity", ImGuiTreeNodeFlags_DefaultOpen))
			{
				auto& t = m_Entities[m_SelectedIndex].GetComponent<Cosmic::TransformComponent>();
				auto& d = m_Entities[m_SelectedIndex].GetComponent<DinoCharacterComponent>();
				auto& tag = m_Entities[m_SelectedIndex].GetComponent<Cosmic::TagComponent>();

				ImGui::Text("Tag: %s", tag.Tag.c_str());
				ImGui::Text("Position:  (%.2f, %.2f, %.2f)", t.Position.x, t.Position.y, t.Position.z);
				ImGui::Text("Rotation:  %.1f deg", t.Rotation.z);
				ImGui::Text("Scale:     (%.2f, %.2f)", t.Scale.x, t.Scale.y);
				ImGui::Text("Facing:    %s", d.FacingLeft ? "Left" : "Right");
				ImGui::Spacing();

				if (ImGui::BeginCombo("Atlas Variant", m_AtlasNames[m_AtlasIndex[m_SelectedIndex]].c_str()))
				{
					for (int n = 0; n < static_cast<int>(m_AtlasNames.size()); ++n)
					{
						bool picked = (m_AtlasIndex[m_SelectedIndex] == n);
						if (ImGui::Selectable(m_AtlasNames[n].c_str(), picked))
						{
							m_AtlasIndex[m_SelectedIndex] = n;
							UpdateSubTexture(m_SelectedIndex);
						}
						if (picked) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				ImGui::SliderFloat("Walk Speed", &d.WalkSpeed, 0.5f, 8.0f, "%.1f");
				ImGui::SliderFloat("Bob Freq", &d.BobFrequency, 0.5f, 6.0f, "%.1f Hz");
				ImGui::SliderFloat("Bob Amp", &d.BobAmplitude, 0.0f, 0.5f, "%.2f");
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Checkbox("Show Grid", &m_ShowGrid);
			ImGui::Checkbox("Show Tracking Ring", &m_ShowRings);
			ImGui::Checkbox("Show Debug Bounds", &m_ShowDebugBounds);
			ImGui::SliderFloat("Ring Pulse Speed", &m_RingPulseSpeed, 0.5f, 8.0f, "%.1f Hz");
			ImGui::SliderFloat("Ring Pulse Amp", &m_RingPulseAmp, 0.0f, 0.4f, "%.2f");
		}

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Animation"))
		{
			ImGui::SliderFloat("Frame Duration", &m_FrameDuration, 0.04f, 0.4f, "%.2f s");
			ImGui::Text("Current Run Column: %d", m_RunStartCol + m_CurrentRunCol);
			ImGui::Text("Layer Time: %.2f s", m_LocalTime);
		}

		ImGui::Spacing();

		if (ImGui::CollapsingHeader("Camera Zoom"))
		{
			float z;
			z = m_CamTL.GetZoomLevel(); if (ImGui::SliderFloat("TL Zoom", &z, 0.5f, 10.f)) m_CamTL.SetZoomLevel(z);
			z = m_CamTR.GetZoomLevel(); if (ImGui::SliderFloat("TR Zoom", &z, 2.0f, 30.f)) m_CamTR.SetZoomLevel(z);
			z = m_CamBL.GetZoomLevel(); if (ImGui::SliderFloat("BL Zoom", &z, 0.5f, 10.f)) m_CamBL.SetZoomLevel(z);
			z = m_CamBR.GetZoomLevel(); if (ImGui::SliderFloat("BR Zoom", &z, 1.0f, 20.f)) m_CamBR.SetZoomLevel(z);
		}

		ImGui::Spacing();
		ImGui::Separator();

		auto stats = Cosmic::Renderer2D::GetStats();
		ImGui::Text("Draw Calls: %u", stats.DrawCalls);
		ImGui::Text("Quads:      %u", stats.QuadCount);
		ImGui::Text("Vertices:   %u", stats.GetTotalVertexCount());

		ImGui::End(); // End "Project Inspector Mid"

		Cosmic::Renderer2D::ResetStats();
	}

	// ============================================================================
	// Events
	// ============================================================================

	void TemplateSpriteLayer::OnEvent(Cosmic::Event& e)
	{
		m_CamTL.OnEvent(e);
		m_CamTR.OnEvent(e);
		m_CamBL.OnEvent(e);
		m_CamBR.OnEvent(e);
		if (e.Handled) return;

		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::KeyPressedEvent>(
			[this](Cosmic::KeyPressedEvent& ev) { return OnKeyPressed(ev); });
		dispatcher.Dispatch<Cosmic::MouseButtonPressedEvent>(
			[this](Cosmic::MouseButtonPressedEvent& ev) { return OnMouseClicked(ev); });
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>(
			[this](Cosmic::WindowResizeEvent& ev) { return OnWindowResize(ev); });
	}

	bool TemplateSpriteLayer::OnKeyPressed(Cosmic::KeyPressedEvent& e)
	{
		if (e.GetRepeatCount() > 0) return false;

		if (e.GetKeyCode() == CS_KEY_TAB)
		{
			m_SelectedIndex = (m_SelectedIndex + 1) % static_cast<int>(m_Entities.size());
			return true;
		}
		return false;
	}

	bool TemplateSpriteLayer::OnMouseClicked(Cosmic::MouseButtonPressedEvent& e)
	{
		if (e.GetMouseButton() != CS_MOUSE_BUTTON_LEFT) return false;

		glm::vec2 screenPos = Cosmic::Input::GetMousePosition();
		if (screenPos.x > m_ViewportSize.x * 0.5f) return false;
		if (screenPos.y > m_ViewportSize.y * 0.5f) return false;

		glm::vec2 worldPos = ScreenToWorldTL(screenPos);
		for (int i = 0; i < static_cast<int>(m_Entities.size()); ++i)
		{
			if (HitTestEntity(m_Entities[i], worldPos))
			{
				m_SelectedIndex = i;
				return true;
			}
		}
		return false;
	}

	bool TemplateSpriteLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		float w = static_cast<float>(e.GetWidth());
		float h = static_cast<float>(e.GetHeight());
		float hw = w * 0.5f;
		float hh = h * 0.5f;

		m_ViewportSize = { w, h };

		m_CamTL.OnResize(hw, hh);
		m_CamTR.OnResize(hw, hh);
		m_CamBL.OnResize(hw, hh);
		m_CamBR.OnResize(hw, hh);

		return false;
	}

	// ============================================================================
	// Utility
	// ============================================================================

	glm::vec2 TemplateSpriteLayer::ScreenToWorldTL(glm::vec2 screenPos) const
	{
		float hw = m_ViewportSize.x * 0.5f;
		float hh = m_ViewportSize.y * 0.5f;

		float ndcX = (screenPos.x / hw) * 2.0f - 1.0f;
		float ndcY = -(screenPos.y / hh) * 2.0f + 1.0f;

		ndcX = glm::clamp(ndcX, -1.0f, 1.0f);
		ndcY = glm::clamp(ndcY, -1.0f, 1.0f);

		glm::mat4 invVP = glm::inverse(m_CamTL.GetCamera().GetViewProjectionMatrix());
		glm::vec4 world = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
		return { world.x, world.y };
	}

	bool TemplateSpriteLayer::HitTestEntity(Cosmic::Entity entity, glm::vec2 worldPos) const
	{
		if (!entity) return false;
		auto& t = entity.GetComponent<Cosmic::TransformComponent>();

		float hw = t.Scale.x * 0.5f;
		float hh = t.Scale.y * 0.5f;

		return (worldPos.x >= t.Position.x - hw && worldPos.x <= t.Position.x + hw &&
			worldPos.y >= t.Position.y - hh && worldPos.y <= t.Position.y + hh);
	}

} // namespace Workspace