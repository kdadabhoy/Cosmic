// GuideWalkthroughSelfTest.cpp — the PendulumLab walkthrough guide's driver
// (docs/guide/pendulumlab-walkthrough.md), executed inside the REAL Starforge editor.
//
// Armed by COSMIC_GUIDE_SELFTEST=<result.json>; COSMIC_GUIDE_ROOT=<folder the project is
// created in>, COSMIC_GUIDE_SHOTS=<folder for the PNG screenshots>, COSMIC_GUIDE_REF=<the
// Projects/PendulumLab/src folder whose service + screen sources the guide pastes>,
// COSMIC_GUIDE_SKIP_PACKAGE=1 to stop before File ▸ Package….
//
// Every step is the guide's step, performed through the same editor command the menu /
// panel / button runs (NewProjectAt = the New Project modal's Create, ScreensPanel::NewScreen =
// the New Screen popup's Create, Commands::Create with the Entity ▸ UI build lambdas,
// Commands::SetField = an Inspector edit, FlowAsset Load/Save = the flow editor's inspector,
// BuildScripts = Ctrl+B, PlayScene = the Play button, FeedSignal = clicking a screen button,
// PackageProject = File ▸ Package… ▸ Package). Menus are opened with injected pointer events
// (the L05/AP-03 pattern) at rects located through Dear ImGui's own DebugLocateItem, which
// also draws the green highlight the guide's images carry (re-coloured red by the
// post-processing script). Screenshots read the presented back buffer (the L05 pattern).
//
// Verdict -> exit code: PASS -> graceful close; FAIL -> quick_exit(1). JSON always written.

#include "StarforgeApp.h"
#include "commands/EditorCommands.h"
#include "editors/FlowEditor.h"

