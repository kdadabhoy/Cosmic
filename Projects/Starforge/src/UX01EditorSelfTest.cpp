// UX01EditorSelfTest.cpp — UX-01 (UX & Shipping): the flow-editor / Editors-host acceptance
// cases FE03 / FE04 / FE05 driven inside the REAL Starforge editor
// (docs/plans/ux-shipping-2026-09-24/03-Acceptance-Catalog.md).
//
// Armed by COSMIC_UX01_SELFTEST=<result.json>; COSMIC_UX01_ROOT=<folder the PendulumLab copy
// is made in>, COSMIC_UX01_SHOTS=<folder for the FE04 PNG>. The wrapper
// (tests/acceptance/fixtures/Run-UX01Editor.ps1) runs it from a fresh child CWD, so the
// editor's prefs (user:// — imgui.ini, layouts, the project library) start empty.
//
// Everything goes through the editor's own commands, panels and Dear ImGui's input queue
// (the L05 / AP-03 injected pointer; items located with Dear ImGui's own DebugLocateItem —
// the GUIDE driver's pattern). The harness owns the pointer for the whole run: a NewFramePre
// context hook drops every queued mouse / app-focus event it did not inject, so the real OS
// cursor (moved by the user, or by another GUI test on the same desktop) cannot override an
// injected gesture (the GLFW backend feeds the OS cursor to a focused window every frame). The
// WO-07 ImGui stack oracle (tests/WO07UiOracle.h) is judged after every step.
//
//   FE04  window 1920x1080, a copy of Projects/PendulumLab opened, an injected click on
//         Screens ▸ Flow graph -> the frame after the document opens the "Editors" window is
//         focused and its canvas is >= 800x400 px; once the one-time CenterOnContent has run,
//         every state node's rect lies inside the canvas; a PNG of the editor is saved.
//   FE03  an injected click on the Editors tab's ✕ -> the window is absent the next frames
//         with the document still open; Screens ▸ Flow graph's command shows it again;
//         Close Project leaves AnyOpen() == false (after logging the dirty document it drops).
//   FE05  an injected Content Browser right-click ▸ New ▸ Flow, the new file opened through
//         the Content Browser's double-click request -> Dirty() == false after two drawn
//         frames (and ten); an injected drag of its node dirties it.
//
// Verdict -> exit code: PASS -> graceful close; FAIL -> TerminateProcess(1) after the result JSON
// and the console excerpt are closed (KI-85). JSON always written.

#include "StarforgeApp.h"
#include "editors/FlowEditor.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"
#include "utils/ImageIO.h"
#include "ui/IconsLucide.h"
#include "../../../tests/WO07UiOracle.h"

