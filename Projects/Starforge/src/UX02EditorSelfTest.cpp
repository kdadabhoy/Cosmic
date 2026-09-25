// UX02EditorSelfTest.cpp — UX-02 (UX & Shipping): the ED01..ED05 editor sequence, driven
// inside the REAL Starforge editor by its own commands and panels.
//
// Armed by COSMIC_UX02_SELFTEST=<result.json>, COSMIC_UX02_ROOT=<temp root holding the
// wrapper's copies of tests/fixtures/ux02 (Ux02Fixture/) and Projects/PendulumLab
// (PendulumLab/); the App-template project is scaffolded there too>, optional
// COSMIC_UX02_SHOTS=<folder for PNG window shots>, and COSMIC_AP03_RECORD_SHELL (the
// SourceLocator seam: Reveal records instead of launching Explorer).
// Test-side seams only: Dear ImGui's input queue (the E03 / L05 pattern — the rect gizmo,
// the Inspector button, the Screens rows, the File menu and the modals all read io), the
// OS cursor + posted mouse messages for the viewport pick (the GUIDE pattern — the viewport
// pick polls the OS through Input::), and injected TIME through the real Autosave(ts).
// The WO-07 ImGui stack oracle (tests/WO07UiOracle.h) is judged after EVERY step.
//
//   ED01  App template Dashboard: Plot selected -> the transform gizmo is not submitted
//         (probe counter), the rect gizmo draws, the transform chips are disabled; a world
//         entity re-enables both (control). KI-73: an overlay drawn over the Plot's centre ->
//         a drag on the gizmo's centre square still moves the Plot by (40,-20)/scale, one
//         "Move UI Rect" entry. Fixture SpriteUnderUi: the selected sprite under a full-
//         screen opaque UiImage -> an OS press-drag of (40,-20) px on its body moves
//         Transform.Position by the world equivalent, one CommandStack entry, the selection
//         is kept, undo restores.
//   ED03  PendulumLab Settings, Back button selected: the Inspector's Signal row shows
//         "Flow: Settings —back_clicked→ Lab"; its "Open in flow editor" (injected click)
//         opens flows/Main.cflow with state 2 / transition 0 selected; the viewport context
//         menu (injected right-click on the button) shows the same line.
//   ED04  Fixture: Screens ▸ Scenes lists exactly Main, overlays/Pause, SpriteUnderUi (the
//         .bak never), start + open marked; a double-click opens overlays/Pause; File ▸
//         Open Scene (opened with the pointer) draws the same set and a click opens a scene.
//   ED05  Edit ▸ Preferences… (injected clicks): autosave off -> 2x the interval of injected
//         time through the real Autosave writes nothing; on -> one file + "autosaved HH:MM";
//         prompt on + dirty -> the OpenScene command raises Save / Discard / Cancel: Cancel
//         keeps the scene, Discard switches; prompt off -> switches directly. Last: a dirty
//         scene is left for OnDetach (the window-close path) — the WRAPPER checks the copy.
//
// Verdict -> exit code: PASS -> graceful close (Application::Close, the chrome ✕ path);
// FAIL -> quick_exit(1). JSON always written.

