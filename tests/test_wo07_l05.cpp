// test_wo07_l05.cpp — L05 (2D stability): 200 scripted UI cycles over the real
// SF_Telem screens with a per-action Dear ImGui balance oracle.
//
// Skipped by default (real Application + window + GL + native dialogs); the WO-04
// runner launches it as a fresh isolated child. The child hosts WO07UiCyclesFixture.dll
// (the real Workspace::SF_Telem over the WO-04 FakeSerialTransport) which runs the
// script (see the fixture header) and writes the exact action log + screenshots into
// COSMIC_WO07_L05_OUT. After Run() the host asserts the exe-owned totals: every cycle
// and action kind actually happened (non-vacuous), every real button changed the
// screen, every native dialog was seen and cancelled, and the oracle stayed clean —
// zero recovered ImGui errors, zero end-of-frame stack leaks, zero context drift, zero
// per-layer imbalance, zero failed actions.
#include <doctest.h>
#include "WO07UiCyclesReport.h"
#include "WO05NativeWindow.h"

#include "core/Application.h"
#include "core/Layer.h"

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
    class L05Driver final : public Cosmic::Layer
    {
        WO07UiCyclesReport& rep;
        bool done = false;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    public:
        explicit L05Driver(WO07UiCyclesReport& r) : Cosmic::Layer("WO07 L05 driver"), rep(r) {}
        void OnUpdate(float) override
        {
            if (done) return;
            if (rep.done || std::chrono::steady_clock::now() - start > std::chrono::seconds(660))
            {
                if (!rep.done) ++rep.failures;
                done = true;
                Cosmic::WindowCloseEvent e;
                Cosmic::Application::Get().OnEvent(e);
            }
        }
    };
}

TEST_CASE("WO-07 L05 host: 200 scripted SF_Telem UI cycles with per-action ImGui stack balance" * doctest::skip())
{
    WO07UiCyclesReport rep;
    char* n = nullptr; size_t len = 0;
    _dupenv_s(&n, &len, "COSMIC_WO07_L05_CYCLES");
    if (n) { rep.cyclesPlanned = std::atoi(n); free(n); }
    const int cycles = rep.cyclesPlanned;
    char ptr[32];
    std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)&rep);
    _putenv_s("COSMIC_WO07_L05_REPORT", ptr);
    char* out = nullptr;
    _dupenv_s(&out, &len, "COSMIC_WO07_L05_OUT");
    if (!out) _putenv_s("COSMIC_WO07_L05_OUT", ".");
    free(out);
    {
        wchar_t exePath[MAX_PATH]{};
        REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
        const auto dll = (std::filesystem::path(exePath).parent_path() / "WO07UiCyclesFixture.dll").string();
        Cosmic::Application app(dll);
        // The window stays VISIBLE (screenshots of the presented image; native dialogs).
        app.PushLayer(new L05Driver(rep));
        app.Run();
    }
    _putenv_s("COSMIC_WO07_L05_REPORT", "");

    CHECK(rep.done == 1);
    CHECK(rep.failures == 0);
    CHECK(rep.cyclesDone == cycles);
    CHECK(rep.actions >= cycles * 10);
    CHECK(rep.actionsFailed == 0);
    // every action kind really happened (non-vacuous)
    CHECK(rep.screenSwitches >= cycles * 5);
    CHECK(rep.screenMismatch == 0);
    CHECK(rep.serialClosed >= cycles / 3); CHECK(rep.serialOpened >= cycles / 3); CHECK(rep.serialLost >= cycles / 3);
    CHECK(rep.replayLoads == cycles); CHECK(rep.replayUnloads == cycles);
    CHECK(rep.dialogsOpened == cycles / 10); CHECK(rep.dialogsCancelled == cycles / 10);
    CHECK(rep.minimizes == (cycles + 4) / 5); CHECK(rep.restores == rep.minimizes);
    CHECK(rep.resizes >= cycles / 5);
    CHECK(rep.fullscreenToggles == 2 * ((cycles + 21) / 25));
    CHECK(rep.undocks >= cycles / 5); CHECK(rep.redocks == rep.undocks);
    CHECK(rep.screenshots >= 1 + cycles / 50);
    // the oracle
    CHECK(rep.recoveredErrors == 0);
    CHECK(rep.endFrameLeaks == 0);
    CHECK(rep.contextDrift == 0);
    CHECK(rep.layerImbalance == 0);

    std::printf("WO07 L05: cycles=%d actions=%d failed=%d screens=%d mismatch=%d serial[closed=%d open=%d lost=%d] replay[load=%d unload=%d] dialogs[opened=%d cancelled=%d] window[min=%d restore=%d resize=%d fullscreen=%d undock=%d redock=%d] shots=%d oracle[errors=%d leaks=%d drift=%d layer=%d]\n",
                rep.cyclesDone.load(), rep.actions.load(), rep.actionsFailed.load(), rep.screenSwitches.load(), rep.screenMismatch.load(),
                rep.serialClosed.load(), rep.serialOpened.load(), rep.serialLost.load(), rep.replayLoads.load(), rep.replayUnloads.load(),
                rep.dialogsOpened.load(), rep.dialogsCancelled.load(), rep.minimizes.load(), rep.restores.load(), rep.resizes.load(),
                rep.fullscreenToggles.load(), rep.undocks.load(), rep.redocks.load(), rep.screenshots.load(),
                rep.recoveredErrors.load(), rep.endFrameLeaks.load(), rep.contextDrift.load(), rep.layerImbalance.load());
}
