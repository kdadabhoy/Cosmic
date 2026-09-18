// X01SelfTest.cpp — the in-app acceptance harness of the analysis sample (WO-10 /
// X01, 2D stability).
//
// Armed by COSMIC_X01_SELFTEST=<result.json> (and COSMIC_X01_OUTPUT=<dir> for the
// exported PNG / series metadata; defaults to the result's directory). Driven once
// per frame from AnalysisLayer::OnUpdate through the SAME transport functions the
// UI buttons call — never a fake app, never a second scheduler:
//   1. LOAD    — the fixtures are loaded (1,201 double rows == the catalog
//                equations, the 100,000 x 8 series finite with its metadata's
//                extrema where the equations say, the v1 recording written by the
//                real recorder and loaded by the real player), and the 1e11 world
//                origin subtracted in DOUBLE reproduces every local value to the
//                double ulp at 1e11 + the float ulp of the value;
//   2. PLAY    — Play(); the replay head advances monotonically under the real
//                frame clock until it passes 0.4 s;
//   3. PAUSE   — Pause(); the head stays exactly put for 10 frames;
//   4. SCRUB   — exact sample times and midpoints: the marker (float replay path),
//                the plot's selection (double series interpolated) and the double
//                equations agree within the declared bounds; every scrub is written
//                to the result for the out-of-process oracle;
//   5. EXPORT  — scrub to 4.5 s, export the viewport framebuffer as PNG; the pixel
//                at the marker's projected position carries the marker colour and a
//                far pixel the clear colour (the wrapper re-checks the PNG itself);
//   6. FINISH  — verdict -> JSON; PASS closes the app (exit 0), FAIL quick_exit(1).
#include "AnalysisSampleLayer.h"

