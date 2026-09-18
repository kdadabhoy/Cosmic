// test_wo07_p01.cpp — P01 (2D stability): ImPlot lifetime + known data.
//
// Skipped by default (real Application + window + GL); the WO-04 runner launches it as
// fresh isolated children. One child = one process that loads WO07PlotFixture.dll (a
// real runtime plugin adopting the host's ImGui/ImPlot contexts), lets it plot the
// known time-series / XY / scatter / shaded-band / legend / log-axis / nonfinite /
// empty data under the themed UI for a cycle, reloads it (TransitionToLauncher + load
// again) 50 times, and finally asserts the exe-owned report: every cycle attached /
// detached / destroyed exactly once, the plot window was reopened every cycle, the
// contexts were always the host's, fitted limits matched the known ranges, nonfinite
// samples never moved them, log/linear spacing held, the legend listed the series,
// vertices with the series colours were drawn at the known samples' pixels, the ImGui
// stacks balanced, no ImGui error was recovered, and the front-buffer pixel probe saw
// the line colour on the presented image (counted only when the window was visible
// and owned that pixel).
#include <doctest.h>
#include "WO07PlotReport.h"
#include "WO05NativeWindow.h"

#include "core/Application.h"
#include "core/Layer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace
{
    constexpr int kCycles = 50;

    class P01Driver final : public Cosmic::Layer
    {
        WO07PlotReport& rep;
        std::string dll;
        int launcherFrames = 0;
        bool done = false;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        void Close() { Cosmic::WindowCloseEvent e; Cosmic::Application::Get().OnEvent(e); }
    public:
        P01Driver(WO07PlotReport& r, std::string d) : Cosmic::Layer("WO07 P01 driver"), rep(r), dll(std::move(d)) {}
        void OnUpdate(float) override
        {
            if (done) return;
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(90)) { ++rep.failures; done = true; Close(); return; }
            auto& app = Cosmic::Application::Get();
            const int cycle = rep.cycle;
            // The plugin asks to be unloaded once its cycle plotted enough frames.
            if (rep.requestUnload > cycle && rep.detached == cycle)
            {
                app.TransitionToLauncher();
                return;
            }
            if (rep.detached == cycle + 1)
            {
                if (++launcherFrames < 3) return;   // a couple of launcher frames between loads
                launcherFrames = 0;
                if (cycle + 1 >= kCycles) { done = true; Close(); return; }
                rep.cycle = cycle + 1;
                app.TransitionFromLauncherToWorkspace(dll);   // reload the same plugin
            }
        }
    };

}

TEST_CASE("WO-07 P01 host: ImPlot known data under 50 plugin reloads with adopted contexts" * doctest::skip())
{
    WO07PlotReport rep;
    char ptr[32];
    std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)&rep);
    _putenv_s("COSMIC_WO07_PLOT_REPORT", ptr);
    {
        wchar_t exePath[MAX_PATH]{};
        REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
        const auto dll = (std::filesystem::path(exePath).parent_path() / "WO07PlotFixture.dll").string();
        Cosmic::Application app(dll);
        app.GetWindow().SetVSync(false);
        // The window stays VISIBLE: the front-buffer pixel probe needs pixel ownership.
        // (ImGui/ImPlot are static libraries with per-module globals, so the engine's
        // contexts are observed through the plugin's InitializePluginContexts adoption —
        // recorded in the report — not from this exe's own copies.)
        app.PushLayer(new P01Driver(rep, dll));
        app.Run();
    }
    _putenv_s("COSMIC_WO07_PLOT_REPORT", "");

    CHECK(rep.failures == 0);
    // Adopted contexts: handed over on every load, non-null, and identical across all 50 loads.
    CHECK(rep.adoptions == kCycles);
    CHECK(rep.adoptionMismatch == 0);
    CHECK(rep.hostImGuiCtx.load() != nullptr);
    CHECK(rep.hostImPlotCtx.load() != nullptr);
    CHECK(rep.attached == kCycles); CHECK(rep.detached == kCycles); CHECK(rep.destroyed == kCycles);
    CHECK(rep.reopens == kCycles);
    CHECK(rep.themesApplied == kCycles);
    CHECK(rep.framesPlotted >= kCycles * 12);
    CHECK(rep.framesInspected >= kCycles * 8);
    CHECK(rep.contextMismatch == 0);
    CHECK(rep.limitMismatch == 0);
    CHECK(rep.nonfiniteLeak == 0);
    CHECK(rep.logAxisMismatch == 0);
    CHECK(rep.legendMismatch == 0);
    CHECK(rep.vertexMiss == 0);
    CHECK(rep.shadedMiss == 0);
    CHECK(rep.scatterMiss == 0);
    CHECK(rep.emptySeriesProblem == 0);
    CHECK(rep.stackImbalance == 0);
    CHECK(rep.imguiErrors == 0);
    // Rendered pixels: at least one presented frame per cycle must have been probed
    // with the window visible and unoccluded; a fully occluded run is NOT a pass.
    CHECK(rep.pixelChecked >= kCycles);
    CHECK(rep.pixelMismatch == 0);
    CHECK(rep.pixelMatch >= kCycles);

    std::printf("WO07 P01: cycles=%d framesPlotted=%d inspected=%d reopens=%d themes=%d fit X=[%.6f,%.6f] Y=[%.6f,%.6f] legend=%lld verts=%lld pixel[checked=%d match=%d mismatch=%d occluded=%d (nohwnd=%d notvisible=%d otherwin=%d noproc=%d glerr=0x%X) last=(%d,%d,%d)] errors=%d stack=%d\n",
                rep.attached.load(), rep.framesPlotted.load(), rep.framesInspected.load(), rep.reopens.load(), rep.themesApplied.load(),
                rep.fitXMinE6 / 1e6, rep.fitXMaxE6 / 1e6, rep.fitYMinE6 / 1e6, rep.fitYMaxE6 / 1e6,
                rep.legendEntries.load(), rep.drawVertices.load(),
                rep.pixelChecked.load(), rep.pixelMatch.load(), rep.pixelMismatch.load(), rep.pixelOccluded.load(), rep.dbgNoHwnd.load(), rep.dbgNotVisible.load(), rep.dbgOtherWindow.load(), rep.dbgNoProc.load(), rep.dbgGlError.load(),
                rep.lastPixelR.load(), rep.lastPixelG.load(), rep.lastPixelB.load(),
                rep.imguiErrors.load(), rep.stackImbalance.load());
}
