// panels/MaterialEditorPanel.cpp — see MaterialEditorPanel.h.

#include "panels/MaterialEditorPanel.h"
#include "EditorContext.h"
#include "widgets/PropertyRows.h"

#include <imgui.h>

using namespace Cosmic;

namespace Starforge
{
    void MaterialEditorPanel::OnImGuiRender(EditorContext& ctx)
    {
        if (ImGui::Begin("Material Editor"))
        {
            if (!ctx.ProjectOpen)
            {
                ImGui::TextDisabled("Open a project to author materials.");
                ImGui::End();
                return;
            }

            // --- Toolbar: New / name / Save --------------------------------
            if (ImGui::Button("New"))
            {
                m_Asset = MaterialAsset{};
                m_Path.clear();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(160.0f);
            ImGui::InputText("##matname", m_SaveName, sizeof(m_SaveName));
            ImGui::SameLine();
            if (ImGui::Button("Save .cmat") && m_SaveName[0])
            {
                const std::string vfs = std::string("project://materials/") + m_SaveName + ".cmat";
                if (AssetLibrary::SaveMaterialAsset(m_Asset, vfs))
                {
                    m_Path = vfs;
                    AssetLibrary::Reload(vfs);   // drop any cached build so re-resolve picks up edits
                    ctx.Log("[Material] Saved " + vfs);
                }
                else
                {
                    ctx.Log("[Material] Save failed: " + vfs, LogSeverity::Error);
                }
            }
            if (!m_Path.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", m_Path.c_str());
            }

            // Empty-state hint (E21): the New -> tune -> Save -> Assign flow.
            if (m_Path.empty())
                ImGui::TextDisabled("New material — tune below, Save .cmat, then Assign to a selected mesh.");

            ImGui::Separator();

            // --- Reflected material fields (auto-UI) -----------------------
            if (const auto* desc = Reflect::GetRegistry().Find<MaterialAsset>())
                for (const auto& f : desc->Fields)
                    PropertyRows::DrawField(f, &m_Asset, /*mixed*/ false);

            ImGui::Separator();

            // --- Assign / load to the selected MeshRenderer ----------------
            Entity sel = ctx.PrimaryEntity();
            const bool hasMesh = sel && sel.HasComponent<MeshRendererComponent>();

            ImGui::BeginDisabled(!hasMesh);
            if (ImGui::Button("Assign to Selection"))
            {
                auto& mr = sel.GetComponent<MeshRendererComponent>();
                mr.MaterialPath         = m_Path;   // "" when unsaved -> live-only until saved
                mr.MaterialAsset        = AssetLibrary::BuildMaterial(m_Asset, m_SaveName);
                mr.MaterialPathResolved = true;     // don't let the sync overwrite our live build
                ctx.MarkDirty();
                ctx.Log("[Material] Assigned to selection.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Load from Selection") && hasMesh)
            {
                auto& mr = sel.GetComponent<MeshRendererComponent>();
                if (!mr.MaterialPath.empty() && AssetLibrary::LoadMaterialAsset(m_Asset, mr.MaterialPath))
                    m_Path = mr.MaterialPath;
                else
                    ctx.Log("[Material] Selection has no .cmat to load.", LogSeverity::Warn);
            }
            ImGui::EndDisabled();

            ImGui::TextDisabled("Edits apply live to the assigned entity (not undoable in v1);");
            ImGui::TextDisabled("Save then re-assign, or edit the .cmat and re-open the scene.");
        }
        ImGui::End();
    }
}
