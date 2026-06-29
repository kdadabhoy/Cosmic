// SF_Telem.cpp — root manager + homescreen. See SF_Telem.h for the overview.

#include "SF_Telem.h"
#include "MainLayer.h"
#include "DrivetrainLayer.h"
#include "ReplayLayer.h"

#include "layers/WorkspaceLayer.h"   // dock-port registration
#include "ui/ThemeManager.h"         // accent colour for the homescreen tiles
#include "ui/Fonts.h"
#include "ui/Overlay.h"              // UI::Text

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <string>

namespace Workspace
{
    namespace
    {
        // Motor count input shown as poles OR pole pairs (stored as poles).
        void PolesInput(const char* label, int& poles, bool asPairs)
        {
            ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 2.0f
                + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f + 56.0f);
            if (asPairs)
            {
                int pairs = poles / 2;
                if (ImGui::InputInt(label, &pairs))
                {
                    if (pairs < 1) pairs = 1;
                    poles = pairs * 2;
                }
            }
            else
            {
                ImGui::InputInt(label, &poles);
                if (poles < 2) poles = 2;
            }
        }

        // One homescreen entry.
        struct TileDef
        {
            int         screen;   // SF_Telem::Screen
            const char* icon;
            const char* title;
            const char* blurb;
        };
    }

    SF_Telem::SF_Telem() : Cosmic::Layer("SF_Telem") {}

    // =========================================================================
    void SF_Telem::OnAttach()
    {
        CS_INFO("SF_Telem: Attaching combined telemetry app.");

        Cosmic::FileSystem::SetActiveProject("SF_Telem");
        Cosmic::Log::SetLogDirectory(Cosmic::FileSystem::Resolve("project://logs"));

        // One shared connection feeds every screen.
        m_TelemHub.Init(&m_Link);
        m_Testing.Init(&m_Link);

        m_Main     = std::make_shared<MainLayer>(&m_TelemHub);
        m_Analysis = std::make_shared<DrivetrainLayer>();
        m_Replay   = std::make_shared<ReplayLayer>(&m_TelemHub);
        m_Main->OnAttach();
        m_Analysis->OnAttach();
        m_Replay->OnAttach();

        // Tile art for the homescreen (same hardware photos as the dashboard).
        m_WeaponTex     = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Weapon.PNG"));
        m_DrivetrainTex = Cosmic::Texture2D::Create(Cosmic::FileSystem::Resolve("project://images/SF_Drivetrain.PNG"));

        SetScreen(SCREEN_HOME);
        CS_INFO("SF_Telem: screens ready (Main / Testing / Analysis / Replay).");
    }

    // =========================================================================
    void SF_Telem::OnDetach()
    {
        if (m_Main)     m_Main->OnDetach();
        if (m_Analysis) m_Analysis->OnDetach();
        if (m_Replay)   m_Replay->OnDetach();
        m_Main.reset(); m_Analysis.reset(); m_Replay.reset();

        m_Testing.Shutdown();
        m_TelemHub.Shutdown();
        m_Link.Shutdown();          // root owns the shared connection

        Cosmic::Log::SetLogDirectory("logs");
        CS_INFO("SF_Telem: Detached.");
    }

    // =========================================================================
    void SF_Telem::OnUpdate(float ts)
    {
        // The shared link's port scan / async auto-reconnect runs on every screen
        // so the connection stays alive (and reconnects) regardless of where we are.
        m_Link.OnUpdate(ts);

        switch (m_Screen)
        {
        case SCREEN_MAIN:     m_TelemHub.OnUpdate(ts); if (m_Main) m_Main->OnUpdate(ts); break;
        case SCREEN_REPLAY:   m_TelemHub.OnUpdate(ts); break;   // player-driven; no scene
        case SCREEN_TESTING:  m_Testing.OnUpdate(ts); break;
        case SCREEN_ANALYSIS: if (m_Analysis) m_Analysis->OnUpdate(ts); break;
        case SCREEN_HOME:
        default: break;
        }
    }

    void SF_Telem::OnFixedUpdate(float dt)
    {
        // Record only while on the live Main screen (not during replay/testing/idle).
        if (m_Screen == SCREEN_MAIN) m_TelemHub.RecordFixed(dt);
    }

    // =========================================================================
    void SF_Telem::OnImGuiRender()
    {
        if (m_AppliedDock != DockStateKey())
            ApplyDockLayout();

        if (m_Screen == SCREEN_HOME)
        {
            DrawHomescreen();
            return;
        }

        DrawTopPanel();

        switch (m_Screen)
        {
        case SCREEN_MAIN:     m_TelemHub.DrawSerialPanel(); if (m_Main) m_Main->OnImGuiRender(); break;
        case SCREEN_REPLAY:   if (m_Replay)   m_Replay->OnImGuiRender();   break;
        case SCREEN_TESTING:  m_Testing.DrawScreen();                      break;
        case SCREEN_ANALYSIS: if (m_Analysis) m_Analysis->OnImGuiRender(); break;
        default: break;
        }
    }

    // =========================================================================
    void SF_Telem::OnEvent(Cosmic::Event& e)
    {
        // Application events (resize, etc.) broadcast to all screens so inactive
        // cameras keep a correct projection.
        if (e.IsInCategory(Cosmic::EventCategoryApplication))
        {
            if (m_Main)     m_Main->OnEvent(e);
            if (m_Analysis) m_Analysis->OnEvent(e);
            if (m_Replay)   m_Replay->OnEvent(e);
            return;
        }
        if (e.Handled) return;

        switch (m_Screen)
        {
        case SCREEN_MAIN:     if (m_Main)     m_Main->OnEvent(e);     break;
        case SCREEN_ANALYSIS: if (m_Analysis) m_Analysis->OnEvent(e); break;
        case SCREEN_REPLAY:   if (m_Replay)   m_Replay->OnEvent(e);   break;
        default: break;
        }
    }

    // =========================================================================
    void SF_Telem::SetScreen(Screen s)
    {
        m_Screen = s;

        // The shared telemetry panel is Live on Main (so recording works) and
        // Replay on the Replay screen (so the player drives the dashboard).
        if (s == SCREEN_MAIN)
        {
            m_TelemHub.Panel().SetMode(Cosmic::TelemetryPanel::Mode::Live);
            if (m_Main) m_Main->RequestDashboardFocus();
        }
        else if (s == SCREEN_REPLAY)
        {
            m_TelemHub.Panel().SetMode(Cosmic::TelemetryPanel::Mode::Replay);
            if (m_Replay) m_Replay->RequestDashboardFocus();
        }
        // Dock layout re-applies automatically (DockStateKey changes).
    }

    int SF_Telem::DockStateKey() const
    {
        // Testing re-docks when its sub-mode changes; other screens key on screen.
        if (m_Screen == SCREEN_TESTING) return 100 + m_Testing.ActiveMode();
        return static_cast<int>(m_Screen);
    }

    // =========================================================================
    // Dock layout per screen (re-applied when the screen / testing sub-mode changes).
    // =========================================================================
    void SF_Telem::ApplyDockLayout()
    {
        auto* ws = Cosmic::Application::Get().GetWorkspaceLayer();
        if (!ws) { m_AppliedDock = DockStateKey(); return; }

        ws->ClearDockWindows();

        if (m_Screen == SCREEN_HOME)
        {
            // Homescreen is a single docked window in the central node. Docking (vs a
            // fullscreen overlay) keeps it BELOW the engine's persistent title bar /
            // window chrome, so the chrome stays visible here too. AutoHideTabBar on
            // the dockspace hides the lone tab for a clean menu.
            ws->SetViewportVisible(false);
            ws->DockWindow("Home", Cosmic::DockPort::Center);
            m_AppliedDock = DockStateKey();
            return;
        }

        ws->DockWindow("Project Inspector Top", Cosmic::DockPort::LeftTop);

        switch (m_Screen)
        {
        case SCREEN_MAIN:
            ws->SetEdgeRatios(0.20f, 0.20f, 0.18f, 0.30f);
            ws->SetViewportVisible(false);   // Live Dashboard owns the center
            ws->DockWindow("Serial Link",    Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Live Dashboard", Cosmic::DockPort::Center);
            ws->DockWindow("ESC Plots",      Cosmic::DockPort::RightTop);
            ws->DockWindow("Left Drive",     Cosmic::DockPort::BottomLeft);
            ws->DockWindow("Weapon",         Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Right Drive",    Cosmic::DockPort::BottomRight);
            ws->ShowThemeSelector(true, Cosmic::DockPort::RightBottom, ICON_LC_PALETTE "  Themes");
            if (m_Main) m_Main->RequestDashboardFocus();
            break;

        case SCREEN_REPLAY:
            ws->SetEdgeRatios(0.20f, 0.20f, 0.18f, 0.30f);
            ws->SetViewportVisible(false);
            ws->DockWindow("Replay",         Cosmic::DockPort::LeftBottom);   // transport
            ws->DockWindow("Live Dashboard", Cosmic::DockPort::Center);
            ws->DockWindow("ESC Plots",      Cosmic::DockPort::RightTop);
            ws->DockWindow("Left Drive",     Cosmic::DockPort::BottomLeft);
            ws->DockWindow("Weapon",         Cosmic::DockPort::BottomCenter);
            ws->DockWindow("Right Drive",    Cosmic::DockPort::BottomRight);
            ws->ShowThemeSelector(true, Cosmic::DockPort::RightBottom, ICON_LC_PALETTE "  Themes");
            if (m_Replay) m_Replay->RequestDashboardFocus();
            break;

        case SCREEN_ANALYSIS:
            ws->SetEdgeRatios(0.20f, 0.20f, 0.18f, 0.22f);
            ws->SetViewportVisible(true);    // schematic uses the viewport
            ws->DockWindow("Drivetrain Inputs",    Cosmic::DockPort::LeftMiddle);
            ws->DockWindow("Drivetrain Results",   Cosmic::DockPort::LeftBottom);
            ws->DockWindow("Performance Curves",   Cosmic::DockPort::RightTop);
            ws->DockWindow("Drivetrain KPIs",      Cosmic::DockPort::RightBottom);
            ws->DockWindow("Drivetrain Explorers", Cosmic::DockPort::BottomCenter);
            ws->ShowThemeSelector(true, Cosmic::DockPort::RightMiddle, ICON_LC_PALETTE "  Themes");
            break;

        case SCREEN_TESTING:
            ws->SetViewportVisible(false);
            ws->DockWindow("Serial Link",              Cosmic::DockPort::LeftBottom);
            ws->DockWindow(m_Testing.ActiveWindowName(), Cosmic::DockPort::Center);
            ws->ShowThemeSelector(true, Cosmic::DockPort::RightTop, ICON_LC_PALETTE "  Themes");
            break;

        default: break;
        }

        m_AppliedDock = DockStateKey();
    }

    // =========================================================================
    // Shared top bar: Home + screen tabs, then screen-specific controls.
    // =========================================================================
    void SF_Telem::DrawTopPanel()
    {
        ImGui::Begin("Project Inspector Top");

        ImGui::TextColored({ 0.4f, 1.0f, 0.8f, 1.0f }, "Shear Force Telemetry");
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Navigation ----
        if (ImGui::Button(ICON_LC_HOME "  Home", ImVec2(-1, 0))) SetScreen(SCREEN_HOME);
        ImGui::Spacing();

        struct NavDef { Screen s; const char* label; };
        static const NavDef kNav[] = {
            { SCREEN_MAIN,     ICON_LC_GAUGE   "  Main"     },
            { SCREEN_TESTING,  ICON_LC_FLASK_CONICAL "  Testing" },
            { SCREEN_ANALYSIS, ICON_LC_CHART_LINE "  Analysis" },
            { SCREEN_REPLAY,   ICON_LC_HISTORY "  Replay"   },
        };
        const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        for (int i = 0; i < 4; ++i)
        {
            if (i % 2 != 0) ImGui::SameLine();
            const bool active = (m_Screen == kNav[i].s);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.45f, 1.0f));
            if (ImGui::Button(kNav[i].label, ImVec2(bw, 0))) SetScreen(kNav[i].s);
            if (active) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Screen-specific controls ----
        if (m_Screen == SCREEN_TESTING)
        {
            m_Testing.DrawInspector();
        }
        else if (m_Screen == SCREEN_MAIN || m_Screen == SCREEN_REPLAY)
        {
            if (m_Screen == SCREEN_MAIN)
            {
                m_TelemHub.DrawRecordingControls();
                ImGui::Spacing(); ImGui::Separator();

                ImGui::SeparatorText("Live Stats");
                ImGui::SetNextItemWidth(110);
                ImGui::DragFloat("Avg window (s)", &m_TelemHub.StatsWindowSec(), 0.1f, 0.0f, 120.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Auto-reset max/avg every N seconds (0 = accumulate until manual reset).");
                if (ImGui::Button("Reset All Stats")) m_TelemHub.ResetStats();
                ImGui::Spacing(); ImGui::Separator();
            }

            if (ImGui::CollapsingHeader("Decode Constants"))
            {
                DriveConfig&  d = m_TelemHub.DriveCfg();
                WeaponConfig& w = m_TelemHub.WeaponCfg();

                const char* countLabel = m_PolesAsPairs ? "Pole pairs" : "Poles";
                ImGui::Checkbox("Enter as pole pairs", &m_PolesAsPairs);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Pole pairs = poles / 2. Switches the inputs below between the two;\n"
                                      "the decode math is identical either way.");

                ImGui::SeparatorText("Drive (Right + Left)");
                PolesInput((std::string(countLabel) + "##d").c_str(), d.Poles, m_PolesAsPairs);
                ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##d",     &d.GearRatio,       0, 0, "%.2f");
                ImGui::SetNextItemWidth(110); ImGui::InputFloat("Wheel dia (in)##d", &d.WheelDiameterIn, 0, 0, "%.2f");
                ImGui::SetNextItemWidth(110); ImGui::InputFloat("Slip factor##d",    &d.SlipFactor,      0, 0, "%.3f");
                ImGui::SetNextItemWidth(110); ImGui::InputFloat("Motor Kv##d",       &d.MotorKv,         0, 0, "%.0f");

                ImGui::SeparatorText("Weapon");
                PolesInput((std::string(countLabel) + "##w").c_str(), w.Poles, m_PolesAsPairs);
                bool wchg = false;
                ImGui::SetNextItemWidth(110); wchg |= ImGui::InputFloat("Gear ratio##w",      &w.GearRatio,        0, 0, "%.2f");
                ImGui::SetNextItemWidth(110); wchg |= ImGui::InputFloat("Weapon dia (in)##w", &w.WeaponDiameterIn, 0, 0, "%.2f");
                if (wchg) m_TelemHub.MarkModelDirty();
            }
            ImGui::Spacing(); ImGui::Separator();
        }

        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("FPS: %.1f (%.2f ms)", fps, 1000.0f / fps);

        ImGui::End();
    }

    // =========================================================================
    // Homescreen — a Minecraft-style 2x2 tile menu over the (empty) work area.
    // =========================================================================
    void SF_Telem::DrawHomescreen()
    {
        // Docked into the central node (see ApplyDockLayout) so it sits under the
        // engine's persistent title bar / window chrome. The lone tab is auto-hidden
        // by the dockspace's AutoHideTabBar flag.
        ImGui::Begin("Home", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 accent  = Cosmic::ThemeManager::Accent();
        const ImU32  accentU = ImGui::ColorConvertFloat4ToU32(accent);
        const ImU32  textU   = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32  subU    = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        ImFont* iconFont  = ImGui::GetFont();                                 // default font carries the Lucide glyphs
        ImFont* titleFont = Cosmic::UI::Fonts::Get("Roboto-Bold", 22.0f);
        ImFont* headFont  = Cosmic::UI::Fonts::Get("Roboto-Bold", 34.0f);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        // ---- Heading ----
        Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 28.0f),
                         textU, "Shear Force Telemetry", headFont, 34.0f, Cosmic::UI::Align::TopCenter);
        Cosmic::UI::Text(dl, ImVec2(origin.x + avail.x * 0.5f, origin.y + 72.0f),
                         subU, "Choose a workspace", titleFont, 16.0f, Cosmic::UI::Align::TopCenter);

        // ---- 2x2 tile grid, centered ----
        static const TileDef kTiles[] = {
            { SCREEN_MAIN,     ICON_LC_GAUGE,         "Main Telemetry", "Live dashboard, plots & recording" },
            { SCREEN_TESTING,  ICON_LC_FLASK_CONICAL, "Testing",        "Drive / weapon / dual / sniffer" },
            { SCREEN_ANALYSIS, ICON_LC_CHART_LINE,    "Analysis",       "Drivetrain spin-up calculator" },
            { SCREEN_REPLAY,   ICON_LC_HISTORY,       "Replay",         "Scrub a recording on the diagram" },
        };

        const float gap   = 24.0f;
        const float gridW = std::min(avail.x * 0.82f, 840.0f);
        const float tileW = (gridW - gap) * 0.5f;
        const float tileH = 168.0f;
        const float gridH = tileH * 2.0f + gap;
        const float offX  = origin.x + (avail.x - gridW) * 0.5f;
        const float offY  = origin.y + std::max(120.0f, (avail.y - gridH) * 0.5f);

        for (int i = 0; i < 4; ++i)
        {
            const int row = i / 2, coln = i % 2;
            const ImVec2 p0(offX + coln * (tileW + gap), offY + row * (tileH + gap));
            const ImVec2 p1(p0.x + tileW, p0.y + tileH);

            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(i);
            const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(tileW, tileH));
            const bool hov     = ImGui::IsItemHovered();
            ImGui::PopID();

            const ImU32 base   = ImGui::GetColorU32(hov ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
            const ImU32 border = hov ? accentU : ImGui::GetColorU32(ImGuiCol_Border);
            dl->AddRectFilled(p0, p1, base, 12.0f);
            dl->AddRect(p0, p1, border, 12.0f, 0, hov ? 2.5f : 1.0f);

            const float cx = (p0.x + p1.x) * 0.5f;
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.30f), accentU, kTiles[i].icon, iconFont, 44.0f, Cosmic::UI::Align::Center);
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.60f), textU,   kTiles[i].title, titleFont, 22.0f, Cosmic::UI::Align::Center);
            Cosmic::UI::Text(dl, ImVec2(cx, p0.y + tileH * 0.78f), subU,    kTiles[i].blurb, nullptr,   14.0f, Cosmic::UI::Align::Center);

            if (clicked) SetScreen(static_cast<Screen>(kTiles[i].screen));
        }

        ImGui::End();
    }

} // namespace Workspace

// =============================================================================
// Required C-linkage DLL entry points — do not rename or remove
// =============================================================================
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }

    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        return new Workspace::SF_Telem();
    }
}
