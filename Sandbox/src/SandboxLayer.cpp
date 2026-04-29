#include "SandboxLayer.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>

namespace Cosmic
{
	SandboxLayer::SandboxLayer()
		: Layer("Sandbox"), m_RandomEngine(std::random_device{}()),
		m_CameraController(1280.0f / 720.0f, true)
	{
	}

	void SandboxLayer::OnAttach()
	{
		m_Texture = Texture2D::Create("assets/shaders/Texture.png");
		Renderer2D::SetStatsStatus(m_ShowStats);
		ResetGame();
		ResetCamera();
	}

	void SandboxLayer::OnDetach() {}

	void SandboxLayer::ResetGame()
	{
		m_Obstacles.clear();
		m_FlightPath.clear();
		m_VelocityY = 0.0f;
		m_Score = 0.0f;
		m_IsGrounded = true;
		m_DinoRotation = 0.0f;

		if (m_CurrentMode == SceneMode::DinoRunner)
			m_DinoPos = { -1.0f, -0.5f, 0.0f };
		else
			m_DinoPos = { 0.0f, 0.0f, 0.0f };
	}

	void SandboxLayer::ResetCamera()
	{
		m_CameraController.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });
		m_CameraController.SetZoomLevel(1.0f);
	}

	void SandboxLayer::OnUpdate(float deltaTime)
	{
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + deltaTime * 0.05f;

		// --- 1. Viewport Resizing Logic ---
		if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
		{
			auto& fb = Application::Get().GetFrameBuffer();
			if (fb->GetWidth() != (uint32_t)m_ViewportSize.x || fb->GetHeight() != (uint32_t)m_ViewportSize.y)
			{
				fb->Resize((uint32_t)m_ViewportSize.x, (uint32_t)m_ViewportSize.y);
				m_CameraController.OnResize(m_ViewportSize.x, m_ViewportSize.y);
			}
		}

		// --- 2. Global Input (Always Active) ---
		if (Input::IsKeyPressed(KEY_F))
		{
			if (!m_FKeyPressed)
			{
				m_CurrentMode = (m_CurrentMode == SceneMode::DinoRunner) ? SceneMode::FlightSim : SceneMode::DinoRunner;
				ResetGame(); ResetCamera(); m_FKeyPressed = true;
			}
		}
		else { m_FKeyPressed = false; }

		if (Input::IsKeyPressed(KEY_F1))
		{
			if (!m_F1KeyPressed)
			{
				m_ShowStats = !m_ShowStats;
				Renderer2D::SetStatsStatus(m_ShowStats);
				m_F1KeyPressed = true;
			}
		}
		else { m_F1KeyPressed = false; }

		// --- 3. Game Logic (Only updates if Viewport is active) ---
		if (m_ViewportFocused || m_ViewportHovered)
		{
			m_CameraController.OnUpdate(deltaTime);

			if (Input::IsKeyPressed(KEY_G))
			{
				if (!m_GKeyPressed) { m_ChaosMode = !m_ChaosMode; m_GKeyPressed = true; }
			}
			else { m_GKeyPressed = false; }

			if (m_CurrentMode == SceneMode::DinoRunner)
			{
				if (Input::IsKeyPressed(KEY_T))
				{
					if (!m_TKeyPressed)
					{
						m_StressTestMode = !m_StressTestMode;
						ResetGame();
						if (m_StressTestMode)
						{
							for (float y = -0.9f; y < 0.9f; y += 0.04f)
								for (float x = -1.6f; x < 1.6f; x += 0.04f)
									m_Obstacles.push_back({ {x, y, 0.0f}, {0.03f, 0.03f}, {(x + 1.6f) / 3.2f, 0.2f, (y + 0.9f) / 1.8f, 1.0f} });
						}
						m_TKeyPressed = true;
					}
				}
				else { m_TKeyPressed = false; }

				if (!m_StressTestMode)
				{
					m_Score += deltaTime * 10.0f;
					if (Input::IsKeyPressed(KEY_LEFT)) m_DinoRotation += 5.0f * deltaTime;
					if (Input::IsKeyPressed(KEY_RIGHT)) m_DinoRotation -= 5.0f * deltaTime;

					if (Input::IsKeyPressed(KEY_SPACE) && m_IsGrounded) { m_VelocityY = 5.0f; m_IsGrounded = false; }

					if (!m_IsGrounded)
					{
						m_VelocityY -= 12.0f * deltaTime;
						m_DinoPos.y += m_VelocityY * deltaTime;
					}

					if (m_DinoPos.y <= -0.5f) { m_DinoPos.y = -0.5f; m_VelocityY = 0.0f; m_IsGrounded = true; }

					if (m_ChaosMode)
					{
						std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
						m_DinoPos.x += jitter(m_RandomEngine);
						m_DinoRotation += jitter(m_RandomEngine) * 50.0f;
					}

					m_SpawnTimer += deltaTime;
					if (m_SpawnTimer > m_NextSpawnTime)
					{
						std::uniform_real_distribution<float> hDist(0.3f, 0.8f);
						float h = hDist(m_RandomEngine);
						m_Obstacles.push_back({ { 2.0f, -0.8f + (h / 2.0f), 0.0f }, { 0.3f, h }, { 0.9f, 0.1f, 0.1f, 1.0f } });
						m_SpawnTimer = 0.0f;
						m_NextSpawnTime = std::uniform_real_distribution<float>(1.0f, 2.5f)(m_RandomEngine);
					}

					for (auto& obs : m_Obstacles)
					{
						obs.Position.x -= 1.8f * deltaTime;
						bool colX = m_DinoPos.x + 0.2f > obs.Position.x - (obs.Size.x / 2) && obs.Position.x + (obs.Size.x / 2) > m_DinoPos.x - 0.2f;
						bool colY = m_DinoPos.y + 0.2f > obs.Position.y - (obs.Size.y / 2) && obs.Position.y + (obs.Size.y / 2) > m_DinoPos.y - 0.2f;
						if (colX && colY) { ResetGame(); break; }
					}
					m_Obstacles.erase(std::remove_if(m_Obstacles.begin(), m_Obstacles.end(), [](const Obstacle& o) { return o.Position.x < -2.5f; }), m_Obstacles.end());
				}
			}
			else // Flight Sim Mode
			{
				m_DinoPos.x += m_FlightSpeed * deltaTime;
				m_DinoPos.y += (m_FlightSpeed * m_FlightSlope) * deltaTime;

				if (m_ChaosMode)
				{
					std::uniform_real_distribution<float> noise(-0.2f, 0.2f);
					m_DinoPos.x += noise(m_RandomEngine); m_DinoPos.y += noise(m_RandomEngine);
				}

				m_FlightPath.push_back(m_DinoPos);
				if (m_FlightPath.size() > 500) m_FlightPath.erase(m_FlightPath.begin());

				if (Input::IsKeyPressed(KEY_C))
				{
					if (!m_CKeyPressed) { m_CameraFollow = !m_CameraFollow; m_CKeyPressed = true; }
				}
				else { m_CKeyPressed = false; }

				if (m_CameraFollow) m_CameraController.SetPosition({ m_DinoPos.x, m_DinoPos.y, 0.0f });
			}
		}

		Renderer2D::ResetStats();
		OnRender();
	}

	void SandboxLayer::OnRender()
	{
		Renderer2D::BeginScene(m_CameraController.GetCamera());

		if (m_CurrentMode == SceneMode::DinoRunner)
		{
			if (m_StressTestMode)
			{
				for (const auto& obs : m_Obstacles) Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);
			}
			else
			{
				for (int i = 0; i < 20; i++) Renderer2D::DrawQuad({ -1.5f + (i * 0.2f), 0.5f, -0.1f }, { 0.02f, 0.02f }, { 0.4f, 0.4f, 0.4f, 1.0f });
				Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 4.0f, 0.2f }, { 0.2f, 0.2f, 0.22f, 1.0f });
				for (const auto& obs : m_Obstacles) Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);

				float pulse = (sin(ImGui::GetTime() * 5.0f) + 1.0f) * 0.5f;
				Renderer2D::DrawRotatedQuad(m_DinoPos, { 0.5f, 0.5f }, m_DinoRotation, m_Texture, 1.0f, { 1.0f, 0.8f + (pulse * 0.2f), 0.8f + (pulse * 0.2f), 1.0f });
			}
		}
		else
		{
			float startX = floor(m_DinoPos.x) - 10;
			float startY = floor(m_DinoPos.y) - 10;
			for (float x = startX; x < startX + 20; x += 1.0f)
			{
				for (float y = startY; y < startY + 20; y += 1.0f)
				{
					bool isEven = (int(x) + int(y)) % 2 == 0;
					Renderer2D::DrawQuad({ x, y, -0.1f }, { 1.0f, 1.0f }, isEven ? glm::vec4(0.2f, 0.2f, 0.25f, 1.0f) : glm::vec4(0.15f, 0.15f, 0.18f, 1.0f));
				}
			}

			if (m_FlightPath.size() > 1)
			{
				for (size_t i = 0; i < m_FlightPath.size() - 1; i++)
					Renderer2D::DrawLine(m_FlightPath[i], m_FlightPath[i + 1], { 1.0f, 0.0f, 0.0f, 1.0f });
			}
			Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, m_Texture);
		}

		Renderer2D::EndScene();
	}

	void SandboxLayer::OnImGuiRender()
	{
		static bool dockspaceOpen = true;
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("CosmicEditorDockSpace", &dockspaceOpen, window_flags);
		ImGui::PopStyleVar(3);

		ImGuiIO& io = ImGui::GetIO();
		if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
		{
			ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

			if (!ImGui::DockBuilderGetNode(dockspace_id))
			{
				ImGui::DockBuilderRemoveNode(dockspace_id);
				ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

				ImGuiID dock_main_id = dockspace_id;
				ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.20f, nullptr, &dock_main_id);
				ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
				ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

				ImGui::DockBuilderDockWindow("Cosmic Engine Monitor", dock_id_left);
				ImGui::DockBuilderDockWindow("Mission Control", dock_id_right);
				ImGui::DockBuilderDockWindow("Camera Settings", dock_id_bottom);
				ImGui::DockBuilderDockWindow("Viewport", dock_main_id);
				ImGui::DockBuilderFinish(dockspace_id);
			}
			ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
		}

		// --- Viewport Window ---
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
		ImGui::Begin("Viewport");

		m_ViewportFocused = ImGui::IsWindowFocused();
		m_ViewportHovered = ImGui::IsWindowHovered();
		Application::Get().GetImGuiLayer()->BlockEvents(!m_ViewportFocused && !m_ViewportHovered);

		ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
		m_ViewportSize = { viewportPanelSize.x, viewportPanelSize.y };

		uint32_t textureID = Application::Get().GetFrameBuffer()->GetColorAttachmentRendererID();
		ImGui::Image((void*)(uintptr_t)textureID, viewportPanelSize, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

		ImGui::End();
		ImGui::PopStyleVar();

		// --- Mission Control ---
		ImGui::Begin("Mission Control");
		const char* modes[] = { "Dino Runner", "Flight Simulator" };
		int currentModeIdx = (int)m_CurrentMode;
		if (ImGui::Combo("Active Scene", &currentModeIdx, modes, IM_ARRAYSIZE(modes)))
		{
			m_CurrentMode = (SceneMode)currentModeIdx; ResetGame(); ResetCamera();
		}
		ImGui::Checkbox("Chaos Mode [G]", &m_ChaosMode);
		if (m_CurrentMode == SceneMode::DinoRunner)
		{
			ImGui::Text("Score: %.0f", m_Score);
			ImGui::Checkbox("Stress Test", &m_StressTestMode);
		}
		else
		{
			ImGui::Checkbox("Camera Follow", &m_CameraFollow);
			ImGui::DragFloat("Flight Speed", &m_FlightSpeed, 0.1f, 0.0f, 20.0f);
			ImGui::DragFloat("Flight Slope", &m_FlightSlope, 0.05f, -2.0f, 2.0f);
		}
		if (ImGui::Button("Reset Scene")) ResetGame();
		ImGui::End();

		// --- Engine Monitor ---
		ImGui::Begin("Cosmic Engine Monitor");
		ImGui::Text("Timing:");
		ImGui::Text(" - FPS: %.0f", 1.0f / m_SmoothedDeltaTime);
		ImGui::Text(" - Frame Time: %.2f ms", m_SmoothedDeltaTime * 1000.0f);
		ImGui::Separator();
		auto stats = Renderer2D::GetStats();
		ImGui::Text("Renderer Statistics:");
		ImGui::Text(" - Draw Calls: %d", stats.DrawCalls);
		ImGui::Text(" - Quads: %d", stats.QuadCount);
		ImGui::End();

		// --- Camera Settings ---
		ImGui::Begin("Camera Settings");
		float zoomLevel = m_CameraController.GetZoomLevel();
		if (ImGui::DragFloat("Zoom Level", &zoomLevel, 0.1f, 0.1f, 10.0f)) m_CameraController.SetZoomLevel(zoomLevel);
		if (ImGui::Button("Reset Camera View")) ResetCamera();
		ImGui::End();

		ImGui::End(); // End DockSpace
	}

	void SandboxLayer::OnEvent(Event& event)
	{
		if (m_ViewportFocused || m_ViewportHovered)
			m_CameraController.OnEvent(event);
	}
}