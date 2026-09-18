// test_wo07_host.cpp — L01 (2D stability): runtime-plugin (F-LIFETIME) teardown.
//
// Skipped by default (a real Application + window + GL context). The WO-04 runner
// launches it as fresh child processes; each child loads WO07LifetimeFixture.dll into
// a real Application and exercises ONE runtime load/unload:
//   * case 0 (reload): warm up, then Application::TransitionToLauncher() — the real
//     UnloadProjectDLL path — then run quiescence frames in the launcher and fire a
//     post-unload EntitySelection change to prove the fixture's listener is gone.
//   * case 1 (fresh launch/close): warm up, then close the window; Shutdown unloads it.
// After Run() the host asserts: OnDetach ran before the layer destructor (seqDetach <
// seqDestroy); every owned resource created was released; no listener fired after
// unload; and the host's own listener DID see the post-unload emit (non-vacuous).
#include <doctest.h>
#include "WO07LifetimeReport.h"
#include "WO05NativeWindow.h"

#include "core/Application.h"
#include "core/Layer.h"
#include "telemetry/EntitySelection.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace
{
    long long HandleCount()
    {
        DWORD c = 0; GetProcessHandleCount(GetCurrentProcess(), &c); return (long long)c;
    }
    long long ThreadCount()
    {
        long long n = 0; HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (s == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te{}; te.dwSize = sizeof(te); const DWORD pid = GetCurrentProcessId();
        if (Thread32First(s, &te)) do { if (te.th32OwnerProcessID == pid) ++n; } while (Thread32Next(s, &te));
        CloseHandle(s); return n;
    }

    // Survives the plugin unload (it is exe code). Drives the post-unload phase.
    class Driver final : public Cosmic::Layer
    {
        WO07LifetimeReport& rep;
        int phase = 0, quiesce = 0;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        void Close()
        {
            Cosmic::WindowCloseEvent e;
            Cosmic::Application::Get().OnEvent(e);
        }
    public:
        explicit Driver(WO07LifetimeReport& r) : Cosmic::Layer("WO07 driver"), rep(r) {}
        void OnUpdate(float) override
        {
            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(15))
            {
                if (!rep.detached) ++rep.failures;
                Close();
                return;
            }
            if (rep.freshLaunchOnly)
                return;   // the fixture closes the window itself after warmup
            switch (phase)
            {
            case 0: if (rep.detached) phase = 1; break;                 // wait for unload
            case 1: if (++quiesce > 12) phase = 2; break;              // let owned workers quiesce
            case 2:
                // Post-unload emit: the host listener must fire; the (unsubscribed,
                // freed) fixture listener must not — and must not crash.
                Cosmic::EntitySelection::SetByName("wo07-post-unload");
                rep.postHandles = HandleCount();
                rep.postThreads = ThreadCount();
                phase = 3;
                Close();
                break;
            default: break;
            }
        }
    };

    void RunLifetime(bool freshLaunch)
    {
        WO07LifetimeReport rep;
        rep.freshLaunchOnly = freshLaunch ? 1 : 0;

        // The host's own EntitySelection listener — proves the post-unload emit is real.
        auto hostSub = Cosmic::EntitySelection::OnChanged(
            [&rep](const std::string&, const std::string&) { ++rep.hostProbeFired; });

        char ptr[32];
        std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)&rep);
        _putenv_s("COSMIC_WO07_LIFETIME_REPORT", ptr);
        {
            wchar_t exePath[MAX_PATH]{};
            REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
            const auto fixture = std::filesystem::path(exePath).parent_path() / "WO07LifetimeFixture.dll";
            Cosmic::Application app(fixture.string());
            app.GetWindow().SetVSync(false);
            if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
            // Warmed baseline: the app is up and the fixture is attached, but its lazy
            // GPU/job/watcher resources are only created on the first Run() frame — so
            // this is the steady state the scenario-owned resources are measured against.
            rep.baselineHandles = HandleCount();
            rep.baselineThreads = ThreadCount();
            app.PushLayer(new Driver(rep));
            app.Run();
            // Unsubscribe the host listener while the Application (and the global
            // EntitySelection service it lives beside) is still alive.
            Cosmic::EntitySelection::Unsubscribe(hostSub);
        }
        _putenv_s("COSMIC_WO07_LIFETIME_REPORT", "");

        // --- teardown contract ---
        CHECK(rep.failures == 0);
        CHECK(rep.attached == 1);
        CHECK(rep.detached == 1);
        CHECK(rep.destroyed == 1);
        // OnDetach ran before the module-owned destructors, which ran before the host
        // returned from Run() (i.e. before FreeLibrary completed).
        CHECK(rep.seqDetach > 0);
        CHECK(rep.seqDestroy > rep.seqDetach);
        // Every owned resource released.
        CHECK(rep.texCreated == 1);  CHECK(rep.texFreed == rep.texCreated);
        CHECK(rep.fboCreated == 1);  CHECK(rep.fboFreed == rep.fboCreated);
        CHECK(rep.componentDestroyed == 1);
        CHECK(rep.sinkAdded == 1);   CHECK(rep.sinkRemoved == rep.sinkAdded);
        CHECK(rep.listenerSubscribed == 1); CHECK(rep.listenerUnsubscribed == rep.listenerSubscribed);
        CHECK(rep.jobSubmitted == 1); CHECK(rep.jobRan == rep.jobSubmitted);
        CHECK(rep.watcherStarted == 1); CHECK(rep.watcherStopped == rep.watcherStarted);
        // No callback after unload; the live baseline fired.
        CHECK(rep.listenerFiredWhileLive == 2);
        CHECK(rep.listenerFiredAfterUnload == 0);
        if (!freshLaunch)
        {
            CHECK(rep.hostProbeFired >= 3);   // 2 live + 1 post-unload
            // Scenario-owned OS threads return to the warmed baseline after quiescence:
            // warmup adds the file-watcher worker; OnDetach joins it. Thread count is
            // deterministic (unlike handle count, which finite GL/driver caches make
            // noisy — logged separately below, asserted only not to have grown).
            CHECK(rep.warmedThreads > rep.baselineThreads);      // the watcher thread showed up
            CHECK(rep.postThreads == rep.baselineThreads);       // and went away after quiescence
            CHECK(rep.postHandles <= rep.warmedHandles);         // no handle growth across the cycle
        }

        std::printf("WO07 lifetime: fresh=%d handles base=%lld warm=%lld post=%lld threads base=%lld warm=%lld post=%lld\n",
                    freshLaunch ? 1 : 0,
                    rep.baselineHandles.load(), rep.warmedHandles.load(), rep.postHandles.load(),
                    rep.baselineThreads.load(), rep.warmedThreads.load(), rep.postThreads.load());
    }
}

