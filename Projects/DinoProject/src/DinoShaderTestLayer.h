#pragma once

#include <Cosmic.h>
#include "ISimulationMode.h"

namespace Workspace
{
	class DinoShaderTestLayer : public ISimulationMode
	{
	public:
		DinoShaderTestLayer(Cosmic::Ref<Cosmic::Scene> scene, Cosmic::Ref<Cosmic::Material> material);
		virtual ~DinoShaderTestLayer() override = default;

		virtual void OnUpdate(float ts) override;
		virtual void OnFixedUpdate(float deltaFixedTime) override {}
		virtual void OnRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;
		virtual void OnImGuiRender() override;

		virtual void SetViewportSize(float width, float height) override;
		virtual const std::string& GetName() const override { return m_DebugName; }

	private:
		std::string m_DebugName = "ShaderTestSandbox";
		Cosmic::Ref<Cosmic::Material> m_FullscreenMaterial;
		Cosmic::OrthographicCameraController m_CameraController;
		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
	};
}