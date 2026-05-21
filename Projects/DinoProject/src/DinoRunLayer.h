#pragma once
#include "ISimulationMode.h"
#include <random>

namespace Workspace
{
	class DinoRunLayer : public ISimulationMode
	{
	public:
		DinoRunLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoRunLayer() = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override { m_CameraController.OnEvent(e); }
		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }

		void Reset();

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_Material;
		Cosmic::OrthographicCameraController m_CameraController;

		Cosmic::Entity m_DinoEntity;
		std::vector<Cosmic::Entity> m_ObstacleEntities;

		float m_VelocityY = 0.0f;
		bool m_IsGrounded = true;

		std::mt19937 m_RandomEngine;
		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 2.0f;
		float m_Score = 0.0f;
	};
}