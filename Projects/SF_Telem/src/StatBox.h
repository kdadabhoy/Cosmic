#pragma once

// StatBox.h — SF_Telem
// Small framed "indication" widgets for the data-box panels. These now build on
// the engine's theme-aware UI::StatCard, so they pick up the active theme and
// render the big value with a crisp scaled font (no more SetWindowFontScale blur).

#include <imgui.h>
#include "ui/Widgets.h"
#include "ui/Fonts.h"

#include <cstdio>

namespace Workspace
{
    // Draw a large value inside the current window. Kept for any direct callers;
    // uses a real bold face at a large size so it stays crisp at any scale.
    inline void BigValue(const char* text, ImVec4 color)
    {
        Cosmic::UI::Fonts::Push("Roboto-Bold", 30.0f);
        ImGui::TextColored(color, "%s", text);
        Cosmic::UI::Fonts::Pop();
    }

    // A bordered indication card: label, big live value + unit, then the running
    // average and max underneath (dimmed).
    inline void StatBox(const char* id, const char* label, float value,
                        const char* unit, float avgValue, float maxValue, ImVec4 accent,
                        ImVec2 size = ImVec2(168.0f, 84.0f))
    {
        char val[48]; std::snprintf(val, sizeof(val), "%.1f %s", value, unit);
        char sub[48]; std::snprintf(sub, sizeof(sub), "avg %.1f  max %.1f", avgValue, maxValue);
        Cosmic::UI::StatCard(id, nullptr, label, val, sub, accent, size);
    }

    // RPM-vs-predicted card: big measured RPM, average + max underneath. The
    // value is tinted green as it approaches/exceeds the predicted ceiling.
    inline void RpmBox(const char* id, const char* label, float rpm,
                       float predicted, float avgValue, float maxValue, ImVec4 accent,
                       ImVec2 size = ImVec2(200.0f, 84.0f))
    {
        const float frac = (predicted > 1.0f) ? (rpm / predicted) : 0.0f;
        const ImVec4 vc = frac >= 0.9f ? ImVec4(0.30f, 1.0f, 0.40f, 1.0f)
                        : frac >= 0.5f ? ImVec4(1.0f, 0.90f, 0.30f, 1.0f)
                                       : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        char val[32]; std::snprintf(val, sizeof(val), "%.0f", rpm);
        char sub[48]; std::snprintf(sub, sizeof(sub), "avg %.0f  max %.0f", avgValue, maxValue);
        Cosmic::UI::StatCard(id, nullptr, label, val, sub, accent, size, vc);
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
