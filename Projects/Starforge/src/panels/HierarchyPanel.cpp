// HierarchyPanel.cpp — see header.

#include "panels/HierarchyPanel.h"
#include "commands/EditorCommands.h"
#include "Prefabs.h"
#include "ui/IconsLucide.h"
#include "scene/ui/UiComponents.h"   // T14 — UI component icons

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

using namespace Cosmic;
using Cosmic::Reflect::FieldValue;

namespace Starforge
{
    namespace
    {
        uint64_t IdOf(Entity e)
        {
            return e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0;
        }

        bool IsRoot(Scene& scene, entt::entity e)
        {
            auto* rel = scene.GetRegistry().try_get<RelationshipComponent>(e);
            return !rel || !rel->Parent.IsValid();
        }

        std::vector<Entity> ChildrenOf(Scene& scene, Entity e)
        {
            std::vector<Entity> out;
            if (auto* rel = scene.GetRegistry().try_get<RelationshipComponent>((entt::entity)e))
                for (UUID c : rel->Children)
                    if (Entity ce = scene.FindByUUID(c)) out.push_back(ce);
            return out;
        }

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        // T14 — the row glyph + accent, chosen from an entity's DOMINANT component
        // (first match wins), colored consistently with the T5 asset-type table.
        struct EntityIcon { const char* Glyph; ImU32 Color; };
        EntityIcon IconFor(Entity e)
        {
            if (e.HasComponent<CameraComponent>())            return { ICON_LC_CAMERA,   IM_COL32(120, 170, 240, 255) };
            if (e.HasComponent<DirectionalLightComponent>() ||
                e.HasComponent<PointLightComponent>())        return { ICON_LC_LIGHTBULB, IM_COL32(240, 200,  90, 255) };
            if (e.HasComponent<TerrainComponent>())           return { ICON_LC_MOUNTAIN, IM_COL32(150, 175, 110, 255) };
            if (e.HasComponent<WaterComponent>())             return { ICON_LC_WAVES,    IM_COL32( 80, 180, 200, 255) };
            if (e.HasComponent<ParticleEmitterComponent>())   return { ICON_LC_SPARKLES, IM_COL32(236,  90, 190, 255) };
            if (e.HasComponent<VoxelVolumeComponent>())       return { ICON_LC_BLOCKS,   IM_COL32(140, 150, 160, 255) };
            if (e.HasComponent<CanvasComponent>()      || e.HasComponent<RectTransformComponent>() ||
                e.HasComponent<UiImageComponent>()     || e.HasComponent<UiTextComponent>() ||
                e.HasComponent<UiButtonComponent>())          return { ICON_LC_TYPE,     IM_COL32(240, 200,  60, 255) };
            if (e.HasComponent<TilemapComponent>())           return { ICON_LC_GRID_2X2, IM_COL32(180, 120, 230, 255) };
            if (e.HasComponent<SpriteRendererComponent>())    return { ICON_LC_IMAGE,    IM_COL32(180, 120, 230, 255) };
            if (e.HasComponent<MeshRendererComponent>()  ||
                e.HasComponent<PrimitiveMeshComponent>() ||
                e.HasComponent<LODGroupComponent>())          return { ICON_LC_BOX,      IM_COL32(120, 190, 100, 255) };
            if (e.HasComponent<NativeScriptComponent>() ||
                e.HasComponent<SystemScriptComponent>())      return { ICON_LC_CODE,     IM_COL32(100, 200, 180, 255) };
            return { ICON_LC_CIRCLE, IM_COL32(150, 150, 155, 255) };   // an empty / transform-only entity
        }

        // Attach a light/camera/mesh at spawn — the create-menu builders.
        void MakeDirLight(Entity e)   { e.AddComponent<DirectionalLightComponent>(); }
        void MakePointLight(Entity e) { e.AddComponent<PointLightComponent>(); }
        void MakeCamera(Entity e)     { e.AddComponent<CameraComponent>(); }
        // Parametric primitives (E15): attach shape + params + a default-tint
        // MeshRenderer; Scene::SyncPrimitiveMeshes builds the mesh at render time.
        void MakePrimitive(Entity e, PrimitiveMeshComponent::Shape shape)
        {
            e.AddComponent<PrimitiveMeshComponent>(shape);
            e.AddComponent<MeshRendererComponent>().Color = { 0.8f, 0.8f, 0.82f, 1.0f };
        }
        void MakeCube(Entity e)     { MakePrimitive(e, PrimitiveMeshComponent::Shape::Box); }
        void MakeSphere(Entity e)   { MakePrimitive(e, PrimitiveMeshComponent::Shape::Sphere); }
        void MakePlane(Entity e)    { MakePrimitive(e, PrimitiveMeshComponent::Shape::Plane); }
        void MakeCylinder(Entity e) { MakePrimitive(e, PrimitiveMeshComponent::Shape::Cylinder); }
        void MakeCone(Entity e)     { MakePrimitive(e, PrimitiveMeshComponent::Shape::Cone); }
        void MakeTorus(Entity e)    { MakePrimitive(e, PrimitiveMeshComponent::Shape::Torus); }
    }

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        ImGui::Begin("Hierarchy", pOpen);

        if (!ctx.Scene)
        {
            ImGui::TextDisabled("No scene open.");
            ImGui::End();
            return;
        }

        // --- Toolbar: create menu + search ---------------------------------
        if (ImGui::Button("+ Create"))
            ImGui::OpenPopup("create_root");
        if (ImGui::BeginPopup("create_root"))
        {
            DrawCreateMenu(ctx, Entity{});
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search", m_Search, sizeof(m_Search));

        // T14 — entity + selected counts in the header.
        {
            size_t nEntities = 0;
            for (auto _ : ctx.Scene->GetRegistry().view<TagComponent>()) { (void)_; ++nEntities; }
            ImGui::TextDisabled("%zu entities  \xC2\xB7  %zu selected", nEntities, ctx.Selection.size());
        }
        ImGui::Separator();

        const std::string filter = ToLower(m_Search);

        ImGui::BeginChild("tree", ImVec2(0, 0));

        if (!filter.empty())
        {
            // Flat filtered list.
            auto view = ctx.Scene->GetRegistry().view<TagComponent>();
            bool anyMatch = false;
            for (auto handle : view)
            {
                const auto& tag = view.get<TagComponent>(handle);
                if (ToLower(tag.Tag).find(filter) == std::string::npos)
                    continue;
                anyMatch = true;
                Entity e(handle, ctx.Scene.get());
                const bool sel = ctx.IsSelected(handle);
                if (ImGui::Selectable(tag.Tag.c_str(), sel))
                {
                    if (ImGui::GetIO().KeyCtrl) ctx.ToggleSelect(e);
                    else                        ctx.SelectOnly(e);
                }
            }
            if (!anyMatch)
                ImGui::TextDisabled("No entities match \"%s\".", m_Search);
        }
        else
        {
            // Tree of roots.
            auto view = ctx.Scene->GetRegistry().view<TagComponent>();
            std::vector<Entity> roots;
            for (auto handle : view)
                if (IsRoot(*ctx.Scene, handle))
                    roots.push_back(Entity(handle, ctx.Scene.get()));
            if (roots.empty())
            {
                // Empty-state hint (E21): how to put the first entity on screen.
                ImGui::TextDisabled("Scene is empty.");
                ImGui::TextDisabled("Click \"+ Create\" above (or use the Entity menu)");
                ImGui::TextDisabled("to add your first entity.");
            }
            for (Entity r : roots)
                DrawNode(ctx, r);
        }

        // Click empty space to deselect / drop to root.
        ImGui::Dummy(ImVec2(0, ImGui::GetContentRegionAvail().y > 4 ? ImGui::GetContentRegionAvail().y : 4));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY_UUID"))
            {
                const uint64_t src = *(const uint64_t*)p->Data;
                m_Deferred.push_back([&ctx, src] {
                    if (Entity e = ctx.Scene->FindByUUID(UUID(src)))
                        Commands::Reparent(ctx, e, Entity{});   // detach to root
                });
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::IsItemClicked())
            ctx.ClearSelection();

        ImGui::EndChild();

        // Run any structural mutations collected during the draw.
        for (auto& fn : m_Deferred) fn();
        m_Deferred.clear();

        ImGui::End();
    }

    void HierarchyPanel::DrawNode(EditorContext& ctx, Entity e)
    {
        if (!e) return;
        auto& reg = ctx.Scene->GetRegistry();
        const std::string tag = e.HasComponent<TagComponent>()
            ? e.GetComponent<TagComponent>().Tag : std::string("Entity");
        const uint64_t id = IdOf(e);
        std::vector<Entity> children = ChildrenOf(*ctx.Scene, e);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ctx.IsSelected((entt::entity)e)) flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty())                flags |= ImGuiTreeNodeFlags_Leaf;

        ImGui::PushID((int)(uint32_t)(entt::entity)e);

        // T13 — per-row active toggle (own flag) + dim the row when effectively
        // inactive (own ∧ ancestors). Undoable, targets just this entity.
        const bool ownActive = e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Active : true;
        const bool effActive = ctx.Scene->IsActiveInHierarchy((entt::entity)e);
        if (ImGui::SmallButton(ownActive ? ICON_LC_EYE : ICON_LC_EYE_OFF) && e.HasComponent<TagComponent>())
        {
            const bool nv = !ownActive;
            e.GetComponent<TagComponent>().Active = nv;   // apply, then record (one undo step)
            Commands::CommitFieldEditFor(ctx, e, nv ? "Show entity" : "Hide entity",
                entt::type_hash<TagComponent>::value(), "Active", FieldValue{ ownActive }, FieldValue{ nv });
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle active");
        ImGui::SameLine();

        // T14 — dominant-component glyph (colored) before the name.
        const EntityIcon icon = IconFor(e);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(icon.Color), "%s", icon.Glyph);
        ImGui::SameLine(0, 4);

        if (!effActive)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

        bool open = false;
        if (m_RenameTarget == id)
        {
            // Inline rename — a bullet keeps the row indent; children hide until done.
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            if (m_RenameFocus) { ImGui::SetKeyboardFocusHere(); m_RenameFocus = false; }
            if (ImGui::InputText("##rn", m_RenameBuf, sizeof(m_RenameBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                std::string newName = m_RenameBuf;
                uint64_t rid = id;
                m_Deferred.push_back([&ctx, rid, newName] {
                    if (Entity re = ctx.Scene->FindByUUID(UUID(rid)))
                    {
                        FieldValue before{ std::string(re.GetComponent<TagComponent>().Tag) };
                        re.GetComponent<TagComponent>().Tag = newName;
                        Commands::CommitFieldEdit(ctx, "Rename",
                            entt::type_hash<TagComponent>::value(), "Tag", before, FieldValue{ newName });
                    }
                });
                m_RenameTarget = 0;
            }
            if (ImGui::IsItemDeactivated()) m_RenameTarget = 0;
        }
        else
        {
            open = ImGui::TreeNodeEx("node", flags, "%s", tag.c_str());

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                if (ImGui::GetIO().KeyCtrl) ctx.ToggleSelect(e);
                else                        ctx.SelectOnly(e);
            }
            if (ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2) && ctx.IsSelected((entt::entity)e))
            {
                m_RenameTarget = id; m_RenameFocus = true;
                std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s", tag.c_str());
            }

            // Drag source.
            if (ImGui::BeginDragDropSource())
            {
                uint64_t payload = id;
                ImGui::SetDragDropPayload("ENTITY_UUID", &payload, sizeof(payload));
                ImGui::TextUnformatted(tag.c_str());
                ImGui::EndDragDropSource();
            }
            // Drop target → reparent under this node.
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY_UUID"))
                {
                    const uint64_t src = *(const uint64_t*)p->Data;
                    uint64_t dst = id;
                    m_Deferred.push_back([&ctx, src, dst] {
                        Entity c = ctx.Scene->FindByUUID(UUID(src));
                        Entity pnt = ctx.Scene->FindByUUID(UUID(dst));
                        if (c && pnt) Commands::Reparent(ctx, c, pnt);
                    });
                }
                ImGui::EndDragDropTarget();
            }

            DrawContextMenu(ctx, e);
        }

        if (!effActive)
            ImGui::PopStyleColor();

        if (open)
        {
            for (Entity c : children)
                DrawNode(ctx, c);
            ImGui::TreePop();
        }

        ImGui::PopID();
        (void)reg;
    }

    void HierarchyPanel::DrawContextMenu(EditorContext& ctx, Entity e)
    {
        if (!ImGui::BeginPopupContextItem("ctx"))
            return;

        if (!ctx.IsSelected((entt::entity)e))
            ctx.SelectOnly(e);

        if (ImGui::BeginMenu("Create Child"))
        {
            DrawCreateMenu(ctx, e);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Rename", "F2"))
        {
            m_RenameTarget = IdOf(e); m_RenameFocus = true;
            std::snprintf(m_RenameBuf, sizeof(m_RenameBuf), "%s",
                          e.GetComponent<TagComponent>().Tag.c_str());
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        {
            uint64_t id = IdOf(e);
            m_Deferred.push_back([&ctx, id] {
                if (Entity src = ctx.Scene->FindByUUID(UUID(id))) Commands::Duplicate(ctx, src);
            });
        }
        if (ImGui::MenuItem("Delete", "Del"))
        {
            uint64_t id = IdOf(e);
            m_Deferred.push_back([&ctx, id] {
                if (Entity d = ctx.Scene->FindByUUID(UUID(id))) Commands::Destroy(ctx, d);
            });
        }

        // --- Prefabs (E14) -------------------------------------------------
        ImGui::Separator();
        if (ImGui::MenuItem("Save as Prefab"))
        {
            uint64_t id = IdOf(e);
            m_Deferred.push_back([&ctx, id] {
                if (Entity r = ctx.Scene->FindByUUID(UUID(id))) Prefabs::SaveAs(ctx, r);
            });
        }
        if (e.HasComponent<PrefabComponent>())
        {
            if (ImGui::MenuItem("Apply to Prefab"))
            {
                uint64_t id = IdOf(e);
                m_Deferred.push_back([&ctx, id] {
                    if (Entity r = ctx.Scene->FindByUUID(UUID(id))) Prefabs::Apply(ctx, r);
                });
            }
            if (ImGui::MenuItem("Revert to Prefab"))
            {
                uint64_t id = IdOf(e);
                m_Deferred.push_back([&ctx, id] {
                    if (Entity r = ctx.Scene->FindByUUID(UUID(id))) Prefabs::Revert(ctx, r);
                });
            }
        }
        ImGui::EndPopup();
    }

    void HierarchyPanel::DrawCreateMenu(EditorContext& ctx, Entity parent)
    {
        // parent is captured by UUID so the deferred build survives this frame.
        const uint64_t parentId = parent ? IdOf(parent) : 0;
        auto emit = [&](const char* label, void(*build)(Entity))
        {
            if (ImGui::MenuItem(label))
            {
                std::string n = label;
                uint64_t pid = parentId;
                m_Deferred.push_back([&ctx, n, pid, build] {
                    Entity p = pid ? ctx.Scene->FindByUUID(UUID(pid)) : Entity{};
                    Commands::Create(ctx, n, p, build ? std::function<void(Entity)>(build)
                                                      : std::function<void(Entity)>());
                });
            }
        };

        emit("Empty", nullptr);
        if (ImGui::BeginMenu("Primitive"))
        {
            emit("Cube",     &MakeCube);
            emit("Sphere",   &MakeSphere);
            emit("Plane",    &MakePlane);
            emit("Cylinder", &MakeCylinder);
            emit("Cone",     &MakeCone);
            emit("Torus",    &MakeTorus);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Light"))
        {
            emit("Directional Light", &MakeDirLight);
            emit("Point Light",       &MakePointLight);
            ImGui::EndMenu();
        }
        emit("Camera", &MakeCamera);
    }
}
