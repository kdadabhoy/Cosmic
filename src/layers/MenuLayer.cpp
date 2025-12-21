#include "layers/MenuLayer.h"
#include "core/Application.h"
#include "imgui.h"
#include "graphics/VertexBufferLayout.h"
#include "events/WindowEvent.h"
#include "camera/OrthographicCamera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

MenuLayer::MenuLayer()
    : Layer("Menu"),
    m_SquarePos(0.0f, 0.0f, 0.0f) // Will be set in OnAttach based on actual window size
{
}

MenuLayer::~MenuLayer()
{
}

void MenuLayer::OnAttach()
{
    // Centered vertices: square is 100x100, origin (0,0) is the dead center
    float positions[] = {
        -50.0f, -50.0f, // 0 - Bottom Left
         50.0f, -50.0f, // 1 - Bottom Right
         50.0f,  50.0f, // 2 - Top Right
        -50.0f,  50.0f  // 3 - Top Left
    };

    unsigned int indices[] = {
        0, 1, 2,
        2, 3, 0
    };

    m_VAO = std::make_unique<VertexArray>();
    m_VBO = std::make_unique<VertexBuffer>(positions, 4 * 2 * sizeof(float));

    VertexBufferLayout layout;
    layout.push<float>(2);
    m_VAO->addBuffer(*m_VBO, layout);

    m_IBO = std::make_unique<IndexBuffer>(indices, 6);

    // --- DYNAMIC INITIALIZATION ---
    // Fetch actual window dimensions from the application singleton
    auto& window = Application::Get().GetWindow();
    float width = static_cast<float>(window.GetWidth());
    float height = static_cast<float>(window.GetHeight());

    // Initialize camera and square position to the REAL window dimensions
    m_Camera = std::make_unique<OrthographicCamera>(0.0f, width, 0.0f, height);
    m_SquarePos = glm::vec3(width / 2.0f, height / 2.0f, 0.0f);

    // Using the fixed relative path from CMake asset management
    m_Shader = std::make_unique<Shader>("assets/shaders/vert.shader", "assets/shaders/frag.shader");
}

void MenuLayer::OnDetach() {}

void MenuLayer::OnEvent(Event& e)
{
    if (e.GetEventType() == EventType::WindowResize) {
        auto& re = static_cast<WindowResizeEvent&>(e);
        float newWidth = static_cast<float>(re.GetWidth());
        float newHeight = static_cast<float>(re.GetHeight());

        // Update the camera's projection to match the new pixel bounds
        // This stops the square from disappearing or stretching
        m_Camera->setProjection(0.0f, newWidth, 0.0f, newHeight);

        // Keep the square centered regardless of how the window is resized
        m_SquarePos = glm::vec3(newWidth / 2.0f, newHeight / 2.0f, 0.0f);
    }
}

void MenuLayer::OnUpdate(float deltaTime)
{
    // Color cycle logic
    m_Color[0] += m_ColorIncrement * deltaTime * 2.0f;
    if (m_Color[0] > 1.0f || m_Color[0] < 0.0f)
        m_ColorIncrement = -m_ColorIncrement;
}

void MenuLayer::OnRender()
{
    Renderer renderer;
    m_Shader->bind();

    // Calculate Model Matrix: move the centered square to its world position
    glm::mat4 model = glm::translate(glm::mat4(1.0f), m_SquarePos);

    // Final MVP using the dynamic Camera matrices
    glm::mat4 mvp = m_Camera->getProjectionMatrix() * m_Camera->getViewMatrix() * model;

    m_Shader->setUniformMat4f("u_MVP", mvp);
    m_Shader->setUniform4f("u_Color", m_Color[0], m_Color[1], m_Color[2], m_Color[3]);

    renderer.draw(*m_VAO, *m_IBO, *m_Shader);
}

void MenuLayer::OnImGuiRender()
{
    // Use dynamic bounds for sliders so they always match the window edges
    auto& window = Application::Get().GetWindow();
    float width = static_cast<float>(window.GetWidth());
    float height = static_cast<float>(window.GetHeight());

    ImGui::Begin("Menu Controls");
    ImGui::Text("Aircraft Simulation Setup");

    // Slider now limits movement to exactly the current window width/height
    ImGui::SliderFloat2("Square Position", &m_SquarePos.x, 0.0f, width);
    ImGui::ColorEdit4("Square Color", m_Color);

    if (ImGui::Button("Start Flight (Climb)")) {
        // Transition logic here
    }
    ImGui::End();
}