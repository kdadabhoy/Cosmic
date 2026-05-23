#pragma once
// ShowcaseRunLayer.h

#include "IShowcaseMode.h"
#include <random>

namespace Showcase
{
	class ShowcaseRunLayer : public IShowcaseMode
	{
	public:
		ShowcaseRunLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> dinoMaterial);
		virtual ~ShowcaseRunLayer() = default;

		virtual const std::string& GetName() const override
		{
			static std::string s_Name = "Runner";
			return s_Name;
		}

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float fixedDt) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;
		virtual void SetViewportSize(float w, float h) override { m_Camera.OnResize(w, h); }

	private:
		void Reset();
		bool OnKeyPressed(Cosmic::KeyPressedEvent& e);

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;
		Cosmic::OrthographicCameraController m_Camera;

		Cosmic::Entity m_DinoEntity;
		std::vector<Cosmic::Entity> m_Obstacles;

		float m_SpawnTimer = 0.0f;
		float m_NextSpawnTime = 1.8f;
		bool m_GameOver = false;

		std::mt19937 m_Rng{ std::random_device{}() };

		static constexpr float k_GroundY = -0.8f;
		static constexpr float k_Gravity = -16.0f;
		static constexpr float k_JumpV = 6.2f;
	};
}