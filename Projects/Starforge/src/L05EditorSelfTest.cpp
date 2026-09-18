// L05EditorSelfTest.cpp — WO-07 (2D stability): the editor half of L05 — scripted UI
// cycles over the REAL Starforge editor with a per-action Dear ImGui balance oracle.
//
// Armed by COSMIC_L05_SELFTEST=<result-file> (plus COSMIC_L05_OUT=<dir> for the action
// log + screenshots and COSMIC_L05_CYCLES, default 200). It opens a real 2D edit scene
// and, every cycle, drives through Dear ImGui's input queue the REAL viewport-strip
// chips — the three snap chips (KI-1's), the Grid and Collider toggles and the
// World/Local button, each clicked with real mouse press/release events at the rect
// the strip itself reported (the gated probe) and each confirmed to have toggled its
// state — then applies the built-in layout presets in rotation (every panel re-docks:
// the dock/undock churn), hides and re-shows the viewport, enters and stops Play,
// minimizes/restores, resizes, and toggles fullscreen with the real F11 hotkey.
//
// After EVERY action the oracle (tests/WO07UiOracle.h) is judged: no recovered ImGui
// error, no leaked depth at EndFramePre, no context/font drift — plus the depths read
// AT THE WIDGET: every probed chip's colour-stack delta must be zero and the strip
// draw must balance (the KI-1 mechanism). Every action is appended to an exact action
// log; screenshots of the presented window are saved at fixed cycles and on the first
// failure. Verdict -> exit code (PASS -> 0, FAIL -> quick_exit(1)); JSON always written.
#include "StarforgeApp.h"
#include "LayoutPresets.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"
#include "utils/ImageIO.h"
#include "../../../tests/WO07UiOracle.h"
#include "../../../tests/WO05NativeWindow.h"
#include <imgui.h>
#include <imgui_internal.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace Starforge
{
    namespace
    {
        typedef void (APIENTRY* PFN_glReadBuffer)(unsigned int);
        typedef void (APIENTRY* PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
        typedef void (APIENTRY* PFN_glPixelStorei)(unsigned int, int);
        typedef void (APIENTRY* PFN_glGetIntegerv)(unsigned int, int*);
        typedef void (APIENTRY* PFN_glBindFramebuffer)(unsigned int, unsigned int);
        typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);

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
            std::vector<unsigned char> rgba((size_t)w * h * 4), flipped((size_t)w * h * 4);
            int prevRead = 0; getIntegerv(0x8CAA, &prevRead);
            bindFbo(0x8CA8, 0);
            readBuffer(0x0404); pixelStore(0x0D05, 1);
            readPixels(0, 0, w, h, 0x1908, 0x1401, rgba.data());
            readBuffer(0x0405); bindFbo(0x8CA8, (unsigned)prevRead);
            for (int y = 0; y < h; ++y)
                std::memcpy(&flipped[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
            return Cosmic::ImageIO::WritePNG(path, w, h, 4, flipped.data());
        }
    }

    struct StarforgeApp::L05EditorSelfTest
    {
        std::string resultPath, outDir;
        int cyclesPlanned = 200;
        int cycle = 0, step = 0, wait = 0, frame = 0, warm = 0, stable = 0;
        float lastCx = -1, lastCy = -1;
        bool ready = false, finished = false, firstFailureShot = false, bigWindow = true, fullscreen = false;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        CosmicTest::UiOracle oracle;
        std::ofstream log;
        HWND hwnd = nullptr;

        // strip probe (filled by the real strip each draw)
        ViewportController::Ki1ChipProbe probe;
        int  stripImbalance = 0;      // whole-strip depth deltas (PreStrip vs PostStrip)
        int  chipImbalance = 0;       // per-chip colorDelta != 0
        int  chipsProbedThisFrame = 0;
        struct Depths { int color = 0, styleVar = 0, font = 0, popup = 0, id = 0; } before;
        static Depths Capture()
        {
            Depths d;
            ImGuiContext* g = ImGui::GetCurrentContext();
            d.color = g->ColorStack.Size; d.styleVar = g->StyleVarStack.Size; d.font = g->FontStack.Size;
            d.popup = g->BeginPopupStack.Size; d.id = g->CurrentWindow ? g->CurrentWindow->IDStack.Size : 0;
            return d;
        }

        // click machinery
        int clickChip = -1, clickPhase = 0; float cx = 0, cy = 0;
        std::function<bool()> clickCheck;   // true == the click had its documented effect
        std::string pending;
        CosmicTest::UiOracle::Snap snap{};
        int stripAtSnap = 0, chipAtSnap = 0;

        // results
        int actions = 0, actionsFailed = 0, clicks = 0, clicksRegistered = 0, presets = 0, viewportToggles = 0;
        int plays = 0, minimizes = 0, resizes = 0, fullscreens = 0, screenshots = 0, failures = 0;
        std::vector<std::string> failed;
        std::vector<std::function<void()>> plan;

        void LogLine(const std::string& s) { if (log) { log << s << "\n"; log.flush(); } }
    };


    void StarforgeApp::L05SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_L05_SELFTEST");
        if (!rp || !*rp) return;
        m_L05 = new L05EditorSelfTest();
        auto& t = *m_L05;
        t.resultPath = rp;
        if (const char* o = std::getenv("COSMIC_L05_OUT")) t.outDir = o; else t.outDir = ".";
        if (const char* n = std::getenv("COSMIC_L05_CYCLES")) t.cyclesPlanned = std::atoi(n);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        // A real, empty 2D edit scene so the real viewport strip renders (as KI-1).
        NewScene();
        m_Ctx.ProjectOpen = true;
        m_Ctx.ProjectName = "L05SelfTest";
        m_Mode2D          = true;
        m_OpenFirstRun    = false;
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->SetViewportVisible(true);
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        Cosmic::Application::Get().SetPauseOnMinimize(false);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        t.oracle.Install();
        t.log.open(t.outDir + "/l05-editor-actions.txt", std::ios::trunc);
        t.LogLine("# WO-07 L05 Starforge editor scripted UI cycles — one line per judged action");
        CS_INFO("[L05 editor self-test] armed; result -> {}", rp);
    }

    void StarforgeApp::L05SelfTestShutdown()
    {
        if (!m_L05) return;
        m_L05->oracle.Uninstall();
        delete m_L05; m_L05 = nullptr;
    }

    void StarforgeApp::L05SelfTestPreStrip()
    {
        if (!m_L05) return;
        m_L05->probe.count = 0; m_L05->probe.haveWorldLocal = false;
        ViewportController::s_Ki1Probe = &m_L05->probe;
        m_L05->before = L05EditorSelfTest::Capture();
    }

    void StarforgeApp::L05SelfTestPostStrip()
    {
        if (!m_L05) return;
        auto& t = *m_L05;
        const auto after = L05EditorSelfTest::Capture();
        if (after.color != t.before.color || after.styleVar != t.before.styleVar || after.font != t.before.font ||
            after.popup != t.before.popup || after.id != t.before.id)
            ++t.stripImbalance;
        for (int i = 0; i < t.probe.count; ++i) if (t.probe.colorDelta[i] != 0) ++t.chipImbalance;
        if (t.probe.haveWorldLocal && t.probe.colorDelta[5] != 0) ++t.chipImbalance;
        t.chipsProbedThisFrame = t.probe.count + (t.probe.haveWorldLocal ? 1 : 0);
        ViewportController::s_Ki1Probe = nullptr;
        t.oracle.CheckContexts();

        // Drive the pending click through Dear ImGui's input queue (press, hold, release).
        if (t.clickChip >= 0)
        {
            auto& io = ImGui::GetIO();
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(t.cx, t.cy);
            if (t.clickPhase == 0)      io.AddMouseButtonEvent(0, true);
            else if (t.clickPhase == 2) io.AddMouseButtonEvent(0, false);
            if (++t.clickPhase > 2) t.clickChip = -1;   // released: the button fires next frame
        }
    }

    void StarforgeApp::L05SelfTestTick()
    {
        if (!m_L05) return;
        auto& t = *m_L05;
        if (t.finished) return;
        ++t.frame;
        auto& app = Cosmic::Application::Get();
        if (!t.hwnd) t.hwnd = WO05NativeWindow(app.GetWindow());
        if (std::chrono::steady_clock::now() - t.start > std::chrono::seconds(600)) { ++t.failures; t.finished = true; }

        auto begin = [&](const std::string& what)
        {
            t.pending = what; t.snap = t.oracle.Take(); t.stripAtSnap = t.stripImbalance; t.chipAtSnap = t.chipImbalance; t.wait = 3;
        };
        auto judge = [&]()
        {
            std::string why = t.oracle.Judge(t.snap);
            if (t.stripImbalance != t.stripAtSnap) why += "strip-imbalance ";
            if (t.chipImbalance  != t.chipAtSnap)  why += "chip-colorDelta ";
            if (t.clickCheck) { if (t.clickCheck()) ++t.clicksRegistered; else why += "click-not-registered "; t.clickCheck = nullptr; }
            ++t.actions;
            ImGuiContext* g = ImGui::GetCurrentContext();
            char depths[96];
            std::snprintf(depths, sizeof(depths), "C%d S%d F%d P%d chips=%d", g->ColorStack.Size, g->StyleVarStack.Size, g->FontStack.Size, g->BeginPopupStack.Size, t.chipsProbedThisFrame);
            t.LogLine("cycle=" + std::to_string(t.cycle) + " frame=" + std::to_string(t.frame) + " action=" + t.pending + " " + depths +
                      " errors=" + std::to_string(t.oracle.recoveredErrors.load()) + (why.empty() ? " result=ok" : " result=FAIL " + why));
            if (!why.empty())
            {
                ++t.actionsFailed; t.failed.push_back("cycle " + std::to_string(t.cycle) + " " + t.pending + ": " + why);
                if (!t.firstFailureShot) { t.firstFailureShot = true; if (SaveScreenshot(t.outDir + "/l05-editor-shot-first-failure.png")) ++t.screenshots; }
            }
        };
        auto click = [&](int chip, std::function<bool()> check, const char* name)
        {
            const bool ok = chip == 5 ? t.probe.haveWorldLocal : chip < t.probe.count;
            if (!ok) { ++t.failures; begin(std::string("click:") + name + " (chip not probed!)"); return; }
            t.cx = t.probe.cx[chip]; t.cy = t.probe.cy[chip]; t.clickChip = chip; t.clickPhase = 0; t.clickCheck = std::move(check); ++t.clicks;
            begin(std::string("click:") + name);
            t.wait = 5;   // press / hold / release + the frame the button fires + one more
        };

        // ---- warm-up: wait for the strip rects to hold still ----
        if (!t.ready)
        {
            if (t.probe.count >= 3)
            {
                if (std::abs(t.probe.cx[2] - t.lastCx) < 0.5f && std::abs(t.probe.cy[2] - t.lastCy) < 0.5f) ++t.stable; else t.stable = 0;
                t.lastCx = t.probe.cx[2]; t.lastCy = t.probe.cy[2];
                if (t.stable >= 8) { t.ready = true; if (SaveScreenshot(t.outDir + "/l05-editor-shot-c000-boot.png")) ++t.screenshots; }
            }
            if (++t.warm > 600 && !t.ready) { ++t.failures; t.finished = true; }
            if (!t.finished) return;
        }

        if (!t.finished)
        {
            if (t.wait > 0) { if (--t.wait == 0) { judge(); ++t.step; } return; }
            if (t.plan.empty())
            {
                const int c = t.cycle;
                Prefs::EditorSettings s0; m_Viewport.SaveSnapPrefs(s0);
                // 1-3: the KI-1 snap chips (real mouse clicks at the probed rects), each confirmed to toggle
                t.plan.push_back([&, c] { Prefs::EditorSettings b; m_Viewport.SaveSnapPrefs(b); const bool was = b.SnapScaleOn;  click(2, [&, was] { Prefs::EditorSettings a; m_Viewport.SaveSnapPrefs(a); return a.SnapScaleOn  != was; }, "Scale snap chip"); });
                t.plan.push_back([&, c] { Prefs::EditorSettings b; m_Viewport.SaveSnapPrefs(b); const bool was = b.SnapMoveOn;   click(0, [&, was] { Prefs::EditorSettings a; m_Viewport.SaveSnapPrefs(a); return a.SnapMoveOn   != was; }, "Move snap chip"); });
                t.plan.push_back([&, c] { Prefs::EditorSettings b; m_Viewport.SaveSnapPrefs(b); const bool was = b.SnapRotateOn; click(1, [&, was] { Prefs::EditorSettings a; m_Viewport.SaveSnapPrefs(a); return a.SnapRotateOn != was; }, "Rotate snap chip"); });
                // 4-6: the view toggles + World/Local
                t.plan.push_back([&] { const bool was = m_Viewport.ShowGridEnabled();      click(3, [&, was] { return m_Viewport.ShowGridEnabled()      != was; }, "Grid toggle"); });
                t.plan.push_back([&] { const bool was = m_Viewport.ShowCollidersEnabled(); click(4, [&, was] { return m_Viewport.ShowCollidersEnabled() != was; }, "Collider toggle"); });
                t.plan.push_back([&] { const bool was = m_Viewport.GizmoSpaceIsWorld();    click(5, [&, was] { return m_Viewport.GizmoSpaceIsWorld()    != was; }, "World/Local button"); });
                // 7: layout preset (every panel re-docks) — dock/undock churn through the real preset path
                t.plan.push_back([&, c] { const auto& names = LayoutPresets::BuiltIns(); const std::string& n = names[c % names.size()]; ApplyLayoutPreset(n); ++t.presets; begin("layout:preset " + n); });
                // 8-9: viewport hidden / shown
                t.plan.push_back([&] { if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->SetViewportVisible(false); ++t.viewportToggles; begin("viewport:hide"); });
                t.plan.push_back([&] { if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->SetViewportVisible(true);  ++t.viewportToggles; begin("viewport:show"); t.wait = 4; });
                // 10-11: Play / Stop (every 10th cycle)
                if (c % 10 == 4)
                {
                    t.plan.push_back([&] { PlayScene(); if (!IsPlaying()) ++t.failures; ++t.plays; begin("play:start"); t.wait = 4; });
                    t.plan.push_back([&] { StopScene(); if (IsPlaying()) ++t.failures; begin("play:stop"); t.wait = 4; });
                }
                // 12-13: window operations
                t.plan.push_back([&, c]
                {
                    if (c % 5 == 0)       { ShowWindow(t.hwnd, SW_MINIMIZE); ++t.minimizes; begin("window:minimize"); }
                    else if (c % 5 == 1)  { t.bigWindow = !t.bigWindow; Cosmic::Application::Get().GetWindow().SetSize(t.bigWindow ? 1280 : 1000, t.bigWindow ? 720 : 600); ++t.resizes; begin("window:resize"); t.wait = 6; }
                    else if (c % 25 == 3) { const UINT sc = MapVirtualKeyW(VK_F11, MAPVK_VK_TO_VSC); PostMessage(t.hwnd, WM_KEYDOWN, VK_F11, (LPARAM)((sc << 16) | 1)); PostMessage(t.hwnd, WM_KEYUP, VK_F11, (LPARAM)((sc << 16) | 1 | (1u << 30) | (1u << 31))); t.fullscreen = !t.fullscreen; ++t.fullscreens; begin("window:F11 fullscreen (real hotkey)"); t.wait = 8; }
                    else                  { begin("window:no-op"); }
                });
                t.plan.push_back([&, c]
                {
                    if (c % 5 == 0)       { if (!IsIconic(t.hwnd)) ++t.failures; ShowWindow(t.hwnd, SW_RESTORE); begin("window:restore"); t.wait = 6; }
                    else if (c % 25 == 3) { if (!Cosmic::Application::Get().GetWindow().IsFullscreen()) ++t.failures; const UINT sc = MapVirtualKeyW(VK_F11, MAPVK_VK_TO_VSC); PostMessage(t.hwnd, WM_KEYDOWN, VK_F11, (LPARAM)((sc << 16) | 1)); PostMessage(t.hwnd, WM_KEYUP, VK_F11, (LPARAM)((sc << 16) | 1 | (1u << 30) | (1u << 31))); t.fullscreen = !t.fullscreen; ++t.fullscreens; begin("window:F11 fullscreen back (real hotkey)"); t.wait = 8; }
                    else                  { begin("window:no-op"); }
                });
            }
            if (t.step < (int)t.plan.size()) { t.plan[t.step](); if (t.wait == 0) { judge(); ++t.step; } return; }
            // cycle complete
            if (t.cycle % 50 == 0 || t.cycle == t.cyclesPlanned - 1)
            {
                char name[64]; std::snprintf(name, sizeof(name), "/l05-editor-shot-c%03d.png", t.cycle);
                if (SaveScreenshot(t.outDir + name)) ++t.screenshots;
            }
            t.plan.clear(); t.step = 0; ++t.cycle;
            if (t.cycle < t.cyclesPlanned) return;
            t.finished = true;
            if (t.fullscreen) { app.GetWindow().SetFullscreen(false); t.fullscreen = false; }
        }

        // ---- finish: verdict + JSON ----
        const bool pass = t.failures == 0 && t.actionsFailed == 0 && t.cycle >= t.cyclesPlanned &&
                          t.clicks == t.clicksRegistered && t.clicks >= t.cyclesPlanned * 6 &&
                          t.oracle.recoveredErrors == 0 && t.oracle.endFrameLeaks == 0 && t.oracle.contextDrift == 0 &&
                          t.stripImbalance == 0 && t.chipImbalance == 0;
#if defined(NDEBUG)
        const char* cfg = "Release";
#else
        const char* cfg = "Debug";
#endif
        t.LogLine("# done: cycles=" + std::to_string(t.cycle) + " actions=" + std::to_string(t.actions) + " failed=" + std::to_string(t.actionsFailed));
        std::ofstream f(t.resultPath, std::ios::trunc);
        if (f)
        {
            f << "{\n  \"work_order\": \"WO-07\",\n  \"case\": \"L05 editor scripted UI cycles\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"cycles_planned\": " << t.cyclesPlanned << ",\n  \"cycles_done\": " << t.cycle << ",\n";
            f << "  \"actions\": " << t.actions << ",\n  \"actions_failed\": " << t.actionsFailed << ",\n";
            f << "  \"chip_clicks\": " << t.clicks << ",\n  \"chip_clicks_registered\": " << t.clicksRegistered << ",\n";
            f << "  \"layout_presets\": " << t.presets << ",\n  \"viewport_toggles\": " << t.viewportToggles << ",\n  \"play_sessions\": " << t.plays << ",\n";
            f << "  \"minimizes\": " << t.minimizes << ",\n  \"resizes\": " << t.resizes << ",\n  \"fullscreen_toggles\": " << t.fullscreens << ",\n";
            f << "  \"screenshots\": " << t.screenshots << ",\n";
            f << "  \"oracle\": { \"recovered_errors\": " << t.oracle.recoveredErrors << ", \"end_frame_leaks\": " << t.oracle.endFrameLeaks
              << ", \"context_drift\": " << t.oracle.contextDrift << ", \"strip_imbalance\": " << t.stripImbalance << ", \"chip_color_delta\": " << t.chipImbalance << " },\n";
            f << "  \"harness_failures\": " << t.failures << ",\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"failed_actions\": [\n";
            for (size_t i = 0; i < t.failed.size() && i < 50; ++i) f << "    \"" << t.failed[i] << "\"" << (i + 1 < std::min<size_t>(t.failed.size(), 50) ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        std::printf("L05_EDITOR_RESULT=%s config=%s cycles=%d actions=%d failed=%d clicks=%d/%d presets=%d plays=%d errors=%d leaks=%d strip=%d chip=%d\n",
                    pass ? "PASS" : "FAIL", cfg, t.cycle, t.actions, t.actionsFailed, t.clicksRegistered, t.clicks, t.presets, t.plays,
                    t.oracle.recoveredErrors.load(), t.oracle.endFrameLeaks.load(), t.stripImbalance, t.chipImbalance);
        std::fflush(stdout);
        if (pass) Cosmic::Application::Get().Close();
        else      std::quick_exit(1);
    }
}