#include "utils/FileSystem.h"
#include "utils/ImageIO.h"
#include "core/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace AnalysisSample
{
    namespace
    {
        double FloatBar(double expected) { return 1e-6 + 1e-5 * std::abs(expected); }
        double UlpF(double v) { const float f = (float)std::abs(v); return (double)(std::nextafter(f, INFINITY) - f); }
        std::string Esc(const std::string& s)
        {
            std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; } return o;
        }
        std::string Env(const char* name)
        {
            char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, name);
            std::string s = v ? v : ""; if (v) free(v); return s;
        }
    }

    struct AnalysisLayer::SelfTest
    {
        enum Phase { Boot, Load, Play, Playing, Paused, Scrub, ScrubWait, ExportSeek, ExportRequest, ExportWait, Finish, Done };
        Phase phase = Boot;
        int frames = 0, phaseFrames = 0;
        std::string resultPath, outputDir;
        std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point phaseStart = std::chrono::steady_clock::now();

        double playStart = 0.0, playEnd = 0.0, pausedAt = 0.0; int playFrames = 0, playMonotonicViolations = 0;
        std::vector<double> scrubTimes; size_t scrubIndex = 0;
        std::vector<std::string> scrubJson;
        std::string exportJson, seriesMetaPath, userExportPath;
        double localMaxErr = 0.0, markerVsPlotMax = 0.0, plotVsRefMax = 0.0;

        int failures = 0;
        std::vector<std::string> log, checks;
        void note(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[X01] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(buf);
            log.emplace_back(std::string("FAIL ") + buf); std::printf("[X01] FAIL %s\n", buf); std::fflush(stdout);
        }
        void go(Phase p) { phase = p; phaseFrames = 0; phaseStart = std::chrono::steady_clock::now(); }
    };

    void AnalysisLayer::SelfTestInit()
    {
        const std::string rp = Env("COSMIC_X01_SELFTEST");
        if (rp.empty()) return;
        m_SelfTest = new SelfTest();
        auto& t = *m_SelfTest;
        t.resultPath = rp;
        t.outputDir = Env("COSMIC_X01_OUTPUT");
        if (t.outputDir.empty()) t.outputDir = fs::path(rp).parent_path().generic_string();
        std::error_code ec; fs::create_directories(t.outputDir, ec);
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        // Exact sample times and midpoints (t = i/120; midpoints (i + 0.5)/120).
        for (int i : { 0, 1, 119, 120, 600, 611, 1199, 1200 }) t.scrubTimes.push_back(i / 120.0);
        for (double i : { 0.5, 120.5, 611.5, 1199.5 })        t.scrubTimes.push_back(i / 120.0);
        t.note("armed: result=%s output=%s", rp.c_str(), t.outputDir.c_str());
    }

    void AnalysisLayer::SelfTestShutdown() { delete m_SelfTest; m_SelfTest = nullptr; }

    void AnalysisLayer::SelfTestTick()
    {
        if (!m_SelfTest) return;
        auto& t = *m_SelfTest;
        using P = SelfTest;
        ++t.frames; ++t.phaseFrames;
        const double phaseSec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.phaseStart).count();
        if (t.phase != P::Done && phaseSec > 60.0) { t.fail("phase %d exceeded 60 s", (int)t.phase); t.go(P::Finish); }

        switch (t.phase)
        {
        case P::Boot:
            if (t.phaseFrames >= 3) t.go(P::Load);   // let the workspace lay the viewport out
            return;
        case P::Load:
        {
            if (!m_Loaded) { t.fail("fixtures not loaded: %s", m_LoadError.c_str()); t.go(P::Finish); return; }
            if (m_Rows.size() != 1201) t.fail("rows %zu != 1201", m_Rows.size());
            // Equations, units (independent of the loader's own check).
            const double g = 9.80665; double maxRel = 0.0;
            for (size_t i = 0; i < m_Rows.size(); ++i)
            {
                const double tt = (double)i / 120.0;
                maxRel = std::max(maxRel, std::abs(m_Rows[i].x - 30.0 * tt) / std::max(1.0, 30.0 * tt));
                maxRel = std::max(maxRel, std::abs(m_Rows[i].y - (50.0 * tt - 0.5 * g * tt * tt)) / std::max(1.0, std::abs(50.0 * tt - 0.5 * g * tt * tt)));
                maxRel = std::max(maxRel, std::abs(m_Rows[i].vy - (50.0 - g * tt)) / 50.0);
                if (m_Rows[i].vx != 30.0) maxRel = 1.0;
            }
            if (maxRel > 1e-12) t.fail("trajectory rows deviate from the equations (max rel %.3e)", maxRel);
            if (m_Rows.back().t != 10.0 || std::abs(m_Rows.back().x - 300.0) > 1e-9) t.fail("last row is not t=10 x=300");
            // Series metadata: extrema of the analytic channels where the equations say.
            const auto& m = m_SeriesMeta;
            if (m.samples != 100000 || m.channels != 8 || m.channel.size() != 8) t.fail("series meta shape");
            else
            {
                // (value, index modulo `period`): a periodic channel's extremum recurs every
                // period and double rounding picks one of the recurrences (the sawtooth's
                // 0.999 differs in its 16th digit between periods); the VALUE is exact.
                auto expect = [&](int ch, double minV, int minI, double maxV, int maxI, int period)
                {
                    const auto& c = m.channel[(size_t)ch];
                    if (std::abs(c.min.value - minV) > 1e-9 || c.min.index % period != minI % period) t.fail("series %s min %.9f@%d (expected %.9f@%d mod %d)", c.name.c_str(), c.min.value, c.min.index, minV, minI, period);
                    if (std::abs(c.max.value - maxV) > 1e-9 || c.max.index % period != maxI % period) t.fail("series %s max %.9f@%d (expected %.9f@%d mod %d)", c.name.c_str(), c.max.value, c.max.index, maxV, maxI, period);
                };
                expect(0, -1.0, 750, 1.0, 250, 1000);       // sin(2 pi t): max at t=0.25+k, min at t=0.75+k
                expect(1, -1.0, 500, 1.0, 0, 1000);         // cos
                expect(2, 0.0, 0, 99.999, 99999, 100000);   // ramp: unique
                expect(3, 0.0, 0, 0.999, 999, 1000);        // sawtooth
                expect(4, -1.0, 0, 1.0, 50000, 100000);     // step: first occurrences
                if (m.channel[3].discontinuities.size() != 99 || m.channel[3].discontinuities.front() != 1000) t.fail("sawtooth discontinuity markers");
                if (m.channel[4].discontinuities.size() != 1 || m.channel[4].discontinuities[0] != 50000) t.fail("step discontinuity marker");
                if (std::abs(m.channel[5].max.value - 1.0) > 1e-12 || m.channel[5].max.index != 0) t.fail("damped max");
                if (!m.channel[6].seeded || !m.channel[7].seeded) t.fail("noise channels not flagged as seeded");
                for (const auto& c : m.channel) if (!std::isfinite(c.min.value) || !std::isfinite(c.max.value)) t.fail("non-finite extremum in %s", c.name.c_str());
            }
            // Write the metadata (equations + seed + extrema) next to the result.
            t.seriesMetaPath = (fs::path(t.outputDir) / "series-large.meta.json").generic_string();
            { std::ofstream f(t.seriesMetaPath, std::ios::trunc); f << SeriesLarge::MetaJson(m); }
            // Replay path present.
            std::error_code ec;
            if (!fs::exists(fs::path(m_RecordingDir) / "scene.bin", ec)) t.fail("v1 recording missing at %s", m_RecordingDir.c_str());
            if (std::abs(Duration() - 10.0) > 1e-9) t.fail("duration %.9f", Duration());
            if (std::abs((double)m_Player.GetDuration() - 10.0) > 2e-6) t.fail("player duration %.9f", (double)m_Player.GetDuration());
            // The 1e11 origin, subtracted in double before float display — every row.
            const double ulpD = std::nextafter(kOriginX, INFINITY) - kOriginX;
            for (size_t i = 0; i < m_Rows.size(); ++i)
            {
                const double x = m_Rows[i].x, y = m_Rows[i].y;
                const float lx = m_Frame.ToLocalX(kOriginX + x), ly = m_Frame.ToLocalY(kOriginY + y);
                const double ex = std::abs((double)lx - x), ey = std::abs((double)ly - y);
                t.localMaxErr = std::max(t.localMaxErr, std::max(ex, ey));
                if (ex > ulpD / 2.0 + UlpF(x) / 2.0 + 1e-12 || ey > ulpD / 2.0 + UlpF(y) / 2.0 + 1e-12) { t.fail("1e11 local conversion off at row %zu: %.3e / %.3e", i, ex, ey); break; }
            }
            t.note("load ok: 1201 rows, series meta at %s, recording %s, 1e11 local conversion max err %.3e m (double ulp at 1e11 = %.3e)", t.seriesMetaPath.c_str(), m_RecordingDir.c_str(), t.localMaxErr, ulpD);
            t.go(P::Play);
            return;
        }
        case P::Play:
            Play();
            if (!IsPlaying()) { t.fail("Play() did not start playback"); t.go(P::Finish); return; }
            t.playStart = Position();
            t.go(P::Playing);
            return;
        case P::Playing:
        {
            const double p = Position();
            if (p < t.playEnd) ++t.playMonotonicViolations;
            t.playEnd = p; ++t.playFrames;
            if (p >= 0.4)
            {
                Pause();
                t.pausedAt = Position();
                if (IsPlaying()) t.fail("Pause() left playback running");
                t.note("played %d frames: head %.6f -> %.6f s (monotonic violations %d); paused at %.6f", t.playFrames, t.playStart, t.playEnd, t.playMonotonicViolations, t.pausedAt);
                if (t.playMonotonicViolations) t.fail("replay head moved backwards while playing");
                t.go(P::Paused);
            }
            return;
        }
        case P::Paused:
            if (Position() != t.pausedAt) t.fail("paused head moved: %.9f != %.9f", Position(), t.pausedAt);
            if (t.phaseFrames >= 10) t.go(P::Scrub);
            return;
        case P::Scrub:
            if (t.scrubIndex >= t.scrubTimes.size()) { t.go(P::ExportSeek); return; }
            Scrub(t.scrubTimes[t.scrubIndex]);
            t.go(P::ScrubWait);
            return;
        case P::ScrubWait:
        {
            // One frame later the sample reflects the scrub.
            const double want = t.scrubTimes[t.scrubIndex];
            const Sample& s = m_Cur;
            const bool midpoint = std::abs(want * 120.0 - std::round(want * 120.0)) > 1e-9;
            if (s.t != want) t.fail("scrub %.9f: sample time %.9f", want, s.t);
            // Marker (float replay) vs the plot's selection (double series, interpolated):
            // both are chords of the same rows; the float path adds float storage,
            // float interpolation and float time resolution.
            const double barX = FloatBar(s.plotX) + 2.0 * 30.0 * UlpF(want) + 2.0 * UlpF(s.plotX);
            const double barY = FloatBar(s.plotY) + 2.0 * 50.0 * UlpF(want) + 2.0 * UlpF(s.plotY);
            const double dx = std::abs((double)s.markerX - s.plotX), dy = std::abs((double)s.markerY - s.plotY);
            t.markerVsPlotMax = std::max(t.markerVsPlotMax, std::max(dx, dy));
            if (dx > barX || dy > barY) t.fail("scrub %.6f: marker vs plot %.3e / %.3e (bars %.3e / %.3e)", want, dx, dy, barX, barY);
            // Plot selection vs the equations: exact at a sample, a chord at a midpoint
            // (x is linear: exact; y is a parabola: g/8 dt^2 = 8.5e-5 m), plus the double
            // rounding of forming the 1e11 world value (half a double ulp at 1e11 = 7.6e-6 m).
            const double ulpD = std::nextafter(kOriginX, INFINITY) - kOriginX;
            const double chordY = midpoint ? 9.80665 / 8.0 * (1.0 / 120.0) * (1.0 / 120.0) : 0.0;
            const double px = std::abs(s.plotX - s.refX), py = std::abs(s.plotY - s.refY);
            t.plotVsRefMax = std::max(t.plotVsRefMax, std::max(px, py));
            if (px > ulpD + 1e-9 || py > chordY + ulpD + 1e-9) t.fail("scrub %.6f: plot vs equations %.3e / %.3e (chord bound %.3e + %.3e)", want, px, py, chordY, ulpD);
            // The displayed local value of the double world value.
            if (std::abs((double)s.localX - s.refX) > 1e-4 || std::abs((double)s.localY - s.refY) > 1e-4) t.fail("scrub %.6f: 1e11 local display off", want);
            char buf[640];
            std::snprintf(buf, sizeof(buf),
                "    { \"t\": %.17g, \"midpoint\": %s, \"ref\": { \"x\": %.17g, \"y\": %.17g, \"vx\": %.17g, \"vy\": %.17g, \"speed\": %.17g }, \"plot\": { \"x\": %.17g, \"y\": %.17g, \"speed\": %.17g }, \"marker\": { \"x\": %.9g, \"y\": %.9g, \"vx\": %.9g, \"vy\": %.9g }, \"local\": { \"x\": %.9g, \"y\": %.9g }, \"marker_px\": [%.4f, %.4f], \"fb\": [%u, %u] }",
                want, midpoint ? "true" : "false", s.refX, s.refY, s.refVx, s.refVy, s.refSpeed, s.plotX, s.plotY, s.plotSpeed,
                (double)s.markerX, (double)s.markerY, (double)s.markerVx, (double)s.markerVy, (double)s.localX, (double)s.localY, s.markerPx.x, s.markerPx.y, s.fbW, s.fbH);
            t.scrubJson.emplace_back(buf);
            ++t.scrubIndex;
            t.go(P::Scrub);
            return;
        }
        case P::ExportSeek:
            Scrub(4.5);
            t.go(P::ExportRequest);
            return;
        case P::ExportRequest:
            if (m_Cur.fbW == 0 || m_Cur.fbH == 0) { if (t.phaseFrames > 30) { t.fail("viewport framebuffer never sized"); t.go(P::Finish); } return; }
            RequestExport((fs::path(t.outputDir) / "x01-trajectory-4.5s.png").generic_string());
            t.go(P::ExportWait);
            return;
        case P::ExportWait:
        {
            if (m_ExportPending) return;
            const ExportInfo& e = m_LastExport;
            if (!e.ok) { t.fail("export failed (%s)", e.path.c_str()); t.go(P::Finish); return; }
            auto close = [](const glm::u8vec4& a, const glm::u8vec4& b, int tol) { return std::abs((int)a.r - b.r) <= tol && std::abs((int)a.g - b.g) <= tol && std::abs((int)a.b - b.b) <= tol; };
            if (!close(e.pixelAtMarker, e.markerColor, 40)) t.fail("pixel at the marker (%d,%d,%d) is not the marker colour (%d,%d,%d)", e.pixelAtMarker.r, e.pixelAtMarker.g, e.pixelAtMarker.b, e.markerColor.r, e.markerColor.g, e.markerColor.b);
            if (!close(e.pixelFar, e.clearColor, 8)) t.fail("far pixel (%d,%d,%d) is not the clear colour", e.pixelFar.r, e.pixelFar.g, e.pixelFar.b);
            // The projection: the marker pixel must equal the independent formula from
            // the reported window (the wrapper re-derives it from the double reference).
            const Sample& s = m_Cur;
            const double px = (s.markerX - e.xmin) / (e.xmax - e.xmin) * e.width, py = (1.0 - (s.markerY - e.ymin) / (e.ymax - e.ymin)) * e.height;
            if (std::abs(px - e.markerPx.x) > 1e-2 || std::abs(py - e.markerPx.y) > 1e-2) t.fail("marker pixel %.3f/%.3f != projection %.3f/%.3f", e.markerPx.x, e.markerPx.y, px, py);
            // A second copy through the writable user root (the packaged app's data path).
            t.userExportPath = (fs::path(Cosmic::FileSystem::Resolve("user://exports")) / "x01-trajectory-4.5s.png").generic_string();
            {
                std::error_code ec; fs::create_directories(fs::path(t.userExportPath).parent_path(), ec);
                fs::copy_file(e.path, t.userExportPath, fs::copy_options::overwrite_existing, ec);
                if (ec) t.fail("cannot write to user://exports (%s): %s", t.userExportPath.c_str(), ec.message().c_str());
            }
            char buf[900];
            std::snprintf(buf, sizeof(buf),
                "{ \"ok\": true, \"path\": \"%s\", \"user_copy\": \"%s\", \"width\": %u, \"height\": %u, \"t\": %.17g, \"marker_px\": [%.4f, %.4f], \"marker_radius_px\": %.1f, \"marker_local\": [%.9g, %.9g], \"ref_local\": [%.17g, %.17g], \"window\": { \"xmin\": %.17g, \"xmax\": %.17g, \"ymin\": %.17g, \"ymax\": %.17g }, \"marker_color\": [%d, %d, %d], \"clear_color\": [%d, %d, %d], \"pixel_at_marker\": [%d, %d, %d], \"pixel_far\": [%d, %d, %d] }",
                Esc(e.path).c_str(), Esc(t.userExportPath).c_str(), e.width, e.height, s.t, e.markerPx.x, e.markerPx.y, e.markerRadiusPx, (double)s.markerX, (double)s.markerY, s.refX, s.refY,
                e.xmin, e.xmax, e.ymin, e.ymax, e.markerColor.r, e.markerColor.g, e.markerColor.b, e.clearColor.r, e.clearColor.g, e.clearColor.b,
                e.pixelAtMarker.r, e.pixelAtMarker.g, e.pixelAtMarker.b, e.pixelFar.r, e.pixelFar.g, e.pixelFar.b);
            t.exportJson = buf;
            t.note("export ok: %s %ux%u, marker px (%.2f, %.2f) colour (%d,%d,%d), far (%d,%d,%d)", e.path.c_str(), e.width, e.height, e.markerPx.x, e.markerPx.y, e.pixelAtMarker.r, e.pixelAtMarker.g, e.pixelAtMarker.b, e.pixelFar.r, e.pixelFar.g, e.pixelFar.b);
            t.go(P::Finish);
            return;
        }
        case P::Finish:
        {
            const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t.start).count();
            const bool pass = t.failures == 0 && t.scrubIndex == t.scrubTimes.size() && !t.exportJson.empty();
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
                f << "{\n  \"work_order\": \"WO-10\",\n  \"case\": \"X01 analysis sample\",\n  \"config\": \"" << cfg << "\",\n";
                f << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n  \"frames\": " << t.frames << ",\n";
                f << "  \"exe\": \"" << Esc(exe) << "\",\n  \"cwd\": \"" << Esc(cwd) << "\",\n  \"user_root\": \"" << Esc(Cosmic::FileSystem::GetUserDataRoot()) << "\",\n";
                f << "  \"trajectory_csv\": \"" << Esc(Cosmic::FileSystem::Resolve("project://data/trajectory.csv")) << "\",\n  \"rows\": " << m_Rows.size() << ",\n";
                f << "  \"recording_dir\": \"" << Esc(m_RecordingDir) << "\",\n  \"series_meta\": \"" << Esc(t.seriesMetaPath) << "\",\n";
                f << "  \"origin_offset_m\": " << kOriginX << ",\n  \"local_conversion_max_err_m\": " << t.localMaxErr << ",\n";
                f << "  \"play\": { \"frames\": " << t.playFrames << ", \"start\": " << t.playStart << ", \"end\": " << t.playEnd << ", \"paused_at\": " << t.pausedAt << ", \"monotonic_violations\": " << t.playMonotonicViolations << " },\n";
                f << "  \"marker_vs_plot_max_m\": " << t.markerVsPlotMax << ",\n  \"plot_vs_equations_max_m\": " << t.plotVsRefMax << ",\n";
                f << "  \"scrubs\": [\n";
                for (size_t i = 0; i < t.scrubJson.size(); ++i) f << t.scrubJson[i] << (i + 1 < t.scrubJson.size() ? ",\n" : "\n");
                f << "  ],\n  \"export\": " << (t.exportJson.empty() ? "null" : t.exportJson) << ",\n";
                f << "  \"checks_failed\": [\n";
                for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
                f << "  ],\n  \"log\": [\n";
                for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
                f << "  ]\n}\n";
            }
            std::printf("X01_SELFTEST_RESULT=%s config=%s failedChecks=%d scrubs=%zu seconds=%.1f\n", pass ? "PASS" : "FAIL", cfg, t.failures, t.scrubJson.size(), total);
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
}
