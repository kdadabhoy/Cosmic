#pragma once

// GpuProfilerPanel.h
//
// ============================================================================
// Frontier — GPU profiler HUD (doc 10 F3, pulling forward S12.5).
// ============================================================================
//
// A dockable ImGui panel that reads the engine's most recently RESOLVED GPU
// timer frame (Cosmic::RenderCommand::GetGpuZoneResults) and draws a per-pass
// table (Name | ms) with a horizontal bar scaled to the frame's GPU total,
// plus the CPU frame time from ImGui's IO. The zones come from
// SceneRenderer::Render's per-pass instrumentation (Shadow / Reflection /
// Opaque / Transparents / Post+Composite), so the numbers respond live to the
// world's feature toggles.
//
// Stateless — drawn each frame by FrontierApp::OnImGuiRender and docked
// RightBottom by ApplyDockLayout. The window title MUST match the DockWindow
// name ("GPU Profiler").
// ============================================================================

namespace Frontier
{
    class GpuProfilerPanel
    {
    public:
        // Draws the "GPU Profiler" window (ImGui::Begin/End inside). Renders an
        // empty-but-valid table when no GPU timing has resolved yet.
        static void Draw();
    };

} // namespace Frontier
