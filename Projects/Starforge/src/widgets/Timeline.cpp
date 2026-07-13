// Timeline.cpp — see Timeline.h. Reusable display+scrub timeline (M2).

#include "widgets/Timeline.h"
#include "ui/IconsLucide.h"

#include <cstdio>

namespace Starforge
{
    namespace
    {
        // "Nice" ruler step (1/2/5 × 10^n seconds) giving at least `minPx` between
        // labelled ticks at the current zoom.
        float NiceStep(float pixelsPerSecond, float minPx)
        {
            const float minSec = minPx / std::max(1.0f, pixelsPerSecond);
            float mag = std::pow(10.0f, std::floor(std::log10(std::max(1e-4f, minSec))));
            for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
                if (mag * m >= minSec)
                    return mag * m;
            return mag * 10.0f;
        }

        float NearestKey(const std::vector<TimelineTrack>& tracks, float t, float tolSec, bool& hit)
        {
            float best = t;
            float bestD = tolSec;
            hit = false;
            for (const auto& tr : tracks)
                for (float k : tr.Keys)
                {
                    const float d = std::abs(k - t);
                    if (d < bestD) { bestD = d; best = k; hit = true; }
                }
            return best;
        }
    }

    TimelineResult Timeline::Draw(const char* id, TimelineState& st,
                                  const std::vector<TimelineTrack>& tracks,
                                  const TimelineOptions& opts)
    {
        TimelineResult res;
        ImGui::PushID(id);

        const ImU32 colBg      = IM_COL32(24, 26, 32, 255);
        const ImU32 colGutter  = IM_COL32(30, 33, 40, 255);
        const ImU32 colRuler   = IM_COL32(38, 41, 50, 255);
        const ImU32 colTick     = IM_COL32(120, 126, 140, 255);
        const ImU32 colTickFaint = IM_COL32(70, 74, 86, 255);
        const ImU32 colLaneLine = IM_COL32(44, 47, 57, 255);
        const ImU32 colHead     = IM_COL32(255, 120, 60, 255);
        const ImU32 colText     = IM_COL32(190, 194, 205, 255);

        // ---- Transport row ---------------------------------------------------
        if (opts.ShowTransport)
        {
            if (ImGui::Button(st.Playing ? ICON_LC_PAUSE : ICON_LC_PLAY))
            {
                if (st.Playing) st.Pause(); else st.Play();
                res.Toggled = true;
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_LC_SQUARE)) { st.Stop(); res.Toggled = true; }   // stop → rewind
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (rewind to 0)");
            ImGui::SameLine();

            // Loop toggle — accent-tinted when on.
            if (st.Loop)
            {
                const ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(acc.x, acc.y, acc.z, 0.32f));
            }
            if (ImGui::Button(ICON_LC_REPEAT)) { st.Loop = !st.Loop; res.Toggled = true; }
            if (st.Loop) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(st.Loop ? "Looping" : "Play once");

