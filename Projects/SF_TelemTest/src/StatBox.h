#pragma once

// StatBox.h — SF_TelemTest
// Small framed value widgets for the test screens (label + big live value, with
// the running max underneath). Plus a responsive grid that auto-wraps boxes to
// fit the panel width, so the same layout reads well narrow or wide.

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Workspace
{
    inline void BigValue(const char* text, ImVec4 color)
    {
        ImGui::SetWindowFontScale(1.7f);
        ImGui::TextColored(color, "%s", text);
        ImGui::SetWindowFontScale(1.0f);
    }

    // A bordered indication box: label, big live value + unit, running max below.
    inline void StatBox(const char* id, const char* label, float value,
                        const char* unit, float maxValue, ImVec4 accent,
                        ImVec2 size = ImVec2(168.0f, 84.0f))
    {
        ImGui::BeginChild(id, size, true);
        ImGui::TextColored(accent, "%s", label);
        char buf[48]; snprintf(buf, sizeof(buf), "%.1f %s", value, unit);
        BigValue(buf, ImVec4(1, 1, 1, 1));
        ImGui::TextDisabled("max %.1f", maxValue);
        ImGui::EndChild();
    }

    // One cell spec for the responsive grid.
    struct BoxSpec
    {
        const char* id;
        const char* label;
        float       value;
        const char* unit;
        float       maxValue;
        ImVec4      accent;
    };

    // Auto-fit grid: as many columns as the width allows (min 2), boxes stretched
    // to fill the row.
    inline void StatGrid(const BoxSpec* boxes, int count,
                         int minCols = 2, float minColWidth = 168.0f, float maxColWidth = 320.0f)
    {
        if (count <= 0) return;
        const float avail   = ImGui::GetContentRegionAvail().x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        int cols = (int)std::floor((avail + spacing) / (minColWidth + spacing));
        cols = std::max(cols, minCols);
        cols = std::min(cols, count);
        cols = std::max(cols, 1);

        float boxW = (avail - spacing * (cols - 1)) / cols;
        boxW = std::min(boxW, maxColWidth);
        boxW = std::max(boxW, 1.0f);
        const ImVec2 size(boxW, 84.0f);

        for (int i = 0; i < count; ++i)
        {
            if (i % cols != 0) ImGui::SameLine();
            const BoxSpec& b = boxes[i];
            StatBox(b.id, b.label, b.value, b.unit, b.maxValue, b.accent, size);
        }
    }

} // namespace Workspace
