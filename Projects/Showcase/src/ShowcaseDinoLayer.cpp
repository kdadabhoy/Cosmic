#include "ShowcaseDinoLayer.h"
#include <imgui.h>

namespace Showcase
{
	ShowcaseDinoLayer::ShowcaseDinoLayer(Cosmic::Ref<Cosmic::Scene> scene)
		: Cosmic::Layer("ShowcaseDinoLayer")
		, m_Scene(scene)
		, m_CameraController(1280.0f / 720.0f, false)
	{
	}

	void ShowcaseDinoLayer::OnAttach()
	{
		CS_INFO("ShowcaseDinoLayer: Attaching simulation. Resolving virtual assets...");
		m_CameraController.SetZoomLevel(3.5f);

		// 1. Keep original asset paths—the textures match their file names alphabetically
		std::vector<std::string> vfsPaths = {
			"project://sprites/DinoSprites - doux.png", // Index 0 = Blue (Vita)
			"project://sprites/DinoSprites - mort.png", // Index 1 = Red (Mort)
			"project://sprites/DinoSprites - tard.png", // Index 2 = Yellow (Tard)
			"project://sprites/DinoSprites - vita.png"  // Index 3 = Green (Doux)
		};
		m_TextureNames = { "Blue (Vita)", "Red (Mort)", "Yellow (Tard)","Green (Doux)" };

		for (const auto& path : vfsPaths)
		{
			std::string resolvedPath = Cosmic::FileSystem::Resolve(path);
			auto texture = Cosmic::Texture2D::Create(resolvedPath);
			if (texture)
			{
				m_TexturePool.push_back(texture);
			}
			else
			{
				CS_CORE_ERROR("ShowcaseDinoLayer: Failed to load atlas via VFS: {0}", path);
			}
		}

		if (m_TexturePool.empty()) return;

		// 2. Clear out index confusion—assign each Dino its true matching color index
		m_Dinos.resize(4);

		m_Dinos[0].Name = "Player Dino (Main)";
		m_Dinos[0].SelectedAtlasIndex = 0;       // 0 = Blue
		m_Dinos[0].AtlasCoords = { 0.0f, 0.0f };

		m_Dinos[1].Name = "Companion Dino Alpha";
		m_Dinos[1].SelectedAtlasIndex = 1;       // 1 = Red
		m_Dinos[1].AtlasCoords = { 4.0f, 0.0f };

		m_Dinos[2].Name = "Companion Dino Beta";
		m_Dinos[2].SelectedAtlasIndex = 2;       // 2 = Yellow
		m_Dinos[2].AtlasCoords = { 6.0f, 0.0f };

		m_Dinos[3].Name = "Companion Dino Gamma";
		m_Dinos[3].SelectedAtlasIndex = 3;       // 3 = Green
		m_Dinos[3].AtlasCoords = { 14.0f, 0.0f };

		// 3. Register entity configurations directly inside EnTT registry
		for (size_t i = 0; i < m_Dinos.size(); ++i)
		{
			m_Dinos[i].EntityHandle = m_Scene->CreateEntity(m_Dinos[i].Name);
			auto& trans = m_Dinos[i].EntityHandle.GetComponent<Cosmic::TransformComponent>();
			m_Dinos[i].EntityHandle.AddComponent<Cosmic::SpriteRendererComponent>();

			if (i == 0)
			{
				trans.Position = { 0.0f, 0.0f, 0.0f };
				trans.Scale = { 1.5f, 1.5f };
			}
			else
			{
				// Stagger companions across the scene matrix floor
				trans.Position = { -2.0f + (static_cast<float>(i) * 1.5f), -1.2f, 0.0f };
				trans.Scale = { 0.9f, 0.9f };
			}

			UpdateDinoSubTexture(i);
		}
	}

	void ShowcaseDinoLayer::UpdateDinoSubTexture(size_t index)
	{
		auto& dino = m_Dinos[index];
		dino.ActiveAtlas = m_TexturePool[dino.SelectedAtlasIndex];
		dino.SubTexture = Cosmic::SubTexture2D::CreateFromCoords(dino.ActiveAtlas, dino.AtlasCoords, m_SpriteCellSize);
	}

