#pragma once
#include <Cosmic.h>
#include <memory>

namespace Workspace
{
	class TemplateProject : public Cosmic::Layer
	{
	public:
		TemplateProject();
		virtual ~TemplateProject() = default;

		virtual void OnAttach() override {}
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnImGuiRender() override;

	private:
		Cosmic::OrthographicCameraController m_CameraController;
		Cosmic::Ref<Cosmic::Scene> m_Scene;
		Cosmic::Entity m_FireQuadEntity;
		Cosmic::Ref<Cosmic::Material> m_FireMaterial;
		float m_TimeAccumulator = 0.0f;
	};
}