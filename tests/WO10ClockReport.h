#pragma once
// WO10ClockReport.h — WO-10 (2D stability): the exe-owned observation record shared
// between the N01/N02 host tests and the WO10ClockFixture runtime plugin.
//
// The record lives in the host process and is handed to the fixture DLL by
// pointer through an env var (the WO-07 pattern), so what the plugin observed
// survives FreeLibrary. The plugin only ever writes plain fields / atomics here —
// it never allocates into exe-owned storage.
//
// Two observers record every frame:
//   * the DIRECT layer (an overlay the test pushes on the Application itself) —
//     it sees the raw engine dispatch: OnFixedUpdate(±1/Hz) and OnUpdate(raw*scale);
//   * the PLUGIN layer (the fixture DLL, hosted by the real WorkspaceLayer) — it
//     sees the plugin-side dispatch: OnFixedUpdate(fixed*local) and OnUpdate(raw*
//     global*local), i.e. the plugin-LOCAL scaling WorkspaceLayer applies.
// Distinguishing the two is an N01 requirement.
#include <atomic>
#include <cstdint>

struct WO10FrameRecord
{
    double clockNow      = 0.0;   // the schedule timestamp this frame sampled
    float  absoluteTime  = 0.0f;  // Application::GetAbsoluteTime() at the end of the frame
    bool   paused        = false; // Application::IsPaused() during this frame
    float  globalScale   = 1.0f;  // Application::GetTimeScale() during this frame
    float  fixedHz       = 60.0f; // Application::GetFixedTimestepHz() during this frame

    // Direct (overlay) observer.
    int    directTicks       = 0;
    float  directFixedDtSum  = 0.0f;
    float  directFixedDtMin  = 0.0f, directFixedDtMax = 0.0f;
    float  directUpdateDt    = 0.0f;
    float  directLocalTime   = 0.0f;

    // Plugin (WorkspaceLayer-hosted) observer.
    int    pluginTicks       = 0;
    float  pluginFixedDtSum  = 0.0f;
    float  pluginFixedDtMin  = 0.0f, pluginFixedDtMax = 0.0f;
    float  pluginUpdateDt    = 0.0f;
    float  pluginLocalTime   = 0.0f;
};

struct WO10ClockReport
{
    static constexpr int kMaxRecorded = 8192;   // per-frame detail kept for the first N frames

    // --- plugin lifecycle ---
    std::atomic<int> attached{ 0 };
    std::atomic<int> detached{ 0 };
    std::atomic<int> destroyed{ 0 };

    // --- the plugin's live accumulators for the CURRENT frame (reset by the driver
    //     after it snapshots them; the plugin runs before the driver overlay) ---
    std::atomic<int>       pluginTicksNow{ 0 };
    std::atomic<float>     pluginFixedDtSumNow{ 0.0f };
    std::atomic<float>     pluginFixedDtMinNow{ 0.0f };
    std::atomic<float>     pluginFixedDtMaxNow{ 0.0f };
    std::atomic<float>     pluginUpdateDtNow{ 0.0f };
    std::atomic<float>     pluginLocalTimeNow{ 0.0f };
    std::atomic<int>       pluginUpdatesNow{ 0 };    // OnUpdate calls this frame (expect exactly 1)
    std::atomic<int>       pluginNonFinite{ 0 };     // any non-finite dt / local time ever seen

    // --- whole-run aggregates (also for runs longer than kMaxRecorded frames) ---
    std::atomic<long long> pluginTicksTotal{ 0 };
    std::atomic<long long> directTicksTotal{ 0 };
    std::atomic<int>       maxPluginTicksInFrame{ 0 };
    std::atomic<int>       maxDirectTicksInFrame{ 0 };
    std::atomic<long long> framesRecorded{ 0 };     // frames since Go() (may exceed kMaxRecorded)
    std::atomic<int>       directNonFinite{ 0 };
    std::atomic<int>       localTimeDecreased{ 0 }; // monotonicity violations (plugin local time)
    std::atomic<float>     pluginLocalScale{ 1.0f };  // what the plugin applied to itself on attach

    // Per-frame detail, index = frame number since Go() (1-based schedule cursor).
    WO10FrameRecord frames[kMaxRecorded];

    // --- driver bookkeeping ---
    std::atomic<int> failures{ 0 };
    std::atomic<int> runawayClosed{ 0 };   // the real-time guard fired (a failure)
};
