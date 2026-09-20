// panels/DataBusPanel.cpp — see DataBusPanel.h (AP-03).

#include "DataBusPanel.h"

#include "ui/IconsLucide.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Starforge
{
    namespace
    {
        std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

        std::string ValueText(const Cosmic::DataValue& v)
        {
            switch (v.ValueKind)
            {
                case Cosmic::DataValue::Kind::Bool:   return v.Bool ? "true" : "false";
                case Cosmic::DataValue::Kind::String: return "\"" + v.String + "\"";
                default:
                {
                    if (!std::isfinite(v.Number)) return std::isnan(v.Number) ? "nan" : (v.Number > 0 ? "+inf" : "-inf");
                    char b[48]; std::snprintf(b, sizeof(b), "%.6g", v.Number); return b;
                }
            }
        }
    }

    void DataBusPanel::SetPreview(Cosmic::DataBus& preview, const std::string& channel, const std::string& text)
    {
        if (channel.empty()) return;
        const std::string t = Lower(text);
        if (t == "true" || t == "on")   { preview.SetBool(channel, true);  return; }
        if (t == "false" || t == "off") { preview.SetBool(channel, false); return; }
        char* end = nullptr;
        const double d = std::strtod(text.c_str(), &end);
        if (end && *end == '\0' && !text.empty()) { preview.Set(channel, d); return; }
        preview.SetString(channel, text);
    }

    SourceHit DataBusPanel::ProducerHit(const std::string& projectRoot, const std::string& channel, const Cosmic::DataBus& live)
    {
        return SourceLocator(projectRoot).ForChannel(channel, live);
    }

    void DataBusPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen, const std::string& projectRoot,
                                     const Cosmic::DataBus& live, Cosmic::DataBus& preview, bool playing)
    {
        if (!ImGui::Begin("DataBus", pOpen)) { ImGui::End(); return; }
        if (!ctx.ProjectOpen) { ImGui::TextDisabled("Open a project."); ImGui::End(); return; }

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputTextWithHint("##filter", "filter channels…", m_Filter, sizeof(m_Filter));
        ImGui::SameLine();
        const std::string needle = Lower(m_Filter);
        auto passes = [&](const std::string& ch) { return needle.empty() || Lower(ch).find(needle) != std::string::npos; };

        if (playing)
        {
            ImGui::TextColored(ImVec4(0.30f, 1.0f, 0.42f, 1.0f), ICON_LC_RADIO " live  (%zu channels, t = %.1f s)",
                               live.ChannelCount(), live.Now());
            if (ImGui::BeginTable("##live", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Channel",  ImGuiTableColumnFlags_WidthStretch, 0.34f);
                ImGui::TableSetupColumn("Value",    ImGuiTableColumnFlags_WidthStretch, 0.22f);
                ImGui::TableSetupColumn("Age",      ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("Producer", ImGuiTableColumnFlags_WidthStretch, 0.24f);
                ImGui::TableSetupColumn("Open",     ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableHeadersRow();
                const SourceLocator loc(projectRoot);
                for (const std::string& ch : live.Channels())
                {
                    if (!passes(ch)) continue;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(ch.c_str());
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(ValueText(live.Get(ch)).c_str());
                    ImGui::TableSetColumnIndex(2);
                    {
                        const double age = live.Age(ch);
                        if (std::isfinite(age)) ImGui::Text("%.2f s", age); else ImGui::TextDisabled("-");
                    }
                    const std::string producer = live.Producer(ch);
                    ImGui::TableSetColumnIndex(3);
                    if (producer.empty()) ImGui::TextDisabled("(none)"); else ImGui::TextUnformatted(producer.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::PushID(ch.c_str());
                    const SourceHit hit = loc.ForChannel(ch, live);
                    ImGui::BeginDisabled(!hit.Resolved());
                    if (ImGui::SmallButton(ICON_LC_CODE " Open")) SourceLocator::Open(hit);
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", hit.Resolved() ? (hit.Path + ":" + std::to_string(hit.Line)).c_str() : hit.Reason.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        else
        {
            ImGui::TextDisabled(ICON_LC_PENCIL " preview values  (%zu channels) — what bound widgets show while editing",
                                preview.ChannelCount());
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##pch", "channel (e.g. app.sine)", m_NewChannel, sizeof(m_NewChannel));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            const bool enter = ImGui::InputText("##pval", m_NewValue, sizeof(m_NewValue), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button(ICON_LC_PLUS " Set") || enter) && m_NewChannel[0])
                SetPreview(preview, m_NewChannel, m_NewValue);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("number, true/false, or text. Preview values never reach a running app.");
            ImGui::SameLine();
            if (ImGui::Button("Clear all")) preview.Clear();

            if (ImGui::BeginTable("##preview", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch, 0.5f);
                ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableHeadersRow();
                std::string remove;
                for (const std::string& ch : preview.Channels())
                {
                    if (!passes(ch)) continue;
                    ImGui::TableNextRow();
                    ImGui::PushID(ch.c_str());
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(ch.c_str());
                    ImGui::TableSetColumnIndex(1);
                    {
                        const Cosmic::DataValue v = preview.Get(ch);
                        if (v.ValueKind == Cosmic::DataValue::Kind::Bool)
                        {
                            bool b = v.Bool;
                            if (ImGui::Checkbox("##b", &b)) preview.SetBool(ch, b);
                        }
                        else if (v.ValueKind == Cosmic::DataValue::Kind::Number)
                        {
                            float f = (float)v.Number;
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::DragFloat("##n", &f, 0.05f)) preview.Set(ch, (double)f);
                        }
                        else
                        {
                            char buf[128]; std::snprintf(buf, sizeof(buf), "%s", v.String.c_str());
                            ImGui::SetNextItemWidth(-1.0f);
                            if (ImGui::InputText("##s", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)) preview.SetString(ch, buf);
                        }
                    }
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::SmallButton(ICON_LC_X)) remove = ch;
                    ImGui::PopID();
                }
                ImGui::EndTable();
                if (!remove.empty()) preview.Remove(remove);
            }
        }
        ImGui::End();
    }
}
