// test_wo07_l04.cpp — L04 (2D stability): teardown during LIVE background activity.
//
// Skipped by default (a real Application + window + GL context); the WO-04 runner
// launches each case as a fresh isolated child. Two hosts:
//
//   * "teardown during live background activity" (COSMIC_WO07_L04_CASE = mode):
//     WO07TeardownFixture.dll owns a barrier-blocked (genuinely in-flight) job, a
//     file watcher the host writes into every frame, a connected SerialLink over the
//     WO-04 FakeSerialTransport with the host streaming bytes every millisecond, an
//     EntitySelection listener + log sink + hotkey override the host fires every
//     frame, a Scene EventBus, a texture, a framebuffer and a module-owned object.
//     The transition is requested from INSIDE a selection dispatch (deferred):
//       mode 0 — TransitionToLauncher (the real UnloadProjectDLL path);
//       mode 1 — WM_CLOSE while the job is blocked (full Shutdown; the exe releases
//                the barrier 200 ms later);
//       mode 2 — the careless plugin: OnDetach never joins its job; the exe releases
//                the barrier 300 ms AFTER detach — the engine must still not let a
//                worker run into unmapped code.
//     After Run() the host asserts ordering (requested-inside-dispatch < dispatch
//     returned < OnDetach < destructor), the job finished before the code went away,
//     every channel was busy while live and silent after unload (while the same
//     paths still reach exe listeners), a listener removed mid-dispatch did not fire,
//     owned resources balance, and scenario threads return to the warmed baseline.
//
//   * "module registry entries are gone before FreeLibrary": a REAL CS_MODULE module
//     (WO07ModuleFixture.dll, hosted by the real PlayerLayer) is loaded, unloaded,
//     loaded again and unloaded; its ModuleRegistry/Reflect entries — code in that
//     DLL — must be gone after each unload (KI-29, runtime path).
#include <doctest.h>
#include "WO07TeardownReport.h"
#include "WO05NativeWindow.h"
#include "FakeSerialTransport.h"