#include "StarforgeApp.h"
#include "commands/EditorCommands.h"
#include "editors/FlowEditor.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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
        std::string ReadAll(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str();
        }
        size_t CountFiles(const fs::path& dir)
        {
            size_t n = 0; std::error_code ec;
            if (!fs::exists(dir, ec)) return 0;
            for (const auto& e : fs::directory_iterator(dir, ec)) if (e.is_regular_file(ec)) ++n;
            return n;
        }

        // The whole editor window (the presented back buffer) -> PNG: the GUIDE / L05 pattern.
        typedef void (APIENTRY* PFN_glReadBuffer)(unsigned int);
        typedef void (APIENTRY* PFN_glReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
        typedef void (APIENTRY* PFN_glPixelStorei)(unsigned int, int);
        typedef void (APIENTRY* PFN_glGetIntegerv)(unsigned int, int*);
        typedef void (APIENTRY* PFN_glBindFramebuffer)(unsigned int, unsigned int);
        typedef void* (WINAPI* PFN_wglGetProcAddress)(const char*);
        bool SaveWindowShot(const std::string& path)
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
            std::error_code ec; fs::create_directories(fs::path(path).parent_path(), ec);
            return Cosmic::ImageIO::WritePNG(path, w, h, 4, flipped.data());
        }
    }

    struct StarforgeApp::UX02SelfTest
    {
        std::string resultPath, root, shots;
        std::string appDir, fixtureDir, pendulumDir;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
        CosmicTest::UiOracle oracle;
        int frame = 0, wait = 0, step = 0;
        bool finished = false;
        std::vector<std::function<bool()>> plan;
        std::vector<std::string> planNames;
        double deadlineSec = 240.0;

        // ImGui io injection (E03 / L05 pattern) — consumed in FrameEnd, one per frame
        struct Inject { float x = 0, y = 0; int button = -1; bool down = false; ImGuiKey key = ImGuiKey_None; bool keyDown = false; bool hasPos = true; };
        std::vector<Inject> injects;
        std::string focusWindow;          // SetWindowFocus in FrameEnd ("" = none)
        std::string scrollWindow; float scrollToY = 0.0f;   // bring a screen y into view (Inspector)
        bool layoutScreens = false;       // place + size the floating Screens window (a user drag / resize)

        // results
        int failures = 0;
        std::vector<std::string> log, checks, shotsTaken;
        std::map<std::string, std::string> idStatus;
        std::map<std::string, std::string> numbers;   // measured values for the report
        std::string shotRequest;
        std::string detachExpected;       // the OnDetach autosave copy the wrapper must find
        std::string detachMarker = "Ux02DetachMarker";

        // scratch
        uint64_t gizmoCallsBefore = 0, rectDrawsBefore = 0;
        Cosmic::UUID plotUuid, overlayUuid, spriteUuid, buttonUuid;
        glm::vec2 plotMin{}, plotMax{}; float plotScale = 1.0f;
        size_t undoBefore = 0;
        glm::vec3 spriteBefore{ 0.0f };
        POINT osStart{ 0, 0 }, osEnd{ 0, 0 };     // OS screen px
        glm::vec2 imStart{ 0.0f }, imEnd{ 0.0f }; // ImGui screen px (same points)
        Prefs::EditorSettings snapSaved;
        std::string sceneBefore;
        int scrollTries = 0;
        int spriteLegStep = 0, spriteAttempts = 0;
        uint64_t gesturesBefore = 0;

        void note(const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[UX02] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* id, const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(std::string(id) + ": " + buf);
            idStatus[id] = "FAIL";
            log.emplace_back(std::string("FAIL ") + id + ": " + buf); std::printf("[UX02] FAIL %s: %s\n", id, buf); std::fflush(stdout);
        }
        void pass(const char* id) { if (idStatus.find(id) == idStatus.end()) idStatus[id] = "PASS"; }
        void check(const char* id, bool ok, const std::string& what) { if (!ok) fail(id, "%s", what.c_str()); }
        double secondsInStep() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count(); }

        void click(float x, float y, int button = 0)
        {
            injects.push_back({ x, y, -1, false });
            injects.push_back({ x, y, -1, false });
            injects.push_back({ x, y, button, true });
            injects.push_back({ x, y, button, false });
            injects.push_back({ x, y, -1, false });
        }
        void key(ImGuiKey k)
        {
            Inject d; d.hasPos = false; d.key = k; d.keyDown = true;
            Inject u = d; u.keyDown = false;
            injects.push_back(d); injects.push_back(u);
        }
    };

    // =========================================================================
    void StarforgeApp::UX02SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_UX02_SELFTEST");
        if (!rp || !*rp) return;
        m_UX02 = new UX02SelfTest();
        auto& t = *m_UX02;
        t.resultPath = rp;
        if (const char* r = std::getenv("COSMIC_UX02_ROOT")) t.root = r;
        if (t.root.empty()) t.root = (fs::current_path() / "ux02-root").generic_string();
        if (const char* s = std::getenv("COSMIC_UX02_SHOTS")) t.shots = s;
        t.appDir      = (fs::path(t.root) / "Ux02App").generic_string();
        t.fixtureDir  = (fs::path(t.root) / "Ux02Fixture").generic_string();
        t.pendulumDir = (fs::path(t.root) / "PendulumLab").generic_string();
        std::error_code ec; fs::create_directories(t.root, ec);
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        Cosmic::Application::Get().SetPauseOnMinimize(false);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        t.oracle.Install();
        t.note("armed: result=%s root=%s shots=%s", rp, t.root.c_str(), t.shots.c_str());
    }

    void StarforgeApp::UX02SelfTestShutdown()
    {
        if (!m_UX02) return;
        m_UX02->oracle.Uninstall();
        delete m_UX02; m_UX02 = nullptr;
    }

    void StarforgeApp::UX02SelfTestFrameEnd()
    {
        if (!m_UX02) return;
        auto& t = *m_UX02;
        t.oracle.CheckContexts();
        if (!t.focusWindow.empty())
        {
            ImGui::SetWindowFocus(t.focusWindow.c_str());
            t.focusWindow.clear();
        }
        if (t.layoutScreens)
        {
            const ImGuiViewport* mv = ImGui::GetMainViewport();
            ImGui::SetWindowPos("Screens", ImVec2(mv->Pos.x + 250.0f, mv->Pos.y + 60.0f));
            ImGui::SetWindowSize("Screens", ImVec2(560.0f, std::max(320.0f, mv->Size.y - 130.0f)));
            ImGui::SetWindowFocus("Screens");
            t.layoutScreens = false;
        }
        if (!t.scrollWindow.empty())
        {
            if (ImGuiWindow* w = ImGui::FindWindowByName(t.scrollWindow.c_str()))
            {
                const float mid = w->Pos.y + w->Size.y * 0.5f;
                ImGui::SetScrollY(w, std::max(0.0f, w->Scroll.y + (t.scrollToY - mid)));
            }
            t.scrollWindow.clear();
        }
        if (!t.injects.empty())
        {
            const auto in = t.injects.front();
            t.injects.erase(t.injects.begin());
            ImGuiIO& io = ImGui::GetIO();
            if (in.hasPos)
            {
                io.AddMouseViewportEvent(ImGui::GetMainViewport()->ID);
                io.AddMousePosEvent(in.x, in.y);
            }
            if (in.button >= 0) io.AddMouseButtonEvent(in.button, in.down);
            if (in.key != ImGuiKey_None) io.AddKeyEvent(in.key, in.keyDown);
        }
    }

    // =========================================================================
    void StarforgeApp::UX02SelfTestTick()
    {
        if (!m_UX02) return;
        auto& t = *m_UX02;
        if (t.finished) return;
        ++t.frame;
        auto& app = Cosmic::Application::Get();

        // A pending screenshot: the back buffer holds the previous, fully presented frame.
        if (!t.shotRequest.empty())
        {
            if (!t.shots.empty())
            {
                const std::string path = t.shots + "/" + t.shotRequest + ".png";
                if (SaveWindowShot(path)) { t.shotsTaken.push_back(t.shotRequest); t.note("shot %s", t.shotRequest.c_str()); }
                else t.note("shot %s could not be written (not a check)", t.shotRequest.c_str());
            }
            t.shotRequest.clear();
        }

        auto waitFrames = [&](int n) { t.wait = n; };
        auto shot = [&](const char* name) { t.shotRequest = name; };
        auto band = [&]() -> Cosmic::UiRect
        {
            const glm::vec2 vs = app.GetViewportSize();
            return Cosmic::UiRect{ { m_GameBandUv.x * vs.x, m_GameBandUv.y * vs.y },
                                   { (m_GameBandUv.x + m_GameBandUv.z) * vs.x, (m_GameBandUv.y + m_GameBandUv.w) * vs.y } };
        };
        auto findTagged = [&](const std::string& tag) -> Cosmic::Entity
        {
            if (!m_Ctx.Scene) return {};
            auto& reg = m_Ctx.Scene->GetRegistry();
            for (auto e : reg.view<Cosmic::TagComponent>())
                if (reg.get<Cosmic::TagComponent>(e).Tag == tag) return Cosmic::Entity(e, m_Ctx.Scene.get());
            return {};
        };
        auto elementRect = [&](Cosmic::Entity e, Cosmic::UiRect& out, float& scale) -> bool
        {
            std::vector<Cosmic::UiElement> els;
            Cosmic::UiSystem::CollectElements(*m_Ctx.Scene, band(), els);
            for (const auto& el : els)
                if (el.Handle == (uint32_t)(entt::entity)e) { out = el.Rect; scale = el.Scale > 0.0f ? el.Scale : 1.0f; return true; }
            return false;
        };
        auto uuidOf = [&](Cosmic::Entity e) { return e.GetComponent<Cosmic::IDComponent>().ID; };
        // The GUIDE's OS-level pointer: move the real cursor (the viewport pick polls it
        // through Input::) and post the button to the editor's own window.
        auto osTo = [&](const glm::vec2& imPt) -> POINT
        {
            POINT origin{ 0, 0 };
            if (HWND hw = WO05NativeWindow(app.GetWindow())) ClientToScreen(hw, &origin);
            const ImVec2 vp = ImGui::GetMainViewport()->Pos;
            return POINT{ (LONG)std::lround(imPt.x - vp.x + (float)origin.x), (LONG)std::lround(imPt.y - vp.y + (float)origin.y) };
        };
        auto osMove = [&](POINT p, bool held)
        {
            SetCursorPos(p.x, p.y);
            if (HWND hw = WO05NativeWindow(app.GetWindow()))
            {
                POINT cl = p; ScreenToClient(hw, &cl);
                PostMessageW(hw, WM_MOUSEMOVE, held ? MK_LBUTTON : 0, MAKELPARAM(cl.x, cl.y));
            }
        };
        auto osButton = [&](POINT p, bool down)
        {
            if (HWND hw = WO05NativeWindow(app.GetWindow()))
            {
                POINT cl = p; ScreenToClient(hw, &cl);
                PostMessageW(hw, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, MAKELPARAM(cl.x, cl.y));
            }
        };
        // OS screen px -> the ImGui screen space the viewport maps in (inverse of osTo).
        auto imOf = [&](POINT p) -> glm::vec2
        {
            POINT origin{ 0, 0 };
            if (HWND hw = WO05NativeWindow(app.GetWindow())) ClientToScreen(hw, &origin);
            const ImVec2 vp = ImGui::GetMainViewport()->Pos;
            return { (float)(p.x - origin.x) + vp.x, (float)(p.y - origin.y) + vp.y };
        };
        auto s2w = [&](const glm::vec2& screen) -> glm::vec2
        {
            return Cosmic::Camera2DController::ScreenToWorld(screen, app.GetViewportPos(), app.GetViewportSize(),
                                                             m_Camera2D.GetFocus(), m_Camera2D.GetZoom());
        };
        auto w2s = [&](const glm::vec2& world) -> glm::vec2
        {
            // ScreenToWorld is affine per axis: invert it numerically.
            const glm::vec2 p0 = app.GetViewportPos();
            const glm::vec2 a = s2w(p0), b = s2w(p0 + glm::vec2(100.0f, 100.0f));
            const glm::vec2 k = (b - a) / 100.0f;
            return p0 + (world - a) / k;
        };
        auto flowLine = [](const char* s, const char* sig, const char* tgt)
        {
            return std::string("Flow: ") + s + " \xE2\x80\x94" + sig + "\xE2\x86\x92 " + tgt;
        };

        if (t.plan.empty())
        {
            auto add = [&](const char* name, std::function<bool()> fn) { t.planNames.push_back(name); t.plan.push_back(std::move(fn)); };
            add("boot", [&] { return t.frame > 10; });

            // ======================= ED01 — one gizmo (App template) =======================
            add("ED01 new project (app template)", [&]
            {
                const bool ok = NewProjectAt("Ux02App", t.root, "app");
                t.check("ED01", ok && m_Ctx.ProjectOpen, "NewProjectAt(app) did not open a project");
                waitFrames(6);
                return true;
            });
            add("ED01 wait: watcher + builder idle", [&] { return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f && t.secondsInStep() > 1.0; });
            add("ED01 open Dashboard, select Plot", [&]
            {
                m_ShowScreens = false;   // View menu: keep floating panels off the viewport the pointer drives
                OpenScene("project://scenes/Dashboard.cscene");
                m_Aspect = GameAspect::Free;
                Cosmic::Entity plot;
                for (auto e : m_Ctx.Scene->GetRegistry().view<Cosmic::UiPlotComponent>()) { plot = Cosmic::Entity(e, m_Ctx.Scene.get()); break; }
                if (!plot) { t.fail("ED01", "Dashboard has no UiPlot element"); return true; }
                t.plotUuid = uuidOf(plot);
                m_Ctx.SelectOnly(plot);
                waitFrames(4);
                return true;
            });
            add("ED01 counters baseline", [&]
            {
                t.gizmoCallsBefore = m_Viewport.TransformGizmoCalls();
                t.rectDrawsBefore  = m_RectGizmo.DrawCount();
                waitFrames(8);
                return true;
            });
            add("ED01 Plot: no transform gizmo, rect gizmo drawn, chips disabled", [&]
            {
                const uint64_t calls = m_Viewport.TransformGizmoCalls() - t.gizmoCallsBefore;
                const uint64_t draws = m_RectGizmo.DrawCount() - t.rectDrawsBefore;
                t.numbers["ed01_plot_transform_gizmo_calls_8_frames"] = std::to_string(calls);
                t.numbers["ed01_plot_rect_gizmo_draws_8_frames"] = std::to_string(draws);
                if (calls != 0) t.fail("ED01", "transform gizmo submitted %llu times over 8 frames with the UI Plot selected (KI-72)", (unsigned long long)calls);
                t.check("ED01", draws >= 4, "rect gizmo drew " + std::to_string(draws) + " times over 8 frames (expected every frame)");
                t.check("ED01", m_RectGizmo.HasTarget(), "rect gizmo has no target with Plot selected");
                t.check("ED01", m_Viewport.UiSelectionChipsDisabled(), "transform chips not disabled for a UI selection");
                shot("ed01-plot-rect-gizmo-only");
                // control: a world entity gets the transform gizmo and live chips
                Cosmic::Entity cam;
                for (auto e : m_Ctx.Scene->GetRegistry().view<Cosmic::CameraComponent>()) { cam = Cosmic::Entity(e, m_Ctx.Scene.get()); break; }
                if (cam) m_Ctx.SelectOnly(cam); else t.fail("ED01", "Dashboard has no camera entity (control)");
                t.gizmoCallsBefore = m_Viewport.TransformGizmoCalls();
                waitFrames(6);
                return true;
            });
            add("ED01 control: a world entity gets the transform gizmo", [&]
            {
                const uint64_t calls = m_Viewport.TransformGizmoCalls() - t.gizmoCallsBefore;
                t.numbers["ed01_camera_transform_gizmo_calls_6_frames"] = std::to_string(calls);
                t.check("ED01", calls >= 3, "the probe saw " + std::to_string(calls) + " transform-gizmo submissions for a world entity (control)");
                t.check("ED01", !m_Viewport.UiSelectionChipsDisabled(), "chips still disabled for a world entity");
                // KI-73 leg: an overlay drawn over the Plot's centre, then the Plot selected again
                Cosmic::Entity plot = m_Ctx.Scene->FindByUUID(t.plotUuid);
                Cosmic::Entity ov = Commands::Create(m_Ctx, "Ux02Overlay", plot, [](Cosmic::Entity e)
                {
                    auto& rt = e.AddComponent<Cosmic::RectTransformComponent>();
                    rt.AnchorMin = { 0.3f, 0.3f }; rt.AnchorMax = { 0.7f, 0.7f }; rt.OffsetMin = { 0, 0 }; rt.OffsetMax = { 0, 0 }; rt.ZOrder = 50;
                    e.AddComponent<Cosmic::UiImageComponent>().Tint = { 0.2f, 0.6f, 0.9f, 1.0f };
                });
                t.overlayUuid = uuidOf(ov);
                m_Ctx.SelectOnly(plot);
                waitFrames(4);
                return true;
            });
            add("ED01 KI-73: drag the centre square under the overlay by (40,-20)", [&]
            {
                Cosmic::Entity plot = m_Ctx.Scene->FindByUUID(t.plotUuid);
                Cosmic::UiRect r; float sc = 1.0f;
                if (!plot || !elementRect(plot, r, sc)) { t.fail("ED01", "Plot rect not resolved"); return true; }
                uint32_t hit = 0;
                const bool covered = Cosmic::UiSystem::HitTest(*m_Ctx.Scene, band(), r.Center(), hit) &&
                                     hit == (uint32_t)(entt::entity)m_Ctx.Scene->FindByUUID(t.overlayUuid);
                t.check("ED01", covered, "the overlay is not the topmost hit at the Plot's centre (fixture setup)");
                const auto& rt = plot.GetComponent<Cosmic::RectTransformComponent>();
                t.plotMin = rt.OffsetMin; t.plotMax = rt.OffsetMax; t.plotScale = sc;
                t.undoBefore = m_Ctx.Commands.UndoCount();
                if (!m_RectGizmo.HasTarget()) { t.fail("ED01", "rect gizmo has no target"); return true; }
                const glm::vec2 c = m_RectGizmo.HandleScreenRect(RectHandle::Move).Center();
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, -1, false });
                t.injects.push_back({ c.x, c.y, 0, true });
                for (int i = 1; i <= 8; ++i) t.injects.push_back({ c.x + 5.0f * i, c.y - 2.5f * i, -1, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, -1, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, 0, false });
                t.injects.push_back({ c.x + 40.0f, c.y - 20.0f, -1, false });
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("ED01 KI-73 verify + undo", [&]
            {
                Cosmic::Entity plot = m_Ctx.Scene->FindByUUID(t.plotUuid);
                if (!plot) { t.fail("ED01", "Plot lost"); return true; }
                const auto& rt = plot.GetComponent<Cosmic::RectTransformComponent>();
                const glm::vec2 d = glm::vec2(40.0f, -20.0f) / t.plotScale;
                const bool moved = glm::all(glm::epsilonEqual(rt.OffsetMin, t.plotMin + d, 0.01f)) && glm::all(glm::epsilonEqual(rt.OffsetMax, t.plotMax + d, 0.01f));
                if (!moved) t.fail("ED01", "KI-73: Plot offsets after the centre-square drag (%g,%g)-(%g,%g), expected +(%g,%g)",
                                   rt.OffsetMin.x, rt.OffsetMin.y, rt.OffsetMax.x, rt.OffsetMax.y, d.x, d.y);
                const size_t entries = m_Ctx.Commands.UndoCount() - t.undoBefore;
                t.check("ED01", entries == 1, "KI-73: " + std::to_string(entries) + " CommandStack entries for one gesture");
                t.check("ED01", m_Ctx.Commands.UndoName() == "Move UI Rect", "KI-73: undo label '" + m_Ctx.Commands.UndoName() + "'");
                t.check("ED01", m_Ctx.PrimaryEntity() == plot, "KI-73: the selection left the Plot");
                if (entries == 1) m_Ctx.Commands.Undo();
                t.check("ED01", rt.OffsetMin == t.plotMin && rt.OffsetMax == t.plotMax, "KI-73: undo did not restore the Plot");
                m_Ctx.Commands.Undo();   // the overlay's Create
                t.check("ED01", !m_Ctx.Scene->FindByUUID(t.overlayUuid), "undo did not remove the overlay");
                m_Ctx.ClearSelection();
                waitFrames(2);
                return true;
            });

            // ======================= ED01 — the sprite under an opaque UiImage (KI-75) ===========
            add("ED01 open the fixture, SpriteUnderUi, select the sprite, snap off", [&]
            {
                const bool ok = OpenProjectPath(t.fixtureDir);
                t.check("ED01", ok && m_Ctx.ProjectOpen && m_Ctx.ProjectName == "Ux02Fixture", "fixture project did not open: " + t.fixtureDir);
                OpenScene("project://scenes/SpriteUnderUi.cscene");
                m_Aspect = GameAspect::Free;
                Cosmic::Entity s = findTagged("Ux02Sprite");
                if (!s) { t.fail("ED01", "fixture sprite Ux02Sprite missing"); return true; }
                t.spriteUuid = uuidOf(s);
                m_Ctx.SelectOnly(s);
                // Snap off for an exact world delta (the strip's own prefs round trip, restored after).
                m_Viewport.SaveSnapPrefs(t.snapSaved);
                Prefs::EditorSettings off = t.snapSaved; off.SnapMoveOn = false;
                m_Viewport.LoadSnapPrefs(off);
                t.check("ED01", m_Viewport.GetOperation() == Cosmic::Gizmo::Operation::Translate, "the gizmo operation is not Move (W)");
                waitFrames(6);
                return true;
            });
            t.spriteLegStep = (int)t.plan.size();   // the retry target (see the verify step)
            add("ED01 sprite: pointer to the body (OS cursor)", [&]
            {
                Cosmic::Entity s = m_Ctx.Scene->FindByUUID(t.spriteUuid);
                if (!s) { t.fail("ED01", "sprite lost"); return true; }
                m_Ctx.SelectOnly(s);   // (again on a retry)
                const auto& tr = s.GetComponent<Cosmic::TransformComponent>();
                t.spriteBefore = tr.Position;
                // the lower-left quarter of the 4x4-unit body: clear of the gizmo's +X/+Y handles
                const glm::vec2 world{ tr.Position.x - 0.25f * tr.Scale.x, tr.Position.y - 0.25f * tr.Scale.y };
                const glm::vec2 im = w2s(world);
                t.osStart = osTo(im);
                t.osEnd   = POINT{ t.osStart.x + 40, t.osStart.y - 20 };
                t.imStart = imOf(t.osStart);
                t.imEnd   = imOf(t.osEnd);
                const glm::vec2 vp = app.GetViewportPos(), vs = app.GetViewportSize();
                const bool inside = t.imStart.x > vp.x + 4 && t.imStart.y > vp.y + 4 && t.imStart.x < vp.x + vs.x - 4 && t.imStart.y < vp.y + vs.y - 4;
                t.check("ED01", inside, "the press point is outside the viewport");
                uint32_t hit = 0;
                const bool underUi = Cosmic::UiSystem::HitTest(*m_Ctx.Scene, band(), t.imStart - vp, hit) &&
                                     hit == (uint32_t)(entt::entity)findTagged("Backdrop");
                t.check("ED01", underUi, "the press point is not covered by the opaque Backdrop (fixture setup)");
                t.undoBefore = m_Ctx.Commands.UndoCount();
                t.gesturesBefore = m_Viewport.BodyDragGestures();
                t.note("ED01 sprite press at world (%.3f,%.3f) -> OS (%ld,%ld), attempt %d", world.x, world.y, t.osStart.x, t.osStart.y, t.spriteAttempts + 1);
                osMove(t.osStart, false);
                waitFrames(4);
                return true;
            });
            add("ED01 sprite: press", [&] { osMove(t.osStart, false); osButton(t.osStart, true); waitFrames(3); return true; });
            for (int i = 1; i <= 8; ++i)
                add("ED01 sprite: drag", [&, i]
                {
                    const POINT p{ t.osStart.x + 5 * i, t.osStart.y - (5 * i) / 2 };
                    osMove(i == 8 ? t.osEnd : p, true);
                    waitFrames(1);
                    return true;
                });
            add("ED01 sprite: release", [&] { osMove(t.osEnd, true); osButton(t.osEnd, false); waitFrames(5); return true; });
            add("ED01 sprite: verify move + one entry + undo", [&]
            {
                Cosmic::Entity s = m_Ctx.Scene->FindByUUID(t.spriteUuid);
                if (!s) { t.fail("ED01", "sprite lost after the drag"); return true; }
                auto& tr = s.GetComponent<Cosmic::TransformComponent>();
                const glm::vec2 expect = s2w(t.imEnd) - s2w(t.imStart);
                const glm::vec2 got{ tr.Position.x - t.spriteBefore.x, tr.Position.y - t.spriteBefore.y };
                const size_t entries = m_Ctx.Commands.UndoCount() - t.undoBefore;
                const bool gesture = m_Viewport.BodyDragGestures() > t.gesturesBefore;
                const std::string sel = m_Ctx.PrimaryEntity() && m_Ctx.PrimaryEntity().HasComponent<Cosmic::TagComponent>()
                                      ? m_Ctx.PrimaryEntity().GetComponent<Cosmic::TagComponent>().Tag : std::string("none");
                // The OS cursor is shared with every other process (and other lanes' OS-level
                // tests): the viewport's own record of the gesture tells whether the pointer it
                // saw is the one this step drove. A disturbed INPUT is retried (at most 3
                // attempts, each logged); the product checks below are never retried.
                const float px = std::abs(s2w(t.imStart + glm::vec2(1.0f, 0.0f)).x - s2w(t.imStart).x);   // one pixel in world units
                const glm::vec2 sawPress = m_Viewport.BodyDragPressWorld(), sawLast = m_Viewport.BodyDragLastWorld();
                const bool pathOk = gesture &&
                    glm::length(sawPress - s2w(t.imStart)) <= 0.75f * px && glm::length(sawLast - s2w(t.imEnd)) <= 0.75f * px;
                if (gesture && !pathOk && t.spriteAttempts < 2)
                {
                    ++t.spriteAttempts;
                    t.note("ED01 sprite: the OS cursor was disturbed during the gesture (press seen at (%.4f,%.4f), driven (%.4f,%.4f); release seen (%.4f,%.4f), driven (%.4f,%.4f)) - input retry %d",
                           sawPress.x, sawPress.y, s2w(t.imStart).x, s2w(t.imStart).y, sawLast.x, sawLast.y, s2w(t.imEnd).x, s2w(t.imEnd).y, t.spriteAttempts);
                    t.check("ED01", std::abs(got.x - (sawLast - sawPress).x) <= 1e-4f && std::abs(got.y - (sawLast - sawPress).y) <= 1e-4f,
                            "KI-75: the sprite did not follow the pointer the viewport saw");
                    if (entries == 1) m_Ctx.Commands.Undo();
                    t.step = t.spriteLegStep - 1;   // the runner advances to the retry target
                    waitFrames(3);
                    return true;
                }
                t.numbers["ed01_sprite_input_retries"] = std::to_string(t.spriteAttempts);
                char b[200]; std::snprintf(b, sizeof(b), "(%.5f, %.5f) expected (%.5f, %.5f)", got.x, got.y, expect.x, expect.y);
                t.numbers["ed01_sprite_world_delta"] = b;
                t.note("ED01 sprite delta %s", b);
                if (!gesture) t.fail("ED01", "KI-75: the press on the selected sprite did not start a drag (selection is now '%s')", sel.c_str());
                const bool moved = std::abs(got.x - expect.x) <= 1e-3f && std::abs(got.y - expect.y) <= 1e-3f &&
                                   std::abs(expect.x) > 1e-4f && tr.Position.z == t.spriteBefore.z;
                if (!moved) t.fail("ED01", "KI-75: sprite moved by %s%s", b, pathOk || !gesture ? "" : " (the OS cursor was still disturbed after 3 attempts)");
                t.check("ED01", entries == 1, "KI-75: " + std::to_string(entries) + " CommandStack entries for one press-drag");
                t.check("ED01", m_Ctx.PrimaryEntity() == s, "KI-75: the press changed the selection (to '" + sel + "')");
                if (entries == 1) m_Ctx.Commands.Undo();
                t.check("ED01", tr.Position == t.spriteBefore, "KI-75: undo did not restore the sprite's Position");
                m_Viewport.LoadSnapPrefs(t.snapSaved);
                t.pass("ED01");
                m_Ctx.ClearDirty();   // leave no pending edit for the next steps
                waitFrames(2);
                return true;
            });

            // ======================= ED03 — what a button does (PendulumLab) =======================
            add("ED03 open PendulumLab Settings, select Back", [&]
            {
                const bool ok = OpenProjectPath(t.pendulumDir);
                t.check("ED03", ok && m_Ctx.ProjectOpen, "PendulumLab copy did not open: " + t.pendulumDir);
                m_ShowScreens = false;   // View menu: MountProject shows it for app projects; keep it off the viewport
                OpenScene("project://scenes/Settings.cscene");
                m_Aspect = GameAspect::Free;
                Cosmic::Entity back;
                auto& reg = m_Ctx.Scene->GetRegistry();
                for (auto e : reg.view<Cosmic::UiButtonComponent>())
                    if (reg.get<Cosmic::UiButtonComponent>(e).Signal == "back_clicked") back = Cosmic::Entity(e, m_Ctx.Scene.get());
                if (!back) { t.fail("ED03", "Settings has no back_clicked button"); return true; }
                t.buttonUuid = uuidOf(back);
                m_Ctx.SelectOnly(back);
                m_ShowInspector = true;
                t.scrollTries = 0;
                waitFrames(6);
                return true;
            });
            add("ED03 right-click the Back button in the viewport", [&]
            {
                Cosmic::Entity back = m_Ctx.Scene->FindByUUID(t.buttonUuid);
                Cosmic::UiRect r; float sc = 1.0f;
                if (!back || !elementRect(back, r, sc)) { t.fail("ED03", "Back button rect not resolved"); return true; }
                const glm::vec2 c = app.GetViewportPos() + r.Center();
                m_VpMenuFlowFrame = -1;
                t.click(c.x, c.y, 1);
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("ED03 viewport context menu shows the Flow line", [&]
            {
                const std::string want = flowLine("Settings", "back_clicked", "Lab");
                const bool shown = m_VpMenuFlowFrame >= 0 && ImGui::GetFrameCount() - m_VpMenuFlowFrame <= 3 &&
                                   std::find(m_VpMenuFlowLines.begin(), m_VpMenuFlowLines.end(), want) != m_VpMenuFlowLines.end();
                std::string seen; for (const auto& l : m_VpMenuFlowLines) seen += l + " | ";
                t.check("ED03", shown, "the viewport 'Open logic source' menu did not draw '" + want + "' (drew: " + seen + ")");
                shot("ed03-viewport-menu-flow-line");
                t.key(ImGuiKey_Escape);
                t.focusWindow = "Inspector";
                waitFrames(6);
                return true;
            });
            add("ED03 Inspector shows the Flow line (scroll it into view)", [&]
            {
                const std::string want = flowLine("Settings", "back_clicked", "Lab");
                const InspectorPanel::FlowRowProbe* row = nullptr;
                for (const auto& r : m_Inspector.LastFlowRows()) if (r.Line == want) row = &r;
                if (!row)
                {
                    if (++t.scrollTries < 60) return false;
                    std::string seen; for (const auto& r : m_Inspector.LastFlowRows()) seen += r.Line + " | ";
                    t.fail("ED03", "the Inspector drew no '%s' line (drew: %s)", want.c_str(), seen.c_str());
                    return true;
                }
                if (!row->Visible && t.scrollTries < 40)
                {
                    ++t.scrollTries;
                    t.scrollWindow = "Inspector"; t.scrollToY = row->Cy;
                    waitFrames(2);
                    return false;
                }
                t.check("ED03", row->Visible, "the Flow line's button never became visible in the Inspector");
                t.check("ED03", row->Hit.StateIndex == 2 && row->Hit.TransitionIndex == 0 && row->Hit.FlowVfs == "project://flows/Main.cflow",
                        "the Flow hit's indices / document are wrong");
                t.numbers["ed03_inspector_line"] = row->Line;
                shot("ed03-inspector-flow-line");
                return true;
            });
            add("ED03 click Open in flow editor", [&]
            {
                m_LastFlowOpen = FlowOpenRecord{};
                const std::string want = flowLine("Settings", "back_clicked", "Lab");
                for (const auto& r : m_Inspector.LastFlowRows())
                    if (r.Line == want) { t.click(r.Cx, r.Cy); break; }
                waitFrames((int)t.injects.size() + 6);
                return true;
            });
            add("ED03 verify the flow document + selection", [&]
            {
                t.check("ED03", m_LastFlowOpen.Opened && m_LastFlowOpen.Vfs == "project://flows/Main.cflow",
                        "Open in flow editor did not open project://flows/Main.cflow (got '" + m_LastFlowOpen.Vfs + "')");
                t.check("ED03", m_LastFlowOpen.State == 2 && m_LastFlowOpen.Transition == 0, "the transition selected is not Settings[0]");
                t.check("ED03", m_Editors.AnyOpen(), "the Editors host has no document");
                shot("ed03-flow-editor-opened");
                t.pass("ED03");
                waitFrames(4);
                return true;
            });
            // KI-78: a hot-reload build the PendulumLab open queued would complete in the NEXT
            // project's session, reload the wrong module and clear its dirty flag — wait it out
            // here so ED05 exercises the window-close path, not that defect.
            add("ED03 wait: builder idle before leaving PendulumLab (KI-78)", [&]
            {
                return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f && t.secondsInStep() > 1.0;
            });

            // ======================= ED04 — the Scenes list =======================
            add("ED04 open the fixture + Screens", [&]
            {
                const bool ok = OpenProjectPath(t.fixtureDir);
                t.check("ED04", ok && m_Ctx.SceneVfsPath == "project://scenes/Main.cscene",
                        "the fixture did not open on its start scene (open: '" + m_Ctx.SceneVfsPath + "')");
                m_ShowScreens = true;
                m_Screens.Invalidate();
                t.layoutScreens = true;
                waitFrames(8);
                return true;
            });
            add("ED04 Scenes section lists the fixture set", [&]
            {
                const std::vector<std::string> expect = { "project://scenes/Main.cscene", "project://scenes/overlays/Pause.cscene",
                                                          "project://scenes/SpriteUnderUi.cscene" };
                std::string got; for (const auto& s : m_Screens.SceneList()) got += s + ",";
                t.numbers["ed04_scenes_section"] = got;
                t.check("ED04", m_Screens.SceneList() == expect, "Screens > Scenes listed: " + got);
                t.check("ED04", SceneMenuEntries() == expect, "File > Open Scene's lister differs from the Scenes section");
                const bool rows = m_Screens.SceneRows().size() == expect.size();
                int starts = 0, opens = 0; bool startMarked = false, openMarked = false;
                for (const auto& r : m_Screens.SceneRows())
                {
                    starts += r.Start ? 1 : 0; opens += r.Open ? 1 : 0;
                    if (r.Vfs == "project://scenes/Main.cscene") { startMarked = r.Start; openMarked = r.Open; }
                }
                startMarked = startMarked && starts == 1;
                openMarked  = openMarked && opens == 1;
                t.check("ED04", rows, "the Scenes section drew " + std::to_string(m_Screens.SceneRows().size()) + " rows");
                t.check("ED04", startMarked, "the start scene (Main) is not the only one marked start");
                t.check("ED04", openMarked, "the open scene (Main) is not highlighted");
                shot("ed04-screens-scenes-section");
                return true;
            });
            add("ED04 double-click overlays/Pause", [&]
            {
                const ScreensPanel::SceneRowProbe* row = nullptr;
                for (const auto& r : m_Screens.SceneRows()) if (r.Vfs == "project://scenes/overlays/Pause.cscene") row = &r;
                if (!row || !row->Visible) { t.fail("ED04", "the overlays/Pause row is not visible in the Screens panel"); return true; }
                const float x = row->Cx, y = row->Cy;
                t.injects.push_back({ x, y, -1, false });
                t.injects.push_back({ x, y, -1, false });
                t.injects.push_back({ x, y, 0, true });
                t.injects.push_back({ x, y, 0, false });
                t.injects.push_back({ x, y, 0, true });
                t.injects.push_back({ x, y, 0, false });
                t.injects.push_back({ x, y, -1, false });
                waitFrames((int)t.injects.size() + 6);
                return true;
            });
            add("ED04 double-click opened it; File > Open Scene via the pointer", [&]
            {
                t.check("ED04", m_Ctx.SceneVfsPath == "project://scenes/overlays/Pause.cscene",
                        "double-click did not open overlays/Pause (open: '" + m_Ctx.SceneVfsPath + "')");
                m_SceneMenuProbe.Frame = -1000;
                t.click(m_SceneMenuProbe.FileX, m_SceneMenuProbe.FileY);
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("ED04 click Open Scene (the submenu)", [&]
            {
                // A click, not a pure hover: a pointer that only moves can be overridden by the
                // GLFW backend's poll of the real OS cursor when that cursor is outside the window
                // (another lane's window on top); a press is always delivered at the injected spot.
                t.click(m_SceneMenuProbe.OpenSceneX, m_SceneMenuProbe.OpenSceneY);
                waitFrames((int)t.injects.size() + 6);
                return true;
            });
            add("ED04 the menu drew the same set; click SpriteUnderUi", [&]
            {
                const std::vector<std::string> expect = { "project://scenes/Main.cscene", "project://scenes/overlays/Pause.cscene",
                                                          "project://scenes/SpriteUnderUi.cscene" };
                const bool recent = ImGui::GetFrameCount() - m_SceneMenuProbe.Frame <= 3;
                std::string got; for (const auto& s : m_SceneMenuProbe.Drawn) got += s + ",";
                t.numbers["ed04_file_open_scene_menu"] = got;
                t.check("ED04", recent, "File > Open Scene submenu is not open after the pointer opened it");
                t.check("ED04", m_SceneMenuProbe.Drawn == expect, "File > Open Scene drew: " + got);
                shot("ed04-file-open-scene");
                if (recent && m_SceneMenuProbe.Drawn.size() == 3)
                    t.click(m_SceneMenuProbe.ItemX[2], m_SceneMenuProbe.ItemY[2]);
                waitFrames((int)t.injects.size() + 6);
                return true;
            });
            add("ED04 the menu click opened SpriteUnderUi", [&]
            {
                t.check("ED04", m_Ctx.SceneVfsPath == "project://scenes/SpriteUnderUi.cscene",
                        "File > Open Scene > SpriteUnderUi did not open it (open: '" + m_Ctx.SceneVfsPath + "')");
                t.pass("ED04");
                waitFrames(2);
                return true;
            });

            // ======================= ED05 — preferences, autosave, the prompt =======================
            auto togglePref = [&](bool autosaveBox, const char* what)
            {
                t.planNames.push_back(std::string("ED05 open Preferences (") + what + ")");
                t.plan.push_back([&] { m_OpenPreferences = true; waitFrames(3); return true; });
                t.planNames.push_back(std::string("ED05 click ") + what);
                t.plan.push_back([&, autosaveBox]
                {
                    if (!m_PrefsProbe.Drawn) { t.fail("ED05", "the Preferences modal is not drawn"); return true; }
                    if (autosaveBox) t.click(m_PrefsProbe.AutosaveX, m_PrefsProbe.AutosaveY);
                    else             t.click(m_PrefsProbe.PromptX, m_PrefsProbe.PromptY);
                    waitFrames((int)t.injects.size() + 3);
                    return true;
                });
                t.planNames.push_back("ED05 close Preferences");
                t.plan.push_back([&, autosaveBox]
                {
                    shot(autosaveBox ? "ed05-preferences-autosave" : "ed05-preferences-prompt");
                    t.click(m_PrefsProbe.CloseX, m_PrefsProbe.CloseY);
                    waitFrames((int)t.injects.size() + 3);
                    return true;
                });
            };
            togglePref(true, "Autosave the open scene (-> off)");
            add("ED05 autosave off: 2x the interval of injected time writes nothing", [&]
            {
                t.check("ED05", !m_Settings.AutosaveEnabled, "the Preferences checkbox did not turn autosave off");
                std::string toml = ReadAll(Prefs::PrefsPath());
                toml.erase(std::remove(toml.begin(), toml.end(), '\r'), toml.end());
                t.check("ED05", toml.find("autosave_enabled = false\n") != std::string::npos, "editor.toml lacks autosave_enabled = false");
                t.check("ED05", !m_PrefsProbe.Drawn, "the Preferences modal did not close");
                const fs::path dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName);
                std::error_code ec; fs::remove_all(dir, ec);
                m_LastAutosaveHHMM.clear();
                Commands::Create(m_Ctx, "Ux02Dirty", {}, [](Cosmic::Entity) {});
                t.check("ED05", m_Ctx.Dirty, "Commands::Create did not dirty the scene");
                const float interval = (float)Prefs::ClampAutosaveMinutes(m_Settings.AutosaveMinutes) * 60.0f;
                Autosave(interval);
                Autosave(interval);
                const size_t n = CountFiles(dir);
                t.numbers["ed05_autosave_off_files_after_2x_interval"] = std::to_string(n);
                t.check("ED05", n == 0, "autosave OFF wrote " + std::to_string(n) + " file(s) over 2x the interval");
                t.check("ED05", m_LastAutosaveHHMM.empty(), "autosave OFF stamped the status bar");
                return true;
            });
            togglePref(true, "Autosave the open scene (-> on)");
            add("ED05 autosave on: one file + the status text", [&]
            {
                t.check("ED05", m_Settings.AutosaveEnabled, "the Preferences checkbox did not turn autosave back on");
                const fs::path dir = Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName);
                const float interval = (float)Prefs::ClampAutosaveMinutes(m_Settings.AutosaveMinutes) * 60.0f;
                Autosave(interval);
                const size_t n = CountFiles(dir);
                t.numbers["ed05_autosave_on_files_after_1x_interval"] = std::to_string(n);
                t.check("ED05", n == 1, "autosave ON wrote " + std::to_string(n) + " file(s) after one interval");
                t.check("ED05", fs::exists(dir / (m_Ctx.SceneName + ".cscene")), "the autosave copy is not <scene>.cscene");
                waitFrames(3);
                return true;
            });
            add("ED05 status bar shows autosaved HH:MM", [&]
            {
                t.numbers["ed05_status_chip"] = m_AutosaveChipShown;
                t.check("ED05", m_AutosaveChipShown.rfind("autosaved ", 0) == 0 && m_AutosaveChipShown.size() == 15,
                        "the status bar chip is '" + m_AutosaveChipShown + "'");
                // the prompt: dirty scene + prompt_unsaved on -> the OpenScene command asks
                t.check("ED05", m_Settings.PromptUnsaved && m_Ctx.Dirty, "precondition: prompt on + dirty scene");
                t.sceneBefore = m_Ctx.SceneVfsPath;
                RequestOpenScene("project://scenes/Main.cscene");
                waitFrames(3);
                return true;
            });
            add("ED05 the modal is up -> Cancel", [&]
            {
                const bool win = ImGui::FindWindowByName("Unsaved Changes##ux02") != nullptr;
                t.check("ED05", m_UnsavedProbe.Drawn && win, "the Save / Discard / Cancel modal is not shown");
                t.check("ED05", m_Ctx.SceneVfsPath == t.sceneBefore, "the scene switched before the answer");
                shot("ed05-unsaved-prompt");
                if (m_UnsavedProbe.Drawn) t.click(m_UnsavedProbe.CancelX, m_UnsavedProbe.CancelY);
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("ED05 Cancel kept the scene -> ask again, Discard", [&]
            {
                t.check("ED05", !m_UnsavedProbe.Drawn, "Cancel did not close the modal");
                t.check("ED05", m_Ctx.SceneVfsPath == t.sceneBefore && m_Ctx.Dirty, "Cancel did not keep the scene and its edits");
                RequestOpenScene("project://scenes/Main.cscene");
                waitFrames(3);
                return true;
            });
            add("ED05 Discard", [&]
            {
                t.check("ED05", m_UnsavedProbe.Drawn, "the modal did not come back for the second command");
                if (m_UnsavedProbe.Drawn) t.click(m_UnsavedProbe.DiscardX, m_UnsavedProbe.DiscardY);
                waitFrames((int)t.injects.size() + 4);
                return true;
            });
            add("ED05 Discard switched", [&]
            {
                t.check("ED05", m_Ctx.SceneVfsPath == "project://scenes/Main.cscene" && !m_Ctx.Dirty,
                        "Discard did not switch to Main (open: '" + m_Ctx.SceneVfsPath + "')");
                return true;
            });
            togglePref(false, "Ask to save (-> off)");
            add("ED05 prompt off: the command switches directly", [&]
            {
                t.check("ED05", !m_Settings.PromptUnsaved, "the Preferences checkbox did not turn the prompt off");
                Commands::Create(m_Ctx, "Ux02Dirty2", {}, [](Cosmic::Entity) {});
                t.check("ED05", m_Ctx.Dirty, "precondition: dirty scene");
                RequestOpenScene("project://scenes/SpriteUnderUi.cscene");
                t.check("ED05", m_Ctx.SceneVfsPath == "project://scenes/SpriteUnderUi.cscene", "prompt off: the command did not switch at once");
                waitFrames(3);
                return true;
            });
            add("ED05 no modal with the prompt off", [&]
            {
                t.check("ED05", !m_UnsavedProbe.Drawn, "a modal appeared with prompt_unsaved off");
                return true;
            });
            togglePref(false, "Ask to save (-> on again)");
            add("ED05 window-close leg: leave a dirty scene for OnDetach", [&]
            {
                t.check("ED05", m_Settings.PromptUnsaved, "the prompt did not turn back on");
                OpenScene("project://scenes/Main.cscene");
                Commands::Create(m_Ctx, t.detachMarker, {}, [](Cosmic::Entity) {});
                const fs::path copy = fs::path(Cosmic::FileSystem::Resolve("user://starforge/autosave/" + m_Ctx.ProjectName)) / (m_Ctx.SceneName + ".cscene");
                std::error_code ec; fs::remove(copy, ec);
                t.detachExpected = fs::absolute(copy, ec).generic_string();
                t.check("ED05", m_Ctx.Dirty && !fs::exists(copy, ec), "precondition: dirty scene, no autosave copy yet");
                t.note("ED05 OnDetach copy expected at %s (marker %s) — checked by the wrapper", t.detachExpected.c_str(), t.detachMarker.c_str());
                t.pass("ED05");
                return true;
            });
            add("wait: builder idle before closing (a project open may have queued an auto-build)", [&]
            {
                return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f;
            });
            add("finish", [&] { return true; });
        }

        // ---- run the plan ------------------------------------------------------
        if (t.wait > 0) { --t.wait; return; }
        if (t.step < (int)t.plan.size())
        {
            const auto before = t.oracle.Take();
            const bool advance = t.plan[t.step]();
            const std::string why = t.oracle.Judge(before);
            if (!why.empty()) t.fail("ORACLE", "%s during '%s'", why.c_str(), t.planNames[t.step].c_str());
            if (advance)
            {
                t.note("step %d/%zu done: %s (%.1f s)", t.step + 1, t.plan.size(), t.planNames[t.step].c_str(), t.secondsInStep());
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
            f << "{\n  \"work_order\": \"UX-02\",\n  \"case\": \"ED01-ED05 viewport / inspector / scenes / preferences\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n";
            f << "  \"oracle\": { \"recovered_errors\": " << t.oracle.recoveredErrors << ", \"end_frame_leaks\": " << t.oracle.endFrameLeaks
              << ", \"context_drift\": " << t.oracle.contextDrift << " },\n";
            f << "  \"ids\": {";
            bool first = true;
            for (const char* id : { "ED01", "ED03", "ED04", "ED05" })
            {
                auto it = t.idStatus.find(id);
                f << (first ? " " : ", ") << "\"" << id << "\": \"" << (it == t.idStatus.end() ? "NOT_RUN" : it->second) << "\"";
                first = false;
            }
            f << " },\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n";
            f << "  \"detach_autosave_expected\": \"" << Esc(t.detachExpected) << "\",\n  \"detach_marker\": \"" << Esc(t.detachMarker) << "\",\n";
            f << "  \"numbers\": {";
            first = true;
            for (const auto& kv : t.numbers) { f << (first ? "\n" : ",\n") << "    \"" << Esc(kv.first) << "\": \"" << Esc(kv.second) << "\""; first = false; }
            f << "\n  },\n  \"shots\": [";
            for (size_t i = 0; i < t.shotsTaken.size(); ++i) f << (i ? ", " : "") << "\"" << Esc(t.shotsTaken[i]) << "\"";
            f << "],\n  \"checks_failed\": [\n";
            for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
            f << "  ],\n  \"log\": [\n";
            for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        {
            std::ofstream c(fs::path(t.resultPath).parent_path() / "ux02-editor-console.txt", std::ios::trunc);
            for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
        }
        std::printf("UX02_SELFTEST_RESULT=%s config=%s failedChecks=%d seconds=%.1f errors=%d leaks=%d drift=%d\n",
                    pass ? "PASS" : "FAIL", cfg, t.failures, total, t.oracle.recoveredErrors.load(), t.oracle.endFrameLeaks.load(),
                    t.oracle.contextDrift.load());
        std::fflush(stdout);
        if (pass) app.Close();        // the chrome ✕ path: OnDetach writes the dirty scene's autosave copy
        else      std::quick_exit(1);
    }
}