	void ShowcaseDinoLayer::OnDetach()
	{
		for (auto& dino : m_Dinos)
		{
			if (dino.EntityHandle) m_Scene->DestroyEntity(dino.EntityHandle);
			dino.ActiveAtlas.reset();
			dino.SubTexture.reset();
		}
		m_TexturePool.clear();
	}

	void ShowcaseDinoLayer::OnFixedUpdate(float deltaFixedTime)
	{
		if (m_Dinos.empty() || !m_Dinos[0].EntityHandle) return;

		// Handle Main Player input logic tracking
		auto& trans = m_Dinos[0].EntityHandle.GetComponent<Cosmic::TransformComponent>();
		auto& sprite = m_Dinos[0].EntityHandle.GetComponent<Cosmic::SpriteRendererComponent>();

		if (Cosmic::Input::IsKeyPressed(CS_KEY_W) || Cosmic::Input::IsKeyPressed(CS_KEY_UP))    trans.Position.y += m_MoveSpeed * deltaFixedTime;
		if (Cosmic::Input::IsKeyPressed(CS_KEY_S) || Cosmic::Input::IsKeyPressed(CS_KEY_DOWN))  trans.Position.y -= m_MoveSpeed * deltaFixedTime;
		if (Cosmic::Input::IsKeyPressed(CS_KEY_A) || Cosmic::Input::IsKeyPressed(CS_KEY_LEFT))
		{
			trans.Position.x -= m_MoveSpeed * deltaFixedTime;
			sprite.FlipX = true;
		}
		if (Cosmic::Input::IsKeyPressed(CS_KEY_D) || Cosmic::Input::IsKeyPressed(CS_KEY_RIGHT))
		{
			trans.Position.x += m_MoveSpeed * deltaFixedTime;
			sprite.FlipX = false;
		}

		// Apply continuous wave hovering animations for the companions (Indices 1 to 3)
		for (size_t i = 1; i < m_Dinos.size(); ++i)
		{
			if (!m_Dinos[i].EntityHandle) continue;
			auto& compTrans = m_Dinos[i].EntityHandle.GetComponent<Cosmic::TransformComponent>();
			compTrans.Position.y = -1.0f + (sin(Cosmic::Layer::GetLocalTime() * 2.5f + static_cast<float>(i)) * 0.20f);
		}
	}

	void ShowcaseDinoLayer::OnUpdate(float ts)
	{
		auto fb = Cosmic::Application::Get().GetFrameBuffer();
		float activeWidth = static_cast<float>(fb->GetWidth());
		float activeHeight = static_cast<float>(fb->GetHeight());

		if (m_ViewportSize.x != activeWidth || m_ViewportSize.y != activeHeight)
		{
			m_ViewportSize = { activeWidth, activeHeight };
			m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		}

		m_CameraController.OnUpdate(ts);

		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Draw backdrop grid guidelines
		for (float x = -10.0f; x <= 10.0f; x += 1.0f)
			Cosmic::Renderer2D::DrawLine({ x, -6.0f, -0.1f }, { x, 6.0f, -0.1f }, { 0.11f, 0.11f, 0.13f, 1.0f });
		for (float y = -6.0f; y <= 6.0f; y += 1.0f)
			Cosmic::Renderer2D::DrawLine({ -10.0f, y, -0.1f }, { 10.0f, y, -0.1f }, { 0.11f, 0.11f, 0.13f, 1.0f });

		// Iterate and batch-render all four dinos
		for (auto& dino : m_Dinos)
		{
			if (!dino.EntityHandle || !dino.SubTexture) continue;

			auto& trans = dino.EntityHandle.GetComponent<Cosmic::TransformComponent>();
			auto& sprite = dino.EntityHandle.GetComponent<Cosmic::SpriteRendererComponent>();

			glm::vec2 flippedScale = {
				trans.Scale.x * (sprite.FlipX ? -1.0f : 1.0f),
				trans.Scale.y * (sprite.FlipY ? -1.0f : 1.0f)
			};

			glm::vec3 exactPos = trans.Position;
			Cosmic::Renderer2D::DrawQuad(exactPos, flippedScale, dino.SubTexture, glm::vec4(1.0f));
		}

		Cosmic::Renderer2D::EndScene();
	}