#include "core/Application.h"
#include "core/Layer.h"
#include "core/Log.h"
#include "codes/KeyCodes.h"
#include "jobs/JobSystem.h"
#include "telemetry/EntitySelection.h"
#include "reflect/TypeRegistry.h"
#include "scripting/ModuleRegistry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace
{
    long long L04HandleCount()
    {
        DWORD c = 0; GetProcessHandleCount(GetCurrentProcess(), &c); return (long long)c;
    }
    long long L04ThreadCount()
    {
        long long n = 0; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (s == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te{}; te.dwSize = sizeof(te); const DWORD pid = GetCurrentProcessId();
        if (Thread32First(s, &te)) do { if (te.th32OwnerProcessID == pid) ++n; } while (Thread32Next(s, &te));
        CloseHandle(s); return n;
    }

    // The exe-side driver: keeps EVERY channel busy (selection dispatches, engine log
    // lines, a hotkey, files into the watched directory, serial bytes) before, during
    // and after the unload; pulls the trigger from inside a selection dispatch; and
    // keeps hammering for a quiescence window after the fixture is gone so a stale
    // callback would have to fire (or crash).
    class L04Driver final : public Cosmic::Layer
    {
        WO07TeardownReport& rep;
        HWND hwnd = nullptr;
        int frame = 0, quiesce = 0;
        bool triggered = false, done = false;
        std::filesystem::path watchDir;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::thread pusher, releaser;
        std::atomic<bool> stopThreads{false};

        void Close()
        {
            Cosmic::WindowCloseEvent e;
            Cosmic::Application::Get().OnEvent(e);
        }
        void PostKey(int vk)
        {
            if (!hwnd) return;
            const UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
            PostMessage(hwnd, WM_KEYDOWN, (WPARAM)vk, (LPARAM)((sc << 16) | 1));
            PostMessage(hwnd, WM_KEYUP,   (WPARAM)vk, (LPARAM)((sc << 16) | 1 | (1u << 30) | (1u << 31)));
        }
        void Hammer()
        {
            std::ofstream((watchDir / ("f" + std::to_string(frame) + ".txt")).string(), std::ios::trunc) << frame;
            Cosmic::EntitySelection::SetByName((frame % 3 == 0) ? "l04-probe" : ("l04-" + std::to_string(frame)));
            CS_INFO("WO07 L04 tick {0}", frame);
            PostKey(VK_F9);
        }
    public:
        explicit L04Driver(WO07TeardownReport& r) : Cosmic::Layer("WO07 L04 driver"), rep(r) {}
        ~L04Driver() override
        {
            stopThreads = true;
            if (pusher.joinable()) pusher.join();
            if (releaser.joinable()) releaser.join();
        }
        void OnAttach() override
        {
            hwnd = WO05NativeWindow(Cosmic::Application::Get().GetWindow());
            std::error_code ec;
            watchDir = std::filesystem::temp_directory_path(ec) / "wo07-l04-watch";
            std::filesystem::create_directories(watchDir, ec);

            // Serial pusher: streams a line into the fixture's fake transport every
            // millisecond from the moment the fixture publishes it until the fixture's
            // teardown tells it to stop — bytes keep arriving DURING Disconnect/Shutdown.
            pusher = std::thread([this]
            {
                while (!stopThreads && !rep.serialStopPushing)
                {
                    if (auto* fake = static_cast<Cosmic::FakeSerialTransport*>(rep.fake.load()))
                    {
                        fake->PushBytes("$R,25,1680,420,120,350*5D\n");
                        ++rep.serialBytesPushed;
                        if (rep.seqDetachBegin != 0) ++rep.serialBytesPushedDuringDetach;
                        std::this_thread::yield();   // tight: thousands of lines/s so bytes DO land inside OnDetach
                    }
                    else Sleep(1);
                }
                rep.serialPusherStopped = 1;
            });
            // Barrier releaser (modes 1 and 2): the EXE releases the blocked job on a
            // schedule the fixture does not control.
            releaser = std::thread([this]
            {
                if (rep.mode == 0) return;   // mode 0: OnDetach itself releases the job
                while (!stopThreads)
                {
                    if (rep.mode == 1 && rep.closeRequestTick != 0) { Sleep(200); SetEvent(rep.jobBarrier); return; }
                    if (rep.mode == 2 && rep.detached != 0)         { Sleep(300); SetEvent(rep.jobBarrier); return; }
                    Sleep(1);
                }
            });
        }
        void OnUpdate(float) override
        {
            if (done) return;
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(25))
            {
                if (!rep.detached) ++rep.failures;
                SetEvent(rep.jobBarrier);   // never let a failed run deadlock JobSystem::Shutdown
                done = true; Close(); return;
            }
            ++frame;
            if (!rep.detached)
            {
                Hammer();
                // Pull the trigger once everything is warmed: from INSIDE the selection
                // dispatch the fixture requests the transition (deferred) / posts WM_CLOSE.
                if (!triggered && frame >= 20 && rep.serialOpened && WaitForSingleObject(rep.jobEntered, 0) == WAIT_OBJECT_0)
                {
                    triggered = true;
                    Cosmic::EntitySelection::SetByName("l04-go");
                    rep.seqCallbackReturned = ++rep.seq;
                }
                return;
            }
            // ---- after the unload (modes 0 and 2; in mode 1 Run() has already returned) ----
            if (rep.seqAfterUnload == 0)
            {
                rep.seqAfterUnload = ++rep.seq;
                rep.jobActiveAfterUnload = (int)(Cosmic::JobSystem::Get().GetActiveCount() + Cosmic::JobSystem::Get().GetQueuedCount());
                // The hotkey path must still dispatch — into EXE code now (non-vacuous).
                Cosmic::Application::Get().GetWindow().SetFullscreenHotkeyOverride(
                    [this](int key, int action, int) -> bool
                    {
                        if (key == CS_KEY_F9 && action == 1) { ++rep.hostHotkeyAfterUnload; return true; }
                        return false;
                    });
            }
            Hammer();   // stale fixture callbacks would fire (or crash) here
            if (++quiesce >= 12)
            {
                rep.postHandles = L04HandleCount();
                rep.postThreads = L04ThreadCount();
                Cosmic::Application::Get().GetWindow().ClearFullscreenHotkeyOverride();
                done = true; Close();
            }
        }
    };

    void RunTeardown(int mode)
    {
        WO07TeardownReport rep;
        rep.mode = mode;
        rep.jobBarrier = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        rep.jobEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        rep.jobDone    = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        // Host-side probes: the same dispatch paths still reach EXE listeners after unload.
        auto hostSel = Cosmic::EntitySelection::OnChanged([&rep](const std::string&, const std::string&) { if (rep.unloaded) ++rep.hostSelectionAfterUnload; });
        auto hostSink = std::make_shared<Cosmic::CallbackSink>([&rep](spdlog::level::level_enum, const std::string&) { if (rep.unloaded) ++rep.hostSinkAfterUnload; });
        Cosmic::Log::AddSink(hostSink);

        char ptr[32];
        std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)&rep);
        _putenv_s("COSMIC_WO07_TEARDOWN_REPORT", ptr);
        long long closeDone = 0;
        {
            wchar_t exePath[MAX_PATH]{};
            REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
            const auto fixture = std::filesystem::path(exePath).parent_path() / "WO07TeardownFixture.dll";
            Cosmic::Application app(fixture.string());
            app.GetWindow().SetVSync(false);
            if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
            rep.baselineHandles = L04HandleCount();
            rep.baselineThreads = L04ThreadCount();
            app.PushLayer(new L04Driver(rep));
            app.Run();
        }   // ~Application: Shutdown (JobSystem drain, UnloadProjectDLL, window) completes here
        closeDone = (long long)GetTickCount64();
        rep.closeDoneTick = closeDone;
        Cosmic::EntitySelection::Unsubscribe(hostSel);
        Cosmic::Log::RemoveSink(hostSink);
        _putenv_s("COSMIC_WO07_TEARDOWN_REPORT", "");

        CAPTURE(mode);
        // ---- lifecycle + deferred-transition ordering ----
        CHECK(rep.failures == 0);
        CHECK(rep.attached == 1); CHECK(rep.detached == 1); CHECK(rep.destroyed == 1);
        CHECK(rep.transitionRequestedFromCallback == 1);
        CHECK(rep.seqTransitionRequested > 0);
        CHECK(rep.seqTransitionRequested < rep.seqCallbackReturned);   // requested INSIDE the dispatch
        CHECK(rep.seqCallbackReturned < rep.seqDetachBegin);           // applied later (deferred), not re-entrantly
        CHECK(rep.seqDetachBegin < rep.seqDetachEnd);
        CHECK(rep.seqDetachEnd < rep.seqDestroy);
        // ---- the in-flight job ----
        CHECK(rep.jobSubmitted == 1); CHECK(rep.jobRan == 1);
        CHECK(rep.seqJobEntered > 0);
        if (mode == 0)
        {
            CHECK(rep.jobInFlightAtDetach == 1);          // blocked on the barrier when OnDetach began
            CHECK(rep.jobJoinedInDetach == 1);            // OnDetach released it and waited
            CHECK(rep.seqJobDone < rep.seqDetachEnd);     // finished before the plugin's code could go away
        }
        if (mode == 1)
        {
            CHECK(rep.jobInFlightAtClose == 1);           // blocked when WM_CLOSE was posted
            CHECK(rep.seqJobDone < rep.seqDetachBegin);   // JobSystem drained before the unload
            CHECK(rep.closeDoneTick - rep.closeRequestTick <= 2000);   // close bar (2 s)
        }
        if (mode == 2)
        {
            // The careless plugin never joined: the ENGINE must still have let the job
            // finish before unmapping the DLL (else the worker ran into freed code).
            CHECK(rep.seqJobDone > 0);
            CHECK(rep.seqJobDone < rep.seqAfterUnload);
        }
        if (mode != 1) CHECK(rep.jobActiveAfterUnload == 0);
        // ---- callbacks: busy while live, silent after unload, paths still alive ----
        CHECK(rep.selectionWhileLive >= 5); CHECK(rep.sinkWhileLive >= 5); CHECK(rep.hotkeyWhileLive >= 3);
        CHECK(rep.busWhileLive >= 5);       CHECK(rep.watchEventsWhileLive >= 1);
        CHECK(rep.serialOpened == 1);       CHECK(rep.serialBytesWhileLive >= 1);
        CHECK(rep.selectionAfterUnload == 0); CHECK(rep.sinkAfterUnload == 0); CHECK(rep.hotkeyAfterUnload == 0);
        if (mode != 1)
        {
            CHECK(rep.hostSelectionAfterUnload >= 5); CHECK(rep.hostSinkAfterUnload >= 5); CHECK(rep.hostHotkeyAfterUnload >= 3);
        }
        // ---- disconnect during dispatch: a removed listener must not fire ----
        CHECK(rep.esProbeDispatches >= 2);
        CHECK(rep.esRemovedMidDispatchFired == 0);
        CHECK(rep.busRemovedMidDispatchFired == 0);
        // ---- owned resources balanced (module code did all of it before FreeLibrary) ----
        CHECK(rep.texCreated == 1); CHECK(rep.texFreed == 1); CHECK(rep.fboCreated == 1); CHECK(rep.fboFreed == 1);
        CHECK(rep.componentDestroyed == 1);
        CHECK(rep.sinkAdded == rep.sinkRemoved);
        CHECK(rep.listenerSubscribed == rep.listenerUnsubscribed);
        CHECK(rep.watcherStarted == 1); CHECK(rep.watcherStopped == 1);
        CHECK(rep.busConnected == rep.busDisconnected);
        CHECK(rep.serialClosedInDetach == 1);
        CHECK(rep.serialPusherStopped == 1);
        CHECK(rep.fakeHandlesAtDetachEnd == 0); CHECK(rep.fakeReadsAtDetachEnd == 0);
        CHECK(rep.serialCloseMs <= 2000); CHECK(rep.detachMs <= 2000);
        // ---- process-wide: scenario threads back to the warmed baseline ----
        if (mode != 1)
        {
            CHECK(rep.warmedThreads > rep.baselineThreads);   // watcher/serial workers showed up
            CHECK(rep.postThreads == rep.baselineThreads);    // and are gone after quiescence
            CHECK(rep.postHandles <= rep.warmedHandles);
        }
        std::printf("WO07 L04 mode=%d detachMs=%lld serialCloseMs=%lld bytesPushed=%lld duringDetach=%lld live[sel=%d sink=%d hotkey=%d bus=%d watch=%d serialBytes=%d] esProbe=%d esStale=%d busStale=%d threads base=%lld warm=%lld post=%lld handles base=%lld warm=%lld post=%lld close=%lldms\n",
                    mode, rep.detachMs.load(), rep.serialCloseMs.load(), rep.serialBytesPushed.load(), rep.serialBytesPushedDuringDetach.load(),
                    rep.selectionWhileLive.load(), rep.sinkWhileLive.load(), rep.hotkeyWhileLive.load(), rep.busWhileLive.load(),
                    rep.watchEventsWhileLive.load(), rep.serialBytesWhileLive.load(), rep.esProbeDispatches.load(),
                    rep.esRemovedMidDispatchFired.load(), rep.busRemovedMidDispatchFired.load(),
                    rep.baselineThreads.load(), rep.warmedThreads.load(), rep.postThreads.load(),
                    rep.baselineHandles.load(), rep.warmedHandles.load(), rep.postHandles.load(),
                    mode == 1 ? (rep.closeDoneTick - rep.closeRequestTick) : 0LL);

        CloseHandle(rep.jobBarrier); CloseHandle(rep.jobEntered); CloseHandle(rep.jobDone);
    }
}

