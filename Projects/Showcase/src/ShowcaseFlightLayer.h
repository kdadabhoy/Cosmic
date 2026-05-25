#pragma once
#include <Cosmic.h>
#include <vector>

namespace Showcase
{
	struct FlightFlameComponent
	{
		float Speed = 1.0f;
		float Slope = 0.0f;
		glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
		std::vector<glm::vec3> Trail;
		bool Selected = false;
	};

	class ShowcaseFlightLayer : public Cosmic::Layer
	{
	public:
		ShowcaseFlightLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> flameMaterial);
		virtual ~ShowcaseFlightLayer() override = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		bool OnMouseClicked(Cosmic::MouseButtonPressedEvent& e);
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);
		glm::vec2 ScreenToWorld(glm::vec2 screenPos) const;
		bool HitTest(Cosmic::Entity entity, glm::vec2 worldPos) const;
		void SelectEntity(Cosmic::Entity e);
		void DeselectAll();

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_FlameMaterial;
		Cosmic::OrthographicCameraController m_Camera;

		Cosmic::Entity m_FlameA;
		Cosmic::Entity m_FlameB;
		Cosmic::Entity m_SelectedEntity;

		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
		static constexpr size_t k_MaxTrailLength = 40;
	};
}