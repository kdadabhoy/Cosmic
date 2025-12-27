#include "SandboxLayer.h"
#include "imgui.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath> 

using namespace Cosmic;

/////////////////////////////////////////////////////////////////////////////////

SandboxLayer::SandboxLayer()
	: Layer("Sandbox"), 
	m_SquarePos(0.0f, 0.0f, 0.0f), 
	m_Color{ 0.0f, 0.0f, 0.0f, 1.0f }
{

}


/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnAttach()
{
	// 1. Using NDC coordinates (-0.5 to 0.5) 
	// This ensures the square is visible even if the camera projection is off.
	float vertices[] = {
		-0.5f, -0.5f, 0.0f,
		 0.5f, -0.5f, 0.0f,
		 0.5f,  0.5f, 0.0f,
		-0.5f,  0.5f, 0.0f
	};

	m_VAO = VertexArray::Create();
	m_VBO = VertexBuffer::Create(vertices, sizeof(vertices));

	m_VBO->SetLayout({
		{ ShaderDataType::Float3, "a_Position" }
		});
	m_VAO->AddVertexBuffer(m_VBO);

	uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };
	m_IBO = IndexBuffer::Create(indices, sizeof(indices) / sizeof(uint32_t));
	m_VAO->SetIndexBuffer(m_IBO);

	m_Shader = Shader::Create("assets/shaders/FlatColor.glsl");

	// 2. Simplified Camera for 2D
	// Instead of using window pixel units (1280x720), we use a range of -1 to 1.
	m_Camera = std::make_unique<OrthographicCamera>(-1.0f, 1.0f, -1.0f, 1.0f);


	// 3. Color initializing
	m_Color[0] = 0.29f;								// R
	//m_Color[1] = (std::sin(time) + 1.0f) / 2.0f;	// G pulse
	m_Color[2] = 0.4f;								// B
	m_Color[3] = 1.0f;								// A 
}

/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnUpdate(float deltaTime)
{
	Cosmic::Application::Get().SetTimeScale(m_TimeScale);

	static float time = 0.0f;
	time += deltaTime;

	// m_Color[0] = 0.0f;								// R
	m_Color[1] = (std::sin(time) + 1.0f) / 2.0f;	    // G pulse
	// m_Color[2] = 0.0f;								// B
	// m_Color[3] = 1.0f;								// A 
}

/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnRender()
{
	Cosmic::RenderCommand::Clear(1.0f, 0.0f, 1.0f); // Magenta background
	Renderer::BeginScene(*m_Camera);

	// 1. Bind the shader
	m_Shader->Bind();

	// 2. Upload the color
	m_Shader->SetFloat4("u_Color", glm::vec4(m_Color[0], m_Color[1], m_Color[2], m_Color[3]));

	// 3. Submit the draw call
	Renderer::Submit(m_Shader, m_VAO, glm::translate(glm::mat4(1.0f), m_SquarePos));

	Renderer::EndScene();
}

/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnDetach() 
{

}

/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnImGuiRender()
{
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("Square Color", m_Color);
	ImGui::DragFloat3("Square Position", &m_SquarePos.x, 0.01f); // Lower speed for NDC
	ImGui::SliderFloat("Time Scale", &m_TimeScale, 0.0f, 5.0f);
	ImGui::End();
}

/////////////////////////////////////////////////////////////////////////////////

void SandboxLayer::OnEvent(Event& event) 
{

}

/////////////////////////////////////////////////////////////////////////////////