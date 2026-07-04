// ConsolePanel.cpp — see header.

#include "panels/ConsolePanel.h"

#include <imgui.h>

namespace Starforge
{
    void ConsolePanel::OnImGuiRender(EditorContext& ctx)
    {
        ImGui::Begin("Console");

        if (ImGui::SmallButton("Clear"))
            ctx.ConsoleLines.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", (int)ctx.ConsoleLines.size());
        ImGui::Separator();

        ImGui::BeginChild("console_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : ctx.ConsoleLines)
        {
            ImVec4 col(0.85f, 0.86f, 0.88f, 1.0f);
            if (line.Severity == LogSeverity::Warn)  col = ImVec4(0.95f, 0.78f, 0.25f, 1.0f);
            if (line.Severity == LogSeverity::Error) col = ImVec4(0.95f, 0.40f, 0.36f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(line.Text.c_str());
            ImGui::PopStyleColor();
        }
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::End();
    }
}
