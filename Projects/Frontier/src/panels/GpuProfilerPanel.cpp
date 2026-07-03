// GpuProfilerPanel.cpp — see GpuProfilerPanel.h. Doc 10 F3.

#include "GpuProfilerPanel.h"

#include <Cosmic.h>   // RenderCommand + GpuZoneResult
#include <imgui.h>

#include <cfloat>     // FLT_MIN (ProgressBar full-width)
#include <cstdio>     // std::snprintf
#include <string>
#include <vector>

namespace Frontier
{
    void GpuProfilerPanel::Draw()
    {
        ImGui::Begin("GPU Profiler");

        const ImGuiIO& io = ImGui::GetIO();
        const float cpuMs = io.DeltaTime * 1000.0f;
        ImGui::Text("CPU frame: %.2f ms  (%.0f fps)", cpuMs, io.Framerate);

        const std::vector<Cosmic::GpuZoneResult>& zones = Cosmic::RenderCommand::GetGpuZoneResults();

        // Frame GPU total = sum of the top-level zones (depth 0) so nested zones,
        // if any, aren't double-counted. Frontier's passes are all top-level.
        float total = 0.0f;
        for (const Cosmic::GpuZoneResult& z : zones)
            if (z.Depth == 0)
                total += z.Milliseconds;

        ImGui::Text("GPU frame: %.2f ms", total);
        ImGui::Separator();

        if (zones.empty())
        {
            ImGui::TextDisabled("No GPU timing yet (a few frames of warm-up).");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("##gpuzones", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("ms",   ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableHeadersRow();

            const float denom = total > 1e-6f ? total : 1.0f;
            for (const Cosmic::GpuZoneResult& z : zones)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                // Indent nested zones by depth (Frontier's are all depth 0).
                if (z.Depth > 0)
                    ImGui::Indent(z.Depth * 12.0f);
                ImGui::TextUnformatted(z.Name.c_str());
                if (z.Depth > 0)
                    ImGui::Unindent(z.Depth * 12.0f);

                ImGui::TableSetColumnIndex(1);
                // A horizontal bar scaled to the frame total, with the ms overlaid.
                char label[32];
                std::snprintf(label, sizeof(label), "%.2f ms", z.Milliseconds);
                const float frac = z.Milliseconds / denom;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.55f, 0.35f, 0.15f, 1.0f));
                ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), label);
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }

} // namespace Frontier
