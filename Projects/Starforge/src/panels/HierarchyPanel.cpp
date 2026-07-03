// HierarchyPanel.cpp — see header.

#include "panels/HierarchyPanel.h"

#include "scene/Scene.h"
#include "scene/Components.h"

#include <imgui.h>

namespace Starforge
{
    void HierarchyPanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Hierarchy");

        if (!ctx.Scene)
        {
            ImGui::TextDisabled("No scene open.");
            ImGui::End();
            return;
        }

        // TODO(E7): route through a CreateEntityCommand instead of direct calls.
        if (ImGui::Button("+ Entity"))
        {
            Cosmic::Entity e = ctx.Scene->CreateEntity("New Entity");
            ctx.Log("[Hierarchy] Created 'New Entity'.");
            (void)e;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(create menu lands with E8)");
        ImGui::Separator();

        // SKELETON: flat list, creation order not guaranteed (entt storage order).
        // TODO(E3): tree view driven by RelationshipComponent.
        auto view = ctx.Scene->GetRegistry().view<Cosmic::TagComponent>();
        for (auto entity : view)
        {
            const auto& tag = view.get<Cosmic::TagComponent>(entity);

            ImGui::PushID((int)(uint32_t)entity);
            const bool selected = (ctx.Selected == entity);
            if (ImGui::Selectable(tag.Tag.c_str(), selected))
                ctx.Selected = entity;

            // TODO(E8): context menu (rename/duplicate/delete). Delete only for now.
            if (ImGui::BeginPopupContextItem("entity_ctx"))
            {
                if (ImGui::MenuItem("Delete"))
                {
                    if (ctx.Selected == entity)
                        ctx.Selected = entt::null;
                    ctx.Scene->DestroyEntity(Cosmic::Entity(entity, ctx.Scene.get()));
                    ctx.Log(std::string("[Hierarchy] Deleted '") + tag.Tag + "'.");
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break; // registry mutated — stop iterating this frame
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        // Click empty space to deselect.
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ctx.Selected = entt::null;

        ImGui::End();
    }
}
