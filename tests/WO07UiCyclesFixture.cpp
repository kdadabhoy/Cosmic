// WO07UiCyclesFixture.cpp — L05 (2D stability): 200 scripted UI cycles over the REAL
// SF_Telem root (Main / Testing / Analysis / Replay / Home) with a per-action Dear
// ImGui balance oracle.
//
// The fixture is the real Workspace::SF_Telem (its production ownership chain:
// SerialLink -> SerialPort -> the injected WO-04 FakeSerialTransport, TelemHub,
// TestingManager, the screen layers) hosted by the real Application. Every cycle it:
//   * puts the serial link in one of the three states through the production API —
//     CLOSED (Disconnect), OPEN (Connect + bytes streaming), LOST (SignalDrop ->
//     Failed -> auto-reconnect) — and then
//   * presses the REAL Navigation buttons (Main, Testing, Analysis, Replay, Home)
//     through Dear ImGui's own item activation (ImGui::ActivateItemByID on the real
//     button ids: the button's ButtonBehavior fires and its handler calls SetScreen),
//     verifying the screen actually changed;
//   * in Replay: loads a recording the fixture itself produced through the real
//     recorder, presses the REAL "Unload" button; every 10th cycle presses the REAL
//     "Browse..." button, which opens the native IFileDialog (modal on the UI thread) —
//     an exe helper thread finds the dialog and cancels it (WM_CLOSE), and the fixture
//     verifies the dialog was seen and the frame continued;
//   * exercises the window: minimize/restore, resize, the F11 fullscreen hotkey (posted
//     as a real key message, both directions), and undocking a docked window through
//     ImGui's own undock path (DockContextQueueUndockWindow — what a tab drag does),
//     re-docked by the next screen's layout.
// After EVERY scripted action the oracle (WO07UiOracle.h) is judged: no recovered
// ImGui error, no leaked stack depth at EndFramePre, no context/font drift, and the
// depths around SF_Telem::OnImGuiRender balanced. Each action is appended to an exact
// action log (l05-actions.txt) and screenshots of the presented window are saved at
// fixed cycles and on the first failure (l05-shot-*.png).
#include "WO07UiCyclesReport.h"
#include "WO07UiOracle.h"
#include "WO05NativeWindow.h"
#include "FakeSerialTransport.h"
#include "../Projects/SF_Telem/src/SF_Telem.h"

#include "ui/IconsLucide.h"
#include "utils/ImageIO.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <functional>
#include <vector>

namespace
{
    WO07UiCyclesReport* report = nullptr;
    std::string outDir;

    typedef void (APIENTRY* PFN_glReadBuffer)(unsigned int);
    typedef void (APIENTRY* PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
    typedef void (APIENTRY* PFN_glPixelStorei)(unsigned int, int);
    typedef void (APIENTRY* PFN_glGetIntegerv)(unsigned int, int*);
    typedef void (APIENTRY* PFN_glBindFramebuffer)(unsigned int, unsigned int);
    typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);