#include <imgui.h>
#include <imgui_internal.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        std::string Esc(const std::string& s)
        {
            std::string o;
            for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } if (c == '\t') { o += "\\t"; continue; } if (c == '\r') continue; o += c; }
            return o;
        }

        // L05 pattern: the presented front buffer (the whole editor window) into a PNG.
        typedef void (APIENTRY* PFN_glReadBuffer)(unsigned int);
        typedef void (APIENTRY* PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
        typedef void (APIENTRY* PFN_glPixelStorei)(unsigned int, int);
        typedef void (APIENTRY* PFN_glGetIntegerv)(unsigned int, int*);
        typedef void (APIENTRY* PFN_glBindFramebuffer)(unsigned int, unsigned int);
        typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);

        bool SaveWindowShot(const std::string& path, int& wOut, int& hOut)
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
            wOut = w; hOut = h;
            return Cosmic::ImageIO::WritePNG(path, w, h, 4, flipped.data());
        }

        // The item rect DebugLocateItem drew this frame (pure green, alpha 255, in a foreground
        // draw list): where a labelled item lives (the GUIDE driver's locator).
        bool LocatedRect(ImRect& out)
        {
            ImGuiContext& g = *GImGui;
            bool any = false; int taken = 0;
            ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
            for (ImGuiViewportP* vp : g.Viewports)
            {
                ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
                for (int i = 0; i < dl->VtxBuffer.Size && taken < 8; ++i)   // the rect's 8 vertices; the line to the mouse follows
                {
                    const ImDrawVert& v = dl->VtxBuffer[i];
                    if (v.col != IM_COL32(0, 255, 0, 255)) continue;
                    any = true; ++taken;
                    mn.x = std::min(mn.x, v.pos.x); mn.y = std::min(mn.y, v.pos.y);
                    mx.x = std::max(mx.x, v.pos.x); mx.y = std::max(mx.y, v.pos.y);
                }
            }
            if (!any) return false;
            out = ImRect(mn, mx);
            out.Expand(-3.0f);   // DebugLocateItemResolveWithLastItem expands the item rect by 3 px
            return true;
        }

        ImGuiWindow* ActiveWindowNamed(const char* name)
        {
            ImGuiWindow* w = ImGui::FindWindowByName(name);
            return (w && w->Active) ? w : nullptr;
        }

        std::set<std::string> FlowFiles(const fs::path& root)
        {
            std::set<std::string> out;
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() == ".cflow")
                    out.insert(fs::relative(it->path(), root, ec).generic_string());
            return out;
        }
    }

    struct StarforgeApp::UX01SelfTest
    {
        std::string resultPath, root, shots, projectDir;
        const std::string mainFlow = "project://flows/Main.cflow";
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
        CosmicTest::UiOracle oracle;
        int frame = 0, wait = 0, step = 0;
        bool finished = false;
        std::vector<std::function<bool()>> plan;
        std::vector<std::string> planNames;
        double deadlineSec = 90.0;

        // injected pointer (L05 pattern) — consumed in FrameEnd, one per frame
        struct Inject { float x = 0, y = 0; int button = -1; bool down = false; };
        std::vector<Inject> injects;
        ImGuiID locateId = 0;
        bool haveRect = false; ImRect rect;
        std::string shotRequest;
        std::string moveAside; ImVec2 moveAsidePos;   // drag a floating window out of the way (FrameEnd)
        int asideMoves = 0;
        int traceFrames = 0;
        bool expandEditors = false;   // un-collapse the Editors window (FrameEnd)

        // The harness owns the pointer: a NewFramePre hook drops every queued mouse / app-focus
        // event it did not inject (the OS cursor — moved by the user or by another GUI test on
        // the desktop — would otherwise override the injected position on move-only frames,
        // through the GLFW backend's focused-window cursor fallback).
        ImGuiID inputHook = 0;
        std::vector<ImU32> ours;
        int droppedEvents = 0;
        static void OnNewFramePre(ImGuiContext* ctx, ImGuiContextHook* hook)
        {
            auto* self = static_cast<UX01SelfTest*>(hook->UserData);
            ImVector<ImGuiInputEvent>& q = ctx->InputEventsQueue;
            std::vector<ImU32> keep;
            for (int i = 0; i < q.Size;)
            {
                const ImGuiInputEvent& e = q[i];
                const bool isOurs = std::find(self->ours.begin(), self->ours.end(), e.EventId) != self->ours.end();
                const bool pointer = e.Type == ImGuiInputEventType_MousePos || e.Type == ImGuiInputEventType_MouseButton ||
                                     e.Type == ImGuiInputEventType_MouseWheel || e.Type == ImGuiInputEventType_MouseViewport ||
                                     e.Type == ImGuiInputEventType_Focus;
                if (isOurs) keep.push_back(e.EventId);
                if (pointer && !isOurs) { q.erase(q.Data + i); ++self->droppedEvents; continue; }
                ++i;
            }
            self->ours.swap(keep);
        }

        // results
        int failures = 0;
        std::vector<std::string> log, checks;
        std::map<std::string, std::string> idStatus;   // FE03 / FE04 / FE05 -> PASS / FAIL
        std::map<std::string, std::string> facts;       // measured numbers for the report

        // scratch across steps
        int clickFrame = -1, openFrame = -1, draws0 = 0, cbAttempt = 0;
        std::set<std::string> flowsBefore;
        std::string newFlowVfs;

        void note(const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[UX01] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* id, const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(std::string(id) + ": " + buf);
            idStatus[id] = "FAIL";
            log.emplace_back(std::string("FAIL ") + id + ": " + buf); std::printf("[UX01] FAIL %s: %s\n", id, buf); std::fflush(stdout);
        }
        void pass(const char* id) { if (idStatus.find(id) == idStatus.end()) idStatus[id] = "PASS"; }
        void check(const char* id, bool ok, const std::string& what) { if (!ok) fail(id, "%s", what.c_str()); }
        void fact(const std::string& k, const char* fmt, ...)
        {
            char buf[512]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            facts[k] = buf; note("%s = %s", k.c_str(), buf);
        }
        double secondsInStep() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count(); }
        void click(ImVec2 p, int button = 0)
        {
            injects.push_back({ p.x, p.y, -1, false });
            injects.push_back({ p.x, p.y, -1, false });
            injects.push_back({ p.x, p.y, button, true });
            injects.push_back({ p.x, p.y, button, true });
            injects.push_back({ p.x, p.y, button, false });
            injects.push_back({ p.x, p.y, -1, false });
        }
    };

    // =========================================================================
    void StarforgeApp::UX01SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_UX01_SELFTEST");
        if (!rp || !*rp) return;
        m_UX01 = new UX01SelfTest();
        auto& t = *m_UX01;
        t.resultPath = rp;
        if (const char* r = std::getenv("COSMIC_UX01_ROOT")) t.root = r;
        if (t.root.empty()) t.root = (fs::current_path() / "ux01-root").generic_string();
        if (const char* s = std::getenv("COSMIC_UX01_SHOTS")) t.shots = s;
        if (t.shots.empty()) t.shots = fs::path(t.resultPath).parent_path().generic_string();
        std::error_code ec; fs::create_directories(t.root, ec); fs::create_directories(t.shots, ec);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        Cosmic::Application::Get().SetPauseOnMinimize(false);
        Cosmic::Application::Get().GetWindow().SetSize(1920, 1080);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        t.oracle.Install();
        {
            ImGuiContextHook hook;
            hook.Type = ImGuiContextHookType_NewFramePre;
            hook.Callback = &UX01SelfTest::OnNewFramePre;
            hook.UserData = &t;
            t.inputHook = ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
        }
        t.note("armed: result=%s root=%s shots=%s", rp, t.root.c_str(), t.shots.c_str());
    }

    void StarforgeApp::UX01SelfTestShutdown()
    {
        if (!m_UX01) return;
        if (m_UX01->inputHook && ImGui::GetCurrentContext()) ImGui::RemoveContextHook(ImGui::GetCurrentContext(), m_UX01->inputHook);
        m_UX01->oracle.Uninstall();
        delete m_UX01; m_UX01 = nullptr;
    }

    // After the whole UI ran: locate, then inject one pointer event.
    void StarforgeApp::UX01SelfTestFrameEnd()
    {
        if (!m_UX01) return;
        auto& t = *m_UX01;
        t.oracle.CheckContexts();
        if (!t.moveAside.empty())
        {
            ImGui::SetWindowPos(t.moveAside.c_str(), t.moveAsidePos, ImGuiCond_Always);   // = dragging its title bar there
            t.moveAside.clear();
        }
        if (t.expandEditors)
        {
            ImGui::SetWindowCollapsed("Editors", false, ImGuiCond_Always);   // = double-clicking its title bar
            t.expandEditors = false;
        }
        if (t.locateId)
        {
            ImRect r;
            if (LocatedRect(r)) { t.rect = r; t.haveRect = true; }
            ImGui::DebugLocateItem(t.locateId);   // resolves (and draws) during the next frame's ItemAdd
        }
        if (!t.injects.empty())
        {
            const auto in = t.injects.front();
            t.injects.erase(t.injects.begin());
            ImGuiIO& io = ImGui::GetIO();
            ImGuiContext& g = *GImGui;
            const int queued = g.InputEventsQueue.Size;
            io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
            io.AddMousePosEvent(in.x, in.y);
            if (in.button >= 0) io.AddMouseButtonEvent(in.button, in.down);
            for (int i = queued; i < g.InputEventsQueue.Size; ++i) t.ours.push_back(g.InputEventsQueue[i].EventId);
        }
    }

    // =========================================================================
    void StarforgeApp::UX01SelfTestTick()
    {
        if (!m_UX01) return;
        auto& t = *m_UX01;
        if (t.finished) return;
        ++t.frame;
        auto& app = Cosmic::Application::Get();

        if (!t.shotRequest.empty())
        {
            const std::string path = t.shots + "/" + t.shotRequest + ".png";
            int w = 0, h = 0;
            if (SaveWindowShot(path, w, h)) { t.fact("fe04_png", "%s (%dx%d)", path.c_str(), w, h); }
            else t.fail("FE04", "screenshot %s could not be written", path.c_str());
            t.shotRequest.clear();
        }

        if (t.traceFrames > 0)   // input trace while an injected gesture plays (diagnostics in the log)
        {
            --t.traceFrames;
            ImGuiContext& g = *GImGui;
            t.note("trace: mouse (%.0f,%.0f) down=%d hovered=%s nav=%s active=%08X", g.IO.MousePos.x, g.IO.MousePos.y, (int)g.IO.MouseDown[0],
                   g.HoveredWindow ? g.HoveredWindow->Name : "-", g.NavWindow ? g.NavWindow->Name : "-", g.ActiveId);
        }
        auto waitFrames = [&](int n) { t.wait = n; };
        auto flowDoc = [&](const std::string& vfs) -> FlowEditor* { return dynamic_cast<FlowEditor*>(m_Editors.Find(vfs)); };
        // The top-most window at `p`, when it is NOT `target` / its children / its dock host tree:
        // an injected click there would land on that window instead.
        auto occluder = [&](ImVec2 p, ImGuiWindow* target) -> ImGuiWindow*
        {
            ImGuiWindow* hov = nullptr; ImGuiWindow* under = nullptr;
            ImGui::FindHoveredWindowEx(p, true, &hov, &under);
            if (!hov || !target) return nullptr;
            if (hov->RootWindow == target->RootWindow || hov->RootWindowDockTree == target->RootWindowDockTree) return nullptr;
            return hov->RootWindow;
        };

        // ---- the plan (built once) ----------------------------------------------
        if (t.plan.empty())
        {
            auto add = [&](const char* name, std::function<bool()> fn) { t.planNames.push_back(name); t.plan.push_back(std::move(fn)); };
            add("boot", [&] { return t.frame > 10; });

            add("setup: copy Projects/PendulumLab, open it (fresh prefs, 1920x1080)", [&]
            {
                const fs::path src = fs::path(SdkDir()) / "Projects" / "PendulumLab";
                const fs::path dst = fs::path(t.root) / "PendulumLab";
                std::error_code ec;
                fs::remove_all(dst, ec);
                int copied = 0;
                for (auto it = fs::recursive_directory_iterator(src, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                {
                    const fs::path rel = fs::relative(it->path(), src, ec);
                    const std::string top = rel.begin() != rel.end() ? rel.begin()->string() : std::string();
                    if (top == "build" || top == "bin" || top == "out" || top == ".vs") { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
                    if (it->is_directory(ec)) fs::create_directories(dst / rel, ec);
                    else if (it->is_regular_file(ec)) { fs::create_directories((dst / rel).parent_path(), ec); fs::copy_file(it->path(), dst / rel, fs::copy_options::overwrite_existing, ec); ++copied; }
                }
                t.projectDir = dst.generic_string();
                t.fact("project_copy", "%s (%d files from %s)", t.projectDir.c_str(), copied, src.generic_string().c_str());
                const bool ok = OpenProjectPath(t.projectDir);
                t.check("FE04", ok && m_Ctx.ProjectOpen, "OpenProjectPath(PendulumLab copy) failed");
                t.check("FE04", m_ShowScreens, "the Screens panel is not shown for a kind=app project");
                t.fact("main_window", "%ux%u", app.GetWindow().GetWidth(), app.GetWindow().GetHeight());
                waitFrames(20);
                return true;
            });

            // ---------------- FE04 ----------------
            add("FE04 locate Screens > Flow graph", [&]
            {
                ImGuiWindow* sw = ActiveWindowNamed("Screens");
                if (!sw) { t.fail("FE04", "the Screens window is not on screen"); return true; }
                t.check("FE04", !m_Editors.AnyOpen(), "a document was open before the click");
                t.locateId = ImHashStr(ICON_LC_WORKFLOW " Flow graph", 0, sw->ID);
                t.haveRect = false;
                waitFrames(3);
                return true;
            });
            add("FE04 click Flow graph (injected)", [&]
            {
                t.locateId = 0;
                if (!t.haveRect) { t.fail("FE04", "could not locate Screens > Flow graph"); return true; }
                t.fact("flow_graph_button", "(%.0f,%.0f)-(%.0f,%.0f)", t.rect.Min.x, t.rect.Min.y, t.rect.Max.x, t.rect.Max.y);
                if (ImGuiWindow* occ = occluder(t.rect.GetCenter(), ImGui::FindWindowByName("Screens")))
                { t.fail("FE04", "Screens > Flow graph is covered by '%s'", occ->Name); return true; }
                t.click(t.rect.GetCenter());
                t.clickFrame = t.frame;
                return true;
            });
            add("FE04 wait: the document opened", [&]
            {
                if (!m_Editors.AnyOpen()) return false;
                t.openFrame = t.frame;
                t.fact("open_after_frames", "%d", t.openFrame - t.clickFrame);
                return true;
            });
            add("FE04 the next frame: Editors focused, canvas >= 800x400", [&]
            {
                ImGuiWindow* w = ImGui::FindWindowByName("Editors");
                ImGuiWindow* nav = GImGui->NavWindow;
                const bool focused = w && nav && nav->RootWindow == w;
                t.check("FE04", w && w->Active, "the Editors window is not drawn");
                t.check("FE04", focused, std::string("the Editors window is not focused (focus on '") + (nav ? nav->Name : "none") + "')");
                if (w) t.fact("editors_window", "pos (%.0f,%.0f) size %.0fx%.0f docked=%d dock_node=%s", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y,
                              (int)(w->DockIsActive), w->DockNode ? (w->DockNode->IsCentralNode() ? "central" : "edge") : "none");
                FlowEditor* fe = flowDoc(t.mainFlow);
                if (!fe) { t.fail("FE04", "the open document is not the FlowEditor for %s", t.mainFlow.c_str()); return true; }
                const auto& hv = fe->HarnessCanvas();
                const float cw = hv.CanvasMax.x - hv.CanvasMin.x, ch = hv.CanvasMax.y - hv.CanvasMin.y;
                t.fact("canvas", "%.0fx%.0f at (%.0f,%.0f) after %d drawn frame(s)", cw, ch, hv.CanvasMin.x, hv.CanvasMin.y, hv.FramesDrawn);
                if (cw < 800.0f || ch < 400.0f) t.fail("FE04", "canvas %.0fx%.0f px, below 800x400", cw, ch);
                const bool bigWindow = w && w->Size.x >= 1100.0f && w->Size.y >= 680.0f;
                t.check("FE04", bigWindow || (w && w->DockIsActive), "the Editors window is neither >= 1100x680 nor docked");
                waitFrames(3);
                return true;
            });
            add("FE04 every state node inside the canvas (CenterOnContent applied) + PNG", [&]
            {
                FlowEditor* fe = flowDoc(t.mainFlow);
                if (!fe) { t.fail("FE04", "document lost"); return true; }
                const auto& hv = fe->HarnessCanvas();
                int inside = 0;
                std::string outside;
                for (size_t i = 0; i < hv.Nodes.size(); ++i)
                {
                    const auto& n = hv.Nodes[i];
                    const bool in = n.first.x >= hv.CanvasMin.x - 0.5f && n.first.y >= hv.CanvasMin.y - 0.5f &&
                                    n.second.x <= hv.CanvasMax.x + 0.5f && n.second.y <= hv.CanvasMax.y + 0.5f;
                    if (in) ++inside;
                    else { char b[128]; std::snprintf(b, sizeof(b), " #%zu (%.0f,%.0f)-(%.0f,%.0f)", i, n.first.x, n.first.y, n.second.x, n.second.y); outside += b; }
                }
                t.fact("nodes_inside_canvas", "%d of %zu (drawn frames %d)", inside, hv.Nodes.size(), hv.FramesDrawn);
                t.check("FE04", !hv.Nodes.empty(), "no state node drawn");
                if (inside != (int)hv.Nodes.size()) t.fail("FE04", "state nodes outside the canvas:%s", outside.c_str());
                t.shotRequest = "fe04-flow-editor";
                waitFrames(2);
                return true;
            });
            add("FE04 verdict", [&] { t.pass("FE04"); return true; });

            // ---------------- FE03 ----------------
            add("FE03 locate the Editors tab close button", [&]
            {
                ImGuiWindow* w = ActiveWindowNamed("Editors");
                if (!w) { t.fail("FE03", "the Editors window is not on screen"); return true; }
                t.locateId = ImHashStr("#CLOSE", 0, w->ID);   // the tab's / title bar's close button id
                t.haveRect = false;
                waitFrames(3);
                return true;
            });
            add("FE03 click the ✕ (injected)", [&]
            {
                t.locateId = 0;
                if (!t.haveRect) { t.fail("FE03", "could not locate the Editors close button"); return true; }
                t.fact("editors_close_button", "(%.0f,%.0f)-(%.0f,%.0f)", t.rect.Min.x, t.rect.Min.y, t.rect.Max.x, t.rect.Max.y);
                if (ImGuiWindow* occ = occluder(t.rect.GetCenter(), ImGui::FindWindowByName("Editors")))
                {
                    // A floating panel covers the ✕ (KI-78: the Screens panel floats over the central
                    // dock's tab bar): drag it aside, as a user would, and locate again.
                    if (t.asideMoves++ < 2)
                    {
                        t.fact("editors_close_button_covered_by", "%s at (%.0f,%.0f) size %.0fx%.0f -> moved to (1400,300)", occ->Name, occ->Pos.x, occ->Pos.y, occ->Size.x, occ->Size.y);
                        t.moveAside = occ->Name; t.moveAsidePos = ImVec2(1400.0f, 300.0f);
                        t.step -= 2;   // back to "locate"
                        waitFrames(4);
                        return true;
                    }
                    t.fail("FE03", "the Editors close button stays covered by '%s'", occ->Name);
                    return true;
                }
                t.click(t.rect.GetCenter());
                waitFrames(10);
                return true;
            });
            add("FE03 the window is absent, the document still open", [&]
            {
                ImGuiWindow* w = ImGui::FindWindowByName("Editors");
                t.check("FE03", !m_ShowEditors, "View > Editors still on after the ✕");
                t.check("FE03", m_Editors.AnyOpen(), "the document closed with the window");
                t.check("FE03", !w || !w->Active, "the Editors window is still drawn after its ✕ (KI-71)");
                t.fact("after_close_button", "show=%d open_docs=%zu window_active=%d", (int)m_ShowEditors, m_Editors.Count(), w ? (int)w->Active : -1);
                ScreensHost().OpenFlowDocument("project://" + m_ManifestFlow);   // Screens ▸ Flow graph's command
                waitFrames(4);
                return true;
            });
            add("FE03 Open shows it again", [&]
            {
                ImGuiWindow* w = ImGui::FindWindowByName("Editors");
                t.check("FE03", m_ShowEditors, "Open did not raise View > Editors");
                t.check("FE03", w && w->Active, "the Editors window did not come back on Open");
                t.check("FE03", m_Editors.Count() == 1, "re-open duplicated the document");
                return true;
            });

            // ---------------- FE05 ----------------
            add("FE05 right-click the Content Browser's empty space (injected)", [&]
            {
                ImGuiWindow* grid = nullptr;
                for (ImGuiWindow* w : GImGui->Windows)
                    if (w->Active && std::strstr(w->Name, "Content Browser") && std::strstr(w->Name, "##cbGrid")) grid = w;
                if (!grid) { t.fail("FE05", "the Content Browser grid is not on screen"); return true; }
                t.flowsBefore = FlowFiles(t.projectDir);
                const ImRect r = grid->InnerRect;
                const ImVec2 candidates[] = { ImVec2(r.Max.x - 24.0f, r.Max.y - 16.0f), ImVec2(r.Min.x + r.GetWidth() * 0.5f, r.Max.y - 16.0f), ImVec2(r.Max.x - 24.0f, r.Min.y + r.GetHeight() * 0.5f) };
                while (t.cbAttempt < 2 && occluder(candidates[t.cbAttempt], grid)) ++t.cbAttempt;
                const ImVec2 p = candidates[std::min(t.cbAttempt, 2)];
                if (ImGuiWindow* occ = occluder(p, grid)) { t.fail("FE05", "the Content Browser grid is covered by '%s'", occ->Name); return true; }
                t.fact("content_browser_rclick", "(%.0f,%.0f) in grid (%.0f,%.0f)-(%.0f,%.0f), attempt %d", p.x, p.y, r.Min.x, r.Min.y, r.Max.x, r.Max.y, t.cbAttempt + 1);
                t.click(p, 1);
                waitFrames(10);
                return true;
            });
            add("FE05 locate New", [&]
            {
                ImGuiContext& g = *GImGui;
                ImGuiWindow* popup = g.OpenPopupStack.Size > 0 ? g.OpenPopupStack.back().Window : nullptr;
                if (!popup)
                {
                    if (t.cbAttempt < 2) { ++t.cbAttempt; t.note("the context menu did not open; next point"); t.step -= 2; return true; }
                    t.fail("FE05", "the Content Browser context menu did not open");
                    return true;
                }
                t.locateId = ImHashStr(ICON_LC_PLUS " New", 0, popup->ID);
                t.haveRect = false;
                waitFrames(3);
                return true;
            });
            add("FE05 click New (injected)", [&]
            {
                t.locateId = 0;
                if (!t.haveRect) { t.fail("FE05", "could not locate New in the context menu"); return true; }
                t.click(t.rect.GetCenter());
                waitFrames(8);
                return true;
            });
            add("FE05 locate Flow", [&]
            {
                ImGuiContext& g = *GImGui;
                ImGuiWindow* menu = g.OpenPopupStack.Size > 0 ? g.OpenPopupStack.back().Window : nullptr;
                if (!menu) { t.fail("FE05", "the New submenu did not open"); return true; }
                const std::string label = std::string(ICON_LC_WORKFLOW) + " Flow";
                t.locateId = ImHashStr(label.c_str(), 0, menu->ID);
                t.haveRect = false;
                waitFrames(3);
                return true;
            });
            add("FE05 click Flow (injected)", [&]
            {
                t.locateId = 0;
                if (!t.haveRect) { t.fail("FE05", "could not locate New > Flow"); return true; }
                t.click(t.rect.GetCenter());
                waitFrames(10);
                return true;
            });
            add("FE05 open the new flow (the Content Browser's double-click request)", [&]
            {
                std::vector<std::string> added;
                for (const std::string& f : FlowFiles(t.projectDir)) if (!t.flowsBefore.count(f)) added.push_back(f);
                if (added.size() != 1) { t.fail("FE05", "New > Flow created %zu .cflow file(s), expected 1", added.size()); return true; }
                t.newFlowVfs = "project://" + added[0];
                t.fact("new_flow", "%s", t.newFlowVfs.c_str());
                if (ImGuiWindow* w = ImGui::FindWindowByName("Editors"); w && w->Collapsed)
                {
                    // Only reachable with an undocked, tiny Editors window (KI-66): an earlier click
                    // collapsed it. Expand it (a title-bar double-click) so the document can draw.
                    t.fact("editors_collapsed_before_fe05", "yes (%.0fx%.0f) -> expanded", w->Size.x, w->Size.y);
                    t.expandEditors = true;
                }
                m_Ctx.PendingOpenDocument = t.newFlowVfs;   // what a tile double-click queues
                return true;
            });
            add("FE05 wait: the document is open and drawing", [&]
            {
                FlowEditor* fe = flowDoc(t.newFlowVfs);
                if (!fe) return false;
                t.draws0 = fe->HarnessCanvas().FramesDrawn;
                return true;
            });
            add("FE05 after two drawn frames: not dirty", [&]
            {
                FlowEditor* fe = flowDoc(t.newFlowVfs);
                if (!fe) { t.fail("FE05", "document lost"); return true; }
                const int drawn = fe->HarnessCanvas().FramesDrawn;
                if (drawn < 2)
                {
                    if (t.secondsInStep() < 10.0) return false;
                    t.fail("FE05", "the new flow drew %d frame(s) in 10 s", drawn);
                    return true;
                }
                t.fact("new_flow_dirty_after_2_frames", "%d (drawn %d)", (int)fe->Dirty(), drawn);
                t.check("FE05", !fe->Dirty(), "a new flow is dirty on its first frames (KI-69)");
                waitFrames(10);
                return true;
            });
            add("FE05 ten frames later: still not dirty; drag its node (injected)", [&]
            {
                FlowEditor* fe = flowDoc(t.newFlowVfs);
                if (!fe) { t.fail("FE05", "document lost"); return true; }
                const auto& hv = fe->HarnessCanvas();
                t.fact("new_flow_dirty_after_12_frames", "%d (drawn %d)", (int)fe->Dirty(), hv.FramesDrawn);
                t.check("FE05", !fe->Dirty(), "the new flow turned dirty without an edit");
                if (hv.Nodes.empty()) { t.fail("FE05", "the new flow draws no state node"); return true; }
                const ImVec2 a = hv.Nodes[0].first, b = hv.Nodes[0].second;
                const ImVec2 p(a.x + (b.x - a.x) * 0.5f, a.y + (b.y - a.y) * 0.5f);   // the scene row, not a pin
                t.fact("new_flow_drag_from", "(%.0f,%.0f) on node (%.0f,%.0f)-(%.0f,%.0f)", p.x, p.y, a.x, a.y, b.x, b.y);
                if (ImGuiWindow* occ = occluder(p, ImGui::FindWindowByName("Editors")))
                { t.fail("FE05", "the new flow's node is covered by '%s'", occ->Name); return true; }
                t.injects.push_back({ p.x, p.y, -1, false });
                t.injects.push_back({ p.x, p.y, -1, false });
                t.injects.push_back({ p.x, p.y, 0, true });
                for (int i = 1; i <= 8; ++i) t.injects.push_back({ p.x + 6.0f * i, p.y + 2.0f * i, -1, false });
                t.injects.push_back({ p.x + 48.0f, p.y + 16.0f, 0, false });
                t.injects.push_back({ p.x + 48.0f, p.y + 16.0f, -1, false });
                t.traceFrames = (int)t.injects.size() + 2;
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("FE05 a user drag dirties", [&]
            {
                FlowEditor* fe = flowDoc(t.newFlowVfs);
                if (!fe) { t.fail("FE05", "document lost"); return true; }
                const auto& hv = fe->HarnessCanvas();
                if (!hv.Nodes.empty())
                    t.fact("new_flow_node_after_drag", "(%.0f,%.0f)-(%.0f,%.0f)", hv.Nodes[0].first.x, hv.Nodes[0].first.y, hv.Nodes[0].second.x, hv.Nodes[0].second.y);
                t.fact("new_flow_dirty_after_drag", "%d", (int)fe->Dirty());
                t.check("FE05", fe->Dirty(), "dragging the node did not mark the document dirty");
                return true;
            });
            add("FE05 verdict", [&] { t.pass("FE05"); return true; });

            // ---------------- FE03 (Close Project) ----------------
            add("FE03 Close Project drops the documents (logging the dirty one)", [&]
            {
                const size_t docs = m_Editors.Count();
                const bool dirty = m_Editors.AnyDirty();
                const size_t linesBefore = m_Ctx.ConsoleLines.size();
                CloseProject();
                bool logged = false;
                for (size_t i = linesBefore; i < m_Ctx.ConsoleLines.size(); ++i)
                    if (m_Ctx.ConsoleLines[i].Text.find("[Editors] Close Project drops the unsaved changes") != std::string::npos) logged = true;
                t.fact("close_project", "docs before %zu (dirty %d) -> AnyOpen %d, dirty logged %d", docs, (int)dirty, (int)m_Editors.AnyOpen(), (int)logged);
                t.check("FE03", !m_Editors.AnyOpen(), "documents survived Close Project (KI-71)");
                t.check("FE03", !dirty || logged, "the dirty document was dropped without a console line");
                t.pass("FE03");
                waitFrames(4);
                return true;
            });
            add("finish", [&] { return true; });
        }

        // ---- run the plan ------------------------------------------------------
        if (t.wait > 0) { --t.wait; return; }
        if (t.step < (int)t.plan.size())
        {
            const auto before = t.oracle.Take();
            const int stepAtCall = t.step;
            const bool advance = t.plan[t.step]();
            const std::string why = t.oracle.Judge(before);
            if (!why.empty()) t.fail("ORACLE", "%s during '%s'", why.c_str(), t.planNames[stepAtCall].c_str());
            if (advance)
            {
                t.note("step %d/%zu done: %s (%.1f s)", stepAtCall + 1, t.plan.size(), t.planNames[stepAtCall].c_str(), t.secondsInStep());
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            else if (t.secondsInStep() > t.deadlineSec)
            {
                t.fail("DEADLINE", "step '%s' exceeded %.0f s", t.planNames[t.step].c_str(), t.deadlineSec);
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            if (t.step < (int)t.plan.size()) return;
        }

        // ---- verdict + JSON ------------------------------------------------------
        t.finished = true;
        t.facts["os_pointer_events_dropped"] = std::to_string(t.droppedEvents);
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
        const bool oracleOk = t.oracle.recoveredErrors == 0 && t.oracle.endFrameLeaks == 0 && t.oracle.contextDrift == 0;
        const bool pass = t.failures == 0 && oracleOk;
#if defined(NDEBUG)
        const char* cfg = "Release";
#else
        const char* cfg = "Debug";
#endif
        std::ofstream f(t.resultPath, std::ios::trunc);
        if (f)
        {
            f << "{\n  \"work_order\": \"UX-01\",\n  \"case\": \"FE03 + FE04 + FE05 editor halves\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"project\": \"" << Esc(t.projectDir) << "\",\n";
            f << "  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n";
            f << "  \"oracle\": { \"recovered_errors\": " << t.oracle.recoveredErrors << ", \"end_frame_leaks\": " << t.oracle.endFrameLeaks
              << ", \"context_drift\": " << t.oracle.contextDrift << " },\n";
            f << "  \"ids\": {";
            bool first = true;
            for (const char* id : { "FE03", "FE04", "FE05" })
            {
                auto it = t.idStatus.find(id);
                f << (first ? " " : ", ") << "\"" << id << "\": \"" << (it == t.idStatus.end() ? "NOT_RUN" : it->second) << "\"";
                first = false;
            }
            f << " },\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"measured\": {\n";
            size_t n = 0;
            for (const auto& [k, v] : t.facts) f << "    \"" << Esc(k) << "\": \"" << Esc(v) << "\"" << (++n < t.facts.size() ? "," : "") << "\n";
            f << "  },\n  \"checks_failed\": [\n";
            for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
            f << "  ],\n  \"log\": [\n";
            for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        f.close();   // KI-85: the FAIL exit below runs no destructors, so the result is flushed and closed here
        {
            std::ofstream c(fs::path(t.resultPath).parent_path() / "ux01-editor-console.txt", std::ios::trunc);
            for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
        }
        std::printf("UX01_SELFTEST_RESULT=%s config=%s failedChecks=%d seconds=%.1f errors=%d leaks=%d drift=%d\n",
                    pass ? "PASS" : "FAIL", cfg, t.failures, total, t.oracle.recoveredErrors.load(), t.oracle.endFrameLeaks.load(),
                    t.oracle.contextDrift.load());
        std::fflush(stdout);
        if (pass) Cosmic::Application::Get().Close();
        else      ::TerminateProcess(::GetCurrentProcess(), 1);   // KI-85: exit code 1 on every GL driver (quick_exit
                                                                  // ended in 0xC0000409 under Mesa llvmpipe)
    }
}
