#pragma once
#include "GameScene.h"

namespace Cosmic
{
	class LayerTwo : public GameScene
	{
	public:
		LayerTwo() : m_CameraController(1280.0f / 720.0f) {}
		virtual void OnUpdate(float ts) override { m_CameraController.OnUpdate(ts); }
		virtual void OnRender() override
		{
			RenderCommand::Clear(0.2f, 0.2f, 0.8f); // Blue Background
			Renderer2D::BeginScene(m_CameraController.GetCamera());
			Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f });
			Renderer2D::EndScene();
		}
		virtual void OnImGuiRender() override { ImGui::Text("Currently in Layer 2 (Blue)"); }
		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }
	private:
		OrthographicCameraController m_CameraController;
	};
}