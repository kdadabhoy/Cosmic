#include "FakeSerialTransport.h"
#include "WO05HostReport.h"
#include "WO05NativeWindow.h"
#include "core/Application.h"
#include <cstdio>
#include <cstdlib>
#include <doctest.h>
#include <filesystem>
namespace
{
Cosmic::FakeSerialTransport *Transport()
{
    return new Cosmic::FakeSerialTransport();
}
class CloseAfterUnload final : public Cosmic::Layer
{
    WO05HostReport &report;

  public:
    explicit CloseAfterUnload(WO05HostReport &r) : Layer("WO06 launcher driver"), report(r)
    {
    }
    void OnUpdate(float) override
    {
        if (report.detached)
        {
            Cosmic::WindowCloseEvent close;
            Cosmic::Application::Get().OnEvent(close);
        }
    }
};
void Host(int transition)
{
    WO05HostReport report;
    report.transition = transition;
    report.createTransport = &Transport;
    char pointer[32];
    std::snprintf(pointer, sizeof(pointer), "%llX",
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(&report)));
    _putenv_s("COSMIC_WO06_HOST_REPORT", pointer);
    wchar_t executable[MAX_PATH]{};
    REQUIRE(GetModuleFileNameW(nullptr, executable, MAX_PATH));
    {
        Cosmic::Application app(
            (std::filesystem::path(executable).parent_path() / "WO06HostFixture.dll").string());
        app.GetWindow().SetVSync(false);
        auto hwnd = WO05NativeWindow(app.GetWindow());
        if (hwnd)
            ShowWindow(hwnd, SW_HIDE);
        app.PushLayer(new CloseAfterUnload(report));
        app.Run();
    }
    _putenv_s("COSMIC_WO06_HOST_REPORT", "");
    CHECK(report.failures == 0);
    CHECK(report.detached == 1);
    CHECK(report.destroyed == 1);
    CHECK(report.closeMs <= 30000);
    CHECK(GetTickCount64() - report.closeRequestTick.load() <= 30000);
    std::printf("WO06 close_request_unix_ms=%lld close_ms=%lld transition=%d samples_per_entity=432000\n",
                report.closeRequestUnixMs.load(), report.closeMs.load(), transition);
}
} // namespace
TEST_CASE("WO-06 D05 host: two hour pending export native close" * doctest::skip())
{
    Host(0);
}
TEST_CASE("WO-06 D05 host: two hour pending export launcher return" * doctest::skip())
{
    Host(1);
}
