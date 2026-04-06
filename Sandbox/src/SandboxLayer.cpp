#include "SandboxLayer.h"
#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "core/Input.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	SandboxLayer::SandboxLayer()
		: Layer("Sandbox"), m_RandomEngine(std::random_device{}())
	{
	}

	void SandboxLayer::OnAttach()
	{
		m_Texture = Texture2D::Create("assets/shaders/Texture.png");
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
		Renderer2D::SetStatsStatus(m_ShowStats);
	}

	void SandboxLayer::OnDetach() {}

	void SandboxLayer::ResetGame()
	{
		m_Obstacles.clear();
		m_DinoPos = { -1.0f, -0.5f, 0.0f };
		m_VelocityY = 0.0f;
		m_Score = 0.0f;
		m_IsGrounded = true;
	}

	void SandboxLayer::OnUpdate(float deltaTime)
	{
		// --- INPUT HANDLING & TOGGLES ---

		// Toggle Stats/Menu (F1)
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

		// Toggle Stress Mode (T)
		if (Input::IsKeyPressed(KEY_T))
		{
			if (!m_TKeyPressed)
			{
				m_StressTestMode = !m_StressTestMode;
				ResetGame();

				if (m_StressTestMode)
				{
					// Fill screen for the batching demo
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

		Renderer2D::BeginScene(*m_Camera);

		if (m_StressTestMode)
		{
			for (const auto& obs : m_Obstacles)
				Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);
		}
		else
		{
			for (int i = 0; i < 20; i++)
				Renderer2D::DrawQuad({ -1.5f + (i * 0.2f), 0.5f, -0.1f }, { 0.02f, 0.02f }, { 0.4f, 0.4f, 0.4f, 1.0f });

			Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 4.0f, 0.2f }, { 0.2f, 0.2f, 0.22f, 1.0f });

			for (const auto& obs : m_Obstacles)
				Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);

			float pulse = (sin(ImGui::GetTime() * 5.0f) + 1.0f) * 0.5f;
			glm::vec4 dinoTint = { 1.0f, 0.8f + (pulse * 0.2f), 0.8f + (pulse * 0.2f), 1.0f };

			// Flipped dino (Width = -0.5f)
			Renderer2D::DrawRotatedQuad(m_DinoPos, { -0.5f, 0.5f }, 0.0f, m_Texture, 1.0f, dinoTint);
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
			ImGui::Text("Performance Statistics:");
			ImGui::Text(" - Draw Calls: %d", cachedStats.DrawCalls);
			ImGui::Text(" - Quads: %d", cachedStats.QuadCount);
			ImGui::Text(" - Vertices: %d", cachedStats.GetTotalVertexCount());
			ImGui::Separator();
			ImGui::Text("Keybinds:");
			ImGui::BulletText("SPACE to Jump");
			ImGui::BulletText("'T' Toggle Stress Mode");
			ImGui::BulletText("'F1' Toggle This Menu");
			ImGui::End();
		}

		ImGui::Begin("Dino Game");
		ImGui::Text("Current Score: %.0f", m_Score);
		ImGui::ProgressBar(m_Score / 1000.0f, ImVec2(0.f, 0.f), "Level Progress");
		ImGui::End();
	}

	void SandboxLayer::OnEvent(Event& event) {}
}