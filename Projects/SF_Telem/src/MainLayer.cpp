// MainLayer.cpp — SF_Telem screen 1. See MainLayer.h.

#include "MainLayer.h"
#include "TelemHub.h"
#include "StatBox.h"

#include <imgui.h>

namespace Workspace
{
    namespace
    {
        const ImVec4 k_RightColor  = { 1.00f, 0.35f, 0.35f, 1.0f };
        const ImVec4 k_LeftColor   = { 0.35f, 0.70f, 1.00f, 1.0f };
        const ImVec4 k_WeaponColor = { 1.00f, 0.55f, 0.20f, 1.0f };
        ImVec4 DriveColor(int id) { return id == ESC_RIGHT ? k_RightColor : k_LeftColor; }
    }

    void MainLayer::OnAttach()
    {
        m_Camera.SetManualMovementEnabled(false);
    }

    void MainLayer::OnUpdate(float ts)
    {
        m_Camera.OnUpdate(ts);
        // No 2D visual on this screen — render an empty scene to keep the
        // viewport cleanly cleared each frame.
        Cosmic::Renderer2D::BeginScene(m_Camera.GetCamera());
        Cosmic::Renderer2D::EndScene();
    }

    void MainLayer::OnEvent(Cosmic::Event& e) { m_Camera.OnEvent(e); }

    void MainLayer::OnImGuiRender()
    {
        DrawWeaponPanel();
        DrawDrivetrainPanel();
        DrawPlots();
        DrawTelemetry();
    }

    // -------------------------------------------------------------------------
    void MainLayer::DrawWeaponPanel()
    {
        ImGui::Begin("Weapon System");

        ImagePlaceholder("##wpnimg", "[ weapon system — upload texture later ]",
                         ImVec2(-1.0f, 90.0f), k_WeaponColor);

        const int W = ESC_WEAPON;
        if (m_Hub->Present(W))
            ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f }, "LIVE");
        else if (m_Hub->HasData(W))
            ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, "STALE");
        else
            ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "NO SIGNAL — weapon ESC not detected");

        ImGui::Spacing();
        StatBox("##wcur", "Current", m_Hub->Cur(W), "A", m_Hub->MaxCur(W), k_WeaponColor);
        ImGui::SameLine();
        StatBox("##wvolt", "Voltage", m_Hub->Volt(W), "V", m_Hub->MaxVolt(W), k_WeaponColor);
        ImGui::SameLine();
        RpmBox("##wrpm", "Weapon RPM", m_Hub->Rpm(W), m_Hub->PredictedRpm(W), m_Hub->MaxRpm(W), k_WeaponColor);

        ImGui::Spacing();
        if (ImGui::Button("Reset Weapon Stats")) m_Hub->ResetMax(W);

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    void MainLayer::DrawDrivetrainPanel()
    {
        ImGui::Begin("Drivetrain");

        ImagePlaceholder("##dtimg", "[ drivetrain — upload texture later ]",
                         ImVec2(-1.0f, 90.0f), k_LeftColor);

        for (int id = ESC_RIGHT; id <= ESC_LEFT; ++id)
        {
            const ImVec4 col = DriveColor(id);
            ImGui::TextColored(col, "%-6s", IdLabel(id));
            ImGui::SameLine();
            if (m_Hub->Present(id))      ImGui::TextColored({ 0.3f, 1.0f, 0.4f, 1.0f }, "LIVE");
            else if (m_Hub->HasData(id)) ImGui::TextColored({ 1.0f, 0.7f, 0.2f, 1.0f }, "STALE");
            else                         ImGui::TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "no signal");

            char a[8], b[8], c[8], d[8];
            snprintf(a, sizeof(a), "##c%d", id); snprintf(b, sizeof(b), "##v%d", id);
            snprintf(c, sizeof(c), "##r%d", id); snprintf(d, sizeof(d), "##s%d", id);

            StatBox(a, "Current", m_Hub->Cur(id), "A", m_Hub->MaxCur(id), col);
            ImGui::SameLine();
            StatBox(b, "Voltage", m_Hub->Volt(id), "V", m_Hub->MaxVolt(id), col);
            ImGui::SameLine();
            RpmBox(c, "Motor RPM", m_Hub->Rpm(id), m_Hub->PredictedRpm(id), m_Hub->MaxRpm(id), col);
            ImGui::SameLine();
            StatBox(d, "Speed", m_Hub->Speed(id), "mph", m_Hub->MaxSpeed(id), col);
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset Drivetrain Stats"))
        {
            m_Hub->ResetMax(ESC_RIGHT);
            m_Hub->ResetMax(ESC_LEFT);
        }

        ImGui::End();
    }

    // -------------------------------------------------------------------------
    void MainLayer::DrawPlots()
    {
        ImGui::Begin("ESC Plots");
        if (ImGui::BeginTabBar("##escplots"))
        {
            if (ImGui::BeginTabItem("Right"))  { ImGui::BeginChild("##pr"); m_Hub->DrawEscPlots(ESC_RIGHT);  ImGui::EndChild(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Left"))   { ImGui::BeginChild("##pl"); m_Hub->DrawEscPlots(ESC_LEFT);   ImGui::EndChild(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Weapon")) { ImGui::BeginChild("##pw"); m_Hub->DrawEscPlots(ESC_WEAPON); ImGui::EndChild(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // -------------------------------------------------------------------------
    void MainLayer::DrawTelemetry()
    {
        ImGui::Begin("Telemetry (drill-down)");
        m_Hub->Panel().OnImGuiRender();
        ImGui::End();
    }

} // namespace Workspace
