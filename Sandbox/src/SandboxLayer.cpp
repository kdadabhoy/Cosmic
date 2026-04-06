#include "SandboxLayer.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>

using namespace Cosmic;

SandboxLayer::SandboxLayer()
	: Layer("Sandbox"), m_SquarePos(0.0f, 0.0f, 0.0f)
{
}

void SandboxLayer::OnAttach()
{
	// Updated vertices: Position (x, y, z) , TexCoord (u, v)
	float vertices[] = {
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f,
		 0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
		 0.5f,  0.5f, 0.0f, 1.0f, 1.0f,
		-0.5f,  0.5f, 0.0f, 0.0f, 1.0f
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

	// Load Shader and Texture
	m_Shader = Shader::Create("assets/shaders/Texture.glsl");
	m_Texture = Texture::Create("assets/shaders/Texture.png");

	m_Camera = std::make_unique<OrthographicCamera>(-1.6f, 1.6f, -0.9f, 0.9f);
}

void SandboxLayer::OnUpdate(float deltaTime)
{
	Cosmic::Application::Get().SetTimeScale(m_TimeScale);
}

void SandboxLayer::OnRender()
{
	RenderCommand::Clear(0.1f, 0.1f, 0.1f); // Dark grey
	Renderer::BeginScene(*m_Camera);

	m_Texture->Bind(); // Bind texture to slot 0 (default)

	// We don't need to manually set "u_Texture" uniform because 
	// sampler2D defaults to slot 0, which matches m_Texture->Bind()

	Renderer::Submit(m_Shader, m_VAO, glm::translate(glm::mat4(1.0f), m_SquarePos));

	Renderer::EndScene();
}

void SandboxLayer::OnImGuiRender()
{
	ImGui::Begin("Settings");
	ImGui::DragFloat3("Square Position", &m_SquarePos.x, 0.01f);
	ImGui::SliderFloat("Time Scale", &m_TimeScale, 0.0f, 5.0f);
	ImGui::End();
}

void SandboxLayer::OnDetach() {}
void SandboxLayer::OnEvent(Event& event) {}