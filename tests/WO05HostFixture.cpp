// Test-only DLL. Uses the real SF_Telem root and real Application DLL lifecycle.
#include "WO05HostReport.h"
#include "FakeSerialTransport.h"
#include "../Projects/SF_Telem/src/SF_Telem.h"
#include <imgui.h>
#include <implot.h>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstdio>
#include "WO05NativeWindow.h"

namespace
{
    using Clock = std::chrono::steady_clock;
    WO05HostReport* report = nullptr;
    void MarkCloseRequest()
    {
        report->closeRequestTick=GetTickCount64();
        report->closeRequestUnixMs=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    template<class F> bool Until(F predicate)
    {
        auto end = Clock::now()+std::chrono::seconds(2);
        while (!predicate() && Clock::now()<end) std::this_thread::yield();
        if (!predicate()) { ++report->failures; return false; }
        return true;
    }
    class PolicyRoot final : public Workspace::SF_Telem
    {
        Cosmic::FakeSerialTransport* fake = nullptr;
        std::shared_ptr<Cosmic::FakeSerialTransport::Counters> counts;
        int phase = 0;
        size_t fixedAtPause = 0;
        void Feed()
        {
            const char* payload="R,25,1680,420,120,350";
            unsigned checksum=0;
            for (const char* c=payload; *c; ++c) checksum^=static_cast<unsigned char>(*c);
            char line[64]; std::snprintf(line,sizeof(line),"$%s*%02X\n",payload,checksum);
            Until([&] { return counts->reads==1; });
            const int calls=counts->readCalls;
            fake->PushBytes(line);
            Until([&] { return counts->readCalls>calls; });
        }
        void Expect(bool value) { if (!value) ++report->failures; }
    public:
        explicit PolicyRoot(std::unique_ptr<Cosmic::FakeSerialTransport> transport)
            : SF_Telem(std::move(transport)) {}
        void Observe(Cosmic::FakeSerialTransport* f) { fake=f; counts=f->Counts(); }
        ~PolicyRoot() override { ++report->destroyed; }
        void OnUpdate(float dt) override
        {
            SF_Telem::OnUpdate(dt);
            auto& app=Cosmic::Application::Get();
            HWND hwnd=WO05NativeWindow(app.GetWindow());
            switch (phase)
            {
            case 0:
                SetScreen(SCREEN_MAIN); Link().Connect();
                Until([&] { return Link().GetState()==Cosmic::SerialPort::State::Open; });
                app.SetPauseOnMinimize(false);
                Hub().SetSessionName("wo05-host-policy"); Hub().StartRecording();
                app.Pause(); fixedAtPause=Hub().Recorder().GetTotalFrameCount();
                Feed(); phase=1; break;
            case 1:
                Expect(app.IsPaused() && dt==0 && Hub().GoodFrames()==1);
                Expect(Hub().Recorder().GetTotalFrameCount()==fixedAtPause);
                ShowWindow(hwnd,SW_MINIMIZE); Expect(IsIconic(hwnd)!=FALSE);
                Feed(); phase=2; break;
            case 2:
                Expect(IsIconic(hwnd)!=FALSE && !app.GetPauseOnMinimize());
                Expect(Hub().GoodFrames()==2 && Hub().Recorder().GetTotalFrameCount()==fixedAtPause);
                ShowWindow(hwnd,SW_RESTORE); Expect(IsIconic(hwnd)==FALSE); ShowWindow(hwnd,SW_HIDE);
                app.Resume(); SetScreen(SCREEN_HOME); Feed(); phase=3; break;
            case 3:
                Expect(Hub().GoodFrames()==2 && Link().WantConnection());
                SetScreen(SCREEN_MAIN); phase=4; break;
            case 4:
                Expect(Hub().GoodFrames()==3);
                if (Hub().Recorder().GetTotalFrameCount()==fixedAtPause) break;
                Expect(!app.IsPaused()); phase=5;
                MarkCloseRequest();
                Expect(PostMessage(hwnd,WM_CLOSE,0,0)!=FALSE); break;
            default: break;
            }
        }
        void OnDetach() override
        {
            const auto start=Clock::now(); SF_Telem::OnDetach();
            report->closeMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
            Expect(counts->handles==0 && counts->opens==0 && counts->reads==0 && counts->writes==0);
            Expect(!Link().WantConnection() && !Hub().Recorder().IsFlushing());
            ++report->detached;
        }
    };
    class ObservedRoot final : public Workspace::SF_Telem
    {
        Cosmic::FakeSerialTransport* fake;
        std::shared_ptr<Cosmic::FakeSerialTransport::Counters> counts;
        bool queued = false;
        std::thread writer, releaseExport;
        HANDLE exportEntered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        HANDLE exportRelease = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    public:
        explicit ObservedRoot(std::unique_ptr<Cosmic::FakeSerialTransport> transport)
            : SF_Telem(std::move(transport)) {}
        void Observe(Cosmic::FakeSerialTransport* f) { fake=f; counts=f->Counts(); }
        ~ObservedRoot() override
        {
            CloseHandle(exportEntered); CloseHandle(exportRelease);
            ++report->destroyed;
        }
        void OnUpdate(float dt) override
        {
            SF_Telem::OnUpdate(dt);
            if (queued) return;
            queued=true;
            SetScreen(SCREEN_MAIN);
            if (report->schedule < 3)
            {
                fake->SetOpenBarrier(); fake->SetOpenResult(report->schedule!=1);
                Link().Connect();
                if (!fake->WaitOpenEntered()) ++report->failures;
                if (report->schedule==2)
                {
                    fake->ReleaseOpen();
                    Until([&] { return counts->opens==0 && counts->handles==1; });
                }
            }
            else
            {
                Link().Connect();
                Until([&] { return Link().GetState()==Cosmic::SerialPort::State::Open; });
                Until([&] { return counts->reads==1; });
                if (report->schedule==4) SF_Telem::OnUpdate(1.1f);
                if (report->schedule==5 || report->schedule==7)
                {
                    fake->SignalDrop();
                    Until([&] { return Link().GetState()==Cosmic::SerialPort::State::Failed; });
                    if (report->schedule==7)
                    {
                        fake->SetOpenBarrier(); SF_Telem::OnUpdate(3.0f);
                        if (!fake->WaitOpenEntered()) ++report->failures;
                    }
                }
                if (report->schedule==8) Link().Disconnect();
            }
            if (report->storage!=0)
            {
                Hub().SetSessionName("wo05-host"); Hub().StartRecording();
                OnFixedUpdate(1.0f/60.0f);
                if (report->storage==2 || report->storage==3)
                {
                    Hub().Recorder().SetFlushWriteBarrier([this]
                    {
                        SetEvent(exportEntered);
                        if (WaitForSingleObject(exportRelease, 10000)!=WAIT_OBJECT_0) ++report->failures;
                    });
                    if (report->storage==2) Hub().StopRecording();
                    else Hub().Recorder().Tick(5.0f);
                    if (WaitForSingleObject(exportEntered,2000)!=WAIT_OBJECT_0) ++report->failures;
                }
                if (report->storage==4)
                {
                    Hub().StopRecording(); Hub().Recorder().WaitForFlush();
                    if (!Hub().Player().Load("recordings/SF_Telem/wo05-host/scene.bin")) ++report->failures;
                    SetScreen(SCREEN_REPLAY);
                }
            }
            // Drive actual screen commands before closing the host.
            for (int s=0; s<SCREEN_COUNT; ++s) SetScreen(static_cast<Screen>(s));
            if (report->schedule==6)
            {
                fake->SetWritePending();
                writer=std::thread([this] { if (Link().Write("pending")) ++report->failures; });
                Until([&] { return counts->writes==1; });
            }
            if (report->storage==2 || report->storage==3)
            {
                const int closes=fake->CloseCount();
                releaseExport=std::thread([this,closes]
                {
                    Until([&] { return fake->CloseCount()>closes; });
                    SetEvent(exportRelease);
                });
            }
            MarkCloseRequest();
            if (report->transition==0)
            {
                HWND hwnd=WO05NativeWindow(Cosmic::Application::Get().GetWindow());
                if (!hwnd || !PostMessage(hwnd, WM_CLOSE, 0, 0)) ++report->failures;
            }
            else Cosmic::Application::Get().TransitionToLauncher();
        }
        void OnDetach() override
        {
            auto start=Clock::now();
            SF_Telem::OnDetach();
            if (writer.joinable()) writer.join();
            if (releaseExport.joinable()) releaseExport.join();
            report->closeMs=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
            if (counts->handles!=0 || counts->opens!=0 || counts->reads!=0 || counts->writes!=0 ||
                Link().WantConnection() || Hub().Recorder().IsFlushing()) ++report->failures;
            ++report->detached;
        }
    };
}
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    { ImGui::SetCurrentContext(context.ImGuiCtx); ImPlot::SetCurrentContext(context.ImPlotCtx); }
    __declspec(dllexport) Cosmic::Layer* CreatePluginLayer()
    {
        char* value=nullptr; size_t length=0;
        _dupenv_s(&value,&length,"COSMIC_WO05_HOST_REPORT");
        if (!value) return nullptr;
        report=reinterpret_cast<WO05HostReport*>(_strtoui64(value,nullptr,16)); free(value);
        if (!report->createTransport) { ++report->failures; return nullptr; }
        auto transport=std::unique_ptr<Cosmic::FakeSerialTransport>(report->createTransport());
        auto* fake=transport.get(); fake->SetAvailablePorts({"COM_FAKE"});
        if (report->policy)
        {
            auto* root=new PolicyRoot(std::move(transport)); root->Observe(fake); return root;
        }
        auto* root=new ObservedRoot(std::move(transport)); root->Observe(fake);
        return root;
    }
}
