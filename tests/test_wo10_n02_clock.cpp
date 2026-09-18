// test_wo10_n02_clock.cpp — N02 (2D stability, WO-10): clock origins, non-finite
// rate/speed policy, negative replay, the negative-global-scale policy, drift and
// long-uptime quantisation.
//
// HOST rungs (skipped by default; the WO-04 runner launches one CosmicTests.exe
// child per COSMIC_WO10_RUNG, exactly like N01 — WO10ClockHarness.h):
//   origin-0 / origin-2h / origin-24h — the injected clock starts at 0 s / 7,200 s /
//       86,400 s and advances by sub-step 1/144 s frames for 10 s + 1/480: finite
//       state, monotonic local time, 600 ticks, and every OnUpdate dt within the
//       float bar of the declared 1/144 (a float clock SAMPLE quantises the delta to
//       the sample's ulp — 0.49 ms at 2 h, 7.8 ms at 24 h — which is what this pins)
//   policy-hz          — SetFixedTimestepHz 0 / 1e9 / NaN / inf / -inf: clamp to
//       [1, 1000], NaN rejected; the tick count over every window equals the
//       double reference — no window of "no ticks", no catch-up burst afterwards
//   policy-scale-nan   — SetTimeScale(NaN) is rejected: no poisoned accumulator,
//       ticks resume at once, no non-finite dt reaches a layer
//   policy-scale-negative — the NEGATIVE GLOBAL SCALE policy: rejected with a warning,
//       the previous scale is kept; no hidden restart debt when +1 is set again
//   policy-scale-inf   — SetTimeScale(+inf / -inf) rejected (before WO-10: +inf made
//       the drain loop run forever)
//   drift-2h           — 432,000 frames of exactly 1/60 s (two nominal hours): the
//       production tick count vs the integer-tick/double reference (the proposed
//       "<= one 60-Hz step over 2 h" bar) and the stored-float accumulators'
//       (plugin local time, uptime) observed error, reported
// HEADLESS cases (tier U, run in the ordinary suite):
//   KI-16 DataRecorder — 432,000 ticks of 1/60: the recorded duration and every
//       stored float timestamp within one float ulp of i/60 (before WO-10 the float
//       accumulator ended 16.9 s short)
//   negative replay    — DataPlayer over a v1 recording of F-TRAJECTORY: speed -1
//       plays backwards to 0 and auto-stops; SampleAt matches the double equations
//   float accumulator arithmetic at 24 h — the quantisation of `float += 1/60` over
//       5,184,000 frames (what m_AbsoluteTime / m_LocalTime do) vs a double sum,
//       measured and reported (arithmetic only — the host rungs cover the scheduler)
#include <doctest.h>
#include "WO10ClockHarness.h"

#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include "utils/DataExport.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{
    std::string RungFromEnv()
    {
        char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, "COSMIC_WO10_RUNG");
        std::string s = v ? v : ""; if (v) free(v); return s;
    }
    std::string EvidenceDir()
    {
        char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, "COSMIC_WO10_EVIDENCE_DIR");
        std::string s = v ? v : ""; if (v) free(v); return s;
    }

    Wo10::RungSpec MakeN02(const std::string& rung)
    {
        using namespace Wo10;
        RungSpec s; s.name = rung;
        if (rung == "origin-0" || rung == "origin-2h" || rung == "origin-24h")
        {
            s.origin = rung == "origin-0" ? 0.0 : rung == "origin-2h" ? 7200.0 : 86400.0;
            s.deltas = Regular(1440, 1.0 / 144.0);
        }
        else if (rung == "policy-hz")
        {
            s.deltas = Regular(720, 1.0 / 60.0);
            s.actions = {
                { 90,  Action::SetHz, 0.0f },        // clamps to 1 Hz
                { 180, Action::SetHz, 1e9f },        // clamps to 1000 Hz
                { 270, Action::SetHz, NAN },         // rejected: stays 1000 Hz
                { 360, Action::SetHz, 60.0f },
                { 450, Action::SetHz, INFINITY },    // clamps to 1000 Hz
                { 540, Action::SetHz, -INFINITY },   // clamps to 1 Hz
                { 630, Action::SetHz, 60.0f },
            };
        }
        else if (rung == "policy-scale-nan")
        {
            s.deltas = Regular(360, 1.0 / 60.0);
            s.actions = { { 120, Action::SetScale, NAN }, { 240, Action::SetScale, 1.0f } };
        }
        else if (rung == "policy-scale-negative")
        {
            s.deltas = Regular(480, 1.0 / 60.0);
            s.actions = { { 120, Action::SetScale, -1.0f }, { 300, Action::SetScale, 1.0f } };
        }
        else if (rung == "policy-scale-inf")
        {
            s.deltas = Regular(360, 1.0 / 60.0);
            s.actions = { { 120, Action::SetScale, INFINITY }, { 240, Action::SetScale, -INFINITY } };
            s.realTimeBudgetSec = 120.0;   // the pre-WO-10 engine never returns from the drain loop
        }
        else if (rung == "drift-2h")
        {
            s.deltas = Regular(432000, 1.0 / 60.0);
            s.recordEvery = false;
            s.realTimeBudgetSec = 3000.0;
        }
        return s;
    }
}

