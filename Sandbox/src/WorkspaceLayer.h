/**
 * @file WorkspaceLayer.h
 * @brief The primary Editor/Host layer for the Cosmic Engineering Suite.
 *
 * ROLE: This class manages the global state of the application, including
 * the ImGui Dockspace, the Framebuffer resize synchronization, and the
 * high-level Project selection logic. It provides the "shell" in which
 * individual simulations run.
 */





 // Need to add an void OnFixedUpdate(float deltaFixedTime) {};


#pragma once
#include "Cosmic.h"
#include "Simulation.h"
#include <memory>

namespace Workspace
{

    class WorkspaceLayer : public Cosmic::Layer
    {
    public:
        WorkspaceLayer();
        virtual ~WorkspaceLayer() = default;

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnUpdate(float ts) override;
        virtual void OnImGuiRender() override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        /**
         * @brief Template helper to instantiate and initialize a new project.
         * @tparam T The project class (must inherit from Workspace::Simulation).
         */
        template<typename T>
        void LoadProject()
        {
            m_ActiveSim = std::make_unique<T>();
            // If the UI is already open, tell the new project how big the viewport is
            if (m_ViewportSize.x > 0)
                m_ActiveSim->SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
        }

    private:
        std::unique_ptr<Simulation> m_ActiveSim;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
    };

}