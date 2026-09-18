// Ki1SnapChipSelfTest.cpp — WO-07 (2D stability): the KI-1 snap-chip regression.
//
// KI-1 is a confirmed defect that SHIPS in the 2D editor: the viewport-strip snap
// chip (ViewportController::DrawViewportOverlays -> the `snapChip` lambda) pushes a
// style colour guarded on `on`, the button flips `on`, then pops guarded on the
// CHANGED `on` — so every click leaves Dear ImGui's colour stack off by one. In a
// Debug build ImGui's own IM_ASSERT catches that (abort); in a Release build
// IM_ASSERT compiles out, so it is silent stack corruption a whole-frame
// "did it crash?" check never sees.
//
// This harness drives the REAL production control — no copy of the lambda, no
// second widget. When armed by the env var COSMIC_KI1_SELFTEST=<result-file>, it
// opens a real 2D edit scene, waits for the real viewport strip to render, and then
// actuates the real Scale snap chip through Dear ImGui (mouse events into the live
// IO queue) for BOTH toggle directions, twice. Around EVERY strip draw it reads the
// actual ImGui stack depths (Release-safe, via <imgui_internal.h>) and fails on any
// imbalance. It confirms each click truly toggled the chip (SaveSnapPrefs), so a
// mis-aimed click can never masquerade as a pass.
//
// Verdict -> process exit code (the acceptance runner's oracle):
//   * PASS  : all four clicks registered and every strip draw balanced -> the editor
//             is closed gracefully and main() returns 0.
//   * FAIL  : a Release imbalance, or a click that did not register -> quick_exit(1).
//   * Debug : the buggy code aborts inside DrawViewportOverlays before this returns —
//             the runner records that crash as the failing-before evidence.
// A JSON result file is always written (except on the Debug abort) for the WO-07
// evidence bundle.