    // Screenshot of the presented window (front buffer) -> PNG, top-left origin.
    bool SaveScreenshot(const std::string& path)
    {
        HMODULE gl = GetModuleHandleA("opengl32.dll");
        auto readBuffer = (PFN_glReadBuffer)GetProcAddress(gl, "glReadBuffer");
        auto readPixels = (PFN_glReadPixels)GetProcAddress(gl, "glReadPixels");
        auto pixelStore = (PFN_glPixelStorei)GetProcAddress(gl, "glPixelStorei");
        auto getIntegerv = (PFN_glGetIntegerv)GetProcAddress(gl, "glGetIntegerv");
        auto wglGetProc = (PFN_wglGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
        auto bindFbo = wglGetProc ? (PFN_glBindFramebuffer)wglGetProc("glBindFramebuffer") : nullptr;
        if (!readBuffer || !readPixels || !pixelStore || !getIntegerv || !bindFbo) return false;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
        const int w = (int)(vp->Size.x * scale.x), h = (int)(vp->Size.y * scale.y);
        if (w <= 0 || h <= 0) return false;
        std::vector<unsigned char> rgba((size_t)w * h * 4);
        int prevRead = 0; getIntegerv(0x8CAA, &prevRead);
        bindFbo(0x8CA8, 0);
        readBuffer(0x0404 /*GL_FRONT*/);
        pixelStore(0x0D05, 1);
        readPixels(0, 0, w, h, 0x1908, 0x1401, rgba.data());
        readBuffer(0x0405);
        bindFbo(0x8CA8, (unsigned)prevRead);
        // GL rows are bottom-up; flip to top-left origin for the PNG.
        std::vector<unsigned char> flipped((size_t)w * h * 4);
        for (int y = 0; y < h; ++y)
            std::memcpy(&flipped[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
        return Cosmic::ImageIO::WritePNG(path, w, h, 4, flipped.data());
    }

    // The exe-side native-dialog canceller: finds the IFileDialog window (class #32770)
    // owned by our UI thread and closes it; counts what it saw.
    struct DialogCanceller
    {
        std::thread worker;
        std::atomic<bool> armed{false}, stop{false};
        DWORD uiThread = 0;
        void Start()
        {
            uiThread = GetCurrentThreadId();
            worker = std::thread([this]
            {
                while (!stop)
                {
                    if (armed)
                    {
                        struct Found { HWND h = nullptr; } f;
                        EnumThreadWindows(uiThread, [](HWND hwnd, LPARAM lp) -> BOOL
                        {
                            char cls[32] = {};
                            GetClassNameA(hwnd, cls, sizeof(cls));
                            if (std::strcmp(cls, "#32770") == 0 && IsWindowVisible(hwnd)) { reinterpret_cast<Found*>(lp)->h = hwnd; return FALSE; }
                            return TRUE;
                        }, reinterpret_cast<LPARAM>(&f));
                        if (f.h)
                        {
                            ++report->dialogsOpened;
                            PostMessage(f.h, WM_CLOSE, 0, 0);   // == Cancel
                            ++report->dialogsCancelled;
                            armed = false;
                        }
                    }
                    Sleep(5);
                }
            });
        }
        void Stop() { stop = true; if (worker.joinable()) worker.join(); }
    };

    class UiCyclesRoot final : public Workspace::SF_Telem
    {
        Cosmic::FakeSerialTransport* m_Fake = nullptr;
        std::shared_ptr<Cosmic::FakeSerialTransport::Counters> m_Counts;
        CosmicTest::UiOracle m_Oracle;
        DialogCanceller m_Canceller;
        std::ofstream m_Log;
        HWND m_Hwnd = nullptr;
        int m_Frame = 0, m_Cycle = 0, m_Step = 0, m_Wait = 0, m_Dialogs = 0;
        bool m_Booted = false, m_FirstFailureShot = false, m_Finished = false, m_BigWindow = true, m_Fullscreen = false;
        std::string m_RecPath, m_PendingAction;
        CosmicTest::UiOracle::Snap m_Snap{};
        int m_LayerImbalanceAtSnap = 0;
        int m_LayerImbalance = 0;
        std::chrono::steady_clock::time_point m_Start = std::chrono::steady_clock::now();

        static ImGuiID ButtonId(const char* window, const char* label)
        {
            ImGuiWindow* w = ImGui::FindWindowByName(window);
            return w ? w->GetID(label) : 0;
        }
        bool Activate(const char* window, const char* label)
        {
            const ImGuiID id = ButtonId(window, label);
            if (!id) return false;
            ImGui::ActivateItemByID(id);   // the REAL item is pressed on the next frame it is submitted
            return true;
        }
        void Feed()
        {
            if (m_Fake) m_Fake->PushBytes("$R,25,1680,420,120,350*5D\n");
        }
        void LogLine(const std::string& s)
        {
            if (m_Log) { m_Log << s << "\n"; m_Log.flush(); }
        }
        // Begin a scripted action: snapshot the oracle, remember the description.
        void Begin(const std::string& what)
        {
            m_PendingAction = what;
            m_Snap = m_Oracle.Take();
            m_LayerImbalanceAtSnap = m_LayerImbalance;
            m_Wait = 2;   // judge after two full frames
        }
        // Judge the pending action (called once m_Wait frames have passed).
        void Judge(const std::string& extra = "")
        {
            std::string why = m_Oracle.Judge(m_Snap);
            if (m_LayerImbalance != m_LayerImbalanceAtSnap) why += "layer-imbalance ";
            why += extra;
            ++report->actions;
            ImGuiContext* g = ImGui::GetCurrentContext();
            char depths[96];
            std::snprintf(depths, sizeof(depths), "C%d S%d F%d P%d", g->ColorStack.Size, g->StyleVarStack.Size, g->FontStack.Size, g->BeginPopupStack.Size);
            LogLine("cycle=" + std::to_string(m_Cycle) + " frame=" + std::to_string(m_Frame) + " action=" + m_PendingAction +
                    " screen=" + std::to_string((int)CurrentScreen()) + " serial=" + std::to_string((int)Link().GetState()) +
                    " stacks=" + depths + " errors=" + std::to_string(m_Oracle.recoveredErrors.load()) +
                    (why.empty() ? " result=ok" : " result=FAIL " + why));
            if (!why.empty())
            {
                ++report->actionsFailed;
                if (!m_FirstFailureShot) { m_FirstFailureShot = true; if (SaveScreenshot(outDir + "/l05-shot-first-failure.png")) ++report->screenshots; }
            }
            m_PendingAction.clear();
        }

    public:
        explicit UiCyclesRoot(std::unique_ptr<Cosmic::FakeSerialTransport> transport) : SF_Telem(std::move(transport)) {}
        void Observe(Cosmic::FakeSerialTransport* f) { m_Fake = f; m_Counts = f->Counts(); }

        void OnAttach() override
        {
            SF_Telem::OnAttach();
            m_Oracle.Install();
            m_Canceller.Start();
            m_Log.open(outDir + "/l05-actions.txt", std::ios::trunc);
            LogLine("# WO-07 L05 SF_Telem scripted UI cycles — one line per judged action");
            Cosmic::Application::Get().SetPauseOnMinimize(false);
            Cosmic::Application::Get().GetWindow().SetVSync(false);
        }

        void OnImGuiRender() override
        {
            // Per-layer balance: depths before/after the real root's render.
            ImGuiContext* g = ImGui::GetCurrentContext();
            const int c0 = g->ColorStack.Size, s0 = g->StyleVarStack.Size, f0 = g->FontStack.Size, p0 = g->BeginPopupStack.Size;
            SF_Telem::OnImGuiRender();
            if (g->ColorStack.Size != c0 || g->StyleVarStack.Size != s0 || g->FontStack.Size != f0 || g->BeginPopupStack.Size != p0)
                ++m_LayerImbalance;
            m_Oracle.CheckContexts();
        }

        void OnUpdate(float dt) override
        {
            SF_Telem::OnUpdate(dt);
            ++m_Frame;
            if (m_Finished) return;
            auto& app = Cosmic::Application::Get();
            if (!m_Hwnd) m_Hwnd = WO05NativeWindow(app.GetWindow());
            if (std::chrono::steady_clock::now() - m_Start > std::chrono::seconds(600)) { ++report->failures; m_Finished = true; report->done = 1; return; }

            if (!m_Booted)
            {
                // Boot: a recording of our own, through the real serial chain + recorder,
                // for the Replay screen (bytes -> TelemHub -> DataRecorder -> scene.bin).
                if (m_Frame < 5) return;   // let the dock layout settle
                if (m_Frame == 5)
                {
                    SetScreen(SCREEN_MAIN);
                    Link().Connect();
                    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    while (Link().GetState() != Cosmic::SerialPort::State::Open && std::chrono::steady_clock::now() < until) std::this_thread::yield();
                    if (Link().GetState() != Cosmic::SerialPort::State::Open) ++report->failures;
                    Hub().SetSessionName("wo07-l05"); Hub().StartRecording();
                    return;
                }
                if (m_Frame < 40) { Feed(); OnFixedUpdate(1.0f / 60.0f); return; }   // stream + record ~35 fixed steps
                Hub().StopRecording(); Hub().Recorder().WaitForFlush();
                Link().Disconnect();
                m_RecPath = "recordings/SF_Telem/wo07-l05/scene.bin";
                if (!std::filesystem::exists(m_RecPath)) { ++report->failures; LogLine("# FAIL: no recording produced at " + m_RecPath); }
                SetScreen(SCREEN_HOME);
                m_Booted = true;
                if (SaveScreenshot(outDir + "/l05-shot-c000-boot.png")) ++report->screenshots;
                return;
            }
            if (m_Wait > 0) { if (--m_Wait == 0) { Judge(); ++m_Step; } return; }

            // ---- the scripted cycle: a plan of actions built at cycle start ----
            const int c = m_Cycle;
            if (m_Plan.empty())
                BuildPlan(m_Cycle);
            if (m_Step < (int)m_Plan.size())
            {
                m_Plan[m_Step]();   // performs the action and calls Begin(...) (sets m_Wait)
                if (m_Wait == 0) { Judge(); ++m_Step; }
                return;
            }
            // cycle complete
            ++report->cyclesDone;
            if (c % 50 == 0 || c == report->cyclesPlanned - 1)
            {
                char name[64]; std::snprintf(name, sizeof(name), "/l05-shot-c%03d.png", c);
                if (SaveScreenshot(outDir + name)) ++report->screenshots;
            }
            m_Plan.clear(); m_Step = 0; ++m_Cycle;
            if (m_Cycle >= report->cyclesPlanned)
            {
                if (m_Fullscreen) { app.GetWindow().SetFullscreen(false); m_Fullscreen = false; }
                report->recoveredErrors = m_Oracle.recoveredErrors.load();
                report->endFrameLeaks = m_Oracle.endFrameLeaks.load();
                report->contextDrift = m_Oracle.contextDrift.load();
                report->layerImbalance = m_LayerImbalance;
                LogLine("# done: cycles=" + std::to_string(m_Cycle) + " actions=" + std::to_string(report->actions.load()) + " failed=" + std::to_string(report->actionsFailed.load()));
                m_Finished = true; report->done = 1;
            }
        }

        // The per-cycle script. Every entry performs ONE scripted action and calls
        // Begin(); the effect of the previous action is verified at the start of the
        // next entry (after the oracle judged it).
        std::vector<std::function<void()>> m_Plan;
        void ExpectScreen(Screen s) { if (CurrentScreen() != s) ++report->screenMismatch; else ++report->screenSwitches; }
        void PostF11()
        {
            const UINT sc = MapVirtualKeyW(VK_F11, MAPVK_VK_TO_VSC);
            PostMessage(m_Hwnd, WM_KEYDOWN, VK_F11, (LPARAM)((sc << 16) | 1));
            PostMessage(m_Hwnd, WM_KEYUP,   VK_F11, (LPARAM)((sc << 16) | 1 | (1u << 30) | (1u << 31)));
            m_Fullscreen = !m_Fullscreen; ++report->fullscreenToggles;
        }
        void BuildPlan(int c)
        {
            // 1. serial state (production ownership chain over the fake transport)
            m_Plan.push_back([this, c]
            {
                if (c % 3 == 0) { Link().Disconnect(); ++report->serialClosed; Begin("serial:closed(Disconnect)"); return; }
                Link().Connect();
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (Link().GetState() != Cosmic::SerialPort::State::Open && std::chrono::steady_clock::now() < until) std::this_thread::yield();
                if (Link().GetState() != Cosmic::SerialPort::State::Open) ++report->failures;
                Feed(); Feed();
                if (c % 3 == 1) { ++report->serialOpened; Begin("serial:open(Connect+bytes)"); return; }
                m_Fake->SignalDrop();
                const auto until2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (Link().GetState() != Cosmic::SerialPort::State::Failed && std::chrono::steady_clock::now() < until2) std::this_thread::yield();
                ++report->serialLost; Begin("serial:lost(SignalDrop->Failed)");
            });
            // 2. Main: from the homescreen the REAL tile (PushID(0) + InvisibleButton("##tile")),
            //    otherwise the REAL Navigation button (the Navigation panel is not drawn on Home).
            m_Plan.push_back([this]
            {
                if (CurrentScreen() == SCREEN_HOME)
                {
                    ImGuiWindow* w = ImGui::FindWindowByName("Home");
                    if (w) { ImGui::ActivateItemByID(ImHashStr("##tile", 0, w->GetID(0))); Begin("home:Main tile(real InvisibleButton)"); }
                    else { ++report->failures; Begin("home:Main tile - no Home window"); }
                }
                else { Activate("Navigation", ICON_LC_GAUGE "  Main"); Begin("nav:Main(real button)"); }
            });
            // 3. (every 5th) undock a docked window through ImGui's own undock path
            if (c % 5 == 2)
                m_Plan.push_back([this]
                {
                    ExpectScreen(SCREEN_MAIN);
                    ImGuiWindow* w = ImGui::FindWindowByName("Serial Link");
                    if (w && w->DockNode) { ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), w); ++report->undocks; Begin("dock:undock Serial Link (DockContextQueueUndockWindow)"); }
                    else { ++report->failures; Begin("dock:undock - Serial Link not docked"); }
                });
            // 4. Testing
            m_Plan.push_back([this, c]
            {
                if (c % 5 != 2) ExpectScreen(SCREEN_MAIN);
                else { ImGuiWindow* w = ImGui::FindWindowByName("Serial Link"); if (w && w->DockNode) ++report->failures; }   // must be floating now
                Activate("Navigation", ICON_LC_FLASK_CONICAL "  Testing"); Begin(c % 5 == 2 ? "nav:Testing(real button; layout re-docks)" : "nav:Testing(real button)");
                if (c % 5 == 2) ++report->redocks;
            });
            // 5. Analysis
            m_Plan.push_back([this] { ExpectScreen(SCREEN_TESTING); Activate("Navigation", ICON_LC_CHART_LINE "  Analysis"); Begin("nav:Analysis(real button)"); });
            // 6. Replay + open the recording
            m_Plan.push_back([this] { ExpectScreen(SCREEN_ANALYSIS); Activate("Navigation", ICON_LC_HISTORY "  Replay"); Begin("nav:Replay(real button)"); });
            m_Plan.push_back([this] { ExpectScreen(SCREEN_REPLAY); if (Hub().Player().Load(m_RecPath)) ++report->replayLoads; else ++report->failures; Begin("replay:open(Player.Load)"); });
            // 7. (every 10th) the REAL Browse button -> native dialog, cancelled by the helper
            if (c % 10 == 5)
            {
                m_Plan.push_back([this]
                {
                    m_Canceller.armed = true; ++m_Dialogs;
                    Activate("Replay", "Browse...##tp_browse");
                    Begin("dialog:Browse(real button)->IFileDialog, cancelled by helper");
                    m_Wait = 6;
                });
                m_Plan.push_back([this]
                {
                    if (m_Canceller.armed) { m_Canceller.armed = false; ++report->failures; LogLine("# FAIL: dialog never appeared / was not cancelled"); }
                    Begin("dialog:frame resumed after cancel");
                });
            }
            // 8. close the replay with the REAL Unload button
            m_Plan.push_back([this] { Activate("Replay", "  Unload  ##tp_unload"); Begin("replay:close(real Unload button)"); });
            // 9. Home
            m_Plan.push_back([this]
            {
                if (Hub().Player().IsLoaded()) { ++report->failures; LogLine("# FAIL: Unload button did not unload"); } else ++report->replayUnloads;
                Activate("Navigation", ICON_LC_HOME "  Home"); Begin("nav:Home(real button)");
            });
            // 10. window operations
            m_Plan.push_back([this, c]
            {
                ExpectScreen(SCREEN_HOME);
                auto& win = Cosmic::Application::Get().GetWindow();
                if (c % 5 == 0)       { ShowWindow(m_Hwnd, SW_MINIMIZE); ++report->minimizes; Begin("window:minimize"); }
                else if (c % 5 == 1)  { m_BigWindow = !m_BigWindow; win.SetSize(m_BigWindow ? 1280 : 1000, m_BigWindow ? 720 : 600); ++report->resizes; Begin("window:resize"); }
                else if (c % 25 == 3) { PostF11(); Begin("window:F11 fullscreen (real hotkey)"); m_Wait = 6; }
                else                  { Begin("window:no-op"); }
            });
            m_Plan.push_back([this, c]
            {
                if (c % 5 == 0)       { if (!IsIconic(m_Hwnd)) ++report->failures; ShowWindow(m_Hwnd, SW_RESTORE); ++report->restores; Begin("window:restore"); }
                else if (c % 25 == 3) { if (!Cosmic::Application::Get().GetWindow().IsFullscreen()) ++report->failures; PostF11(); Begin("window:F11 fullscreen back (real hotkey)"); m_Wait = 6; }
                else                  { Begin("window:no-op"); }
            });
        }

        void OnDetach() override
        {
            m_Canceller.Stop();
            m_Oracle.Uninstall();   // this DLL's callback/hook: never outlive the module
            SF_Telem::OnDetach();
            m_Log.close();
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
        _dupenv_s(&value, &len, "COSMIC_WO07_L05_REPORT");
        if (!value) return nullptr;
        report = reinterpret_cast<WO07UiCyclesReport*>(_strtoui64(value, nullptr, 16));
        free(value);
        char* out = nullptr; _dupenv_s(&out, &len, "COSMIC_WO07_L05_OUT");
        outDir = out ? out : "."; free(out);
        auto transport = std::make_unique<Cosmic::FakeSerialTransport>();
        auto* fake = transport.get(); fake->SetAvailablePorts({ "COM_FAKE" });
        auto* root = new UiCyclesRoot(std::move(transport)); root->Observe(fake);
        return root;
    }
}
