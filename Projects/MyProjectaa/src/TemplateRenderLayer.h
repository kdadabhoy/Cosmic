#pragma once
#include <Cosmic.h>

namespace Workspace
{
	// ============================================================================
	// TemplateRenderLayer
	//
	// Demonstrates:
	//   - Material-driven rendering with a custom GLSL shader
	//   - OrthographicCameraController with scroll-zoom and WASD pan
	//   - Correct local timeline usage (GetLocalTime() feeds u_Time)
	//   - SDF circles for decorative backdrop
	//   - DrawLine / DrawRect debug geometry
	//   - ImGui uniform editing (color, float sliders)
	//   - Viewport resize / framebuffer sync pattern
	// ============================================================================
	class TemplateRenderLayer : public Cosmic::Layer
	{
	public:
		explicit TemplateRenderLayer(Cosmic::Ref<Cosmic::Material> sharedMaterial);
		virtual ~TemplateRenderLayer() override = default;

		virtual void OnAttach()                          override;
		virtual void OnDetach()                          override;
		virtual void OnUpdate(float ts)                  override;
		virtual void OnImGuiRender()                     override;
		virtual void OnEvent(Cosmic::Event& e)           override;

	private:
		bool OnWindowResize(Cosmic::WindowResizeEvent& e);

	private:
		// Camera
		Cosmic::OrthographicCameraController m_Camera;
		glm::vec2                            m_ViewportSize = { 1280.0f, 720.0f };

		// Rendering
		Cosmic::Ref<Cosmic::Material> m_Material;

		// Inspector state
		glm::vec4 m_Tint = { 1.0f, 0.6f, 0.2f, 1.0f };
		float     m_PulseSpeed = 2.0f;
		float     m_PulseAmp = 0.12f;
		float     m_RingThick = 0.04f;
		bool      m_ShowGrid = true;
		bool      m_ShowRings = true;
	};
}