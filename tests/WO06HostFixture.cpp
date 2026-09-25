// Test-only two-hour fixture through actual Application/SF_Telem DLL ownership.
#include "../Projects/SF_Telem/src/SF_Telem.h"
#include "FakeSerialTransport.h"
#include "WO05HostReport.h"
#include "WO05NativeWindow.h"
#include <chrono>
#include <cstdlib>
#include <imgui.h>
#include <implot.h>
#include <thread>
namespace
{
WO05HostReport *report = nullptr;
class RecordingRoot final : public Workspace::SF_Telem
{
    bool requested = false;
    HANDLE entered = CreateEvent(nullptr, TRUE, FALSE, nullptr),
           release = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    std::thread releaser;

  public:
    explicit RecordingRoot(std::unique_ptr<Cosmic::FakeSerialTransport> transport)
        : SF_Telem(std::move(transport))
    {
    }
    ~RecordingRoot() override
    {
        if (releaser.joinable())
            releaser.join();
        CloseHandle(entered);
        CloseHandle(release);
        ++report->destroyed;
    }
    void OnUpdate(float dt) override
    {
        SF_Telem::OnUpdate(dt);
        if (requested)
            return;
        requested = true;
        SetScreen(SCREEN_MAIN);
        Hub().SetSessionName("wo06-native-two-hour");
        Hub().StartRecording();
        Hub().Recorder().DisableAutosave();
        // Keep 431999 samples dirty; request a pending full snapshot on the final tick.
        for (int i = 0; i < 431999; ++i)
            OnFixedUpdate(1.f / 60.f);
        Hub().Recorder().SetFlushWriteBarrier([this] {
            SetEvent(entered);
            if (WaitForSingleObject(release, 30000) != WAIT_OBJECT_0)
                ++report->failures;
        });
        report->closeRequestTick = GetTickCount64();
        report->closeRequestUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
        OnFixedUpdate(1.f / 60.f);
        if (WaitForSingleObject(entered, 2000) != WAIT_OBJECT_0)
            ++report->failures;
        if (report->transition == 0)
        {
            auto hwnd = WO05NativeWindow(Cosmic::Application::Get().GetWindow());
            if (!hwnd || !PostMessage(hwnd, WM_CLOSE, 0, 0))
                ++report->failures;
        }
        else
            Cosmic::Application::Get().TransitionToLauncher();
    }
    void OnDetach() override
    {
        // Release at the actual owner teardown boundary, not after a guessed delay.
        SetEvent(release);
        SF_Telem::OnDetach();
        if (Hub().Recorder().GetFlushState() != Cosmic::DataRecorder::FlushState::Succeeded ||
            Hub().RecordingDirty() || Hub().Recorder().IsFlushing())
            ++report->failures;
        report->closeMs = GetTickCount64() - report->closeRequestTick.load();
        if (report->closeMs > 30000)
            ++report->failures;
        ++report->detached;
    }
};
} // namespace
extern "C"
{
    __declspec(dllexport) void InitializePluginContexts(Cosmic::HostContext context)
    {
        ImGui::SetCurrentContext(context.ImGuiCtx);
        ImPlot::SetCurrentContext(context.ImPlotCtx);
    }
    __declspec(dllexport) Cosmic::Layer *CreatePluginLayer()
    {
        char *value = nullptr;
        size_t n = 0;
        _dupenv_s(&value, &n, "COSMIC_WO06_HOST_REPORT");
        if (!value)
            return nullptr;
        report = reinterpret_cast<WO05HostReport *>(_strtoui64(value, nullptr, 16));
        free(value);
        if (!report->createTransport)
            return nullptr;
        return new RecordingRoot(std::unique_ptr<Cosmic::FakeSerialTransport>(report->createTransport()));
    }
}
CS_TEST_FIXTURE()   // UX-03: hidden from the Launcher project scan (KI-77)