	void ShowcaseDinoLayer::OnImGuiRender()
	{
		ImGui::Begin("Multi-Dino Sprite Sheet Inspector");
		ImGui::Text("--- Engine VFS Pack Slicing Simulation ---");
		ImGui::Separator();

		// =========================================================================
		// 🦖 TEXT ATLAS CHEAT SHEET DOCUMENTATION BOX
		// =========================================================================
		if (ImGui::CollapsingHeader("CLICK TO READ THIS: Atlas Frame Cheat Sheet (Arks 24x24 px)"))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
			ImGui::TextWrapped("The sheet is sliced horizontally along Row 0. Keep the Row coordinate at 0 for standard frames!");
			ImGui::PopStyleColor();

			ImGui::Spacing();
			ImGui::BulletText("Columns 0 -> 2   : Idle Cycles (Standing/Blinking)");
			ImGui::BulletText("Columns 4 -> 9   : Move / Run Cycle Animation");
			ImGui::BulletText("Columns 11 -> 13 : Kick / Hurt Stance");
			ImGui::BulletText("Columns 14 -> 16 : Shocked / Eyes Open");
			ImGui::BulletText("Columns 18 -> 23 : Sneak / Ducking Down");

			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
			ImGui::TextWrapped("Caution: Row values > 0 or Column values > 23 sample empty texture data zones!");
			ImGui::PopStyleColor();
			ImGui::Separator();
		}

		ImGui::Spacing();

		// Pipeline control sliders
		ImGui::SliderFloat("Global Run Speed", &m_MoveSpeed, 1.0f, 15.0f, "%.1f m/s");
		if (ImGui::DragFloat2("Asset Cell Size Base (Px)", &m_SpriteCellSize.x, 1.0f, 8.0f, 64.0f, "%.0f px"))
		{
			for (size_t i = 0; i < m_Dinos.size(); ++i) UpdateDinoSubTexture(i);
		}

		ImGui::Spacing();

		// Create unique selection configurations for each dinosaur variant
		for (size_t i = 0; i < m_Dinos.size(); ++i)
		{
			auto& dino = m_Dinos[i];
			if (ImGui::TreeNode(dino.Name.c_str()))
			{
				auto& sprite = dino.EntityHandle.GetComponent<Cosmic::SpriteRendererComponent>();

				// 1. Dropdown Selector for switching the Atlas Texture
				if (ImGui::BeginCombo("Texture Variant", m_TextureNames[dino.SelectedAtlasIndex].c_str()))
				{
					for (int n = 0; n < m_TextureNames.size(); n++)
					{
						bool isSelected = (dino.SelectedAtlasIndex == n);
						if (ImGui::Selectable(m_TextureNames[n].c_str(), isSelected))
						{
							dino.SelectedAtlasIndex = n;
							UpdateDinoSubTexture(i);
						}
						if (isSelected) ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}

				// 2. Frame Navigation controls with interactive text help markers
				if (ImGui::DragFloat2("Grid Coord Offset (Col/Row)", &dino.AtlasCoords.x, 1.0f, 0.0f, 24.0f, "%.0f"))
				{
					UpdateDinoSubTexture(i);
				}

				// Inline item tooltip description helper
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("X = Horizontal Frame Column (0-23)");
					ImGui::Text("Y = Vertical Sheet Row (Set to 0 for these sheets!)");
					ImGui::EndTooltip();
				}

				// 3. Individual orientation flip checks
				ImGui::Checkbox("FlipX (Mirror Direction)", &sprite.FlipX);
				ImGui::Checkbox("FlipY (Invert Gravity)", &sprite.FlipY);

				ImGui::TreePop();
				ImGui::Separator();
			}
		}

		ImGui::End();
	}

	void ShowcaseDinoLayer::OnEvent(Cosmic::Event& e)
	{
		m_CameraController.OnEvent(e);
		Cosmic::EventDispatcher dispatcher(e);
		dispatcher.Dispatch<Cosmic::WindowResizeEvent>([this](Cosmic::WindowResizeEvent& event) { return OnWindowResize(event); });
	}

	bool ShowcaseDinoLayer::OnWindowResize(Cosmic::WindowResizeEvent& e)
	{
		m_ViewportSize = { static_cast<float>(e.GetWidth()), static_cast<float>(e.GetHeight()) };
		m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
		return false;
	}
}