TEST_CASE("WO-10 N02 host: clock origins, rate/speed policy and drift over the injected clock" * doctest::skip())
{
    const std::string rung = RungFromEnv();
    REQUIRE_MESSAGE(!rung.empty(), "COSMIC_WO10_RUNG must name a rung (the runner sets it)");
    const Wo10::RungSpec spec = MakeN02(rung);
    REQUIRE_MESSAGE(!spec.deltas.empty(), (std::string("unknown N02 rung: ") + rung));

    const Wo10::Run run = Wo10::RunRung(spec);
    const Wo10::FrameStats st = Wo10::CheckFrames(spec, run);
    Wo10::AssertCommon(spec, run, st);
    const auto& rep = *run.rep; const auto& r = run.ref;
    const WO10FrameRecord& last = rep.frames[0];

    if (rung.rfind("origin-", 0) == 0)
    {
        CHECK(r.ticks == 600);
        CHECK(rep.localTimeDecreased.load() == 0);
        // Sub-step frames at a large clock origin: the observed per-frame dt must
        // still be the declared 1/144 within the float bar — a float sample of the
        // clock cannot deliver that at 2 h (ulp 0.49 ms) or 24 h (ulp 7.8 ms).
        CHECK(st.framesOverFloatBar == 0);
        CHECK(st.maxDirectDtErr <= Wo10::FloatBar(1.0 / 144.0));
        CHECK(rep.maxDirectTicksInFrame.load() <= 1);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[%s] origin %.0f s: float ulp at the origin = %.3e s; observed max dt error %.3e s (declared dt 1/144 = 6.944e-3)",
            rung.c_str(), spec.origin, Wo10::Ulp(spec.origin), st.maxDirectDtErr);
        MESSAGE((std::string(buf)));
    }
    if (rung == "policy-hz")
    {
        CHECK(rep.frames[100].fixedHz == 1.0f);
        CHECK(rep.frames[200].fixedHz == 1000.0f);
        CHECK(rep.frames[300].fixedHz == 1000.0f);     // NaN rejected, previous kept
        CHECK(rep.frames[400].fixedHz == 60.0f);
        CHECK(rep.frames[500].fixedHz == 1000.0f);     // +inf clamps to 1000
        CHECK(rep.frames[600].fixedHz == 1.0f);        // -inf clamps to 1
        CHECK(rep.frames[700].fixedHz == 60.0f);
        // No catch-up burst when a valid rate follows the rejected one: frame 361
        // (first at 60 Hz after the NaN window) earns at most the carried step.
        CHECK(rep.frames[361].directTicks <= 2);
        int nanWindow = 0; for (size_t k = 271; k <= 360; ++k) nanWindow += rep.frames[k].directTicks;
        CHECK(nanWindow >= 1499);   // 1.5 s at 1000 Hz (float carry may shift one)
        MESSAGE((std::string("[policy-hz] ticks in the NaN-rate window (frames 271..360, must still run at 1000 Hz): ") + std::to_string(nanWindow)
            + "; first frame after SetFixedTimestepHz(60): " + std::to_string(rep.frames[361].directTicks) + " ticks"));
    }
    if (rung == "policy-scale-nan")
    {
        CHECK(r.ticks == 360);
        CHECK(rep.frames[200].globalScale == 1.0f);   // NaN rejected
        int poisoned = 0; for (size_t k = 121; k <= 240; ++k) poisoned += rep.frames[k].directTicks;
        CHECK(poisoned == 120);
        int after = 0; for (size_t k = 241; k <= 360; ++k) after += rep.frames[k].directTicks;
        CHECK(after == 120);
        MESSAGE((std::string("[policy-scale-nan] ticks while NaN was requested: ") + std::to_string(poisoned) + "; after SetTimeScale(1): " + std::to_string(after)));
    }
    if (rung == "policy-scale-negative")
    {
        // POLICY: a negative global TimeScale is rejected (warning, previous scale kept).
        CHECK(r.ticks == 480);
        CHECK(rep.frames[200].globalScale == 1.0f);
        int duringNeg = 0; for (size_t k = 121; k <= 300; ++k) duringNeg += rep.frames[k].directTicks;
        int afterNeg = 0;  for (size_t k = 301; k <= 480; ++k) afterNeg  += rep.frames[k].directTicks;
        CHECK(duringNeg == 180);
        CHECK(afterNeg == 180);            // no hidden restart debt
        CHECK(rep.frames[301].directTicks <= 2);
        CHECK(rep.localTimeDecreased.load() == 0);
        CHECK(rep.frames[200].directFixedDtMin > 0.0f);   // never a signed (negative) fixed delta
        MESSAGE((std::string("[policy-scale-negative] ticks while -1 was requested: ") + std::to_string(duringNeg)
            + "; ticks in the 3 s after SetTimeScale(1): " + std::to_string(afterNeg) + " (declared 180 / 180; before WO-10: 0 / 0 — a 3 s restart debt)"));
    }
    if (rung == "policy-scale-inf")
    {
        CHECK(r.ticks == 360);
        CHECK(rep.frames[200].globalScale == 1.0f);
        CHECK(rep.frames[300].globalScale == 1.0f);
    }
    if (rung == "drift-2h")
    {
        CHECK(r.ticks == 432000);
        const long long drift = rep.directTicksTotal.load() - 432000;
        CHECK(std::llabs(drift) <= 1);      // the proposed 2-h bar: <= one 60-Hz step
        char buf[512];
        std::snprintf(buf, sizeof(buf), "[drift-2h] production ticks %lld vs reference 432000: drift %lld step(s) (proposed bar <= 1, measurement — not gating); plugin local time %.6f vs 7200.002083 (err %.4f s); uptime %.6f (err %.4f s); real time %.1f s for %zu frames (%.3f ms/frame)",
            rep.directTicksTotal.load(), drift, (double)last.pluginLocalTime, (double)last.pluginLocalTime - r.pluginLocal,
            (double)last.absoluteTime, (double)last.absoluteTime - r.uptime, run.realSeconds, run.frames, 1000.0 * run.realSeconds / (double)run.frames);
        MESSAGE((std::string(buf)));
        const std::string ev = EvidenceDir();
        if (!ev.empty())
        {
            std::error_code ec; std::filesystem::create_directories(ev, ec);
            FILE* f = nullptr; fopen_s(&f, (std::filesystem::path(ev) / "n02-drift-2h.txt").string().c_str(), "w");
            if (f) { std::fprintf(f, "%s\n", buf); std::fclose(f); }
        }
    }
}

