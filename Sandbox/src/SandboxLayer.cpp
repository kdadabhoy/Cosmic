#include "SandboxLayer.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

using namespace Cosmic;

SandboxLayer::SandboxLayer()
	: Layer("Sandbox")
{
}

void SandboxLayer::OnAttach()
{
	// Renderer2D is initialized in Renderer::Init(), so we just load assets
	m_Texture = Texture2D::Create("assets/textures/Dino.png");
	m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
}

void SandboxLayer::OnDetach() {}

void SandboxLayer::OnUpdate(float deltaTime)
{
	// --- Input & Physics Logic (Keep your existing logic) ---
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

	for (int i = 0; i < m_Obstacles.size(); i++)
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
}

void SandboxLayer::OnRender()
{
	RenderCommand::Clear(0.15f, 0.15f, 0.15f);

	// Start the 2D Batch
	Renderer2D::BeginScene(*m_Camera);

	// --- Render Ground (Solid Color Quad) ---
	Renderer2D::DrawQuad({ 0.0f, -0.8f }, { 5.0f, 0.1f }, { 0.8f, 0.8f, 0.8f, 1.0f });

	// --- Render Obstacles (Batching multiple red quads) ---
	for (const auto& obs : m_Obstacles)
	{
		Renderer2D::DrawQuad(obs.Position, { 0.4f, 0.6f }, { 0.9f, 0.2f, 0.2f, 1.0f });
	}

	// --- Render Dino (Textured + Rotated Quad) ---
	// Note: Renderer2D uses Radians for rotation internally, so we convert from degrees
	Renderer2D::DrawRotatedQuad(m_DinoPos, { 1.0f, 1.0f }, glm::radians(m_DinoRotation), m_Texture);

	// End the batch and submit to GPU
	Renderer2D::EndScene();
}

void SandboxLayer::OnImGuiRender()
{
	ImGui::Begin("Cosmic Flight Log");
	ImGui::Text("Altitude: %.2f", m_DinoPos.y + 0.5f);
	ImGui::Text("Pitch Angle: %.1f deg", m_DinoRotation);
	ImGui::Separator();
	ImGui::Text("Active Obstacles: %d", (int)m_Obstacles.size());
	ImGui::End();
}

void SandboxLayer::OnEvent(Cosmic::Event& event)
{
}