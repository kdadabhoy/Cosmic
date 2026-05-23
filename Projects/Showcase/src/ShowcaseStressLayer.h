#pragma once
// ShowcaseStressLayer.h

#include "IShowcaseMode.h"

namespace Showcase
{
	class ShowcaseStressLayer : public IShowcaseMode
	{
	public:
		ShowcaseStressLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> materialA, Cosmic::Ref<Cosmic::Material> materialB);
		virtual ~ShowcaseStressLayer() = default;

		virtual const std::string& GetName() const override
		{
			static std::string s_Name = "ECS Stress Test";
			return s_Name;
		}

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float fixedDt) override;
		virtual void OnRender() override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;
		virtual void SetViewportSize(float w, float h) override { m_Camera.OnResize(w, h); }

	private:
		void RebuildGrid();

	private:
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Ref<Cosmic::Material> m_MaterialA;
		Cosmic::Ref<Cosmic::Material> m_MaterialB;
		Cosmic::OrthographicCameraController m_Camera;

		std::vector<Cosmic::Entity> m_GridEntities;

		int m_GridRadius = 15;
		float m_CellSpacing = 0.15f;
		float m_Time = 0.0f;
		uint32_t m_FixedTicks = 0;
		bool m_Animate = true;
	};
}