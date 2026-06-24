// TestLayers.cpp — see TestLayers.h.

#include "TestLayers.h"
#include "TestHub.h"

#include <imgui.h>

#include <cstdio>
#include <string>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_RColor = { 1.00f, 0.35f, 0.35f, 1.0f };
        const ImVec4 k_LColor = { 0.35f, 0.70f, 1.00f, 1.0f };
        const ImVec4 k_WColor = { 1.00f, 0.55f, 0.20f, 1.0f };

        const ImVec4 k_BanGreen = { 0.18f, 0.62f, 0.35f, 1.0f };
        const ImVec4 k_BanAmber = { 0.72f, 0.54f, 0.16f, 1.0f };
        const ImVec4 k_BanRed   = { 0.62f, 0.22f, 0.22f, 1.0f };

        enum class Metric { Rpm, Volt, Cur, Speed, Tip, Temp };

        struct Readout
        {
            float       nx, ny;
            const char* label;
            int         id;
            Metric      m;
        };

        // Box positions are normalized over the fitted photo — tweak freely.
        const Readout k_DriveR[] = {
            { 0.85f, 0.22f, "R RPM",     ESC_RIGHT, Metric::Rpm   },
            { 0.85f, 0.50f, "R SPEED",   ESC_RIGHT, Metric::Speed },
            { 0.85f, 0.78f, "R CURRENT", ESC_RIGHT, Metric::Cur   },
            { 0.55f, 0.50f, "VOLTAGE",   ESC_RIGHT, Metric::Volt  },
        };

        const Readout k_Dual[] = {
            { 0.15f, 0.22f, "L RPM",     ESC_LEFT,  Metric::Rpm   },
            { 0.15f, 0.50f, "L SPEED",   ESC_LEFT,  Metric::Speed },
            { 0.15f, 0.78f, "L CURRENT", ESC_LEFT,  Metric::Cur   },
            { 0.85f, 0.22f, "R RPM",     ESC_RIGHT, Metric::Rpm   },
            { 0.85f, 0.50f, "R SPEED",   ESC_RIGHT, Metric::Speed },
            { 0.85f, 0.78f, "R CURRENT", ESC_RIGHT, Metric::Cur   },
            { 0.50f, 0.50f, "VOLTAGE",   ESC_RIGHT, Metric::Volt  },
        };

        const Readout k_Weapon[] = {
            { 0.50f, 0.16f, "WEAPON RPM", ESC_WEAPON, Metric::Rpm  },
            { 0.50f, 0.84f, "TIP SPEED",  ESC_WEAPON, Metric::Tip  },
            { 0.15f, 0.35f, "VOLTAGE",    ESC_WEAPON, Metric::Volt },
            { 0.15f, 0.68f, "CURRENT",    ESC_WEAPON, Metric::Cur  },
            { 0.85f, 0.35f, "TEMP",       ESC_WEAPON, Metric::Temp },
        };

        void FormatMetric(const TestHub* hub, const Readout& r, char* buf, size_t n)
        {
            if (!hub->HasData(r.id)) { snprintf(buf, n, "--"); return; }
            float v = 0.0f; const char* unit = "";
            switch (r.m)
            {
            case Metric::Rpm:   v = hub->Rpm(r.id);   break;
            case Metric::Volt:  v = hub->Volt(r.id);  unit = " V";   break;
            case Metric::Cur:   v = hub->Cur(r.id);   unit = " A";   break;
            case Metric::Speed: v = hub->Speed(r.id); unit = " mph"; break;
            case Metric::Tip:   v = hub->Tip();       unit = " mph"; break;
            case Metric::Temp:  v = (r.id == ESC_WEAPON ? hub->GetWeapon().tempC
                                                        : hub->GetDrive(r.id).tempC); unit = " C"; break;
            }
            const int dec = (r.m == Metric::Rpm || r.m == Metric::Temp) ? 0 : 1;
            snprintf(buf, n, "%.*f%s", dec, v, unit);
        }

        ImU32 StatusBorder(const TestHub* hub, int id)
        {
            if (hub->Present(id)) return IM_COL32(45, 200, 95, 255);
            if (hub->HasData(id)) return IM_COL32(230, 170, 45, 255);
            return IM_COL32(200, 70, 70, 255);
        }

        // Full-width colored banner with centered bold text.
        void Banner(const char* text, ImVec4 col)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  w = ImGui::GetContentRegionAvail().x;
            const float  h = 46.0f;
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::ColorConvertFloat4ToU32(col), 6.0f);
            ImFont* bold = Cosmic::UI::Fonts::Get("Roboto-Bold", 24.0f);
            Cosmic::UI::Text(dl, ImVec2(p.x + w * 0.5f, p.y + h * 0.5f),
                             IM_COL32(245, 247, 250, 255), text, bold, 24.0f, Cosmic::UI::Align::Center);
            ImGui::Dummy(ImVec2(w, h));
            ImGui::Spacing();
        }

        void EscBanner(const TestHub* hub, int id, const char* okMsg)
        {
            if (hub->Present(id))      Banner(okMsg, k_BanGreen);
            else if (hub->HasData(id)) Banner("STALE - telemetry stopped", k_BanAmber);
            else                       Banner("WAITING - no telemetry yet", k_BanRed);
        }

        void Diagnostics(const TestHub* hub, int id, const char* sideLabel)
        {
            ImGui::Text("%s  good: %llu   rate: %.1f /s   bad (CRC/parse): %llu",
                        sideLabel,
                        (unsigned long long)hub->Good(id), hub->Fps(id),
                        (unsigned long long)hub->BadFrames());
            const std::string& lf = hub->LastFrame(id);
            ImGui::TextDisabled("last frame: %s", lf.empty() ? "(none)" : lf.c_str());
        }

        // Photo with overlay readout boxes (reuses the dashboard look).
        void DrawImageOverlay(const Cosmic::Ref<Cosmic::Texture2D>& tex,
                              const Readout* tbl, int n, const TestHub* hub, float height)
        {
            Cosmic::UI::Rect rect = Cosmic::UI::ImageFitted(tex, ImVec2(0.0f, height));
            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImFont* labelFont = Cosmic::UI::Fonts::Get("Roboto-Medium", 13.0f);
            ImFont* boldFont  = Cosmic::UI::Fonts::Get("Roboto-Bold", 26.0f);
            const bool haveBold = boldFont && boldFont != Cosmic::UI::Fonts::Default();

            Cosmic::UI::ReadoutStyle style;
            style.LabelFont = labelFont;
            style.ValueFont = haveBold ? boldFont : nullptr;
            style.FauxBold  = !haveBold;
            style.LabelSize = 13.0f;
            style.ValueSize = 26.0f;
            style.MinSize   = ImVec2(96.0f, 0.0f);

            char buf[48];
            for (int i = 0; i < n; ++i)
            {
                const Readout& b = tbl[i];
                FormatMetric(hub, b, buf, sizeof(buf));
                style.Border = StatusBorder(hub, b.id);
                Cosmic::UI::ReadoutBox(dl, rect.At(b.nx, b.ny), b.label, buf, style);
            }
        }
    }

    // =========================================================================
    // 1. Single drive ESC test
    // =========================================================================
    void DriveSingleLayer::OnAttach()
    {
        m_Tex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Drivetrain.PNG"));
    }

    void DriveSingleLayer::OnImGuiRender()
    {
        ImGui::Begin("Drive ESC Test");

        EscBanner(m_Hub, ESC_RIGHT, "PASS - drive ESC telemetry OK");
        const float h = ImGui::GetContentRegionAvail().y;
        DrawImageOverlay(m_Tex, k_DriveR, IM_ARRAYSIZE(k_DriveR), m_Hub, h * 0.60f);

        ImGui::Spacing();
        Diagnostics(m_Hub, ESC_RIGHT, "RIGHT");

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextWrapped("Flash firmware/sf_test_single_drive. Wire ONE drive ESC telemetry to "
                           "GPIO16 (UART1, board silk RX2) + common GND. Power the ESC so it streams; "
                           "the boxes should populate and the banner turn green.");
        ImGui::End();
    }

    // =========================================================================
    // 2. Single weapon ESC test
    // =========================================================================
    void WeaponSingleLayer::OnAttach()
    {
        m_Tex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Weapon.PNG"));
    }

    void WeaponSingleLayer::OnImGuiRender()
    {
        ImGui::Begin("Weapon ESC Test");

        EscBanner(m_Hub, ESC_WEAPON, "PASS - weapon ESC telemetry OK");
        const float h = ImGui::GetContentRegionAvail().y;
        DrawImageOverlay(m_Tex, k_Weapon, IM_ARRAYSIZE(k_Weapon), m_Hub, h * 0.60f);

        ImGui::Spacing();
        Diagnostics(m_Hub, ESC_WEAPON, "WEAPON");

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextWrapped("Flash firmware/sf_test_single_weapon. Wire the weapon ESC telemetry to "
                           "GPIO13 (UART0 RX, remapped; USB TX kept) + common GND. Power the ESC so it "
                           "streams; the boxes should populate and the banner turn green.");
        ImGui::End();
    }

    // =========================================================================
    // 3. Dual drive ESC test
    // =========================================================================
    void DualDriveLayer::OnAttach()
    {
        m_Tex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Drivetrain.PNG"));
    }

    void DualDriveLayer::OnImGuiRender()
    {
        ImGui::Begin("Dual Drive Test");

        const bool rOk = m_Hub->Present(ESC_RIGHT);
        const bool lOk = m_Hub->Present(ESC_LEFT);
        if (rOk && lOk)      Banner("PASS - both drive ESCs OK", k_BanGreen);
        else if (rOk || lOk) Banner(rOk ? "PARTIAL - only RIGHT OK" : "PARTIAL - only LEFT OK", k_BanAmber);
        else                 Banner("WAITING - no drive telemetry yet", k_BanRed);

        const float h = ImGui::GetContentRegionAvail().y;
        DrawImageOverlay(m_Tex, k_Dual, IM_ARRAYSIZE(k_Dual), m_Hub, h * 0.58f);

        ImGui::Spacing();
        Diagnostics(m_Hub, ESC_RIGHT, "RIGHT");
        Diagnostics(m_Hub, ESC_LEFT,  "LEFT ");

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextWrapped("Flash firmware/sf_test_dual_drive. Wire RIGHT drive ESC telem to GPIO16 "
                           "(UART1) and LEFT to GPIO17 (UART2), with a common GND. Both banners/sides "
                           "should report data.");
        ImGui::End();
    }

    // =========================================================================
    // 4. Telemetry sniffer (any bytes at all?)
    // =========================================================================
    void SnifferLayer::OnImGuiRender()
    {
        ImGui::Begin("Telem Sniffer");

        const bool linkAlive = m_Hub->BytesPerSec() > 0.0f || m_Hub->TotalBytes() > 0;
        Banner(linkAlive ? "ESP32 LINK ALIVE" : "NO LINK DATA - connect the serial port",
               linkAlive ? k_BanGreen : k_BanRed);
        ImGui::Text("Serial link: %.0f B/s     total %llu bytes",
                    m_Hub->BytesPerSec(), (unsigned long long)m_Hub->TotalBytes());

        ImGui::Spacing();
        ImGui::SeparatorText("Per-wire telemetry activity (valid or not)");

        const int         ids[3]   = { ESC_RIGHT, ESC_LEFT, ESC_WEAPON };
        const char*       names[3] = { "RIGHT  GPIO16", "LEFT  GPIO17", "WEAPON  GPIO13" };
        const ImVec4      cols[3]  = { k_RColor, k_LColor, k_WColor };
        const float cardW = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;

        for (int k = 0; k < 3; ++k)
        {
            if (k > 0) ImGui::SameLine();
            char cid[16]; snprintf(cid, sizeof(cid), "##sniff%d", k);
            ImGui::BeginChild(cid, ImVec2(cardW, 150.0f), true);

            ImGui::TextColored(cols[k], "%s", names[k]);
            ImGui::Spacing();

            const TestHub::Sniff& s = m_Hub->GetSniff(ids[k]);
            const bool active = m_Hub->SniffActive(ids[k]);
            ImGui::SetWindowFontScale(1.6f);
            ImGui::TextColored(active ? ImVec4(0.30f, 1.0f, 0.45f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                               active ? "DETECTED" : "SILENT");
            ImGui::SetWindowFontScale(1.0f);

            ImGui::Text("%.0f B/s", s.bps);
            ImGui::Text("total %llu", (unsigned long long)s.total);
            ImGui::TextDisabled("%s", s.lastHex.empty() ? "--" : s.lastHex.c_str());

            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Raw bytes off the link (hex)");

        const std::string& raw = m_Hub->RawTail();
        std::string hex;
        hex.reserve(raw.size() * 3);
        const size_t start = raw.size() > 256 ? raw.size() - 256 : 0;
        char tmp[4];
        for (size_t i = start; i < raw.size(); ++i)
        {
            snprintf(tmp, sizeof(tmp), "%02X ", (unsigned char)raw[i]);
            hex += tmp;
        }
        ImGui::BeginChild("##rawhex", ImVec2(0, 120.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextWrapped("%s", hex.empty() ? "(no bytes yet)" : hex.c_str());
        ImGui::EndChild();

        ImGui::Spacing(); ImGui::Separator();
        ImGui::TextWrapped("Flash firmware/sf_test_sniffer. It watches the raw telemetry wires "
                           "(GPIO16 / GPIO17 / GPIO13) and reports byte activity regardless of whether the "
                           "data is valid KISS — use it to confirm an ESC is emitting ANYTHING at all.");
        ImGui::End();
    }

} // namespace Workspace
