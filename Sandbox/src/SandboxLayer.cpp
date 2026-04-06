#include "SandboxLayer.h"
#include "renderer/Renderer2D.h"
#include "renderer/RenderCommand.h"
#include "core/Input.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	SandboxLayer::SandboxLayer()
		: Layer("Sandbox")
	{
	}

	void SandboxLayer::OnAttach()
	{
		m_Texture = Texture2D::Create("assets/shaders/Texture.png");
		m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);

		// FIX: Manually enable engine stats on startup to match m_ShowStats
		Renderer2D::SetStatsStatus(m_ShowStats);
	}

	void SandboxLayer::OnDetach() {}

	void SandboxLayer::OnUpdate(float deltaTime)
	{
		// Toggle Stats Feature via F1
		if (Input::IsKeyPressed(KEY_F1))
		{
			m_ShowStats = !m_ShowStats;
			Renderer2D::SetStatsStatus(m_ShowStats);
		}

		// 1. Gameplay Logic
		if (Input::IsKeyPressed(KEY_SPACE) && m_IsGrounded)
		{
			m_VelocityY = 4.5f;
			m_IsGrounded = false;
		}

		if (Input::IsKeyPressed(KEY_LEFT))
			m_DinoRotation += 180.0f * deltaTime;
		if (Input::IsKeyPressed(KEY_RIGHT))
			m_DinoRotation -= 180.0f * deltaTime;

		if (!m_IsGrounded)
		{
			m_VelocityY -= 9.8f * deltaTime;
			m_DinoPos.y += m_VelocityY * deltaTime;
		}

		if (m_DinoPos.y <= -0.5f)
		{
			m_DinoPos.y = -0.5f;
			m_VelocityY = 0.0f;
			m_IsGrounded = true;
		}

		m_SpawnTimer += deltaTime;
		if (m_SpawnTimer > 2.0f)
		{
			m_Obstacles.push_back({ { 2.0f, -0.6f, 0.0f } });
			m_SpawnTimer = 0.0f;
		}

		for (int i = 0; i < (int)m_Obstacles.size(); i++)
		{
			m_Obstacles[i].Position.x -= 1.5f * deltaTime;
			if (glm::distance(m_DinoPos, m_Obstacles[i].Position) < 0.4f)
			{
				m_Obstacles.clear();
				m_DinoPos = { -1.0f, -0.5f, 0.0f };
				m_DinoRotation = 0.0f;
				break;
			}
		}

		// 2. Rendering
		OnRender();
	}

	void SandboxLayer::OnRender()
	{
		RenderCommand::SetClearColor({ 0.15f, 0.15f, 0.15f, 1.0f });
		RenderCommand::Clear();

		Renderer2D::BeginScene(*m_Camera);

		// Ground
		Renderer2D::DrawQuad({ 0.0f, -0.8f }, { 5.0f, 0.1f }, { 0.8f, 0.8f, 0.8f, 1.0f });

		// Obstacles
		for (const auto& obs : m_Obstacles)
		{
			Renderer2D::DrawQuad(obs.Position, { 0.4f, 0.6f }, { 0.9f, 0.2f, 0.2f, 1.0f });
		}

		// Dino
		Renderer2D::DrawRotatedQuad(m_DinoPos, { -0.6f, 0.6f }, glm::radians(m_DinoRotation), m_Texture);

		Renderer2D::EndScene();
	}

	void SandboxLayer::OnImGuiRender()
	{
		if (m_ShowStats)
		{
			auto stats = Renderer2D::GetStats();
			ImGui::Begin("Renderer Statistics");
			ImGui::Text("Draw Calls: %d", stats.DrawCalls);
			ImGui::Text("Quads: %d", stats.QuadCount);
			ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
			ImGui::Text("Indices: %d", stats.GetTotalIndexCount());
			ImGui::End();
		}

		ImGui::Begin("Dino Game");
		ImGui::Text("Altitude: %.2f", m_DinoPos.y + 0.5f);
		ImGui::Text("Pitch Angle: %.1f deg", m_DinoRotation);
		ImGui::Separator();
		ImGui::Text("Active Obstacles: %d", (int)m_Obstacles.size());
		ImGui::End();
	}

	void SandboxLayer::OnEvent(Event& event)
	{
	}
}