TEST_CASE("WO-07 L01 host: runtime plugin load/unload teardown (reload path)" * doctest::skip())
{
    int fresh = 0;
    char* sel = nullptr; size_t n = 0;
    _dupenv_s(&sel, &n, "COSMIC_WO07_LIFETIME_CASE");
    if (sel) { fresh = (sel[0] == '1') ? 1 : 0; free(sel); }
    RunLifetime(fresh != 0);
}

namespace
{
    // Counts frames the (recovered) host actually served, then closes it. If the app
    // reaches N live frames it is a functional launcher, not a crashed/hung half-load.
    class FrameCounter final : public Cosmic::Layer
    {
        int& m_Frames; int m_Limit;
    public:
        FrameCounter(int& frames, int limit) : Cosmic::Layer("WO07 L03 driver"), m_Frames(frames), m_Limit(limit) {}
        void OnUpdate(float) override
        {
            if (++m_Frames >= m_Limit)
            {
                Cosmic::WindowCloseEvent e;
                Cosmic::Application::Get().OnEvent(e);
            }
        }
    };
}

// L03 — a broken runtime-plugin load must be recoverable: Application::LoadProjectDLL
// rejects it (logs, FreeLibrary, no active layer, no stale handle) and the app falls
// back to a live launcher. Cases (COSMIC_WO07_L03_CASE): 0 missing DLL, 1 a DLL missing
// the engine export signatures, 2 a DLL whose CreatePluginLayer returns nullptr.
TEST_CASE("WO-07 L03 host: broken plugin load is recoverable" * doctest::skip())
{
    int which = 0;
    char* sel = nullptr; size_t n = 0;
    _dupenv_s(&sel, &n, "COSMIC_WO07_L03_CASE");
    if (sel) { which = std::atoi(sel); free(sel); }

    wchar_t exePath[MAX_PATH]{};
    REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
    const auto dir = std::filesystem::path(exePath).parent_path();
    std::string plugin;
    switch (which)
    {
    case 0: plugin = (dir / "wo07-does-not-exist.dll").string(); break;  // missing DLL
    case 1: plugin = (dir / "WO07NoExport.dll").string(); break;         // missing exports
    default:                                                             // null CreatePluginLayer
        plugin = (dir / "WO07LifetimeFixture.dll").string();
        _putenv_s("COSMIC_WO07_LIFETIME_REPORT", "");                    // unset -> returns nullptr
        break;
    }
    CAPTURE(which); CAPTURE(plugin);

    int frames = 0;
    {
        Cosmic::Application app(plugin);   // must NOT throw — the failure is handled internally
        app.GetWindow().SetVSync(false);
        if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
        app.PushLayer(new FrameCounter(frames, 5));
        app.Run();                          // a live launcher runs, then closes cleanly
    }
    CHECK(frames >= 5);                      // the host stayed live and functional after the bad load
    std::printf("WO07 L03 case=%d frames=%d recovered\n", which, frames);
}
