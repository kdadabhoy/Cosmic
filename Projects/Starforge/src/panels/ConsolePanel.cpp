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
        ImGui::Separator();

        ImGui::BeginChild("console_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : ctx.ConsoleLines)
            ImGui::TextUnformatted(line.c_str());
        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::End();
    }
}
