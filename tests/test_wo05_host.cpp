#include <doctest.h>
#include "WO05HostReport.h"
#include "core/Application.h"
#include "core/Layer.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include "WO05NativeWindow.h"
#include "FakeSerialTransport.h"

namespace
{
    // Async transport implementation belongs to the process executable, like the
    // shipping implementation belongs to Cosmic.dll, surviving project DLL unload.
    Cosmic::FakeSerialTransport* MakeTransport() { return new Cosmic::FakeSerialTransport(); }
    class CloseAfterReturn final : public Cosmic::Layer
    {
        WO05HostReport& report;
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    public:
        explicit CloseAfterReturn(WO05HostReport& r) : Layer("WO05 host driver"),report(r) {}
        void OnUpdate(float) override
        {
            if (report.detached || std::chrono::steady_clock::now()-start>std::chrono::seconds(15))
            {
                if (!report.detached) ++report.failures;
                Cosmic::WindowCloseEvent close;
                Cosmic::Application::Get().OnEvent(close);
            }
        }
    };
}

namespace
{
void RunHost(bool policy)
{
    // Application owns the process singleton/GL context. Each window-close is a
    // fresh child process; the WO-04 runner orchestrates the 100 repetitions.
    int schedule=0, transition=0, storage=0;
    char* selection=nullptr; size_t length=0;
    _dupenv_s(&selection,&length,"COSMIC_WO05_HOST_CASE");
    if (selection) { sscanf_s(selection,"%d,%d,%d",&schedule,&transition,&storage); free(selection); }
    {
        CAPTURE(schedule); CAPTURE(transition); CAPTURE(storage);
        WO05HostReport report;
        report.schedule=schedule; report.transition=transition;
        report.storage=storage;
        report.policy=policy;
        report.createTransport=&MakeTransport;
        char pointer[32]; std::snprintf(pointer,sizeof(pointer),"%llX",static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&report)));
        _putenv_s("COSMIC_WO05_HOST_REPORT",pointer);
        {
            wchar_t exePath[MAX_PATH]{};
            REQUIRE(GetModuleFileNameW(nullptr,exePath,MAX_PATH)!=0);
            const auto fixture=std::filesystem::path(exePath).parent_path()/"WO05HostFixture.dll";
            Cosmic::Application app(fixture.string());
            app.GetWindow().SetVSync(false);
            HWND hwnd=WO05NativeWindow(app.GetWindow());
            CHECK(hwnd!=nullptr);
            if (hwnd) ShowWindow(hwnd,SW_HIDE);
            app.PushLayer(new CloseAfterReturn(report));
            app.Run();
        }
        CHECK(report.failures==0); CHECK(report.detached==1); CHECK(report.destroyed==1);
        CHECK(report.closeMs<=2000);
        CHECK(GetTickCount64()-report.closeRequestTick.load()<=2000);
        std::printf("WO05 close_request_unix_ms=%lld\n",report.closeRequestUnixMs.load());
    }
    _putenv_s("COSMIC_WO05_HOST_REPORT", "");
    std::printf("WO05 actual host child: schedule=%d transition=%d storage=%d\n",schedule,transition,storage);
}
}

TEST_CASE("WO-05 T03 host: actual window close and deferred launcher unload, 100 iterations" * doctest::skip())
{ RunHost(false); }

TEST_CASE("WO-05 T05 host: actual pause minimize restore and acquisition policy" * doctest::skip())
{ RunHost(true); }