TEST_CASE("WO-07 L04 host: teardown during live background activity" * doctest::skip())
{
    int mode = 0;
    char* sel = nullptr; size_t n = 0;
    _dupenv_s(&sel, &n, "COSMIC_WO07_L04_CASE");
    if (sel) { mode = std::atoi(sel); free(sel); }
    RunTeardown(mode);
}

namespace
{
    // Drives a real PlayerLayer-hosting module through load -> frames -> unload, then
    // load again -> unload, checking the registry after each unload.
    class L04ModuleDriver final : public Cosmic::Layer
    {
        int& frames; int pass = 0; int wait = 0;
        std::string dll;
        bool* cleanAfterFirst; bool* cleanAfterSecond; bool* reloaded; bool* wasLive;
        static bool RegistryClean()
        {
            return Cosmic::ModuleRegistry::Get().FindScript("WO07ModScript") == nullptr &&
                   Cosmic::Reflect::GetRegistry().FindByName("WO07ModComponent") == nullptr &&
                   Cosmic::ModuleRegistry::Get().ComponentTypeIds("WO07ModuleFixture").empty();
        }
        static bool RegistryLive()
        {
            return Cosmic::ModuleRegistry::Get().FindScript("WO07ModScript") != nullptr &&
                   Cosmic::Reflect::GetRegistry().FindByName("WO07ModComponent") != nullptr;
        }
    public:
        L04ModuleDriver(int& f, std::string d, bool* a, bool* b, bool* r, bool* w)
            : Cosmic::Layer("WO07 L04 module driver"), frames(f), dll(std::move(d)), cleanAfterFirst(a), cleanAfterSecond(b), reloaded(r), wasLive(w) {}
        void OnUpdate(float) override
        {
            ++frames;
            auto& app = Cosmic::Application::Get();
            const bool live = RegistryLive();
            switch (pass)
            {
            case 0: if (live) *wasLive = true; if (frames >= 5 && live) { app.TransitionToLauncher(); pass = 1; } break;
            case 1: if (!live || frames >= 30) { *cleanAfterFirst = RegistryClean(); if (++wait >= 3) { app.TransitionFromLauncherToWorkspace(dll); pass = 2; wait = 0; } } break;
            case 2: if (live) { *reloaded = true; if (++wait >= 8) { app.TransitionToLauncher(); pass = 3; wait = 0; } } break;
            case 3: if (!live || ++wait >= 30) { *cleanAfterSecond = RegistryClean(); Cosmic::WindowCloseEvent e; app.OnEvent(e); pass = 4; } break;
            default: break;
            }
            if (frames > 600) { Cosmic::WindowCloseEvent e; app.OnEvent(e); }
        }
    };
}

