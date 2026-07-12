// ConsolePanel.cpp — see header. Console v2 (T16): text search + source chips +
// monospace body, on top of the H7/H10 severity filters / timestamps / copy.

#include "panels/ConsolePanel.h"
#include "ui/IconsLucide.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace Starforge
{
    namespace
    {
        bool ContainsCI(const std::string& hay, const std::string& needle)
        {
            if (needle.empty()) return true;
            auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
            return it != hay.end();
        }

        const char* SourceTag(LogSource s)
        {
            switch (s)
            {
                case LogSource::Engine: return "engine";
                case LogSource::Game:   return "game";
                default:                return "editor";
            }
        }
    }

    void ConsolePanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        ImGui::Begin("Console", pOpen);

        if (ImGui::SmallButton("Clear"))
            ctx.ConsoleLines.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_AutoScroll);

        // Severity filters (H7).
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox("Info",  &m_ShowInfo);  ImGui::SameLine();
        ImGui::Checkbox("Warn",  &m_ShowWarn);  ImGui::SameLine();
        ImGui::Checkbox("Error", &m_ShowError);

        // Source chips (T16).
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Checkbox("Engine", &m_ShowEngine); ImGui::SameLine();
        ImGui::Checkbox("Editor", &m_ShowEditor); ImGui::SameLine();
        ImGui::Checkbox("Game",   &m_ShowGame);

        // Text search (T16) — right-aligned.
        {
            const float searchW = 200.0f;
            float rightX = ImGui::GetContentRegionMax().x - searchW;
            if (rightX > ImGui::GetCursorPosX()) { ImGui::SameLine(); ImGui::SetCursorPosX(rightX); }
            else ImGui::SameLine();
            ImGui::SetNextItemWidth(searchW);
            ImGui::InputTextWithHint("##csearch", ICON_LC_SEARCH " Filter", m_Search, sizeof(m_Search));
        }
        ImGui::Separator();

        auto visible = [&](const ConsoleLine& l)
        {
            if ((l.Severity == LogSeverity::Info  && !m_ShowInfo)  ||
                (l.Severity == LogSeverity::Warn  && !m_ShowWarn)  ||
                (l.Severity == LogSeverity::Error && !m_ShowError))
                return false;
            if ((l.Source == LogSource::Engine && !m_ShowEngine) ||
                (l.Source == LogSource::Editor && !m_ShowEditor) ||
                (l.Source == LogSource::Game   && !m_ShowGame))
                return false;
            return ContainsCI(l.Text, m_Search);
        };

        ImGui::BeginChild("console_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Monospace body (T16): ImGui's built-in ProggyClean (added first as
        // Fonts[0]) is fixed-width — align timestamps/columns without a new face.
        ImGuiIO& io = ImGui::GetIO();
        ImFont* mono = io.Fonts->Fonts.empty() ? nullptr : io.Fonts->Fonts[0];
        if (mono) ImGui::PushFont(mono, 13.0f);

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

        if (mono) ImGui::PopFont();

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
                        all += "["; all += SourceTag(line.Source); all += "] ";
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
