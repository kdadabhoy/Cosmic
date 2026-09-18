#pragma once
// WO10ClockHarness.h — WO-10 (2D stability): drives the PRODUCTION Application
// frame scheduler over an injected clock and compares what the layers observed
// against an integer-tick / double reference.
//
// One rung = one fresh child process (the WO-04 runner launches CosmicTests.exe
// with COSMIC_WO10_RUNG=<name>): a real Application boots WO10ClockFixture.dll
// through the real WorkspaceLayer, a driver OVERLAY (this file) is pushed on the
// Application itself, and the app runs its normal Run() loop — PollEvents,
// RenderSingleFrame (the accumulator, the 0.25 s clamp, pause, TimeScale, the
// layer dispatch, ImGui, SwapBuffers), the Safe Zone — while the only thing that
// is fake is the value RenderSingleFrame samples from the clock. The driver
// snapshots both observers at the end of every frame (it is the last layer to
// run), applies the rung's scripted actions (pause / resume / SetTimeScale /
// SetFixedTimestepHz) for the NEXT frame, and closes the window when the
// schedule is exhausted.
//
// The reference (Reference()) is NOT the engine: it is the declared policy —
// fixed budget += paused ? 0 : min(dt, 0.25) * scale, drained in whole steps of
// 1/Hz in double; variable dt = paused ? 0 : dt * scale (* local for the plugin);
// uptime += dt always — evaluated in double over the same schedule. The
// assertions are exact for tick counts and use the catalog's default float bar
// (1e-6 + 1e-5*|expected|) per frame; the stored-float accumulators (local time,
// uptime) are compared against the worst-case float accumulation bound and their
// observed error is reported, never hidden.
#include <doctest.h>

#include "FakeFrameClock.h"
#include "WO10ClockReport.h"
#include "WO05NativeWindow.h"