// L04 (runtime-path registry lifecycle, KI-29): a real CS_MODULE game module hosted by
// the real PlayerLayer must have its ModuleRegistry + Reflect entries removed BEFORE
// FreeLibrary, and a second load of the same DLL must register cleanly again.
TEST_CASE("WO-07 L04 host: module registry entries are gone before FreeLibrary and reload cleanly" * doctest::skip())
{
    wchar_t exePath[MAX_PATH]{};
    REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
    const auto dll = (std::filesystem::path(exePath).parent_path() / "WO07ModuleFixture.dll").string();
    int frames = 0; bool cleanAfterFirst = false, cleanAfterSecond = false, reloaded = false, wasLive = false;
    {
        Cosmic::Application app(dll);
        app.GetWindow().SetVSync(false);
        if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
        // The startup DLL mounts in the first Safe Zone of Run(), so the driver observes the
        // registry going live (wasLive) before it requests the first unload.
        app.PushLayer(new L04ModuleDriver(frames, dll, &cleanAfterFirst, &cleanAfterSecond, &reloaded, &wasLive));
        app.Run();
    }
    CHECK(wasLive);            // CreatePluginLayer registered the module (non-vacuous)
    CHECK(cleanAfterFirst);    // no factory/thunk into the unloaded DLL survives the first unload
    CHECK(reloaded);           // the same DLL loads and registers again
    CHECK(cleanAfterSecond);   // and is clean again after the second unload
    CHECK(Cosmic::ModuleRegistry::Get().FindScript("WO07ModScript") == nullptr);
    CHECK(Cosmic::Reflect::GetRegistry().FindByName("WO07ModComponent") == nullptr);
    std::printf("WO07 L04 module registry: frames=%d cleanAfterFirst=%d reloaded=%d cleanAfterSecond=%d\n",
                frames, cleanAfterFirst ? 1 : 0, reloaded ? 1 : 0, cleanAfterSecond ? 1 : 0);
}
