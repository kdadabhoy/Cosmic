// Y02SelfTest.cpp — see Y02SelfTest.h.

#include "Y02SelfTest.h"

#include "scene/FlowMachine.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "utils/FileSystem.h"
#include "utils/ImageIO.h"
#include "core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

const char* Y02SelfTestService::kResultEnv = "COSMIC_Y02_SELFTEST";
const char* Y02SelfTestService::kOutputEnv = "COSMIC_Y02_OUTPUT";

namespace
{
    std::string Env(const char* name)
    {
        char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, name);
        std::string s = v ? v : ""; if (v) free(v); return s;
    }
    std::string Esc(const std::string& s)
    {
        std::string o;
        for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; }
        return o;
    }
}

struct Y02SelfTestService::Impl
{
    enum Phase { Boot, ToLab, WaitLab, Sample, ToSettings, WaitSettings, BackToLab, WaitLab2, Verify, Escape, WaitHome, Finish, Done };
    Phase phase = Boot;
    int frames = 0, phaseFrames = 0;
    std::string resultPath, outputDir;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point phaseStart = std::chrono::steady_clock::now();

    // Measurements.
    double angleMin = 1e300, angleMax = -1e300, busNowPrev = -1.0; int busClockViolations = 0, sampleFrames = 0;
    double panelDraws = 0.0; int plotPixels = 0, plotRoiPixels = 0; std::string roiPng;
    float  plotRect[4]{ 0, 0, 0, 0 }; uint32_t fbW = 0, fbH = 0;
    std::vector<std::string> trace;   // state after every transition we requested

    int failures = 0;
    std::vector<std::string> log, checks;
    void note(const char* fmt, ...)
    {
        char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
        log.emplace_back(buf); std::printf("[Y02] %s\n", buf); std::fflush(stdout);
    }
    void fail(const char* fmt, ...)
    {
        char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
        ++failures; checks.emplace_back(buf);
        log.emplace_back(std::string("FAIL ") + buf); std::printf("[Y02] FAIL %s\n", buf); std::fflush(stdout);
    }
    void go(Phase p) { phase = p; phaseFrames = 0; phaseStart = std::chrono::steady_clock::now(); }
};

void Y02SelfTestService::OnAttach(Cosmic::AppContext& ctx)
{
    (void)ctx;
    const std::string rp = Env(kResultEnv);
    if (rp.empty()) return;
    m_Impl = new Impl();
    auto& t = *m_Impl;
    t.resultPath = rp;
    t.outputDir = Env(kOutputEnv);
    if (t.outputDir.empty()) t.outputDir = fs::path(rp).parent_path().generic_string();
    std::error_code ec; fs::create_directories(t.outputDir, ec);
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    Cosmic::Application::Get().GetWindow().SetVSync(false);
    t.note("armed: result=%s output=%s", rp.c_str(), t.outputDir.c_str());
}

void Y02SelfTestService::OnDetach()
{
    delete m_Impl; m_Impl = nullptr;
}

