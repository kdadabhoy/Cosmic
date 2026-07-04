// InspectorPanel.cpp — see header.

#include "panels/InspectorPanel.h"
#include "commands/EditorCommands.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Cosmic;
using Cosmic::Reflect::FieldDescriptor;
using Cosmic::Reflect::FieldValue;
using Cosmic::Reflect::TypeDescriptor;

namespace Starforge
{
    namespace
    {
        const entt::id_type kTagId = entt::type_hash<TagComponent>::value();

        // Components common to EVERY selected entity (intersection), sorted by
        // category then name for a stable layout.
        std::vector<const TypeDescriptor*> CommonComponents(EditorContext& ctx)
        {
            std::vector<const TypeDescriptor*> out;
            if (ctx.Selection.empty() || !ctx.Scene) return out;

            auto& reg = ctx.Scene->GetRegistry();
            auto& registry = Reflect::GetRegistry();
            for (const TypeDescriptor* d : registry.ComponentsOf(reg, ctx.Selection.front()))
            {
                bool inAll = true;
                for (size_t i = 1; i < ctx.Selection.size() && inAll; ++i)
                    inAll = d->Has && d->Has(reg, ctx.Selection[i]);
                if (inAll) out.push_back(d);
            }
            std::sort(out.begin(), out.end(), [](const TypeDescriptor* a, const TypeDescriptor* b)
            {
                if (a->Category != b->Category) return a->Category < b->Category;
                return a->Name < b->Name;
            });
            return out;
        }

        // Does `field` of type `typeId` differ across the selection?
        bool FieldMixed(EditorContext& ctx, entt::id_type typeId, const FieldDescriptor& f)
        {
            if (ctx.Selection.size() < 2) return false;
            const TypeDescriptor* d = Reflect::GetRegistry().Find(typeId);
            if (!d) return false;
            auto& reg = ctx.Scene->GetRegistry();
            FieldValue first;
            bool have = false;
            for (entt::entity h : ctx.Selection)
            {
                void* comp = d->Get(reg, h);
                if (!comp) continue;
                FieldValue v = f.Get(comp);
                if (!have) { first = v; have = true; }
                else if (v != first) return true;
            }
            return false;
        }
    }

    void InspectorPanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Inspector");

        if (!ctx.HasSelection() || !ctx.PrimaryEntity())
        {
            ImGui::TextDisabled("Select an entity in the Hierarchy.");
            ImGui::End();
            return;
        }

        if (ctx.Selection.size() > 1)
        {
            ImGui::TextDisabled("%d entities selected — editing common components.",
                                (int)ctx.Selection.size());
            ImGui::Separator();
        }

        DrawName(ctx);
        ImGui::Separator();

        for (const TypeDescriptor* d : CommonComponents(ctx))
        {
            if (d->TypeId == kTagId)   // shown as the Name row above
                continue;
            DrawComponent(ctx, *d);
        }

        ImGui::Separator();
        DrawAddComponent(ctx);

        ImGui::End();
    }

    void InspectorPanel::DrawName(EditorContext& ctx)
    {
        Entity primary = ctx.PrimaryEntity();
        auto* tag = ctx.Scene->GetRegistry().try_get<TagComponent>((entt::entity)primary);
        if (!tag) return;

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", tag->Tag.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        const bool edited = ImGui::InputText("##name", buf, sizeof(buf));
        if (ImGui::IsItemActivated()) { m_ActiveBefore = FieldValue{ std::string(tag->Tag) }; m_HasActive = true; }
        if (edited) tag->Tag = buf;   // live
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            FieldValue after{ std::string(tag->Tag) };
            FieldValue before = m_HasActive ? m_ActiveBefore : after;
            if (before != after)
                Commands::CommitFieldEdit(ctx, "Rename", kTagId, "Tag", before, after);
            m_HasActive = false;
        }
    }

    void InspectorPanel::DrawComponent(EditorContext& ctx, const TypeDescriptor& desc)
    {
        Entity primary = ctx.PrimaryEntity();
        auto& reg = ctx.Scene->GetRegistry();
        void* comp = desc.Get(reg, (entt::entity)primary);

        ImGui::PushID((int)desc.TypeId);
        const bool open = ImGui::CollapsingHeader(desc.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

        // Remove via header right-click (single-select only — multi remove is a
        // documented v1 limitation). Transform can't be removed.
        bool removeRequested = false;
        const bool removable = desc.TypeId != entt::type_hash<TransformComponent>::value();
        if (removable && ImGui::BeginPopupContextItem("comp_ctx"))
        {
            if (ImGui::MenuItem("Remove Component")) removeRequested = true;
            ImGui::EndPopup();
        }

        if (open && comp)
        {
            if (desc.Fields.empty())
                ImGui::TextDisabled("(no editable properties yet)");

            for (const FieldDescriptor& f : desc.Fields)
            {
                if (f.HasFlag(Cosmic::Reflect::Field_HideInInspector))
                    continue;
                const bool mixed = FieldMixed(ctx, desc.TypeId, f);
                PropertyRows::Result res = PropertyRows::DrawField(f, comp, mixed);

                if (res.Activated) { m_ActiveBefore = res.PreValue; m_HasActive = true; }
                if (res.Committed)
                {
                    FieldValue before = m_HasActive ? m_ActiveBefore : res.PreValue;
                    if (!(before == res.PostValue))
                        Commands::CommitFieldEdit(ctx, "Edit " + desc.Name + "." + f.Name,
                                                  desc.TypeId, f.Name, before, res.PostValue);
                    m_HasActive = false;
                }
            }
        }
        ImGui::PopID();

        if (removeRequested && ctx.Selection.size() == 1)
            Commands::RemoveComponent(ctx, primary, desc.TypeId);
    }

    void InspectorPanel::DrawAddComponent(EditorContext& ctx)
    {
        const bool single = (ctx.Selection.size() == 1);
        if (!single) ImGui::BeginDisabled(true);

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
            ImGui::OpenPopup("add_component");

        if (ImGui::BeginPopup("add_component"))
        {
            Entity primary = ctx.PrimaryEntity();
            auto& reg = ctx.Scene->GetRegistry();

            // Group registry entries by category; hide ones already present.
            std::map<std::string, std::vector<const TypeDescriptor*>> byCategory;
            for (const auto& [id, desc] : Reflect::GetRegistry().Types())
            {
                if (id == kTagId) continue;   // identity/name, not add-able
                if (desc.Has && desc.Has(reg, (entt::entity)primary)) continue;
                byCategory[desc.Category.empty() ? "General" : desc.Category].push_back(&desc);
            }

            for (auto& [cat, list] : byCategory)
            {
                std::sort(list.begin(), list.end(),
                    [](const TypeDescriptor* a, const TypeDescriptor* b) { return a->Name < b->Name; });
                if (ImGui::BeginMenu(cat.c_str()))
                {
                    for (const TypeDescriptor* d : list)
                        if (ImGui::MenuItem(d->Name.c_str()))
                            Commands::AddComponent(ctx, primary, d->TypeId);
                    ImGui::EndMenu();
                }
            }
            ImGui::EndPopup();
        }

        if (!single)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Select a single entity to add components.");
        }
    }
}
