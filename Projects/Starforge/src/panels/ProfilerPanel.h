#pragma once

// ProfilerPanel.h — GPU/CPU frame profiler (T17 / gap §13.2).
//
// Ported from Frontier's GpuProfilerPanel (doc 10 F3): per-pass GPU milliseconds
// from the SceneRenderer's timer-query zones (Cosmic::RenderCommand::
// GetGpuZoneResults) plus the Renderer3D queue telemetry, and adds a rolling
// CPU/GPU history sparkline. Zero cost when closed (nothing runs unless the
// window is open — the timer queries themselves live in the SceneRenderer).

#include "EditorContext.h"

namespace Starforge
{
    class ProfilerPanel
    {
    public:
        void OnImGuiRender(EditorContext& ctx, bool* pOpen = nullptr);

    private:
        static constexpr int kHistory = 120;
        float m_CpuHistory[kHistory] = { 0.0f };
        float m_GpuHistory[kHistory] = { 0.0f };
        int   m_HistoryPos = 0;
    };
}
