#pragma once
#include <Cosmic.h>
#include <vector>

namespace Showcase
{
	class ShowcaseStressLayer : public Cosmic::Layer
	{
	public:
		ShowcaseStressLayer(
			Cosmic::Ref<Cosmic::Scene> scene,
			Cosmic::Ref<Cosmic::Material> materialA,
			Cosmic::Ref<Cosmic::Material> materialB
		);
		virtual ~ShowcaseStressLayer() override = default;

		// Standard engine layer lifecycle contract
		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		void RebuildGrid();
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_MaterialA;
		Cosmic::Ref<Cosmic::Material> m_MaterialB;
		Cosmic::OrthographicCameraController m_Camera;

		std::vector<Cosmic::Entity> m_GridEntities;

		int m_GridRadius = 15;
		float m_CellSpacing = 0.15f;
		float m_Time = 0.0f;
		uint32_t m_UpdateTicks = 0;
		bool m_Animate = true;
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
	};
}