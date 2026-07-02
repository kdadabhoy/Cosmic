// DashboardView.cpp — see DashboardView.h.

#include "DashboardView.h"
#include "TelemHub.h"
#include "StatBox.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_RightColor  = { 1.00f, 0.35f, 0.35f, 1.0f };
        const ImVec4 k_LeftColor   = { 0.35f, 0.70f, 1.00f, 1.0f };
        const ImVec4 k_WeaponColor = { 1.00f, 0.55f, 0.20f, 1.0f };
        ImVec4 DriveColor(int id) { return id == ESC_RIGHT ? k_RightColor : k_LeftColor; }

        // =====================================================================
        // Dashboard readouts — white boxes overlaid on the hardware photos.
        //
        // Positions are normalized (0..1) over the fitted image, so tweaking a
        // box is just editing nx,ny here — no other code changes needed.
        // =====================================================================
        enum class Metric { Rpm, Volt, Cur, Speed, Tip, Temp, Power };

        struct Readout
        {
            float       nx, ny;   // position over the image (0..1) — tune freely
            const char* label;
            int         id;       // ESC_RIGHT / ESC_LEFT / ESC_WEAPON
            Metric      metric;
        };

        const Readout k_WeaponReadouts[] = {
            { 0.50f, 0.16f, "WEAPON RPM", ESC_WEAPON, Metric::Rpm   },
            { 0.50f, 0.84f, "TIP SPEED",  ESC_WEAPON, Metric::Tip   },
            { 0.15f, 0.32f, "VOLTAGE",    ESC_WEAPON, Metric::Volt  },
            { 0.15f, 0.68f, "CURRENT",    ESC_WEAPON, Metric::Cur   },
            { 0.85f, 0.32f, "TEMP",       ESC_WEAPON, Metric::Temp  },
            { 0.85f, 0.68f, "POWER",      ESC_WEAPON, Metric::Power },
        };

        const Readout k_DriveReadouts[] = {
            { 0.15f, 0.22f, "L RPM",     ESC_LEFT,  Metric::Rpm   },
            { 0.15f, 0.50f, "L SPEED",   ESC_LEFT,  Metric::Speed },
            { 0.15f, 0.78f, "L CURRENT", ESC_LEFT,  Metric::Cur   },
            { 0.85f, 0.22f, "R RPM",     ESC_RIGHT, Metric::Rpm   },
            { 0.85f, 0.50f, "R SPEED",   ESC_RIGHT, Metric::Speed },
            { 0.85f, 0.78f, "R CURRENT", ESC_RIGHT, Metric::Cur   },
            { 0.50f, 0.50f, "VOLTAGE",   ESC_RIGHT, Metric::Volt  },
        };

        float MetricValue(const TelemHub* hub, int id, Metric m)
        {
            switch (m)
            {
            case Metric::Rpm:   return hub->Rpm(id);
            case Metric::Volt:  return hub->Volt(id);
            case Metric::Cur:   return hub->Cur(id);
            case Metric::Speed: return hub->Speed(id);
            case Metric::Tip:   return hub->Tip();
            case Metric::Temp:  return id == ESC_WEAPON ? hub->GetWeapon().tempC : hub->GetDrive(id).tempC;
            case Metric::Power: return id == ESC_WEAPON ? hub->GetWeapon().powerW : hub->GetDrive(id).powerW;
            }
            return 0.0f;
        }

        const char* MetricUnit(Metric m)
        {
            switch (m)
            {
            case Metric::Volt:  return " V";
            case Metric::Cur:   return " A";
            case Metric::Speed: return " mph";
            case Metric::Tip:   return " mph";
            case Metric::Temp:  return " C";
            case Metric::Power: return " W";
            case Metric::Rpm:   return "";
            }
            return "";
        }

        void FormatReadout(const TelemHub* hub, const Readout& r, char* buf, size_t n)
        {
            if (!hub->HasData(r.id)) { snprintf(buf, n, "--"); return; }
            const float v   = MetricValue(hub, r.id, r.metric);
            const int   dec = (r.metric == Metric::Rpm || r.metric == Metric::Temp
                            || r.metric == Metric::Power) ? 0 : 1;
            snprintf(buf, n, "%.*f%s", dec, v, MetricUnit(r.metric));
        }

        // Box border reflects link health: live (green) / stale (amber) / none (red).
        ImU32 StatusBorder(const TelemHub* hub, int id)
        {
            if (hub->Present(id)) return IM_COL32(45, 200, 95, 255);
            if (hub->HasData(id)) return IM_COL32(230, 170, 45, 255);
            return IM_COL32(200, 70, 70, 255);
        }

        // =====================================================================
        // Responsive stat-box grid. One spec per box; the grid auto-fits as many
        // columns as the panel width allows (min 2 by default) and stretches the
        // boxes to fill the row.
        // =====================================================================
        struct BoxSpec
        {
            const char* id;
            const char* label;
            bool        isRpm;
            float       value;
            const char* unit;
            float       avg;
            float       maxValue;
            float       predicted;
            ImVec4      accent;
        };

        void DrawStatGrid(const BoxSpec* boxes, int count,
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
                if (b.isRpm)
                    RpmBox(b.id, b.label, b.value, b.predicted, b.avg, b.maxValue, b.accent, size);
                else
                    StatBox(b.id, b.label, b.value, b.unit, b.avg, b.maxValue, b.accent, size);
            }
        }

        // Compact link-status line ("LIVE" / "STALE" / "no signal").
        void StatusLine(const TelemHub* hub, int id)
        {
            if (hub->Present(id))      ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f }, "LIVE");
            else if (hub->HasData(id)) ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, "STALE");
            else                       ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "no signal");
        }
    }

    // -------------------------------------------------------------------------
    void DashboardView::DrawDashboard(TelemHub* hub,
                                      const Cosmic::Ref<Cosmic::Texture2D>& weaponTex,
                                      const Cosmic::Ref<Cosmic::Texture2D>& drivetrainTex,
                                      int* focusFrames)
    {
        // Select the dashboard tab in the center node for a few frames after a
        // (re)dock, so it shows instead of the empty Viewport it's tabbed with.
        if (focusFrames && *focusFrames > 0) { ImGui::SetNextWindowFocus(); --(*focusFrames); }

        ImGui::Begin("Live Dashboard");

        ImFont* labelFont = Cosmic::UI::Fonts::Get("Roboto-Medium", 13.0f);
        ImFont* boldFont  = Cosmic::UI::Fonts::Get("Roboto-Bold", 28.0f);
        const bool haveBold = boldFont && boldFont != Cosmic::UI::Fonts::Default();

        Cosmic::UI::ReadoutStyle style;
        style.LabelFont = labelFont;
        style.ValueFont = haveBold ? boldFont : nullptr;
        style.FauxBold  = !haveBold;
        style.LabelSize = 13.0f;
        style.ValueSize = 28.0f;
        style.MinSize   = ImVec2(104.0f, 0.0f);
        style.Anchor    = Cosmic::UI::Align::Center;

        const float halfH = ImGui::GetContentRegionAvail().y * 0.5f;

        auto drawBlock = [&](const Cosmic::Ref<Cosmic::Texture2D>& tex,
                             const Readout* table, size_t count, float height)
        {
            Cosmic::UI::Rect rect = Cosmic::UI::ImageFitted(tex, ImVec2(0.0f, height));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            char buf[48];
            for (size_t i = 0; i < count; ++i)
            {
                const Readout& r = table[i];
                FormatReadout(hub, r, buf, sizeof(buf));
                style.Border = StatusBorder(hub, r.id);
                Cosmic::UI::ReadoutBox(dl, rect.At(r.nx, r.ny), r.label, buf, style);
            }
        };

        drawBlock(weaponTex,     k_WeaponReadouts, IM_ARRAYSIZE(k_WeaponReadouts), halfH);
        drawBlock(drivetrainTex, k_DriveReadouts,  IM_ARRAYSIZE(k_DriveReadouts),  0.0f);

        ImGui::End();
    }

    // ---- Readout panel BODIES (no Begin/End) so they can stack in one window ----
    namespace
    {
        void WeaponReadoutsBody(TelemHub* hub)
        {
            const int W = ESC_WEAPON;
            StatusLine(hub, W);
            ImGui::Spacing();

            const BoxSpec boxes[] = {
                { "##wcur",  "Current",    false, hub->Cur(W),  "A", hub->AvgCur(W),  hub->MaxCur(W),  0.0f,                  k_WeaponColor },
                { "##wvolt", "Voltage",    false, hub->Volt(W), "V", hub->AvgVolt(W), hub->MaxVolt(W), 0.0f,                  k_WeaponColor },
                { "##wrpm",  "Weapon RPM", true,  hub->Rpm(W),  "",  hub->AvgRpm(W),  hub->MaxRpm(W),  hub->PredictedRpm(W),  k_WeaponColor },
            };
            DrawStatGrid(boxes, IM_ARRAYSIZE(boxes));

            ImGui::Spacing();
            if (ImGui::Button("Reset Stats##w")) hub->ResetMax(W);
        }

        void DriveReadoutsBody(TelemHub* hub, int id)
        {
            const ImVec4 col = DriveColor(id);
            StatusLine(hub, id);
            ImGui::Spacing();

            char a[8], b[8], c[8], d[8];
            snprintf(a, sizeof(a), "##c%d", id); snprintf(b, sizeof(b), "##v%d", id);
            snprintf(c, sizeof(c), "##r%d", id); snprintf(d, sizeof(d), "##s%d", id);

            const BoxSpec boxes[] = {
                { a, "Current",   false, hub->Cur(id),   "A",   hub->AvgCur(id),   hub->MaxCur(id),   0.0f,                  col },
                { b, "Voltage",   false, hub->Volt(id),  "V",   hub->AvgVolt(id),  hub->MaxVolt(id),  0.0f,                  col },
                { c, "Motor RPM", true,  hub->Rpm(id),   "",    hub->AvgRpm(id),   hub->MaxRpm(id),   hub->PredictedRpm(id), col },
                { d, "Speed",     false, hub->Speed(id), "mph", hub->AvgSpeed(id), hub->MaxSpeed(id), 0.0f,                  col },
            };
            DrawStatGrid(boxes, IM_ARRAYSIZE(boxes));

            ImGui::Spacing();
            char rs[16]; snprintf(rs, sizeof(rs), "Reset Stats##%d", id);
            if (ImGui::Button(rs)) hub->ResetMax(id);
        }
    }

    // -------------------------------------------------------------------------
    void DashboardView::DrawWeaponReadouts(TelemHub* hub)
    {
        ImGui::Begin("Weapon");
        WeaponReadoutsBody(hub);
        ImGui::End();
    }

    void DashboardView::DrawDriveReadouts(TelemHub* hub, int id, const char* title)
    {
        ImGui::Begin(title);
        DriveReadoutsBody(hub, id);
        ImGui::End();
    }

    // Combined min/max stats panel (Replay left column): Left / Weapon / Right.
    void DashboardView::DrawStatsPanel(TelemHub* hub)
    {
        ImGui::Begin("Stats");
        ImGui::SeparatorText("Left Drive");
        DriveReadoutsBody(hub, ESC_LEFT);
        ImGui::Spacing(); ImGui::SeparatorText("Weapon");
        WeaponReadoutsBody(hub);
        ImGui::Spacing(); ImGui::SeparatorText("Right Drive");
        DriveReadoutsBody(hub, ESC_RIGHT);
        ImGui::End();
    }

    // -------------------------------------------------------------------------
    void DashboardView::DrawPlots(TelemHub* hub)
    {
        ImGui::Begin("ESC Plots");
        hub->DrawPlotToolbar();
        if (ImGui::BeginTabBar("##escplots"))
        {
            if (ImGui::BeginTabItem("Right"))  { ImGui::BeginChild("##pr"); hub->DrawEscChannels(ESC_RIGHT);  ImGui::EndChild(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Left"))   { ImGui::BeginChild("##pl"); hub->DrawEscChannels(ESC_LEFT);   ImGui::EndChild(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Weapon")) { ImGui::BeginChild("##pw"); hub->DrawEscChannels(ESC_WEAPON); ImGui::EndChild(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // Replay center: Right / Left / Weapon as three equal side-by-side columns.
    void DashboardView::DrawPlotsTriple(TelemHub* hub)
    {
        ImGui::Begin("ESC Plots");
        hub->DrawPlotToolbar();

        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float colW    = (ImGui::GetContentRegionAvail().x - 2.0f * spacing) / 3.0f;

        const int   ids[3]    = { ESC_RIGHT, ESC_LEFT, ESC_WEAPON };
        const char* titles[3] = { "Right", "Left", "Weapon" };
        for (int k = 0; k < 3; ++k)
        {
            if (k > 0) ImGui::SameLine();
            ImGui::BeginChild(titles[k], ImVec2(colW, 0.0f), true);
            const ImVec4 tcol = (ids[k] == ESC_WEAPON) ? k_WeaponColor : DriveColor(ids[k]);
            ImGui::TextColored(tcol, "%s", titles[k]);
            ImGui::Separator();
            hub->DrawEscChannels(ids[k]);
            ImGui::EndChild();
        }
        ImGui::End();
    }

} // namespace Workspace
