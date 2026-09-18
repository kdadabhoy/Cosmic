// WO07TeardownFixture.cpp — L04 (2D stability): teardown during LIVE background
// activity, runtime-plugin lifecycle.
//
// A real project DLL loaded by the real Application. By the time the host pulls the
// trigger it owns, all live at once:
//   * a JobSystem job that is genuinely IN FLIGHT — blocked on a barrier the EXE owns
//     (it signals "entered" first, so in-flight is observed, not timed);
//   * a FileWatcher on a directory the HOST writes a file into every frame (the
//     watcher worker is delivering right up to the unload);
//   * a connected SerialLink over the WO-04 FakeSerialTransport, the host pushing
//     bytes every frame (the reader thread is live during teardown);
//   * an EntitySelection listener, a log sink and a Window hotkey override — the host
//     fires all three every frame, before, during and after the unload;
//   * a Scene + EventBus with a listener (emitted every frame);
//   * a GPU texture, a framebuffer and a module-owned component object.
// The transition itself (TransitionToLauncher, or WM_CLOSE) is requested from INSIDE
// the EntitySelection dispatch (a "deferred transition": the Application applies it
// in the Safe Zone after the frame). OnDetach releases the job barrier and WAITS for
// the job (mode 0/1), stops the watcher, closes the serial link, disconnects every
// listener and releases the GPU objects — all recorded into the exe-owned report
// with sequence stamps. Mode 2 is the careless plugin: OnDetach does NOT join its job;
// the exe releases the barrier only after detach — the engine must still not let the
// worker run into unmapped code.
//
// Disconnect-during-dispatch probes: listener A unsubscribes listener B from inside a
// dispatch (EntitySelection and EventBus); B firing in that same dispatch is a defect.
#include "WO07TeardownReport.h"
#include "WO05NativeWindow.h"
#include "FakeSerialTransport.h"

#include <Cosmic.h>
#include "core/Log.h"
#include "graphics/Texture.h"
#include "graphics/FrameBuffer.h"
#include "jobs/JobSystem.h"
#include "telemetry/EntitySelection.h"
#include "utils/FileWatcher.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "serial/SerialLink.h"

#include <imgui.h>
#include <implot.h>