#include "StarforgeApp.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"   // SetViewportVisible (complete type)
#include <imgui.h>
#include <imgui_internal.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace Starforge
{
    namespace
    {
        struct Depths { int color = 0, styleVar = 0, font = 0, popup = 0, id = 0; };

        Depths CaptureDepths()
        {
            Depths d;
            ImGuiContext* g = ImGui::GetCurrentContext();
            if (!g)
                return d;
            d.color    = g->ColorStack.Size;
            d.styleVar = g->StyleVarStack.Size;
            d.font     = g->FontStack.Size;
            d.popup    = g->BeginPopupStack.Size;
            if (ImGuiWindow* w = g->CurrentWindow)
                d.id = w->IDStack.Size;
            return d;
        }
    }

    // ---- state machine ------------------------------------------------------
    struct StarforgeApp::Ki1SnapSelfTest
    {
        enum Phase { Warmup, ArmPress, HoldPress, ArmRelease, CheckToggle, Finish, Done };

        std::string resultPath;
        Phase       phase        = Warmup;
        int         warmupFrames = 0;
        int         stableFrames = 0;       // consecutive frames the chip rect held still
        float       lastCx = -1, lastCy = -1;
        int         actuation    = 0;
        static constexpr int kActuations = 4;   // OFF->ON, ON->OFF, twice (both directions)

        Depths before;                          // captured in PreStrip

        // click geometry (the REAL Scale-chip centre, learned from the probe)
        ViewportController::Ki1ChipProbe probe;
        float cx = 0, cy = 0;
        bool  prevFlag = false;                 // Scale-snap flag latched before a click

        // results
        bool imbalance     = false;
        bool notRegistered = false;
        int  registered    = 0;
        Depths worst;                           // worst |delta| seen (per field)
        std::vector<std::string> lines;

        void note(const char* fmt, ...)
        {
            char buf[256];
            va_list ap; va_start(ap, fmt);
            _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
            va_end(ap);
            lines.emplace_back(buf);
        }
    };

    void StarforgeApp::Ki1SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_KI1_SELFTEST");
        if (!rp || !*rp)
            return;

        m_Ki1 = new Ki1SnapSelfTest();
        m_Ki1->resultPath = rp;

#if defined(_DEBUG)
        // Route the Debug CRT assertion (ImGui's IM_ASSERT == assert()) to stderr,
        // and strip abort()'s own "abort() has been called" message box + fault
        // report, so KI-1's failing-before Debug abort is a clean, non-hanging
        // nonzero exit for the child process — exactly the "isolated child processes
        // for aborting before-reproductions" the work order asks for.
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        // Never let a crash/abort raise the Windows error-report UI (the child must
        // exit for the runner, not block on a dialog). Applies in both configs.
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

        // Open a real, empty 2D edit scene so the REAL viewport strip renders (the
        // snap chip only draws with a project + scene open, and not while playing).
        NewScene();
        m_Ctx.ProjectOpen  = true;
        m_Ctx.ProjectName  = "Ki1SelfTest";
        m_Mode2D           = true;
        // OnAttach arms the first-run sample offer when the bundled sample is absent
        // (it is, in the isolated test user-data root). That modal popup would sit in
        // front of the viewport and swallow every injected click. Suppress it so the
        // clicks reach the real snap chip.
        m_OpenFirstRun = false;
        if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())
            ws->SetViewportVisible(true);

        // The window stays shown (as the engine reveals it): Dear ImGui suppresses
        // FindHoveredWindow while the platform window is hidden, so a hidden window
        // would make every injected click miss. VSync off keeps the ~20-frame run fast.
        Cosmic::Application::Get().GetWindow().SetVSync(false);

        // Multi-viewport is on (ImGuiConfigFlags_ViewportsEnable). The GLFW backend
        // sets ImGuiBackendFlags_HasMouseHoveredViewport from the real OS cursor —
        // which is never over our window during an automated run — so it pins the
        // mouse to viewport 0 and FindHoveredWindow rejects the docked strip. Clear
        // that hint so ImGui resolves the injected cursor's viewport geometrically
        // from its position; nothing else about the editor changes.
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;

        m_Ki1->note("armed: opened empty 2D edit scene, hid window");
        CS_INFO("[KI-1 self-test] armed; result -> {}", rp);
    }

    void StarforgeApp::Ki1SelfTestShutdown()
    {
        delete m_Ki1;
        m_Ki1 = nullptr;
    }

    void StarforgeApp::Ki1SelfTestPreStrip()
    {
        if (!m_Ki1)
            return;
        // Arm the probe so the strip records the three real snap-chip rects, and
        // snapshot the ImGui stacks so PostStrip can prove the draw balanced.
        m_Ki1->probe.count = 0;
        ViewportController::s_Ki1Probe = &m_Ki1->probe;
        m_Ki1->before = CaptureDepths();
    }

    void StarforgeApp::Ki1SelfTestPostStrip()
    {
        if (!m_Ki1)
            return;
        auto& t = *m_Ki1;

        // ---- 1) balance check for the strip draw that just ran ----
        const Depths after = CaptureDepths();
        const int dC = after.color    - t.before.color;
        const int dS = after.styleVar - t.before.styleVar;
        const int dF = after.font     - t.before.font;
        const int dP = after.popup    - t.before.popup;
        const int dI = after.id       - t.before.id;
        if (dC || dS || dF || dP || dI)
        {
            t.imbalance = true;
            if (std::abs(dC) > std::abs(t.worst.color))    t.worst.color    = dC;
            if (std::abs(dS) > std::abs(t.worst.styleVar)) t.worst.styleVar = dS;
            if (std::abs(dF) > std::abs(t.worst.font))     t.worst.font     = dF;
            if (std::abs(dP) > std::abs(t.worst.popup))    t.worst.popup    = dP;
            if (std::abs(dI) > std::abs(t.worst.id))       t.worst.id       = dI;
            t.note("WHOLE-STRIP IMBALANCE at actuation %d phase %d: color=%d styleVar=%d font=%d popup=%d id=%d",
                   t.actuation, (int)t.phase, dC, dS, dF, dP, dI);
        }
        // Per-chip colour-stack delta, measured at the widget (before ImGui's
        // end-of-window recovery masks it). This is the Release-safe KI-1 oracle:
        // the whole-strip check above nets to zero in Release because 1.92 recovers
        // the stack at EndChild, so a naive after-frame read misses the defect.
        for (int i = 0; i < t.probe.count && i < 3; ++i)
            if (t.probe.colorDelta[i] != 0)
            {
                t.imbalance = true;
                if (std::abs(t.probe.colorDelta[i]) > std::abs(t.worst.color))
                    t.worst.color = t.probe.colorDelta[i];
                t.note("CHIP IMBALANCE at actuation %d phase %d: snap chip %d colorDelta=%d",
                       t.actuation, (int)t.phase, i, t.probe.colorDelta[i]);
            }

        ViewportController::s_Ki1Probe = nullptr;   // detach until the next PreStrip

        auto& io = ImGui::GetIO();
        const glm::vec2 vpSize = Cosmic::Application::Get().GetViewportSize();

        // The REAL Scale-snap chip centre (probe index 2), captured by the strip
        // this very frame — no coordinate guessing, no copy of the widget.
        const bool haveChip = t.probe.count >= 3;
        const float cx = haveChip ? t.probe.cx[2] : 0.0f;
        const float cy = haveChip ? t.probe.cy[2] : 0.0f;

        // ---- 2) drive the state machine ----
        Prefs::EditorSettings snap;
        m_Viewport.SaveSnapPrefs(snap);

        switch (t.phase)
        {
        case Ki1SnapSelfTest::Warmup:
            ++t.warmupFrames;
            // The dockspace/viewport keeps resizing for the first frames, which
            // shifts the chip rect (608 vs an early 569). Only click once the chip
            // centre has held still for several consecutive frames.
            if (vpSize.x >= 40.0f && vpSize.y >= 40.0f && haveChip)
            {
                if (std::abs(cx - t.lastCx) < 0.5f && std::abs(cy - t.lastCy) < 0.5f)
                    ++t.stableFrames;
                else
                    t.stableFrames = 0;
                t.lastCx = cx; t.lastCy = cy;
                if (t.stableFrames >= 8)
                {
                    t.note("viewport %gx%g; snap chips at (%.1f,%.1f)(%.1f,%.1f)(%.1f,%.1f) (stable)",
                           vpSize.x, vpSize.y,
                           t.probe.cx[0], t.probe.cy[0], t.probe.cx[1], t.probe.cy[1],
                           t.probe.cx[2], t.probe.cy[2]);
                    t.phase = Ki1SnapSelfTest::ArmPress;
                }
            }
            if (t.phase == Ki1SnapSelfTest::Warmup && t.warmupFrames > 600)
            {
                t.notRegistered = true;
                t.note("FAIL: strip never stabilised in 600 frames (vp %gx%g, chips=%d)",
                       vpSize.x, vpSize.y, t.probe.count);
                t.phase = Ki1SnapSelfTest::Finish;
            }
            break;

        case Ki1SnapSelfTest::ArmPress:
            t.cx = cx; t.cy = cy;
            t.prevFlag = snap.SnapScaleOn;
            // Multi-viewport is enabled (ImGuiConfigFlags_ViewportsEnable): hover is
            // rejected unless the mouse is bound to the window's platform viewport,
            // so pin the injected cursor to the main viewport the docked strip lives on.
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(cx, cy);
            io.AddMouseButtonEvent(0, true);
            t.phase = Ki1SnapSelfTest::HoldPress;
            break;

        case Ki1SnapSelfTest::HoldPress:
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(t.cx, t.cy);   // keep hovering; button stays down
            if (t.actuation == 0)
            {
                ImGuiContext* g = ImGui::GetCurrentContext();
                const char* hw = (g && g->HoveredWindow) ? g->HoveredWindow->Name : "(none)";
                // Proof the injected press lands on the REAL strip's snap chip: the
                // hovered window is the viewport strip child and an item is active.
                t.note("press lands on: hoveredWin='%s' hoveredId=%u activeId=%u at (%.0f,%.0f)",
                       hw, g ? g->HoveredId : 0u, g ? g->ActiveId : 0u, io.MousePos.x, io.MousePos.y);
            }
            t.phase = Ki1SnapSelfTest::ArmRelease;
            break;

        case Ki1SnapSelfTest::ArmRelease:
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(t.cx, t.cy);
            io.AddMouseButtonEvent(0, false);  // release over the chip -> Button returns true next frame
            t.phase = Ki1SnapSelfTest::CheckToggle;
            break;

        case Ki1SnapSelfTest::CheckToggle:
        {
            const bool now = snap.SnapScaleOn;
            if (now != t.prevFlag)
            {
                ++t.registered;
                t.note("actuation %d registered: %s->%s at (%.1f,%.1f)",
                       t.actuation, t.prevFlag ? "ON" : "OFF", now ? "ON" : "OFF", t.cx, t.cy);
            }
            else
            {
                t.notRegistered = true;
                t.note("actuation %d DID NOT REGISTER (flag stayed %s); target (%.1f,%.1f) io.MousePos (%.1f,%.1f) down=%d",
                       t.actuation, now ? "ON" : "OFF", t.cx, t.cy,
                       io.MousePos.x, io.MousePos.y, io.MouseDown[0] ? 1 : 0);
            }
            if (++t.actuation < Ki1SnapSelfTest::kActuations)
                t.phase = Ki1SnapSelfTest::ArmPress;
            else
                t.phase = Ki1SnapSelfTest::Finish;
            break;
        }

        case Ki1SnapSelfTest::Finish:
        {
            const bool pass = !t.imbalance && !t.notRegistered &&
                              t.registered == Ki1SnapSelfTest::kActuations;
#if defined(NDEBUG)
            const char* cfg = "Release";
#else
            const char* cfg = "Debug";
#endif
            std::ofstream f(t.resultPath, std::ios::trunc);
            if (f)
            {
                f << "{\n";
                f << "  \"work_order\": \"WO-07\",\n";
                f << "  \"case\": \"KI-1 snap-chip regression\",\n";
                f << "  \"config\": \"" << cfg << "\",\n";
                f << "  \"control\": \"ViewportController::DrawViewportOverlays snapChip (production)\",\n";
                f << "  \"actuations_planned\": " << Ki1SnapSelfTest::kActuations << ",\n";
                f << "  \"actuations_registered\": " << t.registered << ",\n";
                f << "  \"any_click_unregistered\": " << (t.notRegistered ? "true" : "false") << ",\n";
                f << "  \"stack_imbalance\": " << (t.imbalance ? "true" : "false") << ",\n";
                f << "  \"worst_delta\": { \"color\": " << t.worst.color
                  << ", \"styleVar\": " << t.worst.styleVar
                  << ", \"font\": " << t.worst.font
                  << ", \"popup\": " << t.worst.popup
                  << ", \"id\": " << t.worst.id << " },\n";
                f << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n";
                f << "  \"log\": [\n";
                for (size_t i = 0; i < t.lines.size(); ++i)
                    f << "    \"" << t.lines[i] << "\"" << (i + 1 < t.lines.size() ? "," : "") << "\n";
                f << "  ]\n";
                f << "}\n";
                f.close();
            }
            std::printf("KI1_SELFTEST_RESULT=%s config=%s registered=%d/%d imbalance=%d\n",
                        pass ? "PASS" : "FAIL", cfg, t.registered, Ki1SnapSelfTest::kActuations,
                        t.imbalance ? 1 : 0);
            std::fflush(stdout);

            t.phase = Ki1SnapSelfTest::Done;
            if (pass)
            {
                Cosmic::Application::Get().Close();   // graceful shutdown -> main() returns 0
            }
            else
            {
                // Deterministic nonzero exit for the runner. quick_exit skips static
                // destructors, so the static GL resources are NOT torn down without a
                // context (the AV Main.cpp warns about) — the process simply ends.
                std::quick_exit(1);
            }
            break;
        }

        case Ki1SnapSelfTest::Done:
        default:
            break;
        }
    }
}
