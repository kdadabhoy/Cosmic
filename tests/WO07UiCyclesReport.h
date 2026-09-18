#pragma once
#include <atomic>

// WO07UiCyclesReport — L05 (2D stability): exe-owned record for the SF_Telem scripted
// UI-cycle host (tests/WO07UiCyclesFixture.cpp). Counts every scripted action by kind,
// and every per-action oracle failure; the fixture also writes the exact action log and
// screenshots into the directory named by COSMIC_WO07_L05_OUT.
struct WO07UiCyclesReport
{
    std::atomic<int> cyclesPlanned{200};
    std::atomic<int> cyclesDone{0};
    std::atomic<int> actions{0};              // scripted actions judged
    std::atomic<int> actionsFailed{0};        // actions whose oracle diff was non-empty
    std::atomic<int> done{0};                 // fixture finished (host may close)
    std::atomic<int> failures{0};             // harness-level failures (unexpected state)

    // ---- action kinds ----
    std::atomic<int> screenSwitches{0};       // real Navigation buttons activated
    std::atomic<int> screenMismatch{0};       // button activation did not change the screen
    std::atomic<int> serialClosed{0}, serialOpened{0}, serialLost{0};
    std::atomic<int> replayLoads{0}, replayUnloads{0};
    std::atomic<int> dialogsOpened{0}, dialogsCancelled{0};   // native IFileDialog shown / cancelled
    std::atomic<int> minimizes{0}, restores{0}, resizes{0}, fullscreenToggles{0};
    std::atomic<int> undocks{0}, redocks{0};

    // ---- oracle totals (all must stay 0) ----
    std::atomic<int> recoveredErrors{0};
    std::atomic<int> endFrameLeaks{0};
    std::atomic<int> contextDrift{0};
    std::atomic<int> layerImbalance{0};       // depths around SF_Telem::OnImGuiRender differ
    std::atomic<int> screenshots{0};
};