#include <tlhelp32.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
    WO07TeardownReport* report = nullptr;
    long long Stamp() { return ++report->seq; }

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

    struct OwnedComponent
    {
        ~OwnedComponent() { ++report->componentDestroyed; }
    };

    // Set while the victim listener (B) has been unsubscribed from INSIDE a dispatch:
    // if B still fires, the dispatcher invoked a removed listener.
    std::atomic<bool> g_EsVictimRemoved{false};
    std::atomic<bool> g_BusVictimRemoved{false};

    class TeardownLayer final : public Cosmic::Layer
    {
        int  m_Frame = 0;
        Cosmic::Ref<Cosmic::Texture2D>   m_Tex;
        Cosmic::Ref<Cosmic::FrameBuffer> m_Fbo;
        std::unique_ptr<OwnedComponent>  m_Component;
        std::shared_ptr<Cosmic::CallbackSink> m_Sink;
        Cosmic::FileWatcher m_Watcher;
        Cosmic::EntitySelection::SubscriptionHandle m_ListenerA = 0, m_ListenerB = 0;
        bool m_NeedResubscribeB = false;
        Cosmic::Ref<Cosmic::Scene> m_Scene;
        Cosmic::EventBus::Handle m_BusX = 0, m_BusY = 0;
        std::unique_ptr<Cosmic::SerialLink> m_Link;
        Cosmic::FakeSerialTransport* m_Fake = nullptr;
        std::shared_ptr<Cosmic::FakeSerialTransport::Counters> m_Counts;

        void SubscribeB()
        {
            m_ListenerB = Cosmic::EntitySelection::OnChanged(
                [](const std::string&, const std::string&)
                {
                    if (g_EsVictimRemoved) ++report->esRemovedMidDispatchFired;
                });
            ++report->listenerSubscribed;
        }

    public:
        TeardownLayer() : Cosmic::Layer("WO07 L04 teardown") {}

        void OnAttach() override
        {
            m_Sink = std::make_shared<Cosmic::CallbackSink>(
                [](spdlog::level::level_enum, const std::string&)
                {
                    if (report->unloaded) ++report->sinkAfterUnload; else ++report->sinkWhileLive;
                });
            Cosmic::Log::AddSink(m_Sink);
            ++report->sinkAdded;

            // Listener A: the live counter AND the trigger. The host fires
            // "l04-go" once; A requests the transition from inside this dispatch.
            m_ListenerA = Cosmic::EntitySelection::OnChanged(
                [this](const std::string& name, const std::string&)
                {
                    if (report->unloaded) { ++report->selectionAfterUnload; return; }
                    ++report->selectionWhileLive;
                    if (name == "l04-probe")
                    {
                        // Disconnect-during-dispatch: remove B while this dispatch runs.
                        if (m_ListenerB)
                        {
                            Cosmic::EntitySelection::Unsubscribe(m_ListenerB);
                            m_ListenerB = 0; ++report->listenerUnsubscribed;
                            g_EsVictimRemoved = true;
                            m_NeedResubscribeB = true;
                            ++report->esProbeDispatches;
                        }
                    }
                    else if (name == "l04-go")
                    {
                        report->seqTransitionRequested = Stamp();
                        ++report->transitionRequestedFromCallback;
                        if (report->mode == 1)
                        {
                            HWND hwnd = WO05NativeWindow(Cosmic::Application::Get().GetWindow());
                            report->jobInFlightAtClose =
                                (WaitForSingleObject(report->jobEntered, 0) == WAIT_OBJECT_0 && report->jobRan == 0) ? 1 : 0;
                            report->closeRequestTick = (long long)GetTickCount64();
                            if (!hwnd || !PostMessage(hwnd, WM_CLOSE, 0, 0)) ++report->failures;
                        }
                        else
                        {
                            Cosmic::Application::Get().TransitionToLauncher();   // deferred to the Safe Zone
                        }
                    }
                });
            ++report->listenerSubscribed;
            SubscribeB();

            // Hotkey override: captureless (reads the exe-owned report), consumes F9.
            Cosmic::Application::Get().GetWindow().SetFullscreenHotkeyOverride(
                [](int key, int action, int) -> bool
                {
                    if (key != CS_KEY_F9 || action != 1 /*GLFW_PRESS*/) return false;
                    if (report->unloaded) ++report->hotkeyAfterUnload; else ++report->hotkeyWhileLive;
                    return true;
                });

            ++report->attached;
        }

        void OnUpdate(float dt) override
        {
            if (m_Frame == 0)
            {
                m_Tex = Cosmic::Texture2D::Create(64, 64);             ++report->texCreated;
                Cosmic::FramebufferSpecification spec; spec.Width = 128; spec.Height = 128;
                m_Fbo = Cosmic::FrameBuffer::Create(spec);             ++report->fboCreated;
                m_Component = std::make_unique<OwnedComponent>();

                // The in-flight job: announces itself, then blocks on the EXE-owned barrier.
                Cosmic::JobSystem::Get().Submit([]
                {
                    report->seqJobEntered = Stamp();
                    SetEvent(report->jobEntered);
                    WaitForSingleObject(report->jobBarrier, INFINITE);
                    ++report->jobRan;
                    report->seqJobDone = Stamp();
                    SetEvent(report->jobDone);
                });
                ++report->jobSubmitted;

                // Watch the directory the host writes into every frame.
                std::error_code ec;
                const auto dir = std::filesystem::temp_directory_path(ec) / "wo07-l04-watch";
                std::filesystem::create_directories(dir, ec);
                if (m_Watcher.Watch(dir.string(), /*recursive=*/false) && m_Watcher.IsWatching())
                    ++report->watcherStarted;

                // Scene + EventBus: X disconnects Y during dispatch; Y must not fire then.
                m_Scene = Cosmic::Scene::Create();
                m_BusX = m_Scene->Events().Connect("l04-tick", [this](Cosmic::Entity)
                {
                    ++report->busWhileLive;
                    if (m_BusY) { m_Scene->Events().Disconnect(m_BusY); m_BusY = 0; ++report->busDisconnected; g_BusVictimRemoved = true; }
                });
                m_BusY = m_Scene->Events().Connect("l04-tick", [](Cosmic::Entity)
                {
                    if (g_BusVictimRemoved) ++report->busRemovedMidDispatchFired;
                });
                report->busConnected += 2;

                // Serial over the fake transport: connect and let the host stream bytes.
                auto transport = std::make_unique<Cosmic::FakeSerialTransport>();
                m_Fake = transport.get(); m_Counts = m_Fake->Counts();
                m_Fake->SetAvailablePorts({ "COM_FAKE" });
                m_Link = std::make_unique<Cosmic::SerialLink>(std::move(transport));
                m_Link->OnUpdate(1.0f);   // one full scan tick: the link selects the fake port
                m_Link->Connect();
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (m_Link->GetState() != Cosmic::SerialPort::State::Open && std::chrono::steady_clock::now() < until)
                    std::this_thread::yield();
                if (m_Link->GetState() == Cosmic::SerialPort::State::Open) ++report->serialOpened; else ++report->failures;
                report->fake = m_Fake;   // the host's pusher thread starts streaming now
            }

            // Steady state: drain every live channel each frame.
            if (m_Link)
            {
                m_Link->OnUpdate(dt);
                report->serialBytesWhileLive += (long long)m_Link->Poll().size();
            }
            report->watchEventsWhileLive += (int)m_Watcher.Poll().size();
            if (m_Scene)
            {
                g_BusVictimRemoved = false;
                if (!m_BusY)
                {
                    m_BusY = m_Scene->Events().Connect("l04-tick", [](Cosmic::Entity)
                    {
                        if (g_BusVictimRemoved) ++report->busRemovedMidDispatchFired;
                    });
                    ++report->busConnected;
                }
                m_Scene->Events().Emit("l04-tick", Cosmic::Entity{});
            }
            if (m_NeedResubscribeB)
            {
                m_NeedResubscribeB = false;
                g_EsVictimRemoved = false;
                SubscribeB();
            }

            if (m_Frame == 3)
            {
                report->warmedHandles = HandleCount();
                report->warmedThreads = ThreadCount();
            }
            ++m_Frame;
        }

        Cosmic::FakeSerialTransport* Fake() const { return m_Fake; }

        void OnDetach() override
        {
            const auto t0 = std::chrono::steady_clock::now();
            report->seqDetachBegin = Stamp();
            report->jobInFlightAtDetach =
                (WaitForSingleObject(report->jobEntered, 0) == WAIT_OBJECT_0 && report->jobRan == 0) ? 1 : 0;

            // The job: release it and WAIT for it (owned work must finish before this
            // code is unmapped). Mode 2 deliberately skips this (careless plugin).
            if (report->mode != 2)
            {
                SetEvent(report->jobBarrier);
                if (WaitForSingleObject(report->jobDone, 5000) == WAIT_OBJECT_0) ++report->jobJoinedInDetach;
                else ++report->failures;
            }

            // Serial: close through the production ownership chain, timed.
            if (m_Link)
            {
                const auto s0 = std::chrono::steady_clock::now();
                m_Link->Disconnect();
                m_Link->Shutdown();
                // The exe pusher may be mid-PushBytes: tell it to stop and wait until it
                // has, THEN destroy the transport (bounded; a stuck pusher is a failure).
                report->serialStopPushing = 1;
                while (!report->serialPusherStopped && std::chrono::steady_clock::now() < s0 + std::chrono::seconds(5))
                    std::this_thread::yield();
                if (!report->serialPusherStopped) ++report->failures;
                m_Link.reset();   // owns the port + transport; the Counters outlive it
                report->serialCloseMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s0).count();
                ++report->serialClosedInDetach;
                if (m_Counts)
                {
                    report->fakeHandlesAtDetachEnd = m_Counts->handles.load();
                    report->fakeReadsAtDetachEnd   = m_Counts->reads.load();
                }
                m_Fake = nullptr;
            }

            if (m_Scene)
            {
                if (m_BusX) { m_Scene->Events().Disconnect(m_BusX); m_BusX = 0; ++report->busDisconnected; }
                if (m_BusY) { m_Scene->Events().Disconnect(m_BusY); m_BusY = 0; ++report->busDisconnected; }
                m_Scene.reset();
            }
            if (m_Watcher.IsWatching()) { m_Watcher.Stop(); ++report->watcherStopped; }
            if (m_Sink) { Cosmic::Log::RemoveSink(m_Sink); m_Sink.reset(); ++report->sinkRemoved; }
            if (m_ListenerA) { Cosmic::EntitySelection::Unsubscribe(m_ListenerA); m_ListenerA = 0; ++report->listenerUnsubscribed; }
            if (m_ListenerB) { Cosmic::EntitySelection::Unsubscribe(m_ListenerB); m_ListenerB = 0; ++report->listenerUnsubscribed; }
            if (m_Tex) { m_Tex.reset(); ++report->texFreed; }
            if (m_Fbo) { m_Fbo.reset(); ++report->fboFreed; }
            m_Component.reset();

            ++report->detached;
            report->unloaded = 1;   // any fixture callback after this point is a defect
            report->seqDetachEnd = Stamp();
            report->detachMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        }

        ~TeardownLayer() override
        {
            report->seqDestroy = Stamp();
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
        _dupenv_s(&value, &len, "COSMIC_WO07_TEARDOWN_REPORT");
        if (!value) return nullptr;
        report = reinterpret_cast<WO07TeardownReport*>(_strtoui64(value, nullptr, 16));
        free(value);
        return new TeardownLayer();
    }
}
