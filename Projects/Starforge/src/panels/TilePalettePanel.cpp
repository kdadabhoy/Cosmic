// panels/TilePalettePanel.cpp — see header.

#include "panels/TilePalettePanel.h"
#include "EditorContext.h"

#include <imgui.h>

#include <algorithm>

using namespace Cosmic;

namespace Starforge
{
    void TilePalettePanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        if (!ImGui::Begin("Tile Palette", pOpen))
        {
            ImGui::End();
            return;
        }

        if (!ctx.Scene)
        {
            ImGui::TextDisabled("Open a project to paint tilemaps.");
            ImGui::End();
            return;
        }

        Entity prim = ctx.PrimaryEntity();
        if (!prim || !prim.HasComponent<TilemapComponent>())
        {
            ImGui::TextWrapped("Select an entity with a Tilemap component.");
            ImGui::TextDisabled("Create one via Entity > 2D > Tilemap. Set its\n"
                                "TilesetPath in the Inspector, then paint here.");
            ctx.TileBrush.Editing = false;
            ImGui::End();
            return;
        }

        auto& tm = prim.GetComponent<TilemapComponent>();
        tm.EnsureCells();

        // Resolve the atlas exactly like the render pass (lazy, path-keyed).
        if (tm.TilesetPath != tm.ResolvedPath)
        {
            tm.ResolvedPath = tm.TilesetPath;
            tm.Resolved = tm.TilesetPath.empty() ? nullptr
                                                 : AssetLibrary::GetTexture(tm.TilesetPath);
        }

        // --- brush state ----------------------------------------------------
        ImGui::Checkbox("Paint in viewport", &ctx.TileBrush.Editing);
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enable 2D mode on the toolbar, then LMB paints the\n"
                              "selected tile and RMB erases. A drag is one undo step.");

        using Tool = EditorContext::TileBrushState::ToolKind;
        int tool = (int)ctx.TileBrush.Tool;
        ImGui::TextDisabled("Tool:"); ImGui::SameLine();
        ImGui::RadioButton("Paint", &tool, (int)Tool::Paint); ImGui::SameLine();
        ImGui::RadioButton("Flood", &tool, (int)Tool::Flood); ImGui::SameLine();
        ImGui::RadioButton("Rect",  &tool, (int)Tool::Rect);
        ctx.TileBrush.Tool = (Tool)tool;

        ImGui::Separator();

        // --- the tileset grid picker ----------------------------------------
        if (!tm.Resolved || tm.Resolved->GetWidth() <= 0 || tm.TileW <= 0 || tm.TileH <= 0)
        {
            ImGui::TextWrapped("No tileset image. Set TilesetPath on the Tilemap\n"
                               "in the Inspector (drag a texture from the Content Browser).");
            ImGui::End();
            return;
        }

        const float texW = (float)tm.Resolved->GetWidth();
        const float texH = (float)tm.Resolved->GetHeight();
        const int   cols = tm.Columns > 0 ? tm.Columns
                                          : std::max(1, (int)(texW / (float)tm.TileW));
        const int   rows = std::max(1, (int)(texH / (float)tm.TileH));
        const int   count = cols * rows;

        ImGui::TextDisabled("%dx%d tiles (%d) — LMB selects", cols, rows, count);

        const float cell = 34.0f;
        const int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + 6.0f)));

        for (int i = 0; i < count; ++i)
        {
            const int col = i % cols, row = i / cols;
            // Atlas row 0 = TOP of the image; ImGui UVs are GL bottom-left, so
            // uv0 = the tile's top-left, uv1 = its bottom-right (v decreasing).
            const ImVec2 uv0(col * tm.TileW / texW, 1.0f - row * tm.TileH / texH);
            const ImVec2 uv1((col + 1) * tm.TileW / texW, 1.0f - (row + 1) * tm.TileH / texH);

            ImGui::PushID(i);
            const bool selected = (ctx.TileBrush.Tile == (uint16_t)(i + 1));
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.95f, 0.60f, 0.15f, 0.85f));
            if (ImGui::ImageButton("tile", (ImTextureID)(intptr_t)tm.Resolved->GetRendererID(),
                                   ImVec2(cell, cell), uv0, uv1))
                ctx.TileBrush.Tile = (uint16_t)(i + 1);
            if (selected)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Tile %d", i + 1);
            ImGui::PopID();

            if ((i % perRow) != perRow - 1 && i != count - 1)
                ImGui::SameLine();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Map %dx%d, tile %d selected", tm.GridW, tm.GridH,
                            (int)ctx.TileBrush.Tile);

        ImGui::End();
    }
}
