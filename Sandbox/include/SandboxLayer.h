#pragma once


// Example Menu Layer

#include <events/Event.h>
#include <core/Layer.h>
#include <graphics/Shader.h>
#include <graphics/VertexArray.h>
#include <graphics/VertexBuffer.h>
#include <graphics/IndexBuffer.h>
#include <camera/OrthographicCamera.h>

#include <memory>
#include <glm/glm.hpp>


using namespace Cosmic;

class SandboxLayer : public Cosmic::Layer {
public:
    SandboxLayer();
    virtual ~SandboxLayer();

    virtual void OnAttach() override;
    virtual void OnDetach() override;
    virtual void OnUpdate(float deltaTime) override;
    virtual void OnRender() override;
    virtual void OnImGuiRender() override;
    virtual void OnEvent(Event& event) override;


private:
    // Renderer Resources
    std::unique_ptr<VertexArray> m_VAO;
    std::unique_ptr<VertexBuffer> m_VBO;
    std::unique_ptr<IndexBuffer> m_IBO;
    std::unique_ptr<Shader> m_Shader;

    // Camera and Scene Data
    std::unique_ptr<OrthographicCamera> m_Camera;

    glm::vec3 m_SquarePos;
    float m_Color[4] = { 0.8f, 0.3f, 0.8f, 1.0f }; // Initial Purple
    float m_ColorIncrement = 0.5f;
};
