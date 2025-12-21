// Derived from Layer.
// Currently just an example of how we will implement layers



#ifndef MENU_LAYER_H
#define MENU_LAYER_H

#include "core/Layer.h"
#include "graphics/Renderer.h"
#include "graphics/VertexArray.h"
#include "graphics/VertexBufferLayout.h"

#include "graphics/IndexBuffer.h"
#include "graphics/Shader.h"
#include "imgui.h"

class MenuLayer : public Layer {
public:

    MenuLayer() : Layer("Menu") {}

    void OnAttach() override {
        // Move your initialization code here
        float positions[] = {
            -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f
        };
        unsigned int indices[] = { 0, 1, 2, 2, 3, 0 };

        m_VA = std::make_unique<VertexArray>();
        m_VB = std::make_unique<VertexBuffer>(positions, 4 * 2 * sizeof(float));

        VertexBufferLayout layout;
        layout.push<float>(2);
        m_VA->addBuffer(*m_VB, layout);
        m_IB = std::make_unique<IndexBuffer>(indices, 6);
        m_Shader = std::make_unique<Shader>("shaders/vert.shader", "shaders/frag.shader");
    }



    void OnUpdate(float deltaTime) override {
        // Handle color animation logic
        if (m_ColorR > 1.0f || m_ColorR < 0.0f) m_Increment *= -1.0f;
        m_ColorR += m_Increment;
    }



    void OnRender() override {
        Renderer renderer;
        m_Shader->bind();
        m_Shader->setUniform4f("u_Color", m_ColorR, 0.3f, 0.8f, 1.0f);
        renderer.draw(*m_VA, *m_IB, *m_Shader);
    }



    void OnImGuiRender() override {
        ImGui::Begin("Start Menu");
        if (ImGui::Button("Start Flight")) {
            // This is where you'll trigger the aircraft climb later!
            std::cout << "Starting Flight..." << std::endl;
        }
        ImGui::End();
    }



private:
    std::unique_ptr<VertexArray> m_VA;
    std::unique_ptr<VertexBuffer> m_VB;
    std::unique_ptr<IndexBuffer> m_IB;
    std::unique_ptr<Shader> m_Shader;
    float m_ColorR = 0.0f;
    float m_Increment = 0.01f;
};



#endif