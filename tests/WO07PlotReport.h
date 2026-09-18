#pragma once
#include <atomic>

// WO07PlotReport — P01 (2D stability): exe-owned observation record for the ImPlot
// lifetime + known-data test. The host owns it and hands the plot plugin a pointer
// through an env var; it outlives every plugin load/unload of the run.
struct WO07PlotReport
{
    // The host's ImGui / ImPlot contexts as handed over by Application::LoadProjectDLL
    // through InitializePluginContexts. Recorded on the FIRST adoption; every later
    // adoption (50 reloads) must hand over the same, non-null pointers, and every frame
    // the plugin's current contexts must be exactly these. (ImGui/ImPlot are static
    // libs with per-module globals — the exe cannot read the engine's context itself.)
    std::atomic<void*> hostImGuiCtx{nullptr};
    std::atomic<void*> hostImPlotCtx{nullptr};
    std::atomic<int> adoptions{0}, adoptionMismatch{0};

    // ---- lifecycle ----
    std::atomic<int> attached{0}, detached{0}, destroyed{0};
    std::atomic<int> cycle{0};                 // which reload cycle the plugin is in (host-set)
    std::atomic<int> requestUnload{0};         // plugin -> host: this cycle's plotting is done

    // ---- what was plotted / inspected ----
    std::atomic<int> framesPlotted{0};         // frames the plot window was open and drawn
    std::atomic<int> framesInspected{0};       // frames the assertions ran (after the auto-fit settled)
    std::atomic<int> reopens{0};               // plot window closed then reopened (per cycle)
    std::atomic<int> themesApplied{0};

    // ---- failures, by kind (all must stay 0) ----
    std::atomic<int> contextMismatch{0};       // GetCurrentContext != host's
    std::atomic<int> limitMismatch{0};         // auto-fit limits != known data range
    std::atomic<int> nonfiniteLeak{0};         // NaN/Inf changed the fitted range
    std::atomic<int> logAxisMismatch{0};       // log10 spacing not equal per decade / linear spacing not equal
    std::atomic<int> legendMismatch{0};        // legend entries != series plotted
    std::atomic<int> vertexMiss{0};            // no drawn vertex near a known sample's pixel
    std::atomic<int> shadedMiss{0};            // band fill vertices absent
    std::atomic<int> scatterMiss{0};           // marker vertices absent
    std::atomic<int> emptySeriesProblem{0};    // empty series produced vertices or errors
    std::atomic<int> stackImbalance{0};        // ImGui push/pop stacks unbalanced around the plot window
    std::atomic<int> imguiErrors{0};           // ImGui recovered-error callback count (host-installed)
    std::atomic<int> pixelChecked{0}, pixelMatch{0}, pixelMismatch{0}, pixelOccluded{0};
    std::atomic<int> lastPixelR{-1}, lastPixelG{-1}, lastPixelB{-1};
    std::atomic<int> dbgNoHwnd{0}, dbgNotVisible{0}, dbgOtherWindow{0}, dbgNoProc{0}, dbgGlError{0};
    std::atomic<int> failures{0};

    // ---- known values observed (for the evidence file) ----
    std::atomic<long long> fitXMinE6{0}, fitXMaxE6{0}, fitYMinE6{0}, fitYMaxE6{0};   // *1e6
    std::atomic<long long> legendEntries{0};
    std::atomic<long long> drawVertices{0};
};
