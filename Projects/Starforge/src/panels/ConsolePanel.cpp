// ConsolePanel.cpp — see header.

#include "panels/ConsolePanel.h"

#include <imgui.h>

namespace Starforge
{
    void ConsolePanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        ImGui::Begin("Console", pOpen);

        if (ImGui::SmallButton("Clear"))
            ctx.ConsoleLines.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::SameLine();
        // Severity filters (H7): engine CS_CORE_* + editor lines share this panel.
        ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox("Info",  &m_ShowInfo);  ImGui::SameLine();
        ImGui::Checkbox("Warn",  &m_ShowWarn);  ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError); ImGui::SameLine();
        ImGui::TextDisabled("(%d)", (int)ctx.ConsoleLines.size());
        ImGui::Separator();

        ImGui::BeginChild("console_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        auto visible = [&](const ConsoleLine& l)
        {
            return !((l.Severity == LogSeverity::Info  && !m_ShowInfo)  ||
                     (l.Severity == LogSeverity::Warn  && !m_ShowWarn)  ||
                     (l.Severity == LogSeverity::Error && !m_ShowError));
        };

        for (const auto& line : ctx.ConsoleLines)
        {
            if (!visible(line))
                continue;

            if (!line.Timestamp.empty())   // dimmed HH:MM:SS column (H10)
            {
                ImGui::TextDisabled("%s", line.Timestamp.c_str());
                ImGui::SameLine();
            }
            ImVec4 col(0.85f, 0.86f, 0.88f, 1.0f);
            if (line.Severity == LogSeverity::Warn)  col = ImVec4(0.95f, 0.78f, 0.25f, 1.0f);
            if (line.Severity == LogSeverity::Error) col = ImVec4(0.95f, 0.40f, 0.36f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(line.Text.c_str());
            ImGui::PopStyleColor();
        }
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);

        // Right-click anywhere in the log → copy all currently-visible lines (H10).
        if (ImGui::BeginPopupContextWindow("console_ctx"))
        {
            if (ImGui::MenuItem("Copy visible"))
            {
                std::string all;
                for (const auto& line : ctx.ConsoleLines)
                    if (visible(line))
                    {
                        if (!line.Timestamp.empty()) { all += line.Timestamp; all += "  "; }
                        all += line.Text; all += '\n';
                    }
                ImGui::SetClipboardText(all.c_str());
            }
            ImGui::EndPopup();
        }
        ImGui::EndChild();

        ImGui::End();
    }
}
