#pragma once
#include "../../Simulation.h"

namespace Workspace
{

    /**
     * @class LayerTwo
     * @brief A blue-themed simulation component of the ExampleProject.
     */
    class LayerTwo : public Simulation
    {
    public:
        LayerTwo() : m_CameraController(1280.0f / 720.0f) {}

        virtual void OnUpdate(float ts) override
        {
            m_CameraController.OnUpdate(ts);
        }

        virtual void OnRender() override
        {
            // Blue Background for distinction
            Cosmic::RenderCommand::Clear(0.2f, 0.3f, 0.8f);

            Cosmic::Renderer2D::BeginScene(m_CameraController.GetCamera());
            // Draw a white quad in the center
            Cosmic::Renderer2D::DrawQuad({ 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f, 1.0f, 1.0f });
            Cosmic::Renderer2D::EndScene();
        }

        virtual void OnImGuiRender() override
        {
            ImGui::Text("Currently viewing: Blue Sub-Layer");
            ImGui::Text("Camera Zoom: %.2f", m_CameraController.GetZoomLevel());
        }

        virtual void SetViewportSize(float width, float height) override
        {
            m_CameraController.OnResize(width, height);
        }

    private:
        Cosmic::OrthographicCameraController m_CameraController;
    };

}