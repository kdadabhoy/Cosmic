// panels/VoxelPanel.cpp — see VoxelPanel.h.

#include "panels/VoxelPanel.h"
#include "EditorContext.h"
#include "commands/EditorCommands.h"

#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"
#include "voxel/VoxelRender.h"

#include <imgui.h>

using namespace Cosmic;

namespace Starforge
{
    void VoxelPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        if (pOpen && !*pOpen)
            return;
        if (!ImGui::Begin("Voxels", pOpen))
        {
            ImGui::End();
            return;
        }

        Entity e = ctx.PrimaryEntity();
        if (!e || !e.HasComponent<VoxelVolumeComponent>())
        {
            ImGui::TextWrapped("Select a Voxel Volume entity (World > Voxel Volume) to author it.");
            ImGui::End();
            return;
        }

        auto& vc = e.GetComponent<VoxelVolumeComponent>();

        // Runtime assets come online on the first render sync — guard until then.
        if (!vc.Volume || !vc.Palette)
        {
            ImGui::TextDisabled("Initializing voxel world (render one frame)...");
            ImGui::End();
            return;
        }

        VoxelVolume&  vol = *vc.Volume;
        BlockPalette& pal = *vc.Palette;

        // --- Brush --------------------------------------------------------
        ImGui::SeparatorText("Brush");
        ImGui::Checkbox("Edit in viewport", &ctx.VoxelBrush.Editing);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("While on: LMB places the active block, RMB breaks.\nEach drag is one undo step (Ctrl+Z).");
        ImGui::SliderFloat("Reach", &ctx.VoxelBrush.Reach, 4.0f, 256.0f, "%.0f m");

        // --- Palette picker ----------------------------------------------
        ImGui::SeparatorText("Palette");
        for (uint16_t id = 1; id < pal.Count(); ++id)
        {
            const BlockType& b = pal.Get(id);
            const ImVec4 col{ b.Color.r, b.Color.g, b.Color.b, 1.0f };
            const bool active = (ctx.VoxelBrush.ActiveBlock == id);

            ImGui::PushID(id);
            ImGui::ColorButton("##swatch", col,
                ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(20, 20));
            ImGui::SameLine();
            if (ImGui::Selectable(b.Name.c_str(), active))
                ctx.VoxelBrush.ActiveBlock = id;
            ImGui::PopID();
        }

        // --- Rendering ----------------------------------------------------
        ImGui::SeparatorText("Rendering");
        if (ImGui::Checkbox("Greedy meshing", &vc.Greedy))
        {
            // Force a full re-mesh under the new mode.
            vol.ForEachChunk([&](const glm::ivec3& c) { vol.MarkChunkDirty(c); });
        }

        // --- World actions ------------------------------------------------
        ImGui::SeparatorText("World");
        ImGui::Text("Chunks: %zu   Meshes: %zu",
                    vol.ChunkCount(),
                    vc.Render ? vc.Render->ChunkMeshes.size() : (size_t)0);

        if (ImGui::Button("Regenerate"))
        {
            // Drop all voxel data + GPU meshes; streaming (if GenEnabled) refills.
            vol.Clear();
            if (vc.Render) { vc.Render->ChunkMeshes.clear(); vc.Render->Generated.clear(); vc.Render->CollisionDirty.clear(); }
            vc.BuiltGenSignature = 0;
            ctx.MarkDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            vc.GenEnabled = false;   // stop streaming so it stays empty
            vol.Clear();
            if (vc.Render) { vc.Render->ChunkMeshes.clear(); vc.Render->Generated.clear(); vc.Render->CollisionDirty.clear(); }
            ctx.MarkDirty();
        }
        if (!vc.VolumePath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Save .cvox"))
            {
                if (vol.Save(vc.VolumePath))
                    CS_INFO("Saved voxel volume to {}", vc.VolumePath);
            }
        }

        ImGui::End();
    }
}
