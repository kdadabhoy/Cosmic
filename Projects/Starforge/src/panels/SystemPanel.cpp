// SystemPanel.cpp — see header. Jobs + Resources tabs (T18).

#include "panels/SystemPanel.h"

#include "jobs/JobSystem.h"
#include "assets/AssetLibrary.h"

#include <imgui.h>

#include <cstdio>
#include <string>
#include <vector>

namespace Starforge
{
    namespace
    {
        const char* TypeName(Cosmic::AssetType t)
        {
            switch (t)
            {
                case Cosmic::AssetType::Texture:          return "Texture";
                case Cosmic::AssetType::Shader:           return "Shader";
                case Cosmic::AssetType::Mesh:             return "Mesh";
                case Cosmic::AssetType::Model:            return "Model";
                case Cosmic::AssetType::Material:         return "Material";
                case Cosmic::AssetType::AnimationClipSet: return "Clips";
            }
            return "?";
        }

        std::string FmtBytes(uint64_t b)
        {
            char buf[32];
            if (b < 1024ull)                    std::snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)b);
            else if (b < 1024ull * 1024)        std::snprintf(buf, sizeof(buf), "%.1f KiB", b / 1024.0);
            else if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MiB", b / (1024.0 * 1024));
            else                                std::snprintf(buf, sizeof(buf), "%.2f GiB", b / (1024.0 * 1024 * 1024));
            return buf;
        }

        // A short display name (basename) for a normalized cache key.
        std::string ShortName(const std::string& path)
        {
            const size_t slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    void SystemPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        (void)ctx;
        ImGui::Begin("System", pOpen);

        if (ImGui::BeginTabBar("##systabs"))
        {
            // ---- Jobs -------------------------------------------------------
            if (ImGui::BeginTabItem("Jobs"))
            {
                Cosmic::JobSystem& js = Cosmic::JobSystem::Get();
                ImGui::Text("Workers:   %u  (of %u logical cores)", js.GetWorkerCount(), js.GetCoreCount());
                ImGui::Separator();
                ImGui::Text("Queued:    %u", js.GetQueuedCount());
                ImGui::Text("Active:    %u", js.GetActiveCount());
                ImGui::Text("Completed: %llu", (unsigned long long)js.GetCompletedCount());
                ImGui::EndTabItem();
            }

            // ---- Resources --------------------------------------------------
            if (ImGui::BeginTabItem("Resources"))
            {
                struct Row { std::string Path; Cosmic::AssetType Type; long Refs; uint64_t Cpu, Gpu; };
                std::vector<Row> rows;
                uint64_t totCpu = 0, totGpu = 0;
                Cosmic::AssetLibrary::Enumerate([&](const Cosmic::AssetEntry& e)
                {
                    rows.push_back({ e.Path, e.Type, e.Refs, e.CpuBytes, e.GpuBytes });
                    totCpu += e.CpuBytes;
                    totGpu += e.GpuBytes;
                });

                ImGui::Text("%zu assets  \xC2\xB7  %s CPU  /  %s GPU",
                            rows.size(), FmtBytes(totCpu).c_str(), FmtBytes(totGpu).c_str());
                ImGui::Separator();

                std::string reloadTarget;   // deferred to after the table
                if (ImGui::BeginTable("##res", 6,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                    ImGui::TableSetupColumn("Type",  ImGuiTableColumnFlags_WidthStretch, 0.14f);
                    ImGui::TableSetupColumn("Refs",  ImGuiTableColumnFlags_WidthStretch, 0.08f);
                    ImGui::TableSetupColumn("CPU",   ImGuiTableColumnFlags_WidthStretch, 0.14f);
                    ImGui::TableSetupColumn("GPU",   ImGuiTableColumnFlags_WidthStretch, 0.14f);
                    ImGui::TableSetupColumn("",      ImGuiTableColumnFlags_WidthStretch, 0.08f);
                    ImGui::TableHeadersRow();

                    for (const Row& r : rows)
                    {
                        ImGui::TableNextRow();
                        ImGui::PushID(r.Path.c_str());

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(ShortName(r.Path).c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.Path.c_str());

                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(TypeName(r.Type));
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%ld", r.Refs);
                        ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(FmtBytes(r.Cpu).c_str());
                        ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(FmtBytes(r.Gpu).c_str());
                        ImGui::TableSetColumnIndex(5);
                        if (ImGui::SmallButton("Reload"))
                            reloadTarget = r.Path;

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                if (!reloadTarget.empty())
                    Cosmic::AssetLibrary::Reload(reloadTarget);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
