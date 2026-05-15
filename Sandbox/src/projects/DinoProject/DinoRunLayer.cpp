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
		// Input polling for jumps (Fast response)
		if (Cosmic::Input::IsKeyPressed(CS_KEY_SPACE) && m_IsGrounded)
		{
			m_VelocityY = 5.0f;
			m_IsGrounded = false;
		}

		m_CameraController.OnUpdate(ts);
	}

	/**
	 * OnFixedUpdate
	 * * THE DETERMINISTIC CORE: All physics, spawning, and collision detection
	 * happens here. This prevents the "Tunneling" effect where an obstacle
	 * might skip past the Dino during a frame drop.
	 */
	void DinoRunLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_Score += deltaFixedTime * 10.0f;

		// 1. Gravity & Jump Physics
		if (!m_IsGrounded)
		{
			m_VelocityY -= 12.0f * deltaFixedTime; // Constant gravity
			m_DinoPos.y += m_VelocityY * deltaFixedTime;
		}

		// Floor Collision
		if (m_DinoPos.y <= -0.5f)
		{
			m_DinoPos.y = -0.5f;
			m_VelocityY = 0.0f;
			m_IsGrounded = true;
		}

		// 2. Obstacle Spawning Logic
		m_SpawnTimer += deltaFixedTime;
		if (m_SpawnTimer > m_NextSpawnTime)
		{
			float h = std::uniform_real_distribution<float>(0.3f, 0.8f)(m_RandomEngine);
			m_Obstacles.push_back({ { 2.5f, -0.8f + (h / 2.0f), 0.0f }, { 0.3f, h }, { 0.8f, 0.2f, 0.2f, 1.0f } });
			m_SpawnTimer = 0.0f;
			m_NextSpawnTime = std::uniform_real_distribution<float>(1.0f, 2.0f)(m_RandomEngine);
		}

		// 3. Movement & Collision Detection
		for (auto& obs : m_Obstacles)
		{
			obs.Position.x -= 2.0f * deltaFixedTime;

			// Simple AABB Collision
			if (std::abs(m_DinoPos.x - obs.Position.x) < 0.3f &&
				std::abs(m_DinoPos.y - obs.Position.y) < (obs.Size.y / 2.0f + 0.2f))
			{
				Reset();
				return; // Exit loop since we reset
			}
		}

		// Cleanup off-screen obstacles
		m_Obstacles.erase(std::remove_if(m_Obstacles.begin(), m_Obstacles.end(),
			[](const Obstacle& o) { return o.Position.x < -3.0f; }), m_Obstacles.end());
	}

	void DinoRunLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// Floor 
		Cosmic::Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 20.0f, 0.2f }, { 0.3f, 0.3f, 0.33f, 1.0f });

		// Obstacles 
		for (const auto& obs : m_Obstacles)
			Cosmic::Renderer2D::DrawQuad(obs.Position, obs.Size, obs.Color);

		// Dino - Respects the Material passed from the Project Manager
		Cosmic::Renderer2D::DrawQuad(m_DinoPos, { 0.5f, 0.5f }, m_Material);

		Cosmic::Renderer2D::EndScene();
	}

	void DinoRunLayer::OnImGuiRender()
	{
		ImGui::Text("Runner Simulation Stats");
		ImGui::Separator();
		ImGui::Value("Score", (int)m_Score);
		ImGui::Text("Grounded: %s", m_IsGrounded ? "Yes" : "No");
		ImGui::Text("Velocity Y: %.2f", m_VelocityY);

		if (ImGui::Button("Manual Reset")) Reset();
	}
}