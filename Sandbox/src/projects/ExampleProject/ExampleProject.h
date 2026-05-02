/**
 * @file ExampleProject.h
 * @brief A container project that manages LayerOne and LayerTwo.
 *
 * PURPOSE: This acts as the "Master" for a specific engineering study.
 * It can toggle between different sub-layers (like different data views)
 * while staying within the same project context.
 */

#pragma once
#include "../../Simulation.h"
#include "LayerOne.h"
#include "LayerTwo.h"

namespace Workspace
{

    class ExampleProject : public Simulation
    {
    public:
        ExampleProject()
        {
            m_LayerOne = std::make_unique<LayerOne>();
            m_LayerTwo = std::make_unique<LayerTwo>();
            m_ActiveLayer = m_LayerOne.get(); // Default to Layer One
        }

        virtual void OnUpdate(float ts) override
        {
            if (m_ActiveLayer) m_ActiveLayer->OnUpdate(ts);
        }

        virtual void OnRender() override
        {
            if (m_ActiveLayer) m_ActiveLayer->OnRender();
        }

        virtual void OnImGuiRender() override
        {
            ImGui::Text("Example Project Dashboard");
            if (ImGui::RadioButton("View: Red Scene", m_ActiveLayer == m_LayerOne.get()))
                m_ActiveLayer = m_LayerOne.get();
            ImGui::SameLine();
            if (ImGui::RadioButton("View: Blue Scene", m_ActiveLayer == m_LayerTwo.get()))
                m_ActiveLayer = m_LayerTwo.get();

            ImGui::Separator();

            // Render the UI of the currently selected sub-layer
            if (m_ActiveLayer) m_ActiveLayer->OnImGuiRender();
        }

        virtual void SetViewportSize(float width, float height) override
        {
            m_LayerOne->SetViewportSize(width, height);
            m_LayerTwo->SetViewportSize(width, height);
        }

    private:
        std::unique_ptr<LayerOne> m_LayerOne;
        std::unique_ptr<LayerTwo> m_LayerTwo;
        Simulation* m_ActiveLayer = nullptr;
    };

}