#include "core/Application.h"
#include "core/Layer.h"
#include "math/Random.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Wo10
{
    // ------------------------------------------------------------------------
    // Rung specification
    // ------------------------------------------------------------------------
    struct Action
    {
        enum Kind { Pause, Resume, SetScale, SetHz };
        size_t atFrame;   // applied at the END of this frame (affects frame+1 onward)
        Kind   kind;
        float  value;
    };

    struct RungSpec
    {
        std::string         name;
        double              origin = 0.0;          // clock origin (seconds) — N02 injects 0 / 2 h / 24 h
        std::vector<double> deltas;                // frame k (1-based) advances the clock by deltas[k-1]
        float               globalScale = 1.0f;    // Application::SetTimeScale before Run()
        float               localScale  = 1.0f;    // the plugin's Layer::SetTimeScale (via env)
        float               fixedHz     = 60.0f;   // Application::SetFixedTimestepHz before Run()
        std::vector<Action> actions;
        double              realTimeBudgetSec = 300.0;   // runaway guard (a failure, never a pass)
        bool                recordEvery = true;    // false: aggregates only (long runs)
    };

    // Reference state, evaluated in double over the declared policy.
    struct Reference
    {
        long long           ticks = 0;
        double              fixedTime = 0.0;       // whole steps delivered (seconds of sim time)
        double              pluginLocal = 0.0;     // sum of paused ? 0 : dt*scale*local
        double              uptime = 0.0;          // sum of every dt
        std::vector<double> directUpdateDt;        // per frame: paused ? 0 : dt*scale
        std::vector<double> pluginUpdateDt;        // per frame: ... * local
        std::vector<bool>   paused;
        std::vector<double> scale;                 // effective (declared-policy) scale per frame
        std::vector<double> hz;
        std::vector<int>    ticksPerFrame;
        int                 maxTicksInFrame = 0;
    };

    inline Reference ComputeReference(const RungSpec& s)
    {
        Reference r;
        const size_t n = s.deltas.size();
        r.directUpdateDt.resize(n); r.pluginUpdateDt.resize(n); r.paused.resize(n);
        r.scale.resize(n); r.hz.resize(n); r.ticksPerFrame.resize(n);
        bool paused = false;
        double scale = s.globalScale, hz = s.fixedHz, acc = 0.0;
        // Declared policy for the setters (after WO-10): a non-finite or negative
        // scale is REJECTED (previous kept); Hz clamps to [1, 1000] (±inf included),
        // NaN rejected.
        auto applyScale = [&](float v) { if (std::isfinite(v) && v >= 0.0f) scale = v; };
        auto applyHz    = [&](float v) { if (!std::isnan(v)) hz = std::clamp((double)v, 1.0, 1000.0); };
        size_t ai = 0;
        std::vector<Action> acts = s.actions;
        std::sort(acts.begin(), acts.end(), [](const Action& a, const Action& b) { return a.atFrame < b.atFrame; });
        for (size_t k = 1; k <= n; ++k)
        {
            const double dt = s.deltas[k - 1];
            r.paused[k - 1] = paused; r.scale[k - 1] = scale; r.hz[k - 1] = hz;
            r.uptime += dt;
            int ticks = 0;
            if (!paused)
            {
                const double step = 1.0 / hz;
                acc += std::min(dt, 0.25) * scale;
                while (acc >= step - 1e-12) { ++ticks; acc -= step; }
                r.pluginLocal += dt * scale * (double)s.localScale;
                r.directUpdateDt[k - 1] = dt * scale;
                r.pluginUpdateDt[k - 1] = dt * scale * (double)s.localScale;
            }
            r.ticks += ticks; r.fixedTime += ticks / hz;
            r.ticksPerFrame[k - 1] = ticks;
            r.maxTicksInFrame = std::max(r.maxTicksInFrame, ticks);
            while (ai < acts.size() && acts[ai].atFrame == k)
            {
                switch (acts[ai].kind)
                {
                case Action::Pause:    paused = true;  break;
                case Action::Resume:   paused = false; break;
                case Action::SetScale: applyScale(acts[ai].value); break;
                case Action::SetHz:    applyHz(acts[ai].value); break;
                }
                ++ai;
            }
        }
        return r;
    }

    // ------------------------------------------------------------------------
    // Schedule builders
    // ------------------------------------------------------------------------
    // N frames of `period` plus one tail frame of `tail` (default 1/480 s = one
    // eighth of a 60-Hz step: keeps every declared total 1/8 step clear of a tick
    // boundary at speeds 0.25 / 1 / 4, so float rounding can never move the count).
    inline std::vector<double> Regular(size_t frames, double period, double tail = 1.0 / 480.0)
    {
        std::vector<double> d(frames, period);
        if (tail > 0.0) d.push_back(tail);
        return d;
    }

    // Irregular frame lengths in [minDt, maxDt] from a seeded PCG32, trimmed so
    // they sum to exactly `total` (the last frame absorbs the remainder), + tail.
    inline std::vector<double> Irregular(double total, double minDt, double maxDt, uint64_t seed, double tail = 1.0 / 480.0)
    {
        Cosmic::Random rng(seed, 0xB0B0);
        std::vector<double> d;
        double sum = 0.0;
        for (;;)
        {
            const double x = minDt + (maxDt - minDt) * (double)rng.NextFloat();
            if (sum + x >= total) { d.push_back(total - sum); sum = total; break; }
            d.push_back(x); sum += x;
        }
        if (tail > 0.0) d.push_back(tail);
        return d;
    }

    // ------------------------------------------------------------------------
    // The driver overlay
    // ------------------------------------------------------------------------
    class Driver final : public Cosmic::Layer
    {
        WO10ClockReport& rep;
        FakeFrameClock&  clock;
        const RungSpec&  spec;
        std::vector<Action> acts;
        size_t ai = 0;
        bool   started = false;
        int    directTicksNow = 0;
        float  directSumNow = 0.0f, directMinNow = 0.0f, directMaxNow = 0.0f;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();

        void Close() { Cosmic::WindowCloseEvent e; Cosmic::Application::Get().OnEvent(e); }
        void ResetNow()
        {
            directTicksNow = 0; directSumNow = directMinNow = directMaxNow = 0.0f;
            rep.pluginTicksNow.store(0); rep.pluginFixedDtSumNow.store(0.0f);
            rep.pluginFixedDtMinNow.store(0.0f); rep.pluginFixedDtMaxNow.store(0.0f);
            rep.pluginUpdateDtNow.store(0.0f); rep.pluginUpdatesNow.store(0);
        }
    public:
        Driver(WO10ClockReport& r, FakeFrameClock& c, const RungSpec& s)
            : Cosmic::Layer("WO10 driver"), rep(r), clock(c), spec(s), acts(s.actions)
        {
            std::sort(acts.begin(), acts.end(), [](const Action& a, const Action& b) { return a.atFrame < b.atFrame; });
        }

        void OnFixedUpdate(float dt) override
        {
            if (!std::isfinite(dt)) ++rep.directNonFinite;
            ++directTicksNow; directSumNow += dt;
            if (directTicksNow == 1) { directMinNow = directMaxNow = dt; }
            else { directMinNow = std::min(directMinNow, dt); directMaxNow = std::max(directMaxNow, dt); }
        }

        void OnUpdate(float dt) override
        {
            auto& app = Cosmic::Application::Get();
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed > spec.realTimeBudgetSec)
            {
                rep.runawayClosed.store(1); ++rep.failures;
                std::printf("[WO10] runaway: %.1f s real time, closing\n", elapsed);
                Close();
                return;
            }
            if (!started)
            {
                // Hold until the plugin is attached (dt = 0 frames never count).
                ResetNow();
                if (rep.attached.load() == 1 && rep.detached.load() == 0) { started = true; clock.Go(); }
                return;
            }
            const size_t k = clock.Cursor();           // 1-based schedule frame
            if (k == 0) { ResetNow(); return; }
            if (!std::isfinite(dt) || !std::isfinite(GetLocalTime())) ++rep.directNonFinite;

            rep.framesRecorded.store((long long)k);
            rep.directTicksTotal.fetch_add(directTicksNow);
            rep.pluginTicksTotal.fetch_add(rep.pluginTicksNow.load());
            if (directTicksNow > rep.maxDirectTicksInFrame.load()) rep.maxDirectTicksInFrame.store(directTicksNow);
            if (rep.pluginTicksNow.load() > rep.maxPluginTicksInFrame.load()) rep.maxPluginTicksInFrame.store(rep.pluginTicksNow.load());
            if (rep.pluginUpdatesNow.load() != 1) ++rep.failures;   // exactly one plugin OnUpdate per frame
            if (spec.recordEvery && k < (size_t)WO10ClockReport::kMaxRecorded)
            {
                WO10FrameRecord& f = rep.frames[k];
                f.clockNow = clock.At(k);
                f.absoluteTime = app.GetAbsoluteTime();
                f.paused = app.IsPaused();
                f.globalScale = app.GetTimeScale();
                f.fixedHz = app.GetFixedTimestepHz();
                f.directTicks = directTicksNow; f.directFixedDtSum = directSumNow;
                f.directFixedDtMin = directMinNow; f.directFixedDtMax = directMaxNow;
                f.directUpdateDt = dt; f.directLocalTime = GetLocalTime();
                f.pluginTicks = rep.pluginTicksNow.load(); f.pluginFixedDtSum = rep.pluginFixedDtSumNow.load();
                f.pluginFixedDtMin = rep.pluginFixedDtMinNow.load(); f.pluginFixedDtMax = rep.pluginFixedDtMaxNow.load();
                f.pluginUpdateDt = rep.pluginUpdateDtNow.load(); f.pluginLocalTime = rep.pluginLocalTimeNow.load();
            }
            // Always keep the LAST frame's record at index 0 (long runs exceed the detail window).
            {
                WO10FrameRecord& f = rep.frames[0];
                f.clockNow = clock.At(k); f.absoluteTime = app.GetAbsoluteTime(); f.paused = app.IsPaused();
                f.globalScale = app.GetTimeScale(); f.fixedHz = app.GetFixedTimestepHz();
                f.directTicks = directTicksNow; f.directUpdateDt = dt; f.directLocalTime = GetLocalTime();
                f.pluginTicks = rep.pluginTicksNow.load(); f.pluginUpdateDt = rep.pluginUpdateDtNow.load();
                f.pluginLocalTime = rep.pluginLocalTimeNow.load();
                f.directFixedDtMin = directMinNow; f.directFixedDtMax = directMaxNow;
                f.pluginFixedDtMin = rep.pluginFixedDtMinNow.load(); f.pluginFixedDtMax = rep.pluginFixedDtMaxNow.load();
            }
            ResetNow();

            while (ai < acts.size() && acts[ai].atFrame == k)
            {
                switch (acts[ai].kind)
                {
                case Action::Pause:    app.Pause();  break;
                case Action::Resume:   app.Resume(); break;
                case Action::SetScale: app.SetTimeScale(acts[ai].value); break;
                case Action::SetHz:    app.SetFixedTimestepHz(acts[ai].value); break;
                }
                ++ai;
            }
            if (clock.Exhausted()) Close();
        }
    };

    // ------------------------------------------------------------------------
    // Run one rung in THIS process (the wrapper gives every rung its own process).
    // ------------------------------------------------------------------------
    struct Run
    {
        std::unique_ptr<WO10ClockReport> rep;
        Reference ref;
        size_t frames = 0;
        double realSeconds = 0.0;
    };

    inline Run RunRung(const RungSpec& spec)
    {
        Run out;
        out.rep = std::make_unique<WO10ClockReport>();
        out.ref = ComputeReference(spec);
        out.frames = spec.deltas.size();

        std::vector<double> sched; sched.reserve(spec.deltas.size() + 1);
        sched.push_back(spec.origin);
        for (double d : spec.deltas) sched.push_back(sched.back() + d);
        auto clockOwner = std::make_unique<FakeFrameClock>(sched);
        FakeFrameClock& clock = *clockOwner;

        char ptr[32];
        std::snprintf(ptr, sizeof(ptr), "%llX", (unsigned long long)(uintptr_t)out.rep.get());
        _putenv_s("COSMIC_WO10_CLOCK_REPORT", ptr);
        char ls[32]; std::snprintf(ls, sizeof(ls), "%.9g", spec.localScale);
        _putenv_s("COSMIC_WO10_LOCAL_SCALE", ls);

        const auto t0 = std::chrono::steady_clock::now();
        {
            wchar_t exePath[MAX_PATH]{};
            REQUIRE(GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0);
            const auto fixture = std::filesystem::path(exePath).parent_path() / "WO10ClockFixture.dll";
            REQUIRE(std::filesystem::exists(fixture));
            Cosmic::Application app(fixture.string(), std::move(clockOwner));
            app.GetWindow().SetVSync(false);
            if (HWND hwnd = WO05NativeWindow(app.GetWindow())) ShowWindow(hwnd, SW_HIDE);
            app.SetTimeScale(spec.globalScale);
            app.SetFixedTimestepHz(spec.fixedHz);
            app.PushOverlay(new Driver(*out.rep, clock, spec));   // an overlay: runs AFTER the plugin each pass
            app.Run();
        }
        out.realSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        _putenv_s("COSMIC_WO10_CLOCK_REPORT", "");
        _putenv_s("COSMIC_WO10_LOCAL_SCALE", "");
        return out;
    }

    // ------------------------------------------------------------------------
    // Shared assertions
    // ------------------------------------------------------------------------
    inline double FloatBar(double expected) { return 1e-6 + 1e-5 * std::abs(expected); }
    inline double Ulp(double v) { const float f = (float)std::abs(v); return (double)(std::nextafter(f, INFINITY) - f); }

    struct FrameStats
    {
        double maxDirectDtErr = 0.0, maxPluginDtErr = 0.0;   // vs the reference per-frame dt
        size_t worstDirectFrame = 0, worstPluginFrame = 0;
        int    framesOverFloatBar = 0;
        int    pausedFramesWithTicks = 0, pausedFramesWithDt = 0;
        int    framesWithZeroTicks = 0, framesWithTwoPlus = 0;
        int    directPluginTickMismatch = 0;
        int    fixedDtOffFrames = 0;   // a fixed dt magnitude that is not 1/Hz (direct) / 1/Hz*local (plugin)
    };

    inline FrameStats CheckFrames(const RungSpec& spec, const Run& run)
    {
        FrameStats st;
        const auto& r = run.ref; const auto& rep = *run.rep;
        // Long runs keep aggregates only (frames[] stays zero) — no per-frame stats then.
        const size_t n = spec.recordEvery ? std::min(run.frames, (size_t)WO10ClockReport::kMaxRecorded - 1) : 0;
        for (size_t k = 1; k <= n; ++k)
        {
            const WO10FrameRecord& f = rep.frames[k];
            const double dRef = r.directUpdateDt[k - 1], pRef = r.pluginUpdateDt[k - 1];
            const double dErr = std::abs((double)f.directUpdateDt - dRef), pErr = std::abs((double)f.pluginUpdateDt - pRef);
            if (dErr > st.maxDirectDtErr) { st.maxDirectDtErr = dErr; st.worstDirectFrame = k; }
            if (pErr > st.maxPluginDtErr) { st.maxPluginDtErr = pErr; st.worstPluginFrame = k; }
            if (dErr > FloatBar(dRef) || pErr > FloatBar(pRef)) ++st.framesOverFloatBar;
            if (r.paused[k - 1])
            {
                if (f.directTicks != 0 || f.pluginTicks != 0) ++st.pausedFramesWithTicks;
                if (f.directUpdateDt != 0.0f || f.pluginUpdateDt != 0.0f) ++st.pausedFramesWithDt;
            }
            if (f.directTicks == 0) ++st.framesWithZeroTicks;
            if (f.directTicks >= 2) ++st.framesWithTwoPlus;
            if (f.directTicks != f.pluginTicks) ++st.directPluginTickMismatch;
            const double step = 1.0 / r.hz[k - 1];
            if (f.directTicks > 0 && (std::abs(f.directFixedDtMin - step) > FloatBar(step) || std::abs(f.directFixedDtMax - step) > FloatBar(step))) ++st.fixedDtOffFrames;
            const double pstep = step * (double)spec.localScale;
            if (f.pluginTicks > 0 && (std::abs(f.pluginFixedDtMin - pstep) > FloatBar(pstep) || std::abs(f.pluginFixedDtMax - pstep) > FloatBar(pstep))) ++st.fixedDtOffFrames;
        }
        return st;
    }

    // The assertions every rung shares. `label` prefixes the MESSAGEs.
    inline void AssertCommon(const RungSpec& spec, const Run& run, const FrameStats& st)
    {
        const auto& rep = *run.rep; const auto& r = run.ref;
        CHECK(rep.attached.load() == 1);
        CHECK(rep.detached.load() == 1);
        CHECK(rep.destroyed.load() == 1);
        CHECK(rep.failures.load() == 0);
        CHECK(rep.runawayClosed.load() == 0);
        CHECK(rep.framesRecorded.load() == (long long)run.frames);
        CHECK(rep.pluginNonFinite.load() == 0);
        CHECK(rep.directNonFinite.load() == 0);
        // The declared tick count, exactly, on BOTH observers.
        CHECK(rep.directTicksTotal.load() == r.ticks);
        CHECK(rep.pluginTicksTotal.load() == r.ticks);
        CHECK(st.directPluginTickMismatch == 0);
        CHECK(st.fixedDtOffFrames == 0);
        CHECK(st.pausedFramesWithTicks == 0);
        CHECK(st.pausedFramesWithDt == 0);
        const WO10FrameRecord& last = rep.frames[0];
        MESSAGE((std::string("[") + spec.name + "] frames=" + std::to_string(run.frames)
            + " ticks direct/plugin/ref=" + std::to_string(rep.directTicksTotal.load()) + "/" + std::to_string(rep.pluginTicksTotal.load()) + "/" + std::to_string(r.ticks)
            + " maxTicksInFrame direct/plugin=" + std::to_string(rep.maxDirectTicksInFrame.load()) + "/" + std::to_string(rep.maxPluginTicksInFrame.load())
            + " zeroTickFrames=" + std::to_string(st.framesWithZeroTicks) + " twoPlusTickFrames=" + std::to_string(st.framesWithTwoPlus)
            + " realSeconds=" + std::to_string(run.realSeconds)));
        char buf[512];
        std::snprintf(buf, sizeof(buf), "[%s] per-frame OnUpdate dt error max: direct %.3e (frame %zu) plugin %.3e (frame %zu); frames over the float bar: %d",
            spec.name.c_str(), st.maxDirectDtErr, st.worstDirectFrame, st.maxPluginDtErr, st.worstPluginFrame, st.framesOverFloatBar);
        MESSAGE((std::string(buf)));
        std::snprintf(buf, sizeof(buf), "[%s] plugin local time: observed %.9g ref %.9g err %.3e (float accumulation over %zu frames, worst-case bound %.3e); uptime: observed %.9g ref %.9g err %.3e",
            spec.name.c_str(), (double)last.pluginLocalTime, r.pluginLocal, std::abs((double)last.pluginLocalTime - r.pluginLocal), run.frames,
            (double)run.frames * Ulp(r.pluginLocal) / 2.0 + 1e-6, (double)last.absoluteTime, r.uptime, std::abs((double)last.absoluteTime - r.uptime));
        MESSAGE((std::string(buf)));
    }
}
