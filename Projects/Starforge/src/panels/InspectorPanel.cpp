// InspectorPanel.cpp — see header.

#include "panels/InspectorPanel.h"

#include "scene/Scene.h"
#include "scene/Components.h"

#include <imgui.h>

#include <cstdio>

namespace Starforge
{
    void InspectorPanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Inspector");

        if (!ctx.HasSelection())
        {
            ImGui::TextDisabled("Select an entity in the Hierarchy.");
            ImGui::End();
            return;
        }

        auto& reg = ctx.Scene->GetRegistry();
        const entt::entity e = ctx.Selected;

        // --- Tag -------------------------------------------------------------
        if (auto* tag = reg.try_get<Cosmic::TagComponent>(e))
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", tag->Tag.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf)))
                tag->Tag = buf;                 // TODO(E7): RenameTag command
        }

        // --- Transform ---------------------------------------------------------
        if (auto* tr = reg.try_get<Cosmic::TransformComponent>(e))
        {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                // TODO(E7): coalesced TransformEdit commands (undo).
                ImGui::DragFloat3("Position", &tr->Position.x, 0.05f);
                ImGui::DragFloat3("Rotation", &tr->Rotation.x, 0.5f);   // Euler degrees
                ImGui::DragFloat3("Scale",    &tr->Scale.x,    0.02f, 0.001f, 1000.0f);
            }
        }

        // --- MeshRenderer --------------------------------------------------------
        if (auto* mr = reg.try_get<Cosmic::MeshRendererComponent>(e))
        {
            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::ColorEdit4("Color", &mr->Color.x);
                ImGui::Checkbox("Cast Shadows", &mr->CastShadows);
                // TODO(E16): MeshAsset / MaterialAsset become AssetPath slots
                //            (content-browser drag targets) once assets have paths.
                ImGui::TextDisabled(mr->MeshAsset ? "Mesh: <procedural>" : "Mesh: <none>");
            }
        }

        // --- Lights ---------------------------------------------------------------
        if (auto* dl = reg.try_get<Cosmic::DirectionalLightComponent>(e))
        {
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::DragFloat3("Direction", &dl->Direction.x, 0.01f, -1.0f, 1.0f);
                ImGui::ColorEdit3("Color", &dl->Color.x);
                ImGui::DragFloat("Intensity", &dl->Intensity, 0.01f, 0.0f, 20.0f);
            }
        }
        if (auto* pl = reg.try_get<Cosmic::PointLightComponent>(e))
        {
            if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::ColorEdit3("Color", &pl->Color.x);
                ImGui::DragFloat("Intensity", &pl->Intensity, 0.01f, 0.0f, 20.0f);
                ImGui::DragFloat("Radius", &pl->Radius, 0.1f, 0.0f, 500.0f);
            }
        }

        ImGui::Separator();

        // TODO(E8): real "Add Component" popup listing the E1 TypeRegistry.
        ImGui::BeginDisabled(true);
        ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Reflection-driven Add Component lands with E8.");

        ImGui::End();
    }
}