// ============================================================================
// Headless (tier U)
// ============================================================================
TEST_SUITE("WO-10 N02 clock (headless)")
{
    TEST_CASE("WO-10 N02: KI-16 DataRecorder time accumulator over two nominal hours (432,000 ticks of 1/60)")
    {
        namespace fs = std::filesystem;
        Cosmic::DataRecorder rec;
        const uint32_t id = rec.Register("clock", "wo10", { "v" });
        const int n = 432000;
        rec.ReserveCapacity((size_t)n);
        for (int i = 0; i < n; ++i) { rec.Tick(1.0f / 60.0f); rec.Record(id, { (float)i }); }
        const double nominal = 432000.0 / 60.0;   // 7200 s
        // Tick takes a FLOAT dt: 1/60 is not representable, so the exact sum of what was
        // ticked is n * (double)(1/60f) = 7200.00053 s. That 5.3e-4 s is the float
        // argument, not accumulator drift — reported against the nominal as well.
        const double ticked = 432000.0 * (double)(1.0f / 60.0f);
        const double dur = rec.GetRecordedDuration();
        char buf[320];
        std::snprintf(buf, sizeof(buf), "KI-16: recorded duration after 432,000 ticks of 1/60f = %.6f s (exact sum of the float ticks %.6f, nominal %.6f; drift vs nominal %.4f s = %.4f steps; one float ulp at 7200 = %.3e)", dur, ticked, nominal, dur - nominal, (dur - nominal) * 60.0, Wo10::Ulp(nominal));
        MESSAGE((std::string(buf)));
        CHECK(std::abs(dur - ticked) <= Wo10::Ulp(nominal));           // the accumulator: within one float ulp of the exact sum
        CHECK(std::abs(dur - nominal) <= 1.0 / 60.0);                  // the catalog bar: <= one 60-Hz step over 2 h

        // Every stored timestamp: within one float ulp of (i+1)/60.
        const fs::path tmp = fs::temp_directory_path() / "wo10-ki16";
        std::error_code ec; fs::remove_all(tmp, ec);
        rec.Flush(tmp.string(), "rec", 60.0f);
        rec.WaitForFlush();
        std::vector<std::vector<double>> cols;
        REQUIRE(Cosmic::DataExport::LoadCSV((tmp / "rec" / "clock.csv").string(), cols));
        REQUIRE(cols.size() == 2);
        REQUIRE(cols[0].size() == (size_t)n);
        double maxErr = 0.0, maxErrNominal = 0.0; size_t worst = 0;
        for (size_t i = 0; i < cols[0].size(); ++i)
        {
            const double exact = (double)(i + 1) * (double)(1.0f / 60.0f);   // the exact sum of the float ticks
            const double e = std::abs(cols[0][i] - exact);
            if (e > maxErr) { maxErr = e; worst = i; }
            maxErrNominal = std::max(maxErrNominal, std::abs(cols[0][i] - (double)(i + 1) / 60.0));
        }
        std::snprintf(buf, sizeof(buf), "KI-16: max stored-timestamp error vs the exact float-tick sum %.4e s at sample %zu (t=%.3f; one float ulp there = %.3e); vs the nominal i/60: %.4e s", maxErr, worst, (double)(worst + 1) / 60.0, Wo10::Ulp((double)(worst + 1) / 60.0), maxErrNominal);
        MESSAGE((std::string(buf)));
        CHECK(maxErr <= Wo10::Ulp(nominal));
        CHECK(maxErrNominal <= 1.0 / 60.0);
        fs::remove_all(tmp, ec);
    }

    TEST_CASE("WO-10 N02: negative replay — DataPlayer speed -1 over a v1 recording of F-TRAJECTORY")
    {
        namespace fs = std::filesystem;
        const double g = 9.80665;
        // Record the fixture through the real recorder (float telemetry path).
        Cosmic::DataRecorder rec;
        const uint32_t id = rec.Register("shot", "wo10", { "x", "y", "vx", "vy" });
        rec.ReserveCapacity(1201);
        for (int i = 0; i <= 1200; ++i)
        {
            const double t = i / 120.0;
            if (i > 0) rec.Tick(1.0f / 120.0f);
            rec.Record(id, { (float)(30.0 * t), (float)(50.0 * t - 0.5 * g * t * t), 30.0f, (float)(50.0 - g * t) });
        }
        const fs::path tmp = fs::temp_directory_path() / "wo10-negreplay";
        std::error_code ec; fs::remove_all(tmp, ec);
        rec.Flush(tmp.string(), "rec", 120.0f);
        rec.WaitForFlush();

        Cosmic::DataPlayer player;
        REQUIRE(player.Load((tmp / "rec").string()));
        CHECK(player.GetDuration() == doctest::Approx(10.0f).epsilon(1e-4));
        player.SetSpeed(-1.0f);
        CHECK(player.GetSpeed() == -1.0f);
        player.SetPosition(player.GetDuration());
        player.Play();
        float prev = player.GetPosition(); int steps = 0; bool monotonic = true;
        while (player.IsPlaying() && steps < 10000)
        {
            player.Tick(0.25f);
            if (player.GetPosition() > prev) monotonic = false;
            prev = player.GetPosition(); ++steps;
        }
        CHECK(monotonic);
        CHECK(player.GetPosition() == 0.0f);
        CHECK_FALSE(player.IsPlaying());          // auto-stopped at the start
        // 10 s / 0.25 s = 40 steps (41 when the float duration rounds a hair above 10).
        CHECK(steps == (int)std::ceil((double)player.GetDuration() / 0.25 - 1e-9));
        // Scrubbed samples (exact and midpoint times) agree with the double equations.
        double maxErr = 0.0;
        for (int i = 0; i <= 1200; i += 7)
        {
            for (int half = 0; half < 2; ++half)
            {
                const double t = (i + 0.5 * half) / 120.0;
                if (t > 10.0) continue;
                Cosmic::TelemetryFrame f;
                REQUIRE(player.SampleAt("shot", (float)t, f));
                const double x = 30.0 * t, y = 50.0 * t - 0.5 * g * t * t;
                maxErr = std::max(maxErr, std::abs((double)f.values[0] - x));
                // Linear interpolation of a parabola at a midpoint carries an O(dt^2) term: g/8*dt^2 = 4.3e-5.
                const double yBar = Wo10::FloatBar(y) + (half ? g / 8.0 * (1.0 / 120.0) * (1.0 / 120.0) : 0.0);
                CHECK(std::abs((double)f.values[1] - y) <= yBar + 1e-4);
                CHECK(std::abs((double)f.values[0] - x) <= Wo10::FloatBar(x) + 1e-4);
            }
        }
        MESSAGE((std::string("negative replay: x max error vs 30t over the scrub set = ") + std::to_string(maxErr)));
        // NaN / inf speed is ignored (previous kept); NaN seek is ignored.
        player.SetSpeed(NAN);      CHECK(player.GetSpeed() == -1.0f);
        player.SetSpeed(INFINITY); CHECK(player.GetSpeed() == -1.0f);
        player.SetPosition(NAN);   CHECK(player.GetPosition() == 0.0f);
        fs::remove_all(tmp, ec);
    }

    TEST_CASE("WO-10 N02: stored-float accumulator quantisation at 24 h uptime (arithmetic measurement)")
    {
        // The exact arithmetic of `float accumulator += frame delta` over 24 h of 60-Hz
        // frames — what Layer::UpdateLayerTime (m_LocalTime) does, and what
        // Application::m_AbsoluteTime did before WO-10 — against a double sum.
        // Reported, not gated: the catalog's clock-drift bar accounts for stored-float
        // quantisation separately.
        const int frames = 24 * 3600 * 60;   // 5,184,000
        const float dt = 1.0f / 60.0f;
        float  fAcc = 0.0f;
        double dAcc = 0.0;
        double worstUlp = 0.0; int lastTimeItMoved = -1; int stalled = 0;
        for (int i = 0; i < frames; ++i)
        {
            const float before = fAcc;
            fAcc += dt; dAcc += (double)dt;
            if (fAcc == before) ++stalled; else lastTimeItMoved = i;
            worstUlp = std::max(worstUlp, Wo10::Ulp(fAcc));
        }
        char buf[512];
        std::snprintf(buf, sizeof(buf), "float accumulator after 24 h of 1/60 frames: %.3f s vs exact %.3f s (error %.3f s = %.3f %%); frames where the float did not move at all: %d; float ulp at 86,400 s = %.4e s (a 1/144 s frame is %.2f ulps, a 1/60 s frame %.2f ulps)",
            (double)fAcc, dAcc, (double)fAcc - dAcc, 100.0 * ((double)fAcc - dAcc) / dAcc, stalled, Wo10::Ulp(86400.0), (1.0 / 144.0) / Wo10::Ulp(86400.0), (1.0 / 60.0) / Wo10::Ulp(86400.0));
        MESSAGE((std::string(buf)));
        (void)lastTimeItMoved;
        // The float-vs-double gap at 24 h exists and is large — pin the measurement's shape.
        CHECK(dAcc == doctest::Approx(86400.0).epsilon(1e-6));
        CHECK(std::abs((double)fAcc - dAcc) > 1.0);   // a stored-float uptime is off by more than a second after a day
        // At 2 h the same arithmetic (432,000 frames):
        float f2 = 0.0f; for (int i = 0; i < 432000; ++i) f2 += dt;
        std::snprintf(buf, sizeof(buf), "float accumulator after 2 h of 1/60 frames: %.4f s vs 7200 (error %.4f s; one 60-Hz step = 0.0167 s)", (double)f2, (double)f2 - 7200.0);
        MESSAGE((std::string(buf)));
    }
}
