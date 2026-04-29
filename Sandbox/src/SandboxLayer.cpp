#include "SandboxLayer.h"
#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "core/Input.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	SandboxLayer::SandboxLayer()
		: Layer("Sandbox"), m_RandomEngine(std::random_device{}()),
		m_CameraController(1280.0f / 720.0f, true) // Initialize with 16:9 and rotation enabled
	{
	}

	void SandboxLayer::OnAttach()
	{
		m_Texture = Texture2D::Create("assets/shaders/Texture.png");
		Renderer2D::SetStatsStatus(m_ShowStats);
	}

	void SandboxLayer::OnDetach() {}

	void SandboxLayer::ResetGame()
	{
		m_Obstacles.clear();
		m_DinoPos = { -1.0f, -0.5f, 0.0f };
		m_DinoRotation = 0.0f;
		m_VelocityY = 0.0f;
		m_Score = 0.0f;
		m_IsGrounded = true;
	}

	void SandboxLayer::OnUpdate(float deltaTime)
	{
		m_SmoothedDeltaTime = m_SmoothedDeltaTime * 0.95f + deltaTime * 0.05f;

		// --- CAMERA CONTROLLER UPDATE ---
		// This handles WASD / Scrolling / Zooming
		m_CameraController.OnUpdate(deltaTime);

		// --- INPUT HANDLING & TOGGLES ---
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
							m_Obstacles.push_back({ {x, y, 0.0f}, {0.03f, 0.03f},
								{(x + 1.6f) / 3.2f, 0.2f, (y + 0.9f) / 1.8f, 1.0f} });
				}
				m_TKeyPressed = true;
			}
		}
		else { m_TKeyPressed = false; }

		// --- GAMEPLAY LOGIC ---
		if (!m_StressTestMode)
		{
			m_Score += deltaTime * 10.0f;

			// Rotation Logic (Game Logic)
			float rotationSpeed = 5.0f;
			if (Input::IsKeyPressed(KEY_LEFT))
				m_DinoRotation += rotationSpeed * deltaTime;
			if (Input::IsKeyPressed(KEY_RIGHT))
				m_DinoRotation -= rotationSpeed * deltaTime;

			// Jump Logic
			if (Input::IsKeyPressed(KEY_SPACE) && m_IsGrounded)
			{
				m_VelocityY = 5.0f;
				m_IsGrounded = false;
			}

			if (!m_IsGrounded)
			{
				m_VelocityY -= 12.0f * deltaTime;
				m_DinoPos.y += m_VelocityY * deltaTime;
			}

			if (m_DinoPos.y <= -0.5f)
			{
				m_DinoPos.y = -0.5f;
				m_VelocityY = 0.0f;
				m_IsGrounded = true;
			}

			// Obstacle Spawning
			m_SpawnTimer += deltaTime;
			if (m_SpawnTimer > m_NextSpawnTime)
			{
				std::uniform_real_distribution<float> heightDist(0.3f, 0.8f);
				std::uniform_real_distribution<float> timeDist(1.0f, 2.5f);

				float h = heightDist(m_RandomEngine);
				m_Obstacles.push_back({ { 2.0f, -0.8f + (h / 2.0f), 0.0f }, { 0.3f, h }, { 0.9f, 0.1f, 0.1f, 1.0f } });

				m_SpawnTimer = 0.0f;
				m_NextSpawnTime = timeDist(m_RandomEngine);
			}

			// Collision & Movement
			for (int i = 0; i < (int)m_Obstacles.size(); i++)
			{
				m_Obstacles[i].Position.x -= 1.8f * deltaTime;

				bool colX = m_DinoPos.x + 0.2f > m_Obstacles[i].Position.x - (m_Obstacles[i].Size.x / 2) &&
					m_Obstacles[i].Position.x + (m_Obstacles[i].Size.x / 2) > m_DinoPos.x - 0.2f;
				bool colY = m_DinoPos.y + 0.2f > m_Obstacles[i].Position.y - (m_Obstacles[i].Size.y / 2) &&
					m_Obstacles[i].Position.y + (m_Obstacles[i].Size.y / 2) > m_DinoPos.y - 0.2f;

				if (colX && colY) { ResetGame(); break; }
			}

			m_Obstacles.erase(std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
				[](const Obstacle& o) { return o.Position.x < -2.5f; }), m_Obstacles.end());
		}

		Renderer2D::ResetStats();
		OnRender();
	}

	void SandboxLayer::OnRender()
	{
		RenderCommand::SetClearColor({ 0.12f, 0.12f, 0.12f, 1.0f });
		RenderCommand::Clear();

		// Start scene with the camera managed by the controller
		Renderer2D::BeginScene(m_CameraController.GetCamera());

		if (m_StressTestMode)
		{
			for (const auto& obs : m_Obstacles)
				Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);
		}
		else
		{
			// Background / Floor
			for (int i = 0; i < 20; i++)
				Renderer2D::DrawQuad({ -1.5f + (i * 0.2f), 0.5f, -0.1f }, { 0.02f, 0.02f }, { 0.4f, 0.4f, 0.4f, 1.0f });

			Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 4.0f, 0.2f }, { 0.2f, 0.2f, 0.22f, 1.0f });

			for (const auto& obs : m_Obstacles)
				Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);

			float pulse = (sin(ImGui::GetTime() * 5.0f) + 1.0f) * 0.5f;
			glm::vec4 dinoTint = { 1.0f, 0.8f + (pulse * 0.2f), 0.8f + (pulse * 0.2f), 1.0f };

			// Draw Dino with Rotation
			Renderer2D::DrawRotatedQuad(m_DinoPos, { 0.5f, 0.5f }, m_DinoRotation, m_Texture, 1.0f, dinoTint);
		}

		Renderer2D::EndScene();
	}

	void SandboxLayer::OnImGuiRender()
	{
		static Renderer2D::Statistics cachedStats;
		cachedStats = Renderer2D::GetStats();

		if (m_ShowStats)
		{
			ImGui::Begin("Cosmic Engine Monitor");

			float fps = 1.0f / m_SmoothedDeltaTime;
			float ms = m_SmoothedDeltaTime * 1000.0f;

			ImGui::Text("Timing (Smoothed):");
			ImGui::Text(" - FPS: %.0f", fps);
			ImGui::Text(" - Frame Time: %.2f ms", ms);

			ImGui::Separator();

			ImGui::Text("Performance Statistics (Current Frame):");
			ImGui::Text(" - Draw Calls: %d", cachedStats.DrawCalls);
			ImGui::Text(" - Quads: %d", cachedStats.QuadCount);
			ImGui::Text(" - Vertices: %d", cachedStats.GetTotalVertexCount());

			ImGui::Separator();
			ImGui::Text("Keybinds:");
			ImGui::BulletText("SPACE: Jump");
			ImGui::BulletText("LEFT/RIGHT ARROWS: Rotate Dino");
			ImGui::BulletText("'T': Toggle Stress Mode");
			ImGui::BulletText("'F1': Toggle This Menu");
			ImGui::BulletText("WASD: Move Camera");
			ImGui::BulletText("Scroll: Zoom Camera");
			ImGui::End();
		}

		ImGui::Begin("Dino Game");
		ImGui::Text("Current Score: %.0f", m_Score);
		ImGui::ProgressBar(m_Score / 1000.0f, ImVec2(0.f, 0.f), "Level Progress");
		ImGui::End();
	}

	void SandboxLayer::OnEvent(Event& event)
	{
		// Pass events to the camera controller (Handles scrolling and resizing)
		m_CameraController.OnEvent(event);
	}
}