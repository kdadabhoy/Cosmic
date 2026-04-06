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
	// Vertices for a standard quad
	// Note: TexCoords are flipped as per your request (1.0 at left, 0.0 at right)
	float vertices[] = {
		-0.4f, -0.4f, 0.0f,  1.0f, 0.0f, // Bottom Left
		 0.4f, -0.4f, 0.0f,  0.0f, 0.0f, // Bottom Right
		 0.4f,  0.4f, 0.0f,  0.0f, 1.0f, // Top Right
		-0.4f,  0.4f, 0.0f,  1.0f, 1.0f  // Top Left
	};

	m_VAO = VertexArray::Create();

	m_VBO = VertexBuffer::Create(vertices, sizeof(vertices));
	m_VBO->SetLayout({
		{ ShaderDataType::Float3, "a_Position" },
		{ ShaderDataType::Float2, "a_TexCoord" }
		});
	m_VAO->AddVertexBuffer(m_VBO);

	uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };
	m_IBO = IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t));
	m_VAO->SetIndexBuffer(m_IBO);

	// Load Shaders and Textures
	m_TextureShader = Shader::Create("assets/shaders/Texture.glsl");
	m_FlatColorShader = Shader::Create("assets/shaders/FlatColor.glsl");
	m_Texture = Texture::Create("assets/shaders/Texture.png"); // Ensure path is correct

	// Initialize Camera (Aspect Ratio 16:9)
	m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
}

void SandboxLayer::OnDetach()
{
}

void SandboxLayer::OnUpdate(float deltaTime)
{
	// 1. Jump Logic
	if (Input::IsKeyPressed(KEY_SPACE) && m_IsGrounded)
	{
		m_VelocityY = 2.5f; // Initial Jump Impulse
		m_IsGrounded = false;
	}

	// 2. Physics (Gravity)
	if (!m_IsGrounded)
	{
		m_VelocityY -= 9.8f * deltaTime;
		m_DinoPos.y += m_VelocityY * deltaTime;
	}

	// 3. Ground Collision
	if (m_DinoPos.y <= -0.5f)
	{
		m_DinoPos.y = -0.5f;
		m_VelocityY = 0.0f;
		m_IsGrounded = true;
	}

	// 4. Obstacle Spawning
	m_SpawnTimer += deltaTime;
	if (m_SpawnTimer > 2.0f)
	{
		m_Obstacles.push_back({ { 2.0f, -0.6f, 0.0f } });
		m_SpawnTimer = 0.0f;
	}

	// 5. Obstacle Movement & Collision
	for (int i = 0; i < m_Obstacles.size(); i++)
	{
		m_Obstacles[i].Position.x -= 1.5f * deltaTime;

		// Simple distance-based collision
		if (glm::distance(m_DinoPos, m_Obstacles[i].Position) < 0.4f)
		{
			m_Obstacles.clear();
			m_DinoPos = { -1.0f, -0.5f, 0.0f }; // Reset position
			break;
		}
	}
}

void SandboxLayer::OnRender()
{
	RenderCommand::Clear(0.15f, 0.15f, 0.15f);

	Renderer::BeginScene(*m_Camera);

	// --- Render Ground ---
	m_FlatColorShader->Bind();
	m_FlatColorShader->SetFloat4("u_Color", { 0.8f, 0.8f, 0.8f, 1.0f });
	glm::mat4 groundTransform = glm::translate(glm::mat4(1.0f), { 0.0f, -0.8f, 0.0f })
		* glm::scale(glm::mat4(1.0f), { 5.0f, 0.1f, 1.0f });
	Renderer::Submit(m_FlatColorShader, m_VAO, groundTransform);

	// --- Render Obstacles ---
	m_FlatColorShader->SetFloat4("u_Color", { 0.9f, 0.2f, 0.2f, 1.0f });
	for (const auto& obs : m_Obstacles)
	{
		glm::mat4 obsTransform = glm::translate(glm::mat4(1.0f), obs.Position)
			* glm::scale(glm::mat4(1.0f), { 0.4f, 0.6f, 1.0f });
		Renderer::Submit(m_FlatColorShader, m_VAO, obsTransform);
	}

	// --- Render Dino (Player) ---
	m_Texture->Bind();
	glm::mat4 playerTransform = glm::translate(glm::mat4(1.0f), m_DinoPos);
	Renderer::Submit(m_TextureShader, m_VAO, playerTransform);

	Renderer::EndScene();
}

void SandboxLayer::OnImGuiRender()
{
	ImGui::Begin("Game Stats");
	ImGui::Text("Dino Position: %.2f, %.2f", m_DinoPos.x, m_DinoPos.y);
	ImGui::Text("Active Obstacles: %d", (int)m_Obstacles.size());
	ImGui::End();
}

void SandboxLayer::OnEvent(Cosmic::Event& event)
{
	// Handle camera resizing if necessary
}