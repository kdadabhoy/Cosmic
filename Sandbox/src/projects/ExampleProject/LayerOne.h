#pragma once
#include "../../Simulation.h"
#include <imgui.h>

namespace Workspace
{
	class LayerOne : public Simulation
	{
	public:
		LayerOne() : m_CameraController(1280.0f / 720.0f) {}

		virtual void OnUpdate(float ts) override { m_CameraController.OnUpdate(ts); }

		virtual void OnRender() override
		{
			Cosmic::RenderCommand::Clear(0.8f, 0.2f, 0.2f); // Red
			Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());
			Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f });
			Cosmic::Renderer2D::EndScene();
		}

		virtual void OnImGuiRender() override
		{
			ImGui::Text("Sub-Layer: Red View");
		}

		virtual void SetViewportSize(float w, float h) override { m_CameraController.OnResize(w, h); }

	private:
		Cosmic::OrthographicCameraController m_CameraController;
	};
}