void Y02SelfTestService::OnUpdate(float ts)
{
    (void)ts;
    if (!m_Impl) return;
    auto& t = *m_Impl;
    using P = Impl::Phase;
    ++t.frames; ++t.phaseFrames;
    const double phaseSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.phaseStart).count();
    if (t.phase != P::Done && t.phase != P::Finish && phaseSec > 60.0) { t.fail("phase %d exceeded 60 s", (int)t.phase); t.go(P::Finish); }

    Cosmic::FlowMachine* flow = Context().Flow;
    const std::string state = flow ? flow->CurrentState() : std::string();
    auto waitFor = [&](const char* want, P next)
    {
        if (state == want) { t.trace.push_back(state); t.note("state -> %s after %d frames", want, t.phaseFrames); t.go(next); }
        else if (t.phaseFrames > 300) { t.fail("flow did not reach %s (at '%s')", want, state.c_str()); t.go(P::Finish); }
    };

    switch (t.phase)
    {
    case P::Boot:
        if (t.phaseFrames < 3) return;
        if (!flow || !flow->IsRunning()) { t.fail("no running startup flow"); t.go(P::Finish); return; }
        if (state != "Home") t.fail("start state '%s' != Home", state.c_str());
        t.trace.push_back(state);
        t.go(P::ToLab);
        return;
    case P::ToLab:
        flow->FeedSignal("start_clicked");
        t.go(P::WaitLab);
        return;
    case P::WaitLab:
        waitFor("Lab", P::Sample);
        return;
    case P::Sample:
    {
        const double a = Bus().GetNumber("pendulum.angle_deg", 0.0);
        t.angleMin = std::min(t.angleMin, a); t.angleMax = std::max(t.angleMax, a);
        const double now = Bus().Now();
        if (t.busNowPrev >= 0.0 && now < t.busNowPrev) ++t.busClockViolations;
        t.busNowPrev = now; ++t.sampleFrames;
        if (t.phaseFrames >= 90 && phaseSec >= 1.5)
        {
            if (!Bus().GetBool("pendulum.running", false)) t.fail("pendulum.running is false on the Lab screen");
            if (t.angleMax - t.angleMin <= 0.05) t.fail("pendulum.angle_deg did not change (min %.4f max %.4f)", t.angleMin, t.angleMax);
            if (t.busClockViolations) t.fail("bus clock moved backwards %d times", t.busClockViolations);
            if (Bus().Producer("pendulum.angle_deg") != "PendulumService") t.fail("pendulum.angle_deg producer is '%s'", Bus().Producer("pendulum.angle_deg").c_str());
            t.note("sampled %d frames: angle %.3f..%.3f deg, running=%d, energy %.4f, period_est %.4f",
                   t.sampleFrames, t.angleMin, t.angleMax, (int)Bus().GetBool("pendulum.running"),
                   Bus().GetNumber("pendulum.energy"), Bus().GetNumber("pendulum.period_est"));
            t.go(P::ToSettings);
        }
        return;
    }
    case P::ToSettings:
        flow->FeedSignal("settings_clicked");
        t.go(P::WaitSettings);
        return;
    case P::WaitSettings:
        waitFor("Settings", P::BackToLab);
        return;
    case P::BackToLab:
        flow->FeedSignal("back_clicked");
        t.go(P::WaitLab2);
        return;
    case P::WaitLab2:
        waitFor("Lab", P::Verify);
        return;
    case P::Verify:
    {
        if (t.phaseFrames < 60 || phaseSec < 1.0) return;
        // Hosted panel: the service counts its PhasePlot draws and publishes them.
        t.panelDraws = Bus().GetNumber("pendulum.phaseplot_draws", 0.0);
        if (!(t.panelDraws > 0.0)) t.fail("PhasePlot hosted panel was never drawn (pendulum.phaseplot_draws = %.0f)", t.panelDraws);
        // Plot ROI: resolve the Lab canvas' "Plot" element over the framebuffer and look for
        // the UiPlot line colours (LineColor / LineColor2 of scenes/Lab.cscene) inside it.
        auto fb = Cosmic::Application::Get().GetFrameBuffer();
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        Cosmic::Scene* scene = Context().ActiveScene;
        // FrameBuffer::ReadPixels reads the CURRENTLY BOUND FBO; bind the viewport target first (the
        // service ticks between frames, when the default framebuffer is bound). AP-Q1 fix: without the
        // Bind the readback returned the window surface, never the scene (AP-04's smoke ROI = flat grey).
        bool readable = false;
        if (fb) { fb->Bind(); readable = fb->ReadPixels(0, rgba, w, h); fb->Unbind(); }
        if (!readable || w == 0 || h == 0) t.fail("viewport framebuffer not readable");
        else if (!scene) t.fail("no active scene bound to the services");
        else
        {
            t.fbW = w; t.fbH = h;
            const Cosmic::UiRect viewport{ { 0.0f, 0.0f }, { (float)w, (float)h } };
            // The "Plot" element's canvas rect: from UiSystem's own layout when the widget is
            // a collected element (AP-02), else resolved here from its RectTransform under the
            // full-viewport canvas with the documented anchor formula (docs/guide/game-ui.md).
            std::vector<Cosmic::UiElement> elems;
            Cosmic::UiSystem::CollectElements(*scene, viewport, elems);
            bool found = false;
            Cosmic::UiRect plotRect{};
            auto& reg = scene->GetRegistry();
            for (auto handle : reg.view<Cosmic::TagComponent>())
            {
                if (reg.get<Cosmic::TagComponent>(handle).Tag != "Plot" || !reg.all_of<Cosmic::RectTransformComponent>(handle)) continue;
                found = true;
                bool collected = false;
                for (const auto& el : elems)
                    if ((entt::entity)el.Handle == handle) { plotRect = el.Rect; collected = true; break; }
                if (!collected)
                {
                    float scale = 1.0f;
                    for (auto c : reg.view<Cosmic::CanvasComponent>()) { scale = Cosmic::UiSystem::CanvasScale(reg.get<Cosmic::CanvasComponent>(c), viewport); break; }
                    const auto& rt = reg.get<Cosmic::RectTransformComponent>(handle);
                    plotRect.Min = viewport.Min + viewport.Size() * rt.AnchorMin + rt.OffsetMin * scale;
                    plotRect.Max = viewport.Min + viewport.Size() * rt.AnchorMax + rt.OffsetMax * scale;
                    t.note("Plot element not collected by UiSystem (pre-AP-02); rect resolved from its RectTransform");
                }
                break;
            }
            if (found)
            {
                t.plotRect[0] = plotRect.Min.x; t.plotRect[1] = plotRect.Min.y; t.plotRect[2] = plotRect.Max.x; t.plotRect[3] = plotRect.Max.y;
                const int x0 = std::max(0, (int)std::floor(plotRect.Min.x)), y0 = std::max(0, (int)std::floor(plotRect.Min.y));
                const int x1 = std::min((int)w, (int)std::ceil(plotRect.Max.x)), y1 = std::min((int)h, (int)std::ceil(plotRect.Max.y));
                auto closeTo = [](int r, int g, int b, int tr, int tg, int tb) { return std::abs(r - tr) <= 48 && std::abs(g - tg) <= 48 && std::abs(b - tb) <= 48; };
                std::vector<uint8_t> crop; crop.reserve((size_t)std::max(0, x1 - x0) * (size_t)std::max(0, y1 - y0) * 4);
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                    {
                        const uint8_t* p = &rgba[((size_t)y * w + (size_t)x) * 4];
                        crop.insert(crop.end(), p, p + 4);
                        ++t.plotRoiPixels;
                        if (closeTo(p[0], p[1], p[2], 77, 204, 255) || closeTo(p[0], p[1], p[2], 255, 153, 51)) ++t.plotPixels;   // LineColor / LineColor2
                    }
                t.roiPng = (fs::path(t.outputDir) / "y02-plot-roi.png").generic_string();
                if (x1 > x0 && y1 > y0) Cosmic::ImageIO::WritePNG(t.roiPng, x1 - x0, y1 - y0, 4, crop.data());
            }
            if (!found) t.fail("Lab scene has no 'Plot' UI element");
            else if (t.plotPixels < 30) t.fail("plot ROI (%d px) has only %d line-colour pixels", t.plotRoiPixels, t.plotPixels);
        }
        t.note("verify: panel draws %.0f, plot rect [%.0f %.0f %.0f %.0f] in %ux%u, %d/%d line-colour px, roi %s",
               t.panelDraws, t.plotRect[0], t.plotRect[1], t.plotRect[2], t.plotRect[3], t.fbW, t.fbH, t.plotPixels, t.plotRoiPixels, t.roiPng.c_str());
        t.go(P::Escape);
        return;
    }
    case P::Escape:
        flow->FeedSignal("key:Escape");   // what FlowKeyBridge feeds on the rising edge of Escape
        t.go(P::WaitHome);
        return;
    case P::WaitHome:
        waitFor("Home", P::Finish);
        return;
    case P::Finish:
    {
        const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
        const std::vector<std::string> expected{ "Home", "Lab", "Settings", "Lab", "Home" };
        const bool traceOk = t.trace == expected;
        if (!traceOk) t.fail("state trace has %zu entries (expected Home,Lab,Settings,Lab,Home)", t.trace.size());
        const bool pass = t.failures == 0;
#if defined(NDEBUG)
        const char* cfg = "Release";
#else
        const char* cfg = "Debug";
#endif
        char exe[MAX_PATH]{}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
        char cwd[MAX_PATH]{}; GetCurrentDirectoryA(MAX_PATH, cwd);
        std::ofstream f(t.resultPath, std::ios::trunc);
        if (f)
        {
            f << "{\n  \"work_order\": \"AP-04\",\n  \"case\": \"Y02 PendulumLab self-test\",\n  \"config\": \"" << cfg << "\",\n";
            f << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n  \"frames\": " << t.frames << ",\n";
            f << "  \"exe\": \"" << Esc(exe) << "\",\n  \"cwd\": \"" << Esc(cwd) << "\",\n  \"user_root\": \"" << Esc(Cosmic::FileSystem::GetUserDataRoot()) << "\",\n";
            f << "  \"project_name\": \"" << Esc(Context().ProjectName) << "\",\n  \"in_editor\": " << (Context().InEditor ? "true" : "false") << ",\n";
            f << "  \"trace\": [";
            for (size_t i = 0; i < t.trace.size(); ++i) f << (i ? ", " : "") << "\"" << t.trace[i] << "\"";
            f << "],\n";
            f << "  \"angle_deg\": { \"frames\": " << t.sampleFrames << ", \"min\": " << t.angleMin << ", \"max\": " << t.angleMax << ", \"bus_clock_violations\": " << t.busClockViolations << " },\n";
            f << "  \"pendulum\": { \"running\": " << (Bus().GetBool("pendulum.running") ? "true" : "false") << ", \"energy\": " << Bus().GetNumber("pendulum.energy")
              << ", \"period_est\": " << Bus().GetNumber("pendulum.period_est") << ", \"producer\": \"" << Esc(Bus().Producer("pendulum.angle_deg")) << "\" },\n";
            f << "  \"hosted_panel\": { \"name\": \"PhasePlot\", \"draws\": " << t.panelDraws << " },\n";
            f << "  \"plot_roi\": { \"rect\": [" << t.plotRect[0] << ", " << t.plotRect[1] << ", " << t.plotRect[2] << ", " << t.plotRect[3] << "], \"fb\": [" << t.fbW << ", " << t.fbH
              << "], \"pixels\": " << t.plotRoiPixels << ", \"line_colour_pixels\": " << t.plotPixels << ", \"png\": \"" << Esc(t.roiPng) << "\" },\n";
            f << "  \"checks_failed\": [\n";
            for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
            f << "  ],\n  \"log\": [\n";
            for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
            f << "  ]\n}\n";
        }
        std::printf("Y02_SELFTEST_RESULT=%s config=%s failedChecks=%d frames=%d seconds=%.1f\n", pass ? "PASS" : "FAIL", cfg, t.failures, t.frames, total);
        std::fflush(stdout);
        t.go(P::Done);
        if (pass) Cosmic::Application::Get().Close();
        else      std::quick_exit(1);
        return;
    }
    case P::Done:
    default:
        return;
    }
}
