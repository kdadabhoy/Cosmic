#pragma once

#include "Cosmic.h"
#include <glm/glm.hpp>

class SandboxLayer : public Cosmic::Layer
{
public:
	SandboxLayer();
	virtual ~SandboxLayer() = default;

	void OnAttach()								override;
	void OnDetach()								override;
	void OnUpdate(float deltaTime)				override;
	void OnRender()								override;
	void OnImGuiRender()						override;
	void OnEvent(Cosmic::Event& event)			override;

private:
	// Use Ref (shared_ptr) for resources that the Renderer also needs to hold
	Cosmic::Ref<Cosmic::VertexArray> m_VAO;
	Cosmic::Ref<Cosmic::VertexBuffer> m_VBO;
	Cosmic::Ref<Cosmic::IndexBuffer> m_IBO;
	Cosmic::Ref<Cosmic::Shader> m_Shader;

	std::unique_ptr<Cosmic::OrthographicCamera> m_Camera;

	glm::vec3 m_SquarePos;
	float m_Color[4] = { 0.8f, 0.3f, 0.8f, 1.0f };
	float m_TimeScale = 1.0f;
};