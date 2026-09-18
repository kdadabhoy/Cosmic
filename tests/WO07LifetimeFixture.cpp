// WO07LifetimeFixture.cpp — L01 (2D stability): the F-LIFETIME runtime plugin.
//
// A real project DLL (loaded by the real Application, adopting the host's ImGui/ImPlot
// contexts) that owns one of every resource class a plugin can leak — a GPU texture, a
// GPU framebuffer, a module-owned component object, an EntitySelection listener, a log
// sink, a JobSystem job, and a file watcher — and records their create/release into an
// exe-owned report (so the counts survive FreeLibrary). It exercises the REAL teardown
// path: the host either returns to the launcher (Application::TransitionToLauncher →
// UnloadProjectDLL) or closes the window (full Shutdown → UnloadProjectDLL), each of
// which runs OnDetach → delete layer → FreeLibrary — the runtime-plugin lifecycle L01
// asserts (OnDetach + destructors before FreeLibrary and GL-context loss; owned
// resources balanced; no callback after unload).
#include "WO07LifetimeReport.h"
#include "WO05NativeWindow.h"

#include <Cosmic.h>
#include "core/Log.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#include "jobs/JobSystem.h"
#include "telemetry/EntitySelection.h"
#include "utils/FileWatcher.h"

#include <imgui.h>
#include <implot.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <atomic>
#include <filesystem>
#include <memory>

namespace
{
    std::atomic<long long> g_Seq{0};
    WO07LifetimeReport* report = nullptr;

    long long HandleCount()
    {
        DWORD c = 0; GetProcessHandleCount(GetCurrentProcess(), &c); return (long long)c;
    }
    long long ThreadCount()
    {
        long long n = 0; HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te{}; te.dwSize = sizeof(te); const DWORD pid = GetCurrentProcessId();
        if (Thread32First(snap, &te)) do { if (te.th32OwnerProcessID == pid) ++n; } while (Thread32Next(snap, &te));
        CloseHandle(snap); return n;
    }

    // The module-owned "component": its destructor MUST run against valid module code,
    // i.e. before FreeLibrary. It bumps the exe-owned report from the DLL's dtor.
    struct OwnedComponent
    {
        WO07LifetimeReport* r;
        explicit OwnedComponent(WO07LifetimeReport* rep) : r(rep) {}
        ~OwnedComponent() { ++r->componentDestroyed; }
    };

    class LifetimeLayer final : public Cosmic::Layer
    {
        int  m_Frame = 0;
        bool m_Warmed = false;
        Cosmic::Ref<Cosmic::Texture2D>   m_Tex;
        Cosmic::Ref<Cosmic::FrameBuffer> m_Fbo;
        std::unique_ptr<OwnedComponent>  m_Component;
        std::shared_ptr<Cosmic::CallbackSink> m_Sink;
        Cosmic::FileWatcher m_Watcher;
        Cosmic::EntitySelection::SubscriptionHandle m_Listener = 0;

    public:
        LifetimeLayer() : Cosmic::Layer("WO07 F-LIFETIME") {}

        void OnAttach() override
        {
            // Cheap, no-GL resources register here (this runs INSIDE LoadProjectDLL,
            // during Application construction). The GPU/threaded resources are created
            // lazily on the first frame so the host can read a pre-resource baseline.
            m_Sink = std::make_shared<Cosmic::CallbackSink>(
                [](spdlog::level::level_enum, const std::string&) {});
            Cosmic::Log::AddSink(m_Sink);
            ++report->sinkAdded;

            m_Listener = Cosmic::EntitySelection::OnChanged(
                [](const std::string&, const std::string&)
                {
                    if (report->unloaded) ++report->listenerFiredAfterUnload;
                    else                  ++report->listenerFiredWhileLive;
                });
            ++report->listenerSubscribed;

            ++report->attached;
        }

        void OnUpdate(float) override
        {
            auto& app = Cosmic::Application::Get();
            if (m_Frame == 0)
            {
                m_Tex = Cosmic::Texture2D::Create(64, 64);
                ++report->texCreated;
                Cosmic::FramebufferSpecification spec; spec.Width = 128; spec.Height = 128;
                m_Fbo = Cosmic::FrameBuffer::Create(spec);
                ++report->fboCreated;
                m_Component = std::make_unique<OwnedComponent>(report);

                Cosmic::JobSystem::Get().Submit([] { ++report->jobRan; });
                ++report->jobSubmitted;
                // Settle the job now, while the pool is definitely alive — the engine
                // tears the JobSystem down BEFORE it unloads the project DLL, so a
                // WaitIdle() in OnDetach could race a dead pool on the full-shutdown path.
                Cosmic::JobSystem::Get().WaitIdle();

                // Watch a fresh, QUIET directory — not the volatile OS temp root the app
                // itself writes to during shutdown, whose change events would race the
                // watcher's own teardown.
                std::error_code ec;
                const auto dir = std::filesystem::temp_directory_path(ec) / "wo07-lifetime-watch";
                std::filesystem::create_directories(dir, ec);
                if (m_Watcher.Watch(dir.string(), /*recursive=*/false) && m_Watcher.IsWatching())
                    ++report->watcherStarted;

                // Fire the listener a few times WHILE LIVE (non-vacuous baseline).
                Cosmic::EntitySelection::SetByName("wo07-live-1");
                Cosmic::EntitySelection::SetByName("wo07-live-2");
            }
            if (m_Frame == 3 && !m_Warmed)
            {
                m_Warmed = true;
                report->warmedHandles = HandleCount();
                report->warmedThreads = ThreadCount();

                if (report->freshLaunchOnly)
                {
                    HWND hwnd = WO05NativeWindow(app.GetWindow());   // fresh launch/close
                    if (!hwnd || !PostMessage(hwnd, WM_CLOSE, 0, 0)) ++report->failures;
                }
                else
                {
                    app.TransitionToLauncher();                       // reload path
                }
            }
            ++m_Frame;
        }

        void OnDetach() override
        {
            report->seqDetach = ++g_Seq;

            // Release every owned resource while module code + the GL context are live.
            if (m_Watcher.IsWatching()) { m_Watcher.Stop(); ++report->watcherStopped; }
            if (m_Sink) { Cosmic::Log::RemoveSink(m_Sink); m_Sink.reset(); ++report->sinkRemoved; }
            if (m_Listener) { Cosmic::EntitySelection::Unsubscribe(m_Listener); m_Listener = 0; ++report->listenerUnsubscribed; }
            if (m_Tex) { m_Tex.reset(); ++report->texFreed; }   // GL delete while context live
            if (m_Fbo) { m_Fbo.reset(); ++report->fboFreed; }
            m_Component.reset();                                 // module-owned dtor runs now

            ++report->detached;
            report->unloaded = 1;   // any listener firing after this point is a defect
        }

        ~LifetimeLayer() override
        {
            report->seqDestroy = ++g_Seq;
            ++report->destroyed;
        }
    };
}

extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        char* value = nullptr; size_t len = 0;
        _dupenv_s(&value, &len, "COSMIC_WO07_LIFETIME_REPORT");
        if (!value) return nullptr;
        report = reinterpret_cast<WO07LifetimeReport*>(_strtoui64(value, nullptr, 16));
        free(value);
        return new LifetimeLayer();
    }
}