#include <Cosmic.h>
#include "layers/WorkspaceLayer.h"
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "utils/ImageIO.h"
#include "ui/IconsLucide.h"
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
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        using Cosmic::Reflect::FieldValue;

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
        bool WriteAll(const fs::path& p, const std::string& text)
        {
            std::error_code ec; fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc); out << text; return (bool)out;
        }
        bool ReplaceOnce(std::string& s, const std::string& from, const std::string& to)
        {
            const size_t pos = s.find(from);
            if (pos == std::string::npos) return false;
            s.replace(pos, from.size(), to);
            return true;
        }

        // L05 pattern: read the presented back buffer (the whole editor window) into a PNG.
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
            return Cosmic::ImageIO::WritePNG(path, w, h, 4, flipped.data());
        }

        // The item rect DebugLocateItem drew this frame (pure green, alpha 255 vertices in a
        // foreground draw list): the way the harness finds where a labelled button lives.
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
            return true;
        }

        // Guide-visible element specs (the tables in the chapter mirror these exactly).
        struct Field { const char* Component; const char* Name; FieldValue Value; };
        struct Elem
        {
            const char* Menu;        // Entity ▸ UI ▸ <Menu> (or "Sprite" = Entity ▸ 2D ▸ Sprite)
            const char* Name;        // the Inspector name field
            glm::vec2 AMin, AMax, OMin, OMax; int Z;
            std::vector<Field> Fields;
        };
        const glm::vec4 kBtnTint{ 0.16f, 0.19f, 0.25f, 0.92f };
        const glm::vec4 kBarTint{ 0.10f, 0.11f, 0.14f, 0.85f };
        const glm::vec4 kDim{ 0.7f, 0.74f, 0.82f, 1.0f };
        const glm::vec4 kAccent{ 0.6f, 0.8f, 1.0f, 1.0f };
        const glm::vec4 kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
        const glm::vec4 kLabelCol{ 0.8f, 0.84f, 0.9f, 1.0f };

        std::vector<Field> ButtonFields(const char* signal, const char* text)
        {
            return { { "UiButton", "Signal", std::string(signal) }, { "UiImage", "Tint", kBtnTint },
                     { "UiText", "Text", std::string(text) }, { "UiText", "SizePx", 26.0f } };
        }
        std::vector<Field> ValueFields(const char* ch, const char* fmt, const char* prefix, const char* suffix, float size, int halign, glm::vec4 col)
        {
            return { { "UiValueText", "Channel", std::string(ch) }, { "UiValueText", "Format", std::string(fmt) },
                     { "UiValueText", "Prefix", std::string(prefix) }, { "UiValueText", "Suffix", std::string(suffix) },
                     { "UiText", "Text", std::string("--") }, { "UiText", "SizePx", size }, { "UiText", "HAlign", (int32_t)halign }, { "UiText", "Color", col } };
        }
        std::vector<Field> TextFields(const char* text, float size, int halign, glm::vec4 col)
        {
            return { { "UiText", "Text", std::string(text) }, { "UiText", "SizePx", size }, { "UiText", "HAlign", (int32_t)halign }, { "UiText", "Color", col } };
        }

        // ---- Lab (the new screen; everything but Camera/Canvas is added by hand) ----
        const std::vector<Elem> kLab = {
            { "Image",       "Header",       {0,0},{1,0},      {0,0},{0,64}, 1,      { { "UiImage", "Tint", kBarTint } } },
            { "Text",        "HeaderTitle",  {0,0},{0,0},      {24,14},{400,50}, 2,  TextFields("Lab", 28.0f, 0, kWhite) },
            { "Indicator",   "Running",      {0,0},{0,0},      {120,20},{144,44}, 2, { { "UiIndicator", "Channel", std::string("pendulum.running") }, { "UiIndicator", "Op", std::string("==") }, { "UiIndicator", "Threshold", 1.0f } } },
            { "Text",        "RunningLabel", {0,0},{0,0},      {152,14},{360,50}, 2, TextFields("running", 18.0f, 0, kDim) },
            { "Value Text",  "HeaderPeriod", {1,0},{1,0},      {-360,14},{-24,50}, 2, ValueFields("pendulum.period_est", "%.3f", "period ", " s", 20.0f, 2, kAccent) },
            { "Value Text",  "AngleValue",   {0.52f,0},{0.75f,0}, {0,84},{0,132}, 1, ValueFields("pendulum.angle_deg", "%+.2f", "", " deg", 40.0f, 1, kWhite) },
            { "Value Text",  "OmegaValue",   {0.76f,0},{1,0},  {0,84},{-24,132}, 1, ValueFields("pendulum.omega", "%+.3f", "", " rad/s", 40.0f, 1, kWhite) },
            { "Text",        "EnergyLabel",  {0.52f,0},{0.52f,0}, {0,144},{120,172}, 1, TextFields("energy", 18.0f, 0, kDim) },
            { "Gauge",       "EnergyGauge",  {0.52f,0},{1,0},  {124,144},{-24,172}, 1, { { "UiGauge", "Channel", std::string("pendulum.energy") }, { "UiGauge", "Min", 0.0f }, { "UiGauge", "Max", 2.0f } } },
            { "Plot",        "Plot",         {0.52f,0},{1,0.52f}, {0,184},{-24,0}, 1, { { "UiPlot", "Channel", std::string("pendulum.angle_deg") }, { "UiPlot", "Channel2", std::string("pendulum.omega") }, { "UiPlot", "WindowSeconds", 10.0f } } },
            { "Hosted Panel","PhasePlot",    {0.52f,0.53f},{1,1}, {0,0},{-24,-84}, 1, { { "UiHostedPanel", "PanelName", std::string("PhasePlot") } } },
            { "Image",       "Footer",       {0,1},{1,1},      {0,-72},{0,0}, 1,     { { "UiImage", "Tint", kBarTint } } },
            { "Button",      "StartStopButton", {0.1f,0.967f},{0.1f,0.967f}, {-90,-22},{90,22}, 2, ButtonFields("startstop_clicked", "Start / Stop") },
            { "Button",      "ResetButton",  {0.24f,0.967f},{0.24f,0.967f}, {-75,-22},{75,22}, 2, ButtonFields("pendulum.reset", "Reset") },
            { "Button",      "NudgeButton",  {0.36f,0.967f},{0.36f,0.967f}, {-75,-22},{75,22}, 2, ButtonFields("pendulum.nudge", "Nudge") },
            { "Button",      "SettingsButton", {0.76f,0.967f},{0.76f,0.967f}, {-90,-22},{90,22}, 2, ButtonFields("settings_clicked", "Settings") },
            { "Button",      "HomeButton",   {0.9f,0.967f},{0.9f,0.967f}, {-75,-22},{75,22}, 2, ButtonFields("home_clicked", "Home") },
        };
        // ---- Stopped (overlay; the scaffold's Canvas gets a UiImage dimmer by hand) ----
        const std::vector<Elem> kStopped = {
            { "Image",      "Panel",        {0.5f,0.5f},{0.5f,0.5f}, {-320,-150},{320,150}, 1, { { "UiImage", "Tint", kBarTint } } },
            { "Text",       "Title",        {0.5f,0.42f},{0.5f,0.42f}, {-300,-30},{300,30}, 2, TextFields("The pendulum came to rest", 36.0f, 1, kWhite) },
            { "Value Text", "Energy",       {0.5f,0.5f},{0.5f,0.5f}, {-300,-18},{300,18}, 2, ValueFields("pendulum.energy", "%.4f", "energy ", " J/kg (below the 0.01 threshold)", 20.0f, 1, kDim) },
            { "Button",     "ResumeButton", {0.5f,0.6f},{0.5f,0.6f}, {-140,-28},{140,28}, 2, ButtonFields("resume_clicked", "Resume (reset)") },
        };
        // ---- Home / Settings: the template's entities, re-pointed (name -> field edits) ----
        struct Edit { const char* Entity; const char* Component; const char* Field; FieldValue Value; };
        const std::vector<Edit> kHomeEdits = {
            { "Title",           "UiText",      "Text",    std::string("PENDULUM LAB") },
            { "Subtitle",        "UiText",      "Text",    std::string("Physics in a C++ service, visuals authored in the editor, glued by the DataBus") },
            { "Uptime",          "UiValueText", "Channel", std::string("pendulum.period_est") },
            { "Uptime",          "UiValueText", "Format",  std::string("%.3f") },
            { "Uptime",          "UiValueText", "Prefix",  std::string("Last period estimate: ") },
            { "Uptime",          "UiValueText", "Suffix",  std::string(" s") },
            { "DashboardButton", "UiButton",    "Signal",  std::string("start_clicked") },
            { "DashboardButton", "UiText",      "Text",    std::string("Start") },
        };
        const std::vector<Edit> kSettingsEdits = {
            { "AmplitudeSliderLabel", "UiText",      "Text",    std::string("Length (m)") },
            { "AmplitudeSlider",      "UiSlider",    "Channel", std::string("settings.length") },
            { "AmplitudeSlider",      "UiSlider",    "Min",     0.25f },
            { "AmplitudeSlider",      "UiSlider",    "Max",     4.0f },
            { "AmplitudeSlider",      "UiSlider",    "PreviewValue", 1.0f },
            { "AmplitudeSliderValue", "UiValueText", "Channel", std::string("settings.length") },
            { "AmplitudeSliderValue", "UiValueText", "Format",  std::string("%.2f") },
            { "AmplitudeSliderValue", "UiValueText", "Suffix",  std::string(" m") },
            { "FrequencySliderLabel", "UiText",      "Text",    std::string("Gravity (m/s^2)") },
            { "FrequencySlider",      "UiSlider",    "Channel", std::string("settings.gravity") },
            { "FrequencySlider",      "UiSlider",    "Min",     1.0f },
            { "FrequencySlider",      "UiSlider",    "Max",     25.0f },
            { "FrequencySlider",      "UiSlider",    "Step",    0.0f },
            { "FrequencySlider",      "UiSlider",    "PreviewValue", 9.80665f },
            { "FrequencySliderValue", "UiValueText", "Channel", std::string("settings.gravity") },
            { "FrequencySliderValue", "UiValueText", "Format",  std::string("%.3f") },
            { "FrequencySliderValue", "UiValueText", "Suffix",  std::string(" m/s^2") },
            { "StepSliderLabel",      "UiText",      "Text",    std::string("Damping (1/s)") },
            { "StepSlider",           "UiSlider",    "Channel", std::string("settings.damping") },
            { "StepSlider",           "UiSlider",    "Min",     0.0f },
            { "StepSlider",           "UiSlider",    "Max",     1.0f },
            { "StepSlider",           "UiSlider",    "Step",    0.0f },
            { "StepSlider",           "UiSlider",    "PreviewValue", 0.0f },
            { "StepSliderValue",      "UiValueText", "Channel", std::string("settings.damping") },
            { "StepSliderValue",      "UiValueText", "Format",  std::string("%.3f") },
            { "StepSliderValue",      "UiValueText", "Suffix",  std::string(" 1/s") },
            { "AutoLabel",            "UiText",      "Text",    std::string("Small-angle model") },
            { "AutoToggle",           "UiToggle",    "Channel", std::string("settings.small_angle") },
            { "AutoToggle",           "UiToggle",    "Signal",  std::string("settings_changed") },
            { "AutoValue",            "UiValueText", "Channel", std::string("settings.small_angle") },
            { "Note",                 "UiText",      "Text",    std::string("Changes apply live: the service re-reads settings.* every fixed step. Reset re-releases from the release angle.") },
        };
        const std::vector<const char*> kSettingsDelete = {};   // the template's rows are all reused
    }

    struct StarforgeApp::GuideSelfTest
    {
        std::string resultPath, root, shots, ref;
        std::string projectName = "PendulumLab2", projectDir;
        bool skipPackage = false;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point stepStart = std::chrono::steady_clock::now();
        int frame = 0, wait = 0, step = 0;
        bool finished = false;
        std::vector<std::function<bool()>> plan;
        std::vector<std::string> planNames;
        double deadlineSec = 300.0;

        // injected pointer / keys (L05 pattern) — consumed in FrameEnd, one per frame
        struct Inject { float x = 0, y = 0; int button = -1; bool down = false; ImGuiKey key = ImGuiKey_None; bool keyDown = false; bool hasPos = true; };
        std::vector<Inject> injects;
        ImGuiID locateId = 0;          // DebugLocateItem target while non-zero (drawn every frame)
        bool    haveRect = false; ImRect rect;   // last located rect
        std::string shotRequest;       // save the back buffer at the next Tick under this name
        ImVec2 osClickPt{ -1, -1 };
        std::string scrollBottomWindow;   // scroll this window to its end once (FrameEnd)
        std::string focusWindow;       // SetWindowFocus(name) each FrameEnd while non-empty (brings a docked tab forward)
        std::vector<std::string> shotsTaken;
        std::map<std::string, ImRect> shotRects;   // shot name -> highlighted rect (for the annotation record)

        // results
        int failures = 0;
        std::vector<std::string> log, checks;
        std::map<std::string, std::string> facts;
        double angleMin = 1e9, angleMax = -1e9; int angleSamples = 0, signChanges = 0; double lastAngle = 0.0;
        int buildsSeen = 0;
        std::string liveState;

        void note(const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[GUIDE] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* fmt, ...)
        {
            char buf[1024]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(buf);
            log.emplace_back(std::string("FAIL: ") + buf); std::printf("[GUIDE] FAIL: %s\n", buf); std::fflush(stdout);
        }
        void check(bool ok, const std::string& what) { if (!ok) fail("%s", what.c_str()); }
        double secondsInStep() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - stepStart).count(); }
    };

    // =========================================================================
    void StarforgeApp::GuideSelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_GUIDE_SELFTEST");
        if (!rp || !*rp) return;
        m_Guide = new GuideSelfTest();
        auto& t = *m_Guide;
        t.resultPath = rp;
        if (const char* r = std::getenv("COSMIC_GUIDE_ROOT")) t.root = r;
        if (t.root.empty()) t.root = (fs::current_path() / "guide-root").generic_string();
        if (const char* s = std::getenv("COSMIC_GUIDE_SHOTS")) t.shots = s;
        if (t.shots.empty()) t.shots = (fs::path(t.resultPath).parent_path() / "shots").generic_string();
        if (const char* s = std::getenv("COSMIC_GUIDE_REF")) t.ref = s;
        if (const char* s = std::getenv("COSMIC_GUIDE_SKIP_PACKAGE")) t.skipPackage = (*s == '1');
        std::error_code ec; fs::create_directories(t.root, ec); fs::create_directories(t.shots, ec);
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        Cosmic::Application::Get().SetPauseOnMinimize(false);
        Cosmic::Application::Get().GetWindow().SetSize(1600, 900);
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_HasMouseHoveredViewport;
        t.note("armed: result=%s root=%s shots=%s ref=%s", rp, t.root.c_str(), t.shots.c_str(), t.ref.c_str());
    }

    void StarforgeApp::GuideSelfTestShutdown()
    {
        if (!m_Guide) return;
        delete m_Guide; m_Guide = nullptr;
    }

    // After the whole UI ran: locate/highlight, then inject one pointer/key event.
    void StarforgeApp::GuideSelfTestFrameEnd()
    {
        if (!m_Guide) return;
        auto& t = *m_Guide;
        if (!t.focusWindow.empty()) ImGui::SetWindowFocus(t.focusWindow.c_str());
        if (!t.scrollBottomWindow.empty())
        {
            for (ImGuiWindow* w : GImGui->Windows)   // by name, or a child whose mangled name contains it
                if (w->Name && std::string(w->Name).find(t.scrollBottomWindow) != std::string::npos) ImGui::SetScrollY(w, w->ScrollMax.y);
            t.scrollBottomWindow.clear();
        }
        if (t.locateId)
        {
            ImRect r;
            if (LocatedRect(r)) { t.rect = r; t.haveRect = true; }
            ImGui::DebugLocateItem(t.locateId);   // draws next frame (ItemAdd resolves it)
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
    void StarforgeApp::GuideSelfTestTick()
    {
        if (!m_Guide) return;
        auto& t = *m_Guide;
        if (t.finished) return;
        ++t.frame;
        auto& app = Cosmic::Application::Get();

        // A pending screenshot: the back buffer holds the previous, fully presented frame.
        if (!t.shotRequest.empty())
        {
            const std::string path = t.shots + "/" + t.shotRequest + ".png";
            if (SaveWindowShot(path)) { t.shotsTaken.push_back(t.shotRequest); t.note("shot %s%s", t.shotRequest.c_str(), t.haveRect ? " (+rect)" : ""); }
            else t.fail("screenshot %s could not be written", t.shotRequest.c_str());
            if (t.locateId && t.haveRect) t.shotRects[t.shotRequest] = t.rect;
            t.shotRequest.clear();
        }

        auto waitFrames = [&](int n) { t.wait = n; };
        auto reg = [&]() -> Cosmic::Reflect::TypeRegistry& { return Cosmic::Reflect::GetRegistry(); };
        auto typeId = [&](const char* name) -> entt::id_type
        {
            const auto* d = reg().FindByName(name);
            if (!d) { t.fail("reflected type '%s' not found", name); return 0; }
            return d->TypeId;
        };
        auto findByTag = [&](const std::string& tag) -> Cosmic::Entity
        {
            if (!m_Ctx.Scene) return {};
            auto& r = m_Ctx.Scene->GetRegistry();
            for (auto e : r.view<Cosmic::TagComponent>())
                if (r.get<Cosmic::TagComponent>(e).Tag == tag) return Cosmic::Entity(e, m_Ctx.Scene.get());
            return {};
        };
        auto setField = [&](Cosmic::Entity e, const char* comp, const char* field, const FieldValue& v)
        {
            const entt::id_type id = typeId(comp);
            if (!id || !e) return;
            const auto* d = reg().FindByName(comp);
            if (!d->Has || !d->Has(m_Ctx.Scene->GetRegistry(), (entt::entity)e)) { t.fail("'%s' has no %s component (field %s)", e.GetComponent<Cosmic::TagComponent>().Tag.c_str(), comp, field); return; }
            Commands::SetField(m_Ctx, e, id, field, v);
        };
        auto rename = [&](Cosmic::Entity e, const char* name) { setField(e, "Tag", "Tag", std::string(name)); };
        // Entity ▸ UI ▸ <menu>: the same build lambdas the menu items run (DrawEntityMenu / DrawUiWidgetMenu).
        auto createUi = [&](const char* menu, Cosmic::Entity parent) -> Cosmic::Entity
        {
            const std::string m = menu;
            std::function<void(Cosmic::Entity)> build;
            if (m == "Image")       build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiImageComponent>(); };
            else if (m == "Text")   build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiTextComponent>(); };
            else if (m == "Button") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiImageComponent>().Tint = { 0.25f, 0.28f, 0.34f, 1.0f }; e.AddComponent<Cosmic::UiButtonComponent>(); };
            else if (m == "Value Text") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiTextComponent>(); e.AddComponent<Cosmic::UiValueTextComponent>(); };
            else if (m == "Gauge")  build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiGaugeComponent>(); };
            else if (m == "Indicator") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiIndicatorComponent>(); };
            else if (m == "Plot")   build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiPlotComponent>(); };
            else if (m == "Slider") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiSliderComponent>(); };
            else if (m == "Toggle") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiImageComponent>().Tint = { 0.5f, 0.5f, 0.5f, 1.0f }; e.AddComponent<Cosmic::UiToggleComponent>(); };
            else if (m == "Hosted Panel") build = [](Cosmic::Entity e) { e.AddComponent<Cosmic::UiHostedPanelComponent>(); };
            else { t.fail("unknown UI menu '%s'", menu); return {}; }
            return Commands::Create(m_Ctx, menu, parent, [build](Cosmic::Entity e) { e.AddComponent<Cosmic::RectTransformComponent>(); build(e); });
        };
        auto buildElems = [&](const std::vector<Elem>& elems)
        {
            Cosmic::Entity canvas = findByTag("Canvas");
            t.check((bool)canvas, "scene has no Canvas entity");
            for (const Elem& el : elems)
            {
                m_Ctx.SelectOnly(canvas);   // the UI menu parents to the selection
                Cosmic::Entity e = createUi(el.Menu, canvas);
                if (!e) continue;
                rename(e, el.Name);
                // A button's label: Add Component ▸ UI ▸ UiText on the button entity (the Inspector path).
                if (std::string(el.Menu) == "Button") Commands::AddComponent(m_Ctx, e, typeId("UiText"));
                setField(e, "RectTransform", "AnchorMin", el.AMin);
                setField(e, "RectTransform", "AnchorMax", el.AMax);
                setField(e, "RectTransform", "OffsetMin", el.OMin);
                setField(e, "RectTransform", "OffsetMax", el.OMax);
                setField(e, "RectTransform", "ZOrder", (int32_t)el.Z);
                for (const Field& f : el.Fields) setField(e, f.Component, f.Name, f.Value);
            }
        };
        auto applyEdits = [&](const std::vector<Edit>& edits)
        {
            for (const Edit& ed : edits)
            {
                Cosmic::Entity e = findByTag(ed.Entity);
                if (!e) { t.fail("template entity '%s' not found", ed.Entity); continue; }
                setField(e, ed.Component, ed.Field, ed.Value);
            }
        };
        // Screen-space rect of a UI element in the current (edit or play) scene.
        auto elementScreenCenter = [&](const std::string& tag, ImVec2& out) -> bool
        {
            if (!m_Ctx.Scene) return false;
            const glm::vec2 vpPos = app.GetViewportPos(), vpSize = app.GetViewportSize();
            const Cosmic::UiRect band{ { m_GameBandUv.x * vpSize.x, m_GameBandUv.y * vpSize.y },
                                       { (m_GameBandUv.x + m_GameBandUv.z) * vpSize.x, (m_GameBandUv.y + m_GameBandUv.w) * vpSize.y } };
            std::vector<Cosmic::UiElement> els;
            Cosmic::UiSystem::CollectElements(*m_Ctx.Scene, band, els);
            auto& r = m_Ctx.Scene->GetRegistry();
            for (const auto& el : els)
            {
                const entt::entity h = static_cast<entt::entity>(el.Handle);
                if (auto* tg = r.try_get<Cosmic::TagComponent>(h); tg && tg->Tag == tag)
                {
                    const glm::vec2 c = el.Rect.Center();
                    out = ImVec2(vpPos.x + c.x, vpPos.y + c.y);
                    return true;
                }
            }
            return false;
        };
        auto clickAt = [&](ImVec2 p)
        {
            t.injects.push_back({ p.x, p.y, -1, false });
            t.injects.push_back({ p.x, p.y, -1, false });
            t.injects.push_back({ p.x, p.y, 0, true });
            t.injects.push_back({ p.x, p.y, 0, true });
            t.injects.push_back({ p.x, p.y, 0, false });
            t.injects.push_back({ p.x, p.y, -1, false });
        };
        // A screen button in Play: the game UI reads the OS cursor (Input::), so click with it.
        // The position is the real OS cursor (Input:: polls it); the button edges go to the
        // editor's own window as messages, so the click cannot land in another window.
        auto osPress = [&](ImVec2 p, bool down)
        {
            HWND hw = WO05NativeWindow(Cosmic::Application::Get().GetWindow());
            POINT cl{ (LONG)p.x, (LONG)p.y }; if (hw) ScreenToClient(hw, &cl);
            const LPARAM lp = MAKELPARAM(cl.x, cl.y);
            SetCursorPos((int)p.x, (int)p.y);
            if (hw) PostMessageW(hw, WM_MOUSEMOVE, down ? MK_LBUTTON : 0, lp);
            if (hw) PostMessageW(hw, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, lp);
        };
        auto addScreenClick = [&](const char* tag, const char* fallbackSignal)
        {
            const std::string tg = tag, sig = fallbackSignal;
            t.planNames.push_back("click " + tg + " (move)"); t.plan.push_back([&, tg, sig]
            {
                ImVec2 c;
                if (!elementScreenCenter(tg, c)) { t.fail("%s rect not found in Play (falling back to the signal %s)", tg.c_str(), sig.c_str()); m_PlayFlow.FeedSignal(sig); t.osClickPt = ImVec2(-1, -1); waitFrames(20); return true; }
                // ImGui space -> OS screen space: the main viewport's origin is the window's client origin.
                POINT origin{ 0, 0 };
                if (HWND hw = WO05NativeWindow(Cosmic::Application::Get().GetWindow())) ClientToScreen(hw, &origin);
                const ImVec2 vp = ImGui::GetMainViewport()->Pos;
                c = ImVec2(c.x - vp.x + (float)origin.x, c.y - vp.y + (float)origin.y);
                t.osClickPt = c; t.note("%s at ImGui (%.0f,%.0f) -> OS (%.0f,%.0f): OS cursor click", tg.c_str(), c.x + vp.x - (float)origin.x, c.y + vp.y - (float)origin.y, c.x, c.y);
                SetCursorPos((int)c.x, (int)c.y);
                if (HWND hw = WO05NativeWindow(Cosmic::Application::Get().GetWindow()))
                { POINT cl{ (LONG)c.x, (LONG)c.y }; ScreenToClient(hw, &cl); PostMessageW(hw, WM_MOUSEMOVE, 0, MAKELPARAM(cl.x, cl.y)); }
                waitFrames(4); return true;
            });
            t.planNames.push_back("click " + tg + " (down)"); t.plan.push_back([&] { if (t.osClickPt.x >= 0) osPress(t.osClickPt, true); waitFrames(3); return true; });
            t.planNames.push_back("click " + tg + " (up)");   t.plan.push_back([&] { if (t.osClickPt.x >= 0) osPress(t.osClickPt, false); waitFrames(24); return true; });
        };
        auto hoverAt = [&](ImVec2 p) { t.injects.push_back({ p.x, p.y, -1, false }); t.injects.push_back({ p.x, p.y, -1, false }); };
        auto pressKey = [&](ImGuiKey k)
        {
            GuideSelfTest::Inject d; d.hasPos = false; d.key = k; d.keyDown = true;
            GuideSelfTest::Inject u = d; u.keyDown = false;
            t.injects.push_back(d); t.injects.push_back(d); t.injects.push_back(u);
        };
        auto windowId = [&](const char* name) -> ImGuiID
        {
            ImGuiWindow* w = ImGui::FindWindowByName(name);
            return w ? w->ID : ImHashStr(name);
        };
        auto flowPath = [&]() { return fs::path(t.projectDir) / "flows" / "Main.cflow"; };
        auto loadFlow = [&](Cosmic::FlowAsset& a) -> bool
        {
            std::string err; const bool ok = Cosmic::FlowAsset::Load(a, flowPath().generic_string(), &err);
            if (!ok) t.fail("flow load failed: %s", err.c_str());
            return ok;
        };
        auto saveFlow = [&](Cosmic::FlowAsset& a)
        {
            t.check(a.Validate().empty(), "flow Validate() not empty after the edit");
            t.check(a.Save(flowPath().generic_string()), "flow save failed");
            m_Screens.Invalidate();
        };
        auto builderIdle = [&]() { return !m_Builder.IsBuilding() && m_Live.Debounce < 0.0f; };
        auto sampleAngle = [&]()
        {
            if (!IsPlaying() || !m_PlayBus.Has("pendulum.angle_deg")) return;
            const double a = m_PlayBus.GetNumber("pendulum.angle_deg");
            if (t.angleSamples > 0 && ((a < 0.0) != (t.lastAngle < 0.0))) ++t.signChanges;
            t.lastAngle = a; ++t.angleSamples;
            t.angleMin = std::min(t.angleMin, a); t.angleMax = std::max(t.angleMax, a);
        };
        // A screenshot step pair: highlight an item id (0 = none), wait, save.
        auto addShot = [&](const char* name, std::function<ImGuiID()> id, const char* focusWindow = nullptr)
        {
            const std::string n = name; const std::string fw = focusWindow ? focusWindow : "";
            t.planNames.push_back("shot arm " + n); t.plan.push_back([&, id, fw] { t.focusWindow = fw; t.locateId = id ? id() : 0; t.haveRect = false; waitFrames(4); return true; });
            t.planNames.push_back("shot hover " + n); t.plan.push_back([&]
            {
                if (t.locateId && t.haveRect)
                {
                    // Park the real cursor on the item too: ImGui draws its locate line from the mouse.
                    POINT origin{ 0, 0 };
                    if (HWND hw = WO05NativeWindow(Cosmic::Application::Get().GetWindow())) ClientToScreen(hw, &origin);
                    const ImVec2 c = t.rect.GetCenter(), vp = ImGui::GetMainViewport()->Pos;
                    SetCursorPos((int)(c.x - vp.x + origin.x), (int)(c.y - vp.y + origin.y));
                    hoverAt(c);
                }
                waitFrames(4); return true;
            });
            t.planNames.push_back("shot save " + n); t.plan.push_back([&, n] { t.shotRequest = n; waitFrames(2); return true; });
            t.planNames.push_back("shot clear " + n); t.plan.push_back([&] { t.locateId = 0; t.focusWindow.clear(); return true; });
        };
        // Locate an item (by id) and click / hover it with injected pointer events.
        auto addLocate = [&](const char* what, std::function<ImGuiID()> id, bool click)
        {
            const std::string w = what;
            t.planNames.push_back("locate arm " + w); t.plan.push_back([&, id] { t.locateId = id(); t.haveRect = false; waitFrames(3); return true; });
            t.planNames.push_back("locate act " + w); t.plan.push_back([&, w, click]
            {
                t.locateId = 0;
                if (!t.haveRect) { t.fail("could not locate '%s' on screen", w.c_str()); return true; }
                const ImVec2 c = t.rect.GetCenter();
                t.note("%s at (%.0f,%.0f) size %.0fx%.0f", w.c_str(), c.x, c.y, t.rect.GetWidth(), t.rect.GetHeight());
                if (click) clickAt(c); else hoverAt(c);
                waitFrames(8);
                return true;
            });
        };

        // ---- the plan (built once) ----------------------------------------------
        if (t.plan.empty())
        {
            auto add = [&](const char* name, std::function<bool()> fn) { t.planNames.push_back(name); t.plan.push_back(std::move(fn)); };
            add("boot", [&] { return t.frame > 12; });

            // ---- 1. homescreen + New Project ----
            addShot("01-homescreen-new-project", [&] { return ImHashStr("New Project", 0, windowId("##StarforgeHome")); });
            addLocate("New Project button", [&] { return ImHashStr("New Project", 0, windowId("##StarforgeHome")); }, true);
            add("type the project name", [&]
            {
                std::snprintf(m_NewProjectName, sizeof(m_NewProjectName), "%s", t.projectName.c_str());
                std::snprintf(m_NewProjectLoc, sizeof(m_NewProjectLoc), "%s", t.root.c_str());
                m_NewProjectKind = "app";
                waitFrames(3); return true;
            });
            addShot("02-new-project-template-app", [&] { return ImHashStr("App##tpl_app", 0, windowId("New Project")); });
            add("Create (New Project modal)", [&]
            {
                const bool ok = NewProjectAt(t.projectName, t.root, "app");
                t.projectDir = (fs::path(t.root) / t.projectName).generic_string();
                t.check(ok, "NewProjectAt(app) failed");
                t.check(m_Ctx.ProjectOpen, "project did not open");
                t.check(m_ProjectKind == "app", "manifest kind not read as app");
                t.check(m_AutoBuild, "AutoBuild not ON for kind app");
                t.check(m_Ctx.SceneName == "Home", "open scene is '" + m_Ctx.SceneName + "', expected the flow start 'Home'");
                pressKey(ImGuiKey_Escape);   // the modal was opened by the click; the command above already ran
                m_ShowScreens = true; m_ShowDataBus = true;   // View ▸ Screens, View ▸ DataBus
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer())   // dock them (the user drags the tabs)
                {
                    ws->DockWindow("Screens", Cosmic::DockPort::LeftBottom);
                    ws->DockWindow("DataBus", Cosmic::DockPort::BottomRight);
                    ws->DockWindow("Editors", Cosmic::DockPort::BottomCenter);
                    ws->SetEdgeRatios(0.19f, 0.27f, 0.08f, 0.26f);   // a wider Inspector for the pictures
                }
                waitFrames(6); return true;
            });
            add("wait: the scaffold's auto-build (kind app) finished", [&] { if (t.secondsInStep() < 2.0) return false; return builderIdle(); });
            add("Build Scripts (Ctrl+B) when the scaffold did not trigger a build", [&]
            {
                if (m_Builder.GetStatus() == BuildRunner::Status::Idle) { BuildScripts(); t.note("Ctrl+B: no auto-build had run after the scaffold"); }
                waitFrames(2); return true;
            });
            add("wait: template built", [&] { return builderIdle(); });
            add("template build ok", [&]
            {
                t.check(m_Builder.GetStatus() == BuildRunner::Status::Success, "the App template did not build");
                t.facts["template_build"] = m_Builder.GetStatus() == BuildRunner::Status::Success ? "ok" : "failed";
                return true;
            });
            addShot("03-template-open-home", nullptr);

            // ---- 2. Screens panel: New Screen Lab / Stopped ----
            addShot("04-screens-panel-new-screen", [&] { return ImHashStr(ICON_LC_PLUS " New Screen", 0, windowId("Screens")); }, "Screens");
            add("New Screen 'Lab' + 'Stopped' (Create script checked)", [&]
            {
                auto r1 = m_Screens.NewScreen(m_Ctx, ScreensHost(), "Lab", true);
                t.check(r1.Ok, "NewScreen Lab: " + r1.Message);
                auto r2 = m_Screens.NewScreen(m_Ctx, ScreensHost(), "Stopped", true);
                t.check(r2.Ok, "NewScreen Stopped: " + r2.Message);
                std::error_code ec;
                t.check(fs::exists(fs::path(t.projectDir) / "scenes" / "Lab.cscene", ec), "scenes/Lab.cscene missing");
                t.check(fs::exists(fs::path(t.projectDir) / "src" / "screens" / "LabScreen.h", ec), "src/screens/LabScreen.h missing");
                t.check(fs::exists(fs::path(t.projectDir) / "src" / "screens" / "StoppedScreen.h", ec), "src/screens/StoppedScreen.h missing");
                waitFrames(4); return true;
            });
            addShot("05-screens-panel-after", nullptr, "Screens");

            // ---- 3. Home: re-point the template's entities ----
            add("Home edits (Inspector)", [&]
            {
                OpenScene("project://scenes/Home.cscene");
                applyEdits(kHomeEdits);
                Cosmic::Entity b = findByTag("DashboardButton"); if (b) rename(b, "StartButton");
                Cosmic::Entity u = findByTag("Uptime");          if (u) rename(u, "Period");
                Cosmic::Entity logo = findByTag("Logo");         if (logo) Commands::Destroy(m_Ctx, logo);
                Cosmic::Entity hint = findByTag("Hint");         if (hint) setField(hint, "UiText", "Text", std::string("Escape returns to this screen from the Lab"));
                t.check(SaveScene(), "Home save failed");
                waitFrames(3); return true;
            });

            // ---- 4. Lab: build the screen by hand ----
            add("open Lab", [&] { OpenScene("project://scenes/Lab.cscene"); waitFrames(4); return true; });
            add("Lab: select Canvas", [&] { Cosmic::Entity c = findByTag("Canvas"); t.check((bool)c, "Lab has no Canvas"); m_Ctx.SelectOnly(c); waitFrames(2); return true; });
            // the Entity ▸ UI menu, opened with the pointer, for the picture
            addLocate("Entity menu", [&] { return ImHashStr("Entity", 0, ImHashStr("##MenuBar", 0, windowId("Starforge"))); }, true);
            addLocate("UI submenu", [&] { return ImHashStr("UI", 0, windowId("Entity###Menu_00")); }, true);   // a click opens it at once (hover needs the delay)
            addShot("06-entity-ui-menu", [&] { return ImHashStr("Value Text", 0, windowId("UI###Menu_01")); });
            add("close the menu", [&] { pressKey(ImGuiKey_Escape); pressKey(ImGuiKey_Escape); waitFrames(6); return true; });
            add("Lab: sprites Pivot / Rod / Bob (Entity ▸ 2D ▸ Sprite)", [&]
            {
                struct S { const char* Name; glm::vec3 Pos, Scale; glm::vec4 Color; int Z; };
                const S sprites[] = {
                    { "Pivot", { -2.5f, 3.2f, 0.0f },  { 0.16f, 0.16f, 1.0f }, { 0.85f, 0.85f, 0.9f, 1.0f }, 3 },
                    { "Rod",   { -2.5f, 1.2f, 0.0f },  { 0.05f, 4.0f, 1.0f },  { 0.75f, 0.78f, 0.85f, 1.0f }, 1 },
                    { "Bob",   { -2.5f, -0.8f, 0.0f }, { 0.6f, 0.6f, 1.0f },   { 1.0f, 0.55f, 0.15f, 1.0f }, 2 },
                };
                for (const S& s : sprites)
                {
                    Cosmic::Entity e = Commands::Create(m_Ctx, "Sprite", Cosmic::Entity{}, [](Cosmic::Entity e) { e.AddComponent<Cosmic::SpriteRendererComponent>(); });
                    rename(e, s.Name);
                    setField(e, "Transform", "Position", s.Pos);
                    setField(e, "Transform", "Scale", s.Scale);
                    setField(e, "SpriteRenderer", "Color", s.Color);
                    setField(e, "SpriteRenderer", "ZOrder", (int32_t)s.Z);
                }
                waitFrames(2); return true;
            });
            add("Lab: UI elements (Entity ▸ UI ▸ …, then the Inspector fields)", [&] { buildElems(kLab); waitFrames(2); return true; });
            add("Lab: select AngleValue for the Inspector picture", [&]
            {
                Cosmic::Entity e = findByTag("AngleValue"); t.check((bool)e, "AngleValue missing"); m_Ctx.SelectOnly(e);
                m_Aspect = GameAspect::Free;
                waitFrames(3); return true;
            });
            add("Inspector: scroll down to the UiValueText block", [&] { t.scrollBottomWindow = "Inspector"; waitFrames(3); return true; });
            addShot("07-inspector-valuetext-channel", [&]
            {
                const ImGuiID win = windowId("Inspector");
                const int tid = (int)typeId("UiValueText");
                const ImGuiID s1 = ImHashData(&tid, sizeof(tid), win);
                const ImGuiID s2 = ImHashStr("Channel", 0, s1);
                return ImHashStr("Channel", 0, s2);
            });
            add("Lab: save", [&] { t.check(SaveScene(), "Lab save failed"); waitFrames(2); return true; });

            // ---- 5. Settings: re-point the template's rows ----
            add("Settings edits (Inspector)", [&]
            {
                OpenScene("project://scenes/Settings.cscene");
                applyEdits(kSettingsEdits);
                const std::pair<const char*, const char*> renames[] = {
                    { "AmplitudeSliderLabel", "LengthSliderLabel" }, { "AmplitudeSlider", "LengthSlider" }, { "AmplitudeSliderValue", "LengthSliderValue" },
                    { "FrequencySliderLabel", "GravitySliderLabel" }, { "FrequencySlider", "GravitySlider" }, { "FrequencySliderValue", "GravitySliderValue" },
                    { "StepSliderLabel", "DampingSliderLabel" }, { "StepSlider", "DampingSlider" }, { "StepSliderValue", "DampingSliderValue" },
                    { "AutoLabel", "SmallAngleLabel" }, { "AutoToggle", "SmallAngleToggle" }, { "AutoValue", "SmallAngleValue" } };
                for (const auto& [from, to] : renames) { Cosmic::Entity e = findByTag(from); if (e) rename(e, to); else t.fail("Settings entity '%s' missing", from); }
                t.check(SaveScene(), "Settings save failed");
                waitFrames(3); return true;
            });

            // ---- 6. Stopped: the overlay ----
            add("Stopped: canvas dimmer + panel + text + Resume", [&]
            {
                OpenScene("project://scenes/Stopped.cscene");
                Cosmic::Entity canvas = findByTag("Canvas");
                if (canvas)
                {
                    Commands::AddComponent(m_Ctx, canvas, typeId("UiImage"));   // Add Component ▸ UI ▸ UiImage
                    setField(canvas, "UiImage", "Tint", glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
                }
                buildElems(kStopped);
                t.check(SaveScene(), "Stopped save failed");
                waitFrames(3); return true;
            });

            // ---- 7. The flow (Screens ▸ Flow graph; the transition inspector) ----
            add("flow edits", [&]
            {
                Cosmic::FlowAsset a; if (!loadFlow(a)) return true;
                // Delete State: Dashboard
                a.States.erase(std::remove_if(a.States.begin(), a.States.end(), [](const Cosmic::FlowState& s) { return s.Name == "Dashboard"; }), a.States.end());
                auto tr = [](const char* on, const char* to) { Cosmic::FlowTransition x; x.On = on; x.To = to; return x; };
                for (Cosmic::FlowState& s : a.States)
                {
                    if (s.Name == "Home")
                    {
                        for (auto& x : s.Transitions) if (x.On == "dashboard_clicked") { x.On = "start_clicked"; x.To = "Lab"; }
                    }
                    else if (s.Name == "Lab")
                    {
                        s.Transitions.clear();
                        Cosmic::FlowTransition when; when.On = "when"; when.To = "Stopped"; when.Push = true; when.HasGuard = true;
                        when.Guard.Channel = "pendulum.energy"; when.Guard.Op = "<"; when.Guard.Value = Cosmic::FlowValue::MakeNumber(0.01);
                        s.Transitions.push_back(when);
                        s.Transitions.push_back(tr("settings_clicked", "Settings"));
                        s.Transitions.push_back(tr("home_clicked", "Home"));
                        s.Transitions.push_back(tr("key:Escape", "Home"));
                        s.EditorPos = { 380.0f, 60.0f };
                    }
                    else if (s.Name == "Settings")
                    {
                        for (auto& x : s.Transitions) if (x.To == "Dashboard") x.To = "Lab";
                        s.EditorPos = { 720.0f, 60.0f };
                    }
                    else if (s.Name == "Stopped")
                    {
                        s.Overlay = true;
                        s.Transitions.clear();
                        s.Transitions.push_back(tr("resume_clicked", "@pop"));
                        s.EditorPos = { 380.0f, 300.0f };
                    }
                }
                a.Start = "Home";
                saveFlow(a);
                // re-read: the when transition round-trips
                Cosmic::FlowAsset b; if (loadFlow(b))
                {
                    const Cosmic::FlowState* lab = b.Find("Lab");
                    t.check(lab && !lab->Transitions.empty() && lab->Transitions[0].On == "when" && lab->Transitions[0].Guard.Channel == "pendulum.energy" && lab->Transitions[0].Push, "when/channel transition did not round-trip");
                    t.check(b.Find("Stopped") && b.Find("Stopped")->Overlay, "Stopped overlay flag not saved");
                    t.check(b.Find("Dashboard") == nullptr, "Dashboard state still present");
                }
                // open the flow document (Screens ▸ Flow graph) and select Lab + its when transition
                ScreensHost().OpenFlowDocument("project://flows/Main.cflow");
                waitFrames(4); return true;
            });
            add("flow editor: select Lab / when", [&]
            {
                IAssetEditor* doc = m_Editors.Open("project://flows/Main.cflow", []() { return std::make_unique<FlowEditor>("project://flows/Main.cflow"); }, &m_ShowEditors);
                Cosmic::FlowAsset a; loadFlow(a);
                int labIdx = -1; for (int i = 0; i < (int)a.States.size(); ++i) if (a.States[i].Name == "Lab") labIdx = i;
                if (auto* fe = dynamic_cast<FlowEditor*>(doc)) fe->HarnessSelect(labIdx, 0); else t.fail("flow document is not a FlowEditor");
                waitFrames(4); return true;
            });
            add("make the bottom dock tall for the flow document", [&]
            {
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->SetEdgeRatios(0.19f, 0.27f, 0.08f, 0.66f);
                waitFrames(6); return true;
            });
            add("flow inspector: scroll to the Transition section", [&] { t.scrollBottomWindow = "flow_inspector_region"; waitFrames(3); return true; });
            addShot("08-flow-when-guard", nullptr, "Editors");
            add("restore the Level layout ratios; View ▸ Editors (Flow / Story) off", [&]
            {
                if (auto* ws = Cosmic::Application::Get().GetWorkspaceLayer()) ws->SetEdgeRatios(0.19f, 0.27f, 0.08f, 0.26f);
                m_ShowEditors = false;
                waitFrames(6); return true;
            });

            // ---- 8. The C++: service + screen scripts + Module.cpp ----
            add("paste the service + screen scripts, edit Module.cpp, delete the template's Dashboard/AppService", [&]
            {
                const fs::path src = fs::path(t.projectDir) / "src";
                const fs::path ref = t.ref.empty() ? fs::path() : fs::path(t.ref);
                std::error_code ec;
                auto copyRef = [&](const char* rel)
                {
                    if (ref.empty()) { t.fail("COSMIC_GUIDE_REF not set: cannot paste %s", rel); return; }
                    const std::string text = ReadAll(ref / rel);
                    t.check(!text.empty(), std::string("reference source empty: ") + rel);
                    t.check(WriteAll(src / rel, text), std::string("could not write ") + rel);
                };
                copyRef("services/PendulumService.h");
                copyRef("services/PendulumService.cpp");
                copyRef("screens/ScreenCommon.h");
                copyRef("screens/LabScreen.h");
                copyRef("screens/SettingsScreen.h");
                copyRef("screens/StoppedScreen.h");
                std::string mod = ReadAll(src / "Module.cpp");
                { std::string lf; lf.reserve(mod.size()); for (char c : mod) if (c != '\r') lf += c; mod = lf; }
                t.check(ReplaceOnce(mod, "#include \"services/AppService.h\"", "#include \"services/PendulumService.h\""), "Module.cpp: AppService include not found");
                t.check(ReplaceOnce(mod, "CS_SERVICE(AppService).Order(0) CS_END;", "CS_SERVICE(PendulumService).Order(0) CS_END;"), "Module.cpp: CS_SERVICE(AppService) not found");
                t.check(ReplaceOnce(mod, "#include \"screens/DashboardScreen.h\"\n", ""), "Module.cpp: Dashboard include not found");
                const size_t db = mod.find("    CS_SCRIPT(DashboardScreen)");
                if (db != std::string::npos) { const size_t e = mod.find("CS_END;\n", db); if (e != std::string::npos) mod.erase(db, e + 8 - db); if (mod.compare(db, 1, "\n") == 0) mod.erase(db, 1); }
                else t.fail("Module.cpp: CS_SCRIPT(DashboardScreen) block not found");
                // the scaffold's CS_FIELD(ExampleField) lines -> the pasted classes' fields
                {
                    const size_t lab = mod.find("CS_SCRIPT(LabScreen)");
                    size_t f = lab == std::string::npos ? lab : mod.find("CS_FIELD(ExampleField)", lab);
                    if (f != std::string::npos) mod.replace(f, std::strlen("CS_FIELD(ExampleField)"), "CS_FIELD(RodWidth).Range(0.01f, 0.5f)"); else t.fail("Module.cpp: Lab CS_FIELD(ExampleField) not found");
                    const size_t st = mod.find("CS_SCRIPT(StoppedScreen)");
                    f = st == std::string::npos ? st : mod.find("CS_FIELD(ExampleField)", st);
                    if (f != std::string::npos) mod.replace(f, std::strlen("CS_FIELD(ExampleField)"), "CS_FIELD(ResetOnResume)"); else t.fail("Module.cpp: Stopped CS_FIELD(ExampleField) not found");
                }
                t.check(WriteAll(src / "Module.cpp", mod), "Module.cpp write failed");
                fs::remove(src / "services" / "AppService.h", ec);
                fs::remove(src / "services" / "AppService.cpp", ec);
                fs::remove(src / "screens" / "DashboardScreen.h", ec);
                fs::remove(fs::path(t.projectDir) / "scenes" / "Dashboard.cscene", ec);
                // Project Settings: Fixed Hz 240 (the RK4 step)
                {
                    const std::string mpath = (fs::path(t.projectDir) / "project.cproj").generic_string();
                    ProjectManifest pm = ProjectManifest::Load(mpath);
                    pm.FixedHz = 240; pm.WindowTitle = "Pendulum Lab";
                    t.check(pm.Save(mpath), "project.cproj save failed");
                }
                m_Ctx.Log("[Guide] Sources pasted; the live loop rebuilds now.");
                t.buildsSeen = m_Live.Builds;
                waitFrames(2); return true;
            });
            add("wait: live loop build started (500 ms debounce)", [&]
            {
                if (m_Builder.IsBuilding()) return true;
                if (m_Live.Builds > t.buildsSeen) return true;
                if (t.secondsInStep() > 20.0) { t.fail("auto-build did not start within 20 s of the src/ edits"); return true; }
                return false;
            });
            add("wait: module built", [&] { if (m_Builder.IsBuilding() || m_Live.Debounce >= 0.0f) return false; waitFrames(10); return true; });
            add("module build verdict", [&]
            {
                const bool ok = m_Builder.GetStatus() == BuildRunner::Status::Success;
                t.facts["pendulum_build"] = ok ? "ok" : "failed";
                if (!ok)
                {
                    t.fail("the PendulumLab2 module did not build (see console)");
                    for (const auto& l : m_Ctx.ConsoleLines) if (l.Text.find("error") != std::string::npos) t.note("console: %s", l.Text.c_str());
                    return true;
                }
                t.check(Cosmic::ModuleRegistry::Get().FindScript("LabScreen") != nullptr, "LabScreen not registered");
                t.check(Cosmic::ModuleRegistry::Get().FindScript("StoppedScreen") != nullptr, "StoppedScreen not registered");
                t.check(Cosmic::ModuleRegistry::Get().FindService("PendulumService") != nullptr, "PendulumService not registered");
                return true;
            });

            // ---- 9. Play ----
            add("Play from Home", [&]
            {
                OpenScene("project://scenes/Home.cscene");
                PlayScene();
                t.check(IsPlaying() && m_PlayFlowActive, "Play with the flow did not start");
                t.check(m_PlayFlow.CurrentState() == "Home", "flow state '" + m_PlayFlow.CurrentState() + "' != Home");
                waitFrames(20); return true;
            });
            add("bring the editor window to the front for the OS clicks", [&]
            {
                if (HWND hw = WO05NativeWindow(Cosmic::Application::Get().GetWindow())) SetForegroundWindow(hw);
                waitFrames(6); return true;
            });
            addScreenClick("StartButton", "start_clicked");
            add("Start verdict", [&]
            {
                if (m_PlayFlow.CurrentState() != "Lab") { t.fail("the OS click on Start did not navigate (state '%s'); feeding start_clicked", m_PlayFlow.CurrentState().c_str()); m_PlayFlow.FeedSignal("start_clicked"); waitFrames(20); }
                return true;
            });
            add("on Lab, sample the swing", [&]
            {
                if (m_PlayFlow.CurrentState() != "Lab") { t.fail("after Start the flow is on '%s', expected Lab", m_PlayFlow.CurrentState().c_str()); return true; }
                t.check(m_PlayBus.GetBool("pendulum.running"), "pendulum.running not true on Lab");
                t.angleSamples = 0; t.signChanges = 0; t.angleMin = 1e9; t.angleMax = -1e9;
                waitFrames(1); return true;
            });
            add("sampling the swing for 3 s (one period at L = 1 m is 2.0 s)", [&] { sampleAngle(); return t.secondsInStep() >= 3.0; });
            add("swing verdict", [&]
            {
                const double span = t.angleMax - t.angleMin;
                t.check(span > 4.0, "pendulum.angle_deg span " + std::to_string(span) + " deg over 3 s (expected > 4: a 5 deg release swings -5..+5)");
                t.check(t.signChanges >= 1, "pendulum.angle_deg never changed sign");
                char b[128]; std::snprintf(b, sizeof(b), "min %.3f max %.3f signChanges %d samples %d", t.angleMin, t.angleMax, t.signChanges, t.angleSamples);
                t.facts["swing"] = b; t.note("swing: %s", b);
                t.check(m_PlayBus.Producer("pendulum.angle_deg") == "PendulumService", "producer of pendulum.angle_deg is '" + m_PlayBus.Producer("pendulum.angle_deg") + "'");
                bool phase = false; for (const auto& h : m_HostedDraws) if (h.Name == "PhasePlot" && h.Drawn) phase = true;
                t.check(phase, "PhasePlot hosted panel not drawn in Play");
                return true;
            });
            addShot("09-play-lab", nullptr, "Console");
            addShot("10-databus-panel-play", nullptr, "DataBus");
            addScreenClick("SettingsButton", "settings_clicked");
            add("on Settings", [&]
            {
                t.check(m_PlayFlow.CurrentState() == "Settings", "flow state '" + m_PlayFlow.CurrentState() + "' != Settings");
                waitFrames(2); return true;
            });
            addShot("11-play-settings", nullptr);
            addScreenClick("BackButton", "back_clicked");
            add("damp it to rest -> Stopped overlay (the when guard)", [&]
            {
                t.check(m_PlayFlow.CurrentState() == "Lab", "flow state '" + m_PlayFlow.CurrentState() + "' != Lab after Back");
                m_PlayBus.Set("settings.damping", 1.0);   // what the Damping slider at its right end writes
                return true;
            });
            add("wait: energy < 0.01 pushes Stopped", [&]
            {
                if (m_PlayFlow.CurrentState() == "Stopped") return true;
                if (t.secondsInStep() > 60.0) { t.fail("Stopped overlay not pushed within 60 s (energy %.5f, state %s)", m_PlayBus.GetNumber("pendulum.energy"), m_PlayFlow.CurrentState().c_str()); return true; }
                return false;
            });
            add("Stopped checks", [&]
            {
                t.check(m_PlayFlow.StackDepth() == 2, "flow stack depth " + std::to_string(m_PlayFlow.StackDepth()) + " != 2 (overlay over Lab)");
                char b[96]; std::snprintf(b, sizeof(b), "energy at push %.5f", m_PlayBus.GetNumber("pendulum.energy")); t.facts["stopped"] = b;
                waitFrames(4); return true;
            });
            addShot("12-play-stopped-overlay", nullptr);
            add("damping back to 0 (the slider)", [&] { m_PlayBus.Set("settings.damping", 0.0); return true; });
            addScreenClick("ResumeButton", "resume_clicked");
            add("after Resume", [&]
            {
                t.check(m_PlayFlow.CurrentState() == "Lab", "flow state '" + m_PlayFlow.CurrentState() + "' != Lab after Resume");
                t.check(m_PlayFlow.StackDepth() == 1, "flow stack not popped");
                t.check(m_PlayBus.GetNumber("pendulum.energy") > 0.01, "energy did not jump back above the threshold after the reset");
                return true;
            });

            // ---- 10. Live loop while playing ----
            add("live loop: edit PendulumService.cpp while playing", [&]
            {
                t.liveState = m_PlayFlow.CurrentState();
                const fs::path cpp = fs::path(t.projectDir) / "src" / "services" / "PendulumService.cpp";
                std::string s = ReadAll(cpp);
                t.check(ReplaceOnce(s, "// PendulumService.cpp — see PendulumService.h.", "// PendulumService.cpp — see PendulumService.h. (guide: live-loop edit)"), "PendulumService.cpp header comment not found");
                WriteAll(cpp, s);
                t.buildsSeen = m_Live.Builds;
                return true;
            });
            add("wait: chip Building…", [&]
            {
                ImVec4 col; const char* chip = LiveChipText(col);
                if (chip && std::string(chip) == "Building…") { t.check(!IsPlaying(), "still playing while building"); return true; }
                if (t.secondsInStep() > 20.0) { t.fail("live loop did not start building within 20 s"); return true; }
                return false;
            });
            addShot("13-live-chip-building", nullptr);
            add("wait: rebuilt + resumed", [&]
            {
                if (m_Builder.IsBuilding() || m_Live.Reloading) return false;
                if (t.secondsInStep() < 1.0) return false;
                return true;
            });
            add("live loop verdict", [&]
            {
                ImVec4 col; const char* chip = LiveChipText(col);
                t.check(IsPlaying(), "not back in Play after the live rebuild");
                t.check(m_PlayFlow.CurrentState() == t.liveState, "resumed on '" + m_PlayFlow.CurrentState() + "', expected '" + t.liveState + "'");
                t.check(chip && std::string(chip) == "Live", "chip '" + std::string(chip ? chip : "(none)") + "' != Live");
                t.check(m_PlayBus.Has("pendulum.angle_deg"), "bus lost pendulum.angle_deg across the rebuild");
                waitFrames(4); return true;
            });
            addShot("14-live-chip-live", nullptr);
            add("live loop: break the build", [&]
            {
                const fs::path cpp = fs::path(t.projectDir) / "src" / "services" / "PendulumService.cpp";
                std::string s = ReadAll(cpp);
                s += "\nthis line does not compile\n";
                WriteAll(cpp, s);
                return true;
            });
            add("wait: Build failed", [&]
            {
                ImVec4 col; const char* chip = LiveChipText(col);
                if (chip && std::string(chip) == "Build failed") return true;
                if (t.secondsInStep() > 120.0) { t.fail("chip never showed 'Build failed'"); return true; }
                return false;
            });
            add("build failed checks", [&] { t.check(!IsPlaying(), "Play resumed after a failed build"); waitFrames(4); return true; });
            addShot("15-live-chip-build-failed", nullptr);
            add("live loop: fix the build", [&]
            {
                const fs::path cpp = fs::path(t.projectDir) / "src" / "services" / "PendulumService.cpp";
                std::string s = ReadAll(cpp);
                t.check(ReplaceOnce(s, "\nthis line does not compile\n", ""), "broken line not found");
                WriteAll(cpp, s);
                return true;
            });
            add("wait: fixed + resumed", [&]
            {
                if (t.secondsInStep() < 2.0) return false;
                if (m_Builder.IsBuilding() || m_Live.Debounce >= 0.0f || m_Live.Reloading) return false;
                waitFrames(10); return true;
            });
            add("fixed verdict", [&]
            {
                t.check(IsPlaying() && m_PlayFlow.CurrentState() == t.liveState, "not resumed on '" + t.liveState + "' after the fix (state '" + m_PlayFlow.CurrentState() + "', playing " + (IsPlaying() ? "yes" : "no") + ")");
                char b[64]; std::snprintf(b, sizeof(b), "builds %d resumes %d failures %d", m_Live.Builds, m_Live.Resumes, m_Live.Failures); t.facts["live"] = b;
                return true;
            });
            add("Escape -> Home, Stop", [&]
            {
                m_PlayFlow.FeedSignal("key:Escape");
                waitFrames(10); return true;
            });
            add("Escape verdict + Stop", [&]
            {
                t.check(m_PlayFlow.CurrentState() == "Home", "key:Escape did not return to Home (state '" + m_PlayFlow.CurrentState() + "')");
                StopScene();
                t.check(!IsPlaying(), "StopScene did not stop");
                waitFrames(4); return true;
            });

            // ---- 11. Export: File ▸ Package… ----
            addLocate("File menu", [&] { return ImHashStr("File", 0, ImHashStr("##MenuBar", 0, windowId("Starforge"))); }, true);
            addShot("16-file-package-menu", [&] { return ImHashStr("Package...", 0, windowId("File###Menu_00")); });
            addLocate("Package... item", [&] { return ImHashStr("Package...", 0, windowId("File###Menu_00")); }, true);
            add("package popup open", [&] { waitFrames(6); return true; });
            addShot("17-package-project-dialog", [&] { return ImHashStr("Package", 0, windowId("Package Project")); });
            add("Package (Build Release first)", [&]
            {
                if (t.skipPackage) { t.note("COSMIC_GUIDE_SKIP_PACKAGE set: not packaging"); return true; }
                m_PkgOpt.ReleaseBuild = true; m_PkgOpt.MakeZip = false; m_PkgOpt.MakeInstaller = false;
                m_LastDistDir.clear();
                PackageProject();
                t.check(m_Builder.IsBuilding(), "Package did not start the Release build");
                waitFrames(4); return true;
            });
            add("wait: package done", [&]
            {
                if (t.skipPackage) return true;
                if (m_Builder.IsBuilding()) return false;
                if (m_LastDistDir.empty()) { if (t.secondsInStep() > 5.0) { t.fail("package finished without a dist dir (build status %d)", (int)m_Builder.GetStatus()); return true; } return false; }
                waitFrames(6); return true;
            });
            add("package verdict", [&]
            {
                if (t.skipPackage) return true;
                t.facts["dist"] = m_LastDistDir;
                std::error_code ec;
                const fs::path d = m_LastDistDir;
                const char* must[] = { "PendulumLab2.exe", "PendulumLab2.dll", "Cosmic.dll", "boot.cfg", "assets/projects/PendulumLab2/project.cproj",
                                       "assets/projects/PendulumLab2/flows/Main.cflow", "assets/projects/PendulumLab2/scenes/Lab.cscene", "user/README.txt" };
                for (const char* m : must) t.check(fs::exists(d / m, ec), std::string("staged package lacks ") + m);
                t.check(!fs::exists(d / "assets" / "projects" / "PendulumLab2" / "src", ec), "staged package carries src/");
                size_t files = 0; for (auto it = fs::recursive_directory_iterator(d, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) if (it->is_regular_file(ec)) ++files;
                t.facts["dist_files"] = std::to_string(files);
                t.note("package: %s (%zu files)", m_LastDistDir.c_str(), files);
                waitFrames(4); return true;
            });
            addShot("18-package-done", nullptr);
            add("close the dialog", [&] { pressKey(ImGuiKey_Escape); waitFrames(4); return true; });
            add("finish", [&] { return true; });
        }

        // ---- run the plan ------------------------------------------------------
        if (t.wait > 0) { --t.wait; return; }
        if (t.step < (int)t.plan.size())
        {
            const bool advance = t.plan[t.step]();
            if (advance)
            {
                t.note("step %d/%zu done: %s (%.1f s)", t.step + 1, t.plan.size(), t.planNames[t.step].c_str(), t.secondsInStep());
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            else if (t.secondsInStep() > t.deadlineSec)
            {
                t.fail("DEADLINE: step '%s' exceeded %.0f s", t.planNames[t.step].c_str(), t.deadlineSec);
                ++t.step; t.stepStart = std::chrono::steady_clock::now();
            }
            if (t.step < (int)t.plan.size()) return;
        }

        // ---- verdict + JSON ------------------------------------------------------
        t.finished = true;
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
        const bool pass = t.failures == 0;
#if defined(NDEBUG)
        const char* cfg = "Release";
#else
        const char* cfg = "Debug";
#endif
        std::ofstream f(t.resultPath, std::ios::trunc);
        if (f)
        {
            f << "{\n  \"case\": \"GUIDE PendulumLab walkthrough (from scratch in the editor + export)\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"project\": \"" << Esc(t.projectDir) << "\",\n  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n";
            f << "  \"facts\": {";
            bool first = true; for (const auto& [k, v] : t.facts) { f << (first ? " " : ", ") << "\"" << k << "\": \"" << Esc(v) << "\""; first = false; }
            f << " },\n  \"shots\": [";
            for (size_t i = 0; i < t.shotsTaken.size(); ++i) f << (i ? ", " : "") << "\"" << t.shotsTaken[i] << "\"";
            f << "],\n  \"shot_rects\": {";
            first = true; for (const auto& [k, r] : t.shotRects) { f << (first ? " " : ", ") << "\"" << k << "\": [" << (int)r.Min.x << ", " << (int)r.Min.y << ", " << (int)r.Max.x << ", " << (int)r.Max.y << "]"; first = false; }
            f << " },\n  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"checks_failed\": [\n";
            for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
            f << "  ],\n  \"log\": [\n";
            for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        {
            std::ofstream c(fs::path(t.resultPath).parent_path() / "guide-editor-console.txt", std::ios::trunc);
            for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
        }
        std::printf("GUIDE_SELFTEST_RESULT=%s config=%s failedChecks=%d seconds=%.1f\n", pass ? "PASS" : "FAIL", cfg, t.failures, total);
        std::fflush(stdout);
        if (pass) Cosmic::Application::Get().Close();
        else      std::quick_exit(1);
    }
}