            ImGui::SameLine();
            ImGui::Text("%.3f / %.3f s", st.Time, st.Duration);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.0f%%)", st.Normalized() * 100.0f);
        }

        // ---- Geometry --------------------------------------------------------
        const float gutterW = 136.0f;
        const float rulerH  = 20.0f;
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        float height = opts.Height > 0.0f ? opts.Height : avail.y;
        height = std::max(height, rulerH + 28.0f);
        const float width = std::max(avail.x, gutterW + 40.0f);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float laneX0 = origin.x + gutterW;
        const float laneX1 = origin.x + width;
        const float laneW  = std::max(10.0f, laneX1 - laneX0);
        const float lanesY0 = origin.y + rulerH;
        const float lanesY1 = origin.y + height;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), colBg);
        dl->AddRectFilled(ImVec2(origin.x, origin.y), ImVec2(laneX0, origin.y + height), colGutter);
        dl->AddRectFilled(ImVec2(laneX0, origin.y), ImVec2(laneX1, lanesY0), colRuler);

        // Interaction surface spanning the whole widget (left = scrub in the lane,
        // middle/right drag = pan, wheel = zoom).
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##surface", ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        auto t2x = [&](float t) { return laneX0 + (t - st.ViewStart) * st.PixelsPerSecond; };
        auto x2t = [&](float x) { return st.ViewStart + (x - laneX0) / st.PixelsPerSecond; };

        // Zoom around the cursor.
        if (hovered && io.MouseWheel != 0.0f && io.MousePos.x >= laneX0)
        {
            const float tUnder = x2t(io.MousePos.x);
            st.PixelsPerSecond = std::clamp(st.PixelsPerSecond * std::pow(1.15f, io.MouseWheel),
                                            8.0f, 4000.0f);
            st.ViewStart = tUnder - (io.MousePos.x - laneX0) / st.PixelsPerSecond;
        }
        // Pan with middle/right drag.
        if (ImGui::IsItemActive() &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
             ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
        {
            st.ViewStart -= io.MouseDelta.x / st.PixelsPerSecond;
        }
        // Scrub with left drag anywhere over the lane (works while paused OR playing).
        if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            io.MousePos.x >= laneX0 - 4.0f)
        {
            float t = x2t(std::clamp(io.MousePos.x, laneX0, laneX1));
            if (opts.SnapSeconds > 0.0f)
                t = std::round(t / opts.SnapSeconds) * opts.SnapSeconds;
            else if (opts.Snap)
            {
                bool hit = false;
                const float snapped = NearestKey(tracks, t, 6.0f / st.PixelsPerSecond, hit);
                if (hit) t = snapped;
            }
            st.Scrub(t);
            res.Scrubbed = true;
        }
        // Keep the clip visible if the view drifted fully off it.
        st.ViewStart = std::clamp(st.ViewStart, -st.Duration, std::max(0.0f, st.Duration));

        // ---- Ruler ticks + labels -------------------------------------------
        dl->PushClipRect(ImVec2(laneX0, origin.y), ImVec2(laneX1, lanesY1), true);
        const float step = NiceStep(st.PixelsPerSecond, 64.0f);
        const float first = std::floor(st.ViewStart / step) * step;
        for (float t = first; t2x(t) <= laneX1 + 1.0f; t += step)
        {
            const float x = t2x(t);
            if (x < laneX0 - 1.0f) continue;
            dl->AddLine(ImVec2(x, origin.y + 4.0f), ImVec2(x, lanesY1), colTickFaint);
            dl->AddLine(ImVec2(x, origin.y + 4.0f), ImVec2(x, lanesY0), colTick);
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.3g", t);
            dl->AddText(ImVec2(x + 3.0f, origin.y + 3.0f), colText, buf);
        }

        // ---- Tracks ----------------------------------------------------------
        const int nTracks = (int)tracks.size();
        const float lanesH = std::max(0.0f, lanesY1 - lanesY0);
        const float trackH = nTracks > 0
            ? std::clamp(lanesH / (float)nTracks, 9.0f, 22.0f) : 0.0f;

        float mouseKeyTime = 0.0f; bool mouseKeyShow = false;
        for (int i = 0; i < nTracks; ++i)
        {
            const float y0 = lanesY0 + i * trackH;
            const float y1 = y0 + trackH;
            if (y0 >= lanesY1) break;   // clip overflow (host window can grow the dock)
            if ((i & 1) == 0)
                dl->AddRectFilled(ImVec2(laneX0, y0), ImVec2(laneX1, y1), IM_COL32(255, 255, 255, 6));
            dl->AddLine(ImVec2(laneX0, y1), ImVec2(laneX1, y1), colLaneLine);

            // Name in the gutter (clipped).
            dl->PushClipRect(ImVec2(origin.x + 4.0f, y0), ImVec2(laneX0 - 4.0f, y1), true);
            dl->AddText(ImVec2(origin.x + 8.0f, y0 + std::max(0.0f, (trackH - ImGui::GetFontSize()) * 0.5f)),
                        colText, tracks[i].Name.c_str());
            dl->PopClipRect();

            // Key ticks (small diamonds).
            const float cy = 0.5f * (y0 + y1);
            const float r  = std::min(4.0f, trackH * 0.30f);
            for (float k : tracks[i].Keys)
            {
                const float x = t2x(k);
                if (x < laneX0 - r || x > laneX1 + r) continue;
                dl->AddQuadFilled(ImVec2(x, cy - r), ImVec2(x + r, cy),
                                  ImVec2(x, cy + r), ImVec2(x - r, cy), tracks[i].Color);
                if (hovered && std::abs(io.MousePos.x - x) <= r + 1.0f &&
                    io.MousePos.y >= y0 && io.MousePos.y <= y1)
                {
                    mouseKeyShow = true; mouseKeyTime = k;
                }
            }
        }
        dl->PopClipRect();

        if (mouseKeyShow)
            ImGui::SetTooltip("key @ %.3f s", mouseKeyTime);

        // ---- Scrub head ------------------------------------------------------
        const float hx = t2x(st.Time);
        if (hx >= laneX0 - 1.0f && hx <= laneX1 + 1.0f)
        {
            dl->AddLine(ImVec2(hx, origin.y), ImVec2(hx, lanesY1), colHead, 1.5f);
            dl->AddTriangleFilled(ImVec2(hx - 5.0f, origin.y), ImVec2(hx + 5.0f, origin.y),
                                  ImVec2(hx, origin.y + 7.0f), colHead);
        }

        ImGui::PopID();
        return res;
    }
}
