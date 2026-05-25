#pragma once
#include <Cosmic.h>
#include <glm/glm.hpp>

namespace Showcase
{
	class CircleDebugLayer : public Cosmic::Layer
	{
	public:
		CircleDebugLayer();
		virtual ~CircleDebugLayer() override = default;

		virtual void OnAttach() override;
		virtual void OnDetach() override;
		virtual void OnUpdate(float ts) override;
		virtual void OnImGuiRender() override;
		virtual void OnEvent(Cosmic::Event& e) override;

	private:
		Cosmic::OrthographicCameraController m_CameraController;

		// Target properties to mutate live via ImGui
		glm::vec4 m_DiskColor = { 0.1f, 0.5f, 0.8f, 1.0f }; // Standard normalized float colors
		glm::vec4 m_RingColor = { 0.0f, 1.0f, 0.5f, 1.0f }; // Sharp neon green

		float m_RingThickness = 0.05f;
		float m_RingFade = 0.005f;

		float m_DiskThickness = 1.0f; // 1.0 = solid filled disk
		float m_DiskFade = 0.1f;  // Soft feathered edge

		glm::vec2 m_ViewportSize = { 1280.0f, 720.0f };
	};
}