#pragma once
#include <atomic>

// WO07LifetimeReport — L01 (2D stability): shared, exe-owned observation record for
// the F-LIFETIME runtime-plugin teardown test. The record lives in the host process
// (test_wo07_host.cpp) and is handed to the fixture DLL by pointer through an env
// var, so it OUTLIVES the DLL: after FreeLibrary the fixture's code is gone but every
// counter it wrote survives for the host to assert. All fields are atomic because the
// job worker and the file-watcher thread touch some of them.
struct WO07LifetimeReport
{
    // --- layer lifecycle ---
    std::atomic<int> attached{0};
    std::atomic<int> detached{0};
    std::atomic<int> destroyed{0};
    // Ordering proof: a monotonically increasing sequence stamped at OnDetach, at the
    // owned-object destructor, and (by the host) at FreeLibrary — OnDetach < destroy < free.
    std::atomic<long long> seqDetach{0};
    std::atomic<long long> seqDestroy{0};

    // --- owned resources: created vs released (must balance) ---
    std::atomic<int> texCreated{0},   texFreed{0};
    std::atomic<int> fboCreated{0},   fboFreed{0};
    std::atomic<int> componentDestroyed{0};   // module-owned object dtor
    std::atomic<int> listenerSubscribed{0}, listenerUnsubscribed{0};
    std::atomic<int> sinkAdded{0},    sinkRemoved{0};
    std::atomic<int> jobSubmitted{0}, jobRan{0};
    std::atomic<int> watcherStarted{0}, watcherStopped{0};

    // --- no-callback-after-unload proof ---
    // The fixture's EntitySelection listener increments this ONLY while the fixture is
    // live. The host fires a post-unload EntitySelection change; if the fixture failed
    // to unsubscribe, that call reaches freed code (crash) or bumps this after unload.
    std::atomic<int> listenerFiredWhileLive{0};
    std::atomic<int> listenerFiredAfterUnload{0};   // must stay 0
    std::atomic<int> unloaded{0};                    // set by the fixture's OnDetach tail

    // --- resource-balance supplement (process-wide; noisy, logged not gated) ---
    std::atomic<long long> baselineHandles{0}, warmedHandles{0}, postHandles{0};
    std::atomic<long long> baselineThreads{0}, warmedThreads{0}, postThreads{0};

    // --- driver bookkeeping ---
    std::atomic<int> failures{0};
    std::atomic<int> hostProbeFired{0};   // the host's own listener saw the post-unload emit (non-vacuous)
    std::atomic<int> freshLaunchOnly{0};  // 1 == a "fresh launch/close" cycle (no reload transition)
};
