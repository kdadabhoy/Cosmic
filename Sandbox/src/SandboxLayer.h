#pragma once

#include "Cosmic.h"
#include "camera/OrthographicCamera.h"
#include "graphics/Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Cosmic
{
	struct Obstacle
	{
		glm::vec3 Position;
		float Speed = 2.0f;
	};

	class SandboxLayer : public Layer
	{
	public:
		SandboxLayer();
		virtual ~SandboxLayer() = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float deltaTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Event& event) override;

		void OnRender();

	private:
		std::unique_ptr<OrthographicCamera> m_Camera;
		Ref<Texture2D> m_Texture;

		// Gameplay Variables
		glm::vec3 m_DinoPos = { -1.0f, -0.5f, 0.0f };
		float m_DinoRotation = 0.0f;
		float m_VelocityY = 0.0f;
		bool m_IsGrounded = true;

		std::vector<Obstacle> m_Obstacles;
		float m_SpawnTimer = 0.0f;
	};
}