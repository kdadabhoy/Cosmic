#pragma once

// LoadingScreen.h
//
// Detroit: Become Human-style loading overlay (Frontier, app-side). A minimalist
// set of concentric rotating arcs + a title, drawn over the viewport while a world
// builds its content on a background job (see IslandWorld's async load). Header-only
// — it draws with ImGui draw lists, so it needs no engine GPU resources and animates
// every frame while the JobSystem builds the heavy terrain off the main thread.
//
// Call from OnImGuiRender inside a WorkspaceLayer::BeginViewportOverlay()/End pair so
// the draw list is the Viewport window's (clipped to the rendered image).

#include <imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{
    class LoadingScreen
    {
    public:
        /** Draw the spinner centered in the [pos, pos+size] rect (screen px). `time`
         *  is a free-running clock (e.g. ImGui::GetTime()) driving the animation. */
        static void Draw(const glm::vec2& pos, const glm::vec2& size, float time,
                         const char* title = "GENERATING WORLD", const char* subtitle = nullptr)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p0(pos.x, pos.y);
            const ImVec2 p1(pos.x + size.x, pos.y + size.y);
            const ImVec2 c(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);

            // Dark scrim — hides the (possibly mid-warm-up) scene behind a clean field.
            dl->AddRectFilled(p0, p1, IM_COL32(6, 9, 14, 236));

            const float baseR = std::min(size.x, size.y) * 0.07f + 30.0f;
            const ImU32  col   = IM_COL32(150, 205, 240, 255);   // cool white-blue
            const ImU32  faint = IM_COL32(150, 205, 240, 38);

            // Three concentric arcs at different radii, speeds, and directions — the
            // recognizable "android booting" motif: clean thin rings, lots of space.
            struct Ring { float rScale, speed, span, thick; };
            static const Ring kRings[3] = {
                { 1.00f,  1.10f, 4.20f, 3.0f },
                { 0.72f, -1.70f, 2.60f, 2.6f },
                { 0.46f,  2.55f, 3.40f, 2.1f },
            };
            for (const Ring& rg : kRings)
            {
                const float r  = baseR * rg.rScale;
                dl->AddCircle(c, r, faint, 64, rg.thick * 0.6f);   // ghost full ring
                const float a0 = time * rg.speed;
                dl->PathArcTo(c, r, a0, a0 + rg.span, 48);
                dl->PathStroke(col, 0, rg.thick);
            }

            // Center pulse.
            const float pulse = 0.5f + 0.5f * std::sin(time * 3.0f);
            dl->AddCircleFilled(c, 2.5f + pulse * 2.5f, col, 20);

            // Title with animated trailing dots.
            ImFont* font = ImGui::GetFont();
            const int  dots = static_cast<int>(std::fmod(time * 2.0f, 4.0f));
            std::string t = title;
            for (int i = 0; i < dots; ++i) t += ".";

            const float ts   = 20.0f;
            const ImVec2 tsz = font->CalcTextSizeA(ts, FLT_MAX, 0.0f, t.c_str());
            dl->AddText(font, ts, ImVec2(c.x - tsz.x * 0.5f, c.y + baseR + 26.0f), col, t.c_str());

            if (subtitle && subtitle[0])
            {
                const float ss   = 14.0f;
                const ImVec2 ssz = font->CalcTextSizeA(ss, FLT_MAX, 0.0f, subtitle);
                dl->AddText(font, ss, ImVec2(c.x - ssz.x * 0.5f, c.y + baseR + 52.0f),
                            IM_COL32(150, 205, 240, 150), subtitle);
            }
        }
    };

} // namespace Frontier
