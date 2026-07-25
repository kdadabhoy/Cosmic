// ProfilerPanel.cpp — see header. Ported from Frontier's GpuProfilerPanel (F3),
// plus CPU/GPU history sparklines (T17).

#include "panels/ProfilerPanel.h"

#include <Cosmic.h>   // RenderCommand + GpuZoneResult + Renderer3D::GetStats
#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <vector>

namespace Starforge
{
    void ProfilerPanel::OnImGuiRender(EditorContext& ctx, bool* pOpen)
    {
        (void)ctx;
        ImGui::Begin("Profiler", pOpen);

        const ImGuiIO& io = ImGui::GetIO();
        const float cpuMs = io.DeltaTime * 1000.0f;

        const std::vector<Cosmic::GpuZoneResult>& zones = Cosmic::RenderCommand::GetGpuZoneResults();

        // Frame GPU total = sum of top-level (depth 0) zones so nested zones aren't
        // double-counted.
        float gpuTotal = 0.0f;
        for (const Cosmic::GpuZoneResult& z : zones)
            if (z.Depth == 0)
                gpuTotal += z.Milliseconds;

        // Rolling history (ring buffer) for the sparklines.
        m_CpuHistory[m_HistoryPos] = cpuMs;
        m_GpuHistory[m_HistoryPos] = gpuTotal;
        m_HistoryPos = (m_HistoryPos + 1) % kHistory;

        ImGui::Text("CPU frame: %.2f ms  (%.0f fps)", cpuMs, io.Framerate);
        ImGui::PlotLines("##cpuhist", m_CpuHistory, kHistory, m_HistoryPos,
                         "CPU ms", 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f));
        ImGui::Text("GPU frame: %.2f ms", gpuTotal);
        ImGui::PlotLines("##gpuhist", m_GpuHistory, kHistory, m_HistoryPos,
                         "GPU ms", 0.0f, FLT_MAX, ImVec2(-1.0f, 40.0f));
        ImGui::Separator();

        if (zones.empty())
        {
            ImGui::TextDisabled("No GPU timing yet (a few frames of warm-up).");
            ImGui::End();
            return;
        }

        // Per-pass GPU breakdown.
        if (ImGui::BeginTable("##gpuzones", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableSetupColumn("ms",   ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableHeadersRow();

            const float denom = gpuTotal > 1e-6f ? gpuTotal : 1.0f;
            for (const Cosmic::GpuZoneResult& z : zones)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                if (z.Depth > 0) ImGui::Indent(z.Depth * 12.0f);
                ImGui::TextUnformatted(z.Name.c_str());
                if (z.Depth > 0) ImGui::Unindent(z.Depth * 12.0f);

                ImGui::TableSetColumnIndex(1);
                char label[32];
                std::snprintf(label, sizeof(label), "%.2f ms", z.Milliseconds);
                const float frac = z.Milliseconds / denom;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.55f, 0.35f, 0.15f, 1.0f));
                ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0.0f), label);
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }

        // CPU-side render breakdown: Renderer3D queue telemetry (S12).
        // W7 — the 2D build has no mesh queue to report on; the Renderer2D batch
        // counters are the equivalent read there.
        ImGui::Separator();
        {
#ifndef COSMIC_2D_ONLY
            const auto stats = Cosmic::Renderer3D::GetStats();
            ImGui::Text("Meshes: %u submitted, %u culled (%.0f%%)",
                stats.MeshesSubmitted, stats.MeshesCulled,
                stats.MeshesSubmitted > 0
                    ? 100.0f * static_cast<float>(stats.MeshesCulled) / static_cast<float>(stats.MeshesSubmitted)
                    : 0.0f);
            ImGui::Text("Mesh draw calls: %u (singles %u)", stats.DrawCalls, stats.MeshesDrawn);
            ImGui::Text("Instanced: %u draws / %u instances (explicit)",
                stats.ExplicitInstanceDraws, stats.ExplicitInstances);
            if (stats.AutoInstanceBatches > 0)
                ImGui::Text("Auto-instanced: %u meshes in %u draws",
                    stats.AutoInstancedMeshes, stats.AutoInstanceBatches);
#else
            const auto stats = Cosmic::Renderer2D::GetStats();
            ImGui::Text("Quads: %u, circles: %u, lines: %u",
                stats.QuadCount, stats.CircleCount, stats.LineCount);
            ImGui::Text("Batch draw calls: %u", stats.DrawCalls);
            ImGui::Text("Vertices: %u", stats.GetTotalVertexCount());
#endif
        }

        ImGui::End();
    }
}
