#pragma once
#include "Cosmic.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

struct Obstacle
{
	glm::vec3 Position;
	float Speed = 2.0f;
};

class SandboxLayer : public Cosmic::Layer
{
public:
	SandboxLayer();
	virtual ~SandboxLayer() = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;
	virtual void OnUpdate(float deltaTime) override;
	virtual void OnImGuiRender() override;
	virtual void OnEvent(Cosmic::Event& event) override;
	virtual void OnRender() override;

private:
	std::unique_ptr<Cosmic::OrthographicCamera> m_Camera;

	// We only need the texture now, Renderer2D handles the shaders/buffers
	Cosmic::Ref<Cosmic::Texture> m_Texture;

	// Gameplay Variables
	glm::vec3 m_DinoPos = { -1.0f, -0.5f, 0.0f };
	float m_DinoRotation = 0.0f;
	float m_VelocityY = 0.0f;
	bool m_IsGrounded = true;

	std::vector<Obstacle> m_Obstacles;
	float m_SpawnTimer = 0.0f;
};