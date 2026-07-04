// HierarchyPanel.cpp — see header.

#include "panels/HierarchyPanel.h"
#include "commands/EditorCommands.h"

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

        // Attach a light/camera/mesh at spawn — the create-menu builders.
        void MakeDirLight(Entity e)   { e.AddComponent<DirectionalLightComponent>(); }
        void MakePointLight(Entity e) { e.AddComponent<PointLightComponent>(); }
        void MakeCamera(Entity e)     { e.AddComponent<CameraComponent>(); }
        void MakeCube(Entity e)
        { e.AddComponent<MeshRendererComponent>(Mesh::CreateBox({ 1.0f, 1.0f, 1.0f })).Color = { 0.8f, 0.8f, 0.82f, 1.0f }; }
        void MakeSphere(Entity e)
        { e.AddComponent<MeshRendererComponent>(Mesh::CreateUVSphere(0.5f, 24, 32)).Color = { 0.8f, 0.8f, 0.82f, 1.0f }; }
        void MakePlane(Entity e)
        { e.AddComponent<MeshRendererComponent>(Mesh::CreatePlane(10.0f, 10.0f)).Color = { 0.5f, 0.5f, 0.55f, 1.0f }; }
    }

    void HierarchyPanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Hierarchy");

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
        ImGui::Separator();

        const std::string filter = ToLower(m_Search);

        ImGui::BeginChild("tree", ImVec2(0, 0));

        if (!filter.empty())
        {
            // Flat filtered list.
            auto view = ctx.Scene->GetRegistry().view<TagComponent>();
            for (auto handle : view)
            {
                const auto& tag = view.get<TagComponent>(handle);
                if (ToLower(tag.Tag).find(filter) == std::string::npos)
                    continue;
                Entity e(handle, ctx.Scene.get());
                const bool sel = ctx.IsSelected(handle);
                if (ImGui::Selectable(tag.Tag.c_str(), sel))
                {
                    if (ImGui::GetIO().KeyCtrl) ctx.ToggleSelect(e);
                    else                        ctx.SelectOnly(e);
                }
            }
        }
        else
        {
            // Tree of roots.
            auto view = ctx.Scene->GetRegistry().view<TagComponent>();
            std::vector<Entity> roots;
            for (auto handle : view)
                if (IsRoot(*ctx.Scene, handle))
                    roots.push_back(Entity(handle, ctx.Scene.get()));
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
        if (ImGui::BeginMenu("Mesh"))
        {
            emit("Cube",   &MakeCube);
            emit("Sphere", &MakeSphere);
            emit("Plane",  &MakePlane);
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
