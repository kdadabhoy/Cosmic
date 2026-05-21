#include "DinoRunLayer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Workspace
{
	DinoRunLayer::DinoRunLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material)
		: m_Scene(scene), m_Material(material), m_CameraController(1280.0f / 720.0f, true), m_RandomEngine(std::random_device{}())
	{
		m_DinoEntity = m_Scene->CreateEntity("Runner Dino");
		// Scene::CreateEntity already automatically adds a TransformComponent by default.
		// Calling AddComponent<TransformComponent>() a second time would trigger an engine assert!
		Reset();
	}

	void DinoRunLayer::Reset()
	{
		auto& trans = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		trans.Position = { -1.0f, -0.5f, 0.0f };
		trans.Scale = { 0.5f, 0.5f };

		m_VelocityY = 0.0f;
		m_Score = 0.0f;
		m_IsGrounded = true;

		// Wipe old obstacles safely out of registry allocations
		for (auto& ent : m_ObstacleEntities)
		{
			m_Scene->DestroyEntity(ent);
		}
		m_ObstacleEntities.clear();
	}

	void DinoRunLayer::OnUpdate(float ts)
	{
		if (Cosmic::Input::IsKeyPressed(CS_KEY_SPACE) && m_IsGrounded)
		{
			m_VelocityY = 5.0f;
			m_IsGrounded = false;
		}
		m_CameraController.OnUpdate(ts);
	}

	void DinoRunLayer::OnFixedUpdate(float deltaFixedTime)
	{
		m_Score += deltaFixedTime * 10.0f;
		auto& dinoTrans = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();

		if (!m_IsGrounded)
		{
			m_VelocityY -= 12.0f * deltaFixedTime;
			dinoTrans.Position.y += m_VelocityY * deltaFixedTime;
		}

		if (dinoTrans.Position.y <= -0.5f)
		{
			dinoTrans.Position.y = -0.5f;
			m_VelocityY = 0.0f;
			m_IsGrounded = true;
		}

		m_SpawnTimer += deltaFixedTime;
		if (m_SpawnTimer > m_NextSpawnTime)
		{
			float h = std::uniform_real_distribution<float>(0.3f, 0.8f)(m_RandomEngine);

			auto obs = m_Scene->CreateEntity("Obstacle");
			auto& t = obs.GetComponent<Cosmic::TransformComponent>(); // Scene auto-creates this component
			t.Position = { 2.5f, -0.8f + (h / 2.0f), 0.0f };
			t.Scale = { 0.3f, h };

			auto& obsComp = obs.AddComponent<RunnerObstacleComponent>();
			obsComp.Size = { 0.3f, h };
			obsComp.Color = { 0.8f, 0.2f, 0.2f, 1.0f };
			obsComp.Speed = 2.0f;

			m_ObstacleEntities.push_back(obs);
			m_SpawnTimer = 0.0f;
			m_NextSpawnTime = std::uniform_real_distribution<float>(1.0f, 2.0f)(m_RandomEngine);
		}

		// ECS Processing and Collisions
		for (auto& obs : m_ObstacleEntities)
		{
			auto& t = obs.GetComponent<Cosmic::TransformComponent>();
			auto& o = obs.GetComponent<RunnerObstacleComponent>();

			t.Position.x -= o.Speed * deltaFixedTime;

			if (std::abs(dinoTrans.Position.x - t.Position.x) < 0.3f &&
				std::abs(dinoTrans.Position.y - t.Position.y) < (o.Size.y / 2.0f + 0.2f))
			{
				Reset();
				return;
			}
		}

		// Cleanup far entities - capturing entity handles by non-const value copy to prevent conversion errors
		m_ObstacleEntities.erase(std::remove_if(m_ObstacleEntities.begin(), m_ObstacleEntities.end(),
			[this](Cosmic::Entity ent) mutable
			{
				if (ent.GetComponent<Cosmic::TransformComponent>().Position.x < -3.0f)
				{
					m_Scene->DestroyEntity(ent);
					return true;
				}
				return false;
			}), m_ObstacleEntities.end());
	}

	void DinoRunLayer::OnRender()
	{
		Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());
		Cosmic::Renderer2D::DrawQuad({ 0.0f, -0.85f }, { 20.0f, 0.2f }, { 0.3f, 0.3f, 0.33f, 1.0f });

		for (auto& obs : m_ObstacleEntities)
		{
			auto& t = obs.GetComponent<Cosmic::TransformComponent>();
			auto& o = obs.GetComponent<RunnerObstacleComponent>();
			Cosmic::Renderer2D::DrawQuad(t.Position, t.Scale, o.Color);
		}

		auto& dinoTrans = m_DinoEntity.GetComponent<Cosmic::TransformComponent>();
		Cosmic::Renderer2D::DrawQuad(dinoTrans.Position, dinoTrans.Scale, m_Material);
		Cosmic::Renderer2D::EndScene();
	}

	void DinoRunLayer::OnImGuiRender()
	{
		ImGui::Text("Runner Simulation Stats (ECS Powered)");
		ImGui::Separator();
		ImGui::Value("Score", (int)m_Score);
		ImGui::Text("Grounded: %s", m_IsGrounded ? "Yes" : "No");
		if (ImGui::Button("Manual Reset")) Reset();
	}
}