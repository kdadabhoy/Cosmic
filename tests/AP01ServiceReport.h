#pragma once
#include <atomic>

// AP01ServiceReport — AP-01 V02 (W): the exe-owned observation record shared with
// AP01ServiceFixture.dll (handed over by pointer through the COSMIC_AP01_REPORT env
// var, the WO-07 F-LIFETIME pattern). It OUTLIVES every load of the DLL: after each
// FreeLibrary the module's code is gone but every counter and sequence stamp it wrote
// survives for the host to assert. `seq` is the ONE monotonic sequence both sides
// stamp, so "OnDetach < ~Service < DLL unmapped < host observed unload" is provable.
struct AP01ServiceReport
{
    std::atomic<long long> seq{0};

    // --- DLL image lifetime (stamped by a static object in the fixture) ---
    std::atomic<int>       dllLoaded{0};
    std::atomic<int>       dllUnloaded{0};
    std::atomic<long long> seqDllDetach{0};      // the fixture's static destructor (DLL_PROCESS_DETACH, inside FreeLibrary)

    // --- service lifecycle (per load; the host snapshots after every cycle) ---
    std::atomic<int>       constructed{0};
    std::atomic<int>       attached{0};
    std::atomic<int>       detached{0};
    std::atomic<int>       destroyed{0};
    std::atomic<long long> seqAttach{0};
    std::atomic<long long> seqDetach{0};
    std::atomic<long long> seqDeleted{0};

    // --- what the service saw at OnAttach ---
    std::atomic<int>       attachInEditor{0};
    std::atomic<int>       attachHadFlow{0};
    std::atomic<int>       attachHadScene{0};
    std::atomic<int>       reloadsSeen{0};        // Bus().GetNumber("ap01.reloads") read at OnAttach (bus persistence proof)
    std::atomic<int>       panelRegistered{0};    // Panels().Has("AP01Panel") right after CS_PANEL

    // --- callbacks ---
    std::atomic<int>       updates{0};
    std::atomic<int>       fixedUpdates{0};
    std::atomic<int>       events{0};
    std::atomic<int>       sceneChanges{0};
    std::atomic<int>       producerSeen{0};       // Bus().Producer("ap01.tick") == "AP01Service" observed INSIDE OnUpdate
    std::atomic<int>       panelDraws{0};
    std::atomic<int>       signalsWhileLive{0};
    std::atomic<int>       signalsAfterUnload{0}; // must stay 0
    std::atomic<int>       pingWhileLive{0};      // the service's own DataBus subscription
    std::atomic<int>       pingAfterUnload{0};    // must stay 0
    std::atomic<int>       unloaded{0};           // set by OnDetach's tail: any callback after this is a defect
    std::atomic<int>       scriptUpdates{0};      // the module's CS_SCRIPT ticked (PlayerLayer path with a scene only)
};
