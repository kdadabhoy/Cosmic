#pragma once

// StatBox.h — SF_Telem
// Small framed "indication" widgets for the data-box panels. Nothing animates;
// these just present a live number prominently with its running max underneath.

#include <imgui.h>
#include <cstdio>

namespace Workspace
{
    // Draw a large value inside the current window (scales the font up briefly).
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
        ImGui::TextDisabled("max %.1f %s", maxValue, unit);
        ImGui::EndChild();
    }

    // RPM-vs-predicted box: big measured RPM, with predicted + max underneath.
    // The value is tinted green as it approaches/exceeds the predicted ceiling.
    inline void RpmBox(const char* id, const char* label, float rpm,
                       float predicted, float maxValue, ImVec4 accent,
                       ImVec2 size = ImVec2(200.0f, 84.0f))
    {
        ImGui::BeginChild(id, size, true);
        ImGui::TextColored(accent, "%s", label);

        const float frac = (predicted > 1.0f) ? (rpm / predicted) : 0.0f;
        const ImVec4 vc = frac >= 0.9f ? ImVec4(0.30f, 1.0f, 0.40f, 1.0f)
                        : frac >= 0.5f ? ImVec4(1.0f, 0.90f, 0.30f, 1.0f)
                                       : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        char buf[32]; snprintf(buf, sizeof(buf), "%.0f", rpm);
        BigValue(buf, vc);
        ImGui::TextDisabled("pred %.0f   max %.0f", predicted, maxValue);
        ImGui::EndChild();
    }

    // Placeholder image plate (until a real texture is uploaded). Draws a framed
    // rectangle with a centered caption so the data boxes have a visual anchor.
    inline void ImagePlaceholder(const char* id, const char* caption,
                                 ImVec2 size, ImVec4 accent)
    {
        ImGui::BeginChild(id, size, true);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + size.x, p0.y + size.y);
        dl->AddRectFilled(p0, p1, IM_COL32(28, 30, 38, 255));
        const ImU32 line = IM_COL32((int)(accent.x * 255), (int)(accent.y * 255),
                                    (int)(accent.z * 255), 90);
        dl->AddRect(ImVec2(p0.x + 4, p0.y + 4), ImVec2(p1.x - 4, p1.y - 4), line, 6.0f, 0, 2.0f);
        const ImVec2 ts = ImGui::CalcTextSize(caption);
        dl->AddText(ImVec2((p0.x + p1.x - ts.x) * 0.5f, (p0.y + p1.y - ts.y) * 0.5f),
                    IM_COL32(150, 155, 170, 255), caption);
        ImGui::EndChild();
    }

} // namespace Workspace
