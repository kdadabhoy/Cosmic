#pragma once
// ShowcaseFlightLayer.h

#include "IShowcaseMode.h"

namespace Showcase
{
	class ShowcaseFlightLayer : public IShowcaseMode
	{
	public:
		ShowcaseFlightLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> dinoMaterial);
		virtual ~ShowcaseFlightLayer() = default;

		virtual const std::string& GetName() const override
		{
			static std::string s_Name = "Flight";
			return s_Name;
		}

		virtual void OnUpdate(float ts)            override;
		virtual void OnFixedUpdate(float fixedDt)  override;
		virtual void OnRender()                    override;
		virtual void OnImGuiRender()               override;
		virtual void OnEvent(Cosmic::Event& e)     override;
		virtual void SetViewportSize(float w, float h) override;

	private:
		bool OnMouseClicked(Cosmic::MouseButtonPressedEvent& e);
		glm::vec2 ScreenToWorld(glm::vec2 screenPos) const;
		bool HitTest(Cosmic::Entity entity, glm::vec2 worldPos) const;
		void SelectEntity(Cosmic::Entity e);
		void DeselectAll();

	private:
		Cosmic::Ref<Cosmic::Scene>    m_Scene;
		Cosmic::Ref<Cosmic::Material> m_DinoMaterial;
		Cosmic::OrthographicCameraController m_Camera;

		Cosmic::Entity m_DinoA;
		Cosmic::Entity m_DinoB;
		Cosmic::Entity m_SelectedEntity;

		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
		static constexpr size_t k_MaxTrailLength = 300;
	};
}