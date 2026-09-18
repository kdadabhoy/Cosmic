#pragma once
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// WO07TeardownReport — L04 (2D stability): exe-owned observation record for the
// "teardown during live background activity" runtime-plugin test. The host
// (test_wo07_host.cpp) owns it and hands the fixture DLL a pointer through an env
// var, so every counter and event survives FreeLibrary. Everything the job worker,
// the file-watcher thread, the serial reader or the driver's helper thread touch is
// atomic; the barrier/event HANDLEs are exe-owned so the fixture's job can block on
// a barrier the EXE releases (the "genuinely in-flight" job).
struct WO07TeardownReport
{
    // ---- mode ----
    std::atomic<int> mode{0};   // 0 reload (TransitionToLauncher from inside a callback)
                                // 1 close  (WM_CLOSE while the job is blocked)
                                // 2 careless-reload (OnDetach does NOT join its job; the
                                //   exe releases the barrier only AFTER detach)

    // ---- sequence stamps (monotonic; ordering proofs) ----
    std::atomic<long long> seq{0};
    std::atomic<long long> seqTransitionRequested{0}; // inside the selection callback
    std::atomic<long long> seqCallbackReturned{0};    // that callback returned
    std::atomic<long long> seqDetachBegin{0};
    std::atomic<long long> seqDetachEnd{0};
    std::atomic<long long> seqDestroy{0};
    std::atomic<long long> seqJobEntered{0};
    std::atomic<long long> seqJobDone{0};
    std::atomic<long long> seqAfterUnload{0};         // host: first launcher frame after unload

    // ---- layer lifecycle ----
    std::atomic<int> attached{0}, detached{0}, destroyed{0}, unloaded{0};
    std::atomic<int> transitionRequestedFromCallback{0};

    // ---- the in-flight job (barrier owned by the exe) ----
    HANDLE jobBarrier  = nullptr;   // manual-reset; the job blocks on it after "entered"
    HANDLE jobEntered  = nullptr;   // set by the job once it is blocked on the barrier
    HANDLE jobDone     = nullptr;   // set by the job after it ran to completion
    std::atomic<int> jobSubmitted{0}, jobRan{0};
    std::atomic<int> jobInFlightAtDetach{0};   // entered && !ran when OnDetach began
    std::atomic<int> jobJoinedInDetach{0};     // OnDetach released + waited for jobDone
    std::atomic<int> jobInFlightAtClose{0};    // (mode 1) entered && !ran when WM_CLOSE posted
    std::atomic<int> jobActiveAfterUnload{0};  // host: JobSystem active/queued after unload (must be 0)

    // ---- callbacks: fired while live vs after unload (after must stay 0) ----
    std::atomic<int> selectionWhileLive{0}, selectionAfterUnload{0};
    std::atomic<int> sinkWhileLive{0},      sinkAfterUnload{0};
    std::atomic<int> hotkeyWhileLive{0},    hotkeyAfterUnload{0};
    std::atomic<int> busWhileLive{0};
    std::atomic<int> watchEventsWhileLive{0};
    std::atomic<int> serialBytesWhileLive{0};
    // host-side non-vacuity probes (the same dispatch paths still fire, into exe code)
    std::atomic<int> hostSelectionAfterUnload{0}, hostSinkAfterUnload{0}, hostHotkeyAfterUnload{0};

    // ---- disconnect-during-dispatch probes ----
    // EntitySelection: listener A unsubscribes listener B from inside a dispatch;
    // B must not fire in that same dispatch (the EventBus contract).
    std::atomic<int> esRemovedMidDispatchFired{0};
    std::atomic<int> esProbeDispatches{0};
    // EventBus (per-scene): same shape, expected 0.
    std::atomic<int> busRemovedMidDispatchFired{0};

    // ---- owned resources (create == release) ----
    std::atomic<int> texCreated{0}, texFreed{0}, fboCreated{0}, fboFreed{0};
    std::atomic<int> componentDestroyed{0};
    std::atomic<int> listenerSubscribed{0}, listenerUnsubscribed{0};
    std::atomic<int> sinkAdded{0}, sinkRemoved{0};
    std::atomic<int> watcherStarted{0}, watcherStopped{0};
    std::atomic<int> busConnected{0}, busDisconnected{0};
    std::atomic<int> serialOpened{0}, serialClosedInDetach{0};
    // The fixture publishes its FakeSerialTransport so an EXE helper thread can keep
    // pushing bytes into it right through Disconnect/Shutdown (bytes arriving DURING
    // the teardown); the handshake below stops the pusher before the transport dies.
    std::atomic<void*> fake{nullptr};
    std::atomic<int> serialStopPushing{0}, serialPusherStopped{0};
    std::atomic<long long> serialBytesPushed{0}, serialBytesPushedDuringDetach{0};
    std::atomic<long long> serialCloseMs{0};      // Disconnect+Shutdown duration in OnDetach
    std::atomic<long long> detachMs{0};           // whole OnDetach duration
    std::atomic<int> fakeHandlesAtDetachEnd{-1};  // FakeSerialTransport counters at detach end
    std::atomic<int> fakeReadsAtDetachEnd{-1};

    // ---- process-wide resource supplement ----
    std::atomic<long long> baselineHandles{0}, warmedHandles{0}, postHandles{0};
    std::atomic<long long> baselineThreads{0}, warmedThreads{0}, postThreads{0};

    // ---- host/driver bookkeeping ----
    std::atomic<int> failures{0};
    std::atomic<long long> closeRequestTick{0}, closeDoneTick{0};   // mode 1: WM_CLOSE -> Run() returned
};
