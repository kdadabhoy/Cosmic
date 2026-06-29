// TestingManager.cpp — see TestingManager.h. (Ported from the SF_TelemTest root.)

#include "TestingManager.h"
#include "TestLayers.h"

#include <imgui.h>

#include <string>

namespace Workspace
{
    namespace
    {
        // Motor count input that can show/edit the value as poles OR pole pairs.
        // The config always stores POLES (poles = 2 x pole pairs).
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
    }

    void TestingManager::Init(Cosmic::SerialLink* link)
    {
        m_Hub.Init(link);

        m_Layers.push_back(std::make_shared<DriveSingleLayer>(&m_Hub));   // MODE_DRIVE
        m_Layers.push_back(std::make_shared<WeaponSingleLayer>(&m_Hub));  // MODE_WEAPON
        m_Layers.push_back(std::make_shared<DualDriveLayer>(&m_Hub));     // MODE_DUAL
        m_Layers.push_back(std::make_shared<SnifferLayer>(&m_Hub));       // MODE_SNIFF

        for (auto& l : m_Layers) l->OnAttach();
    }

    void TestingManager::Shutdown()
    {
        for (auto& l : m_Layers) l->OnDetach();
        m_Layers.clear();
        m_Hub.Shutdown();
    }

    void TestingManager::OnUpdate(float ts)
    {
        m_Hub.OnUpdate(ts);   // serial pump + rate windows
    }

    // -------------------------------------------------------------------------
    // Test-specific controls for the shared top panel.
    // -------------------------------------------------------------------------
    void TestingManager::DrawInspector()
    {
        // ---- Test selector (2 per row so it fits the narrow column) ----
        static const char* kModeIcons[MODE_COUNT] =
            { ICON_LC_CAR, ICON_LC_SWORDS, ICON_LC_GIT_FORK, ICON_LC_RADAR };
        ImGui::TextDisabled("Test");
        const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        for (int i = 0; i < MODE_COUNT; ++i)
        {
            if (i % 2 != 0) ImGui::SameLine();
            const bool active = (m_ActiveMode == i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.45f, 1.0f));
            const std::string lbl = std::string(kModeIcons[i]) + "  " + m_ModeNames[i];
            if (ImGui::Button(lbl.c_str(), ImVec2(bw, 0))) m_ActiveMode = i;
            if (active) ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Reset Counts", ImVec2(-1, 0))) m_Hub.ResetCounts();

        ImGui::Spacing();
        ImGui::Separator();

        // ---- Decode constants (so RPM/speed sanity-check correctly) ----
        if (ImGui::CollapsingHeader("Decode Constants"))
        {
            DriveConfig&  d = m_Hub.DriveCfg();
            WeaponConfig& w = m_Hub.WeaponCfg();

            const char* countLabel = m_PolesAsPairs ? "Pole pairs" : "Poles";
            ImGui::Checkbox("Enter as pole pairs", &m_PolesAsPairs);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Pole pairs = poles / 2. Switches the inputs below between the two;\n"
                                  "the decode math is identical either way.");

            ImGui::SeparatorText("Drive (Right + Left)");
            PolesInput((std::string(countLabel) + "##td").c_str(), d.Poles, m_PolesAsPairs);
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##td",     &d.GearRatio,       0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Wheel dia (in)##td", &d.WheelDiameterIn, 0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Slip factor##td",    &d.SlipFactor,      0, 0, "%.3f");

            ImGui::SeparatorText("Weapon");
            PolesInput((std::string(countLabel) + "##tw").c_str(), w.Poles, m_PolesAsPairs);
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Gear ratio##tw",      &w.GearRatio,        0, 0, "%.2f");
            ImGui::SetNextItemWidth(110); ImGui::InputFloat("Weapon dia (in)##tw", &w.WeaponDiameterIn, 0, 0, "%.2f");
        }
    }

    // -------------------------------------------------------------------------
    void TestingManager::DrawScreen()
    {
        m_Hub.SetActiveTest(m_ActiveMode);   // firmware section follows the active test
        m_Hub.DrawSerialPanel();             // "Serial Link" window

        if (!m_Layers.empty())
            m_Layers[m_ActiveMode]->OnImGuiRender();
    }

} // namespace Workspace
