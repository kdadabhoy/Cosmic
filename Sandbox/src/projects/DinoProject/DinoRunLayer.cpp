#include "DinoRunLayer.h"
#include <imgui.h>

namespace Workspace
{
	DinoRunLayer::DinoRunLayer(Cosmic::Ref<Cosmic::Material> material)
		: m_Material(material), m_CameraController(1280.0f / 720.0f, true), m_RandomEngine(std::random_device{}())
	{
		Reset();
	}

	void DinoRunLayer::Reset()
	{
		m_DinoPos = { -1.0f, -0.5f, 0.0f };
		m_VelocityY = 0.0f;
		m_Score = 0.0f;
		m_IsGrounded = true;
		m_Obstacles.clear();
	}

	void DinoRunLayer::OnUpdate(float ts)
	{
		m_Score += ts * 10.0f;

		// Input
		if (Cosmic::Input::IsKeyPressed(KEY_SPACE) && m_IsGrounded)
		{
			m_VelocityY = 5.0f;
			m_IsGrounded = false;
		}

		// Physics
		if (!m_IsGrounded)
		{
			m_VelocityY -= 12.0f * ts;
			m_DinoPos.y += m_VelocityY * ts;
		}

		if (m_DinoPos.y <= -0.5f)
		{
			m_DinoPos.y = -0.5f;
			m_VelocityY = 0.0f;
			m_IsGrounded = true;
		}

		// Obstacle Spawning
		m_SpawnTimer += ts;
		if (m_SpawnTimer > m_NextSpawnTime)
		{
			float h = std::uniform_real_distribution<float>(0.3f, 0.8f)(m_RandomEngine);
			m_Obstacles.push_back({ { 2.5f, -0.8f + (h / 2.0f), 0.0f }, { 0.3f, h }, { 0.8f, 0.2f, 0.2f, 1.0f } });
			m_SpawnTimer = 0.0f;
			m_NextSpawnTime = std::uniform_real_distribution<float>(1.0f, 2.0f)(m_RandomEngine);
		}

		// Collision & Movement
		for (auto& obs : m_Obstacles)
		{
			obs.Position.x -= 2.0f * ts;
			if (std::abs(m_DinoPos.x - obs.Position.x) < 0.3f &&
				std::abs(m_DinoPos.y - obs.Position.y) < (obs.Size.y / 2.0f + 0.2f))
			{
				Reset();
				break;
			}
		}

		m_Obstacles.erase(std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
			[](const Obstacle& o) { return o.Position.x < -3.0f; }), m_Obstacles.end());

		m_CameraController.OnUpdate(ts);
	}

	void DinoRunLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Floor - Uses default batching
		Cosmic::Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 20.0f, 0.2f }, { 0.3f, 0.3f, 0.33f, 1.0f });

		// Obstacles - Uses default batching
		for (const auto& obs : m_Obstacles)
			Cosmic::Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);

		// Dino - Uses THE Material System
		// This will now respect the "u_Color" and "u_Texture" inside m_Material
		Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, m_Material);

		Cosmic::Renderer2D::EndScene();
	}

	void DinoRunLayer::OnImGuiRender()
	{
		ImGui::Text("Runner Game");
		ImGui::Value("Score", (int)m_Score);
		if (ImGui::Button("Reset Game")) Reset();
	}
}