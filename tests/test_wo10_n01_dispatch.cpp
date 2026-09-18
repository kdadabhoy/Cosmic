// test_wo10_n01_dispatch.cpp — N01 (2D stability, WO-10): the PRODUCTION
// Application fixed/variable scheduler over an injected clock.
//
// Skipped by default (a real Application + window + GL context). The WO-04 runner
// launches one fresh CosmicTests.exe child per rung with COSMIC_WO10_RUNG set; the
// child boots WO10ClockFixture.dll into a real Application whose only fake part is
// the IFrameClock (tests/FakeFrameClock.h), runs the rung's frame schedule through
// Application::Run, and asserts what the direct overlay and the WorkspaceLayer-
// hosted plugin observed against the integer-tick / double reference
// (WO10ClockHarness.h). Every rung declares the same total simulated time
// (10 s + 1/480 s) unless it says otherwise.
//
// Rungs (60 Hz fixed rate unless stated):
//   30hz / 60hz / 144hz / irregular  — equal total time at speed 1 => 600 ticks each
//   speed0 / speed025 / speed4       — the 60-Hz schedule at TimeScale 0 / 0.25 / 4
//   pause                            — pause frames 181..300, resume; no debt, no burst
//   stall                            — one 0.4 s frame: the 0.25 s clamp delivers 15
//                                      ticks, drops 9, no double tick after it
//   local-quarter                    — global 1, plugin-LOCAL 0.25 (WorkspaceLayer path)
//   global-quarter                   — global 0.25, plugin-local 1 — same plugin OnUpdate
//                                      dt as local-quarter but 150 ticks of 1/60 instead
//                                      of 600 ticks of 1/240: the two scalings are
//                                      distinguishable in the observed dt
#include <doctest.h>
#include "WO10ClockHarness.h"

#include <cstdlib>
#include <string>

namespace
{
    std::string RungFromEnv()
    {
        char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, "COSMIC_WO10_RUNG");
        std::string s = v ? v : ""; if (v) free(v); return s;
    }

    Wo10::RungSpec MakeN01(const std::string& rung)
    {
        using namespace Wo10;
        RungSpec s; s.name = rung;
        if (rung == "30hz")        { s.deltas = Regular(300, 1.0 / 30.0); }
        else if (rung == "60hz")   { s.deltas = Regular(600, 1.0 / 60.0); }
        else if (rung == "144hz")  { s.deltas = Regular(1440, 1.0 / 144.0); }
        else if (rung == "irregular") { s.deltas = Irregular(10.0, 0.002, 0.040, 0x0A10'0001ULL); }
        else if (rung == "speed0")    { s.deltas = Regular(600, 1.0 / 60.0); s.globalScale = 0.0f; }
        else if (rung == "speed025")  { s.deltas = Regular(600, 1.0 / 60.0); s.globalScale = 0.25f; }
        else if (rung == "speed4")    { s.deltas = Regular(600, 1.0 / 60.0); s.globalScale = 4.0f; }
        else if (rung == "pause")
        {
            s.deltas = Regular(600, 1.0 / 60.0);
            s.actions = { { 180, Action::Pause, 0.0f }, { 300, Action::Resume, 0.0f } };
        }
        else if (rung == "stall")
        {
            // 300 normal frames, one 0.4 s stall (24 frames' worth), 276 normal frames, tail.
            s.deltas.assign(300, 1.0 / 60.0);
            s.deltas.push_back(0.4);
            s.deltas.insert(s.deltas.end(), 276, 1.0 / 60.0);
            s.deltas.push_back(1.0 / 480.0);
        }
        else if (rung == "local-quarter")  { s.deltas = Regular(600, 1.0 / 60.0); s.localScale = 0.25f; }
        else if (rung == "global-quarter") { s.deltas = Regular(600, 1.0 / 60.0); s.globalScale = 0.25f; s.localScale = 1.0f; }
        else { s.deltas.clear(); }
        return s;
    }
}

TEST_CASE("WO-10 N01 host: production fixed/variable dispatch over the injected clock" * doctest::skip())
{
    const std::string rung = RungFromEnv();
    REQUIRE_MESSAGE(!rung.empty(), "COSMIC_WO10_RUNG must name a rung (the runner sets it)");
    const Wo10::RungSpec spec = MakeN01(rung);
    REQUIRE_MESSAGE(!spec.deltas.empty(), (std::string("unknown N01 rung: ") + rung));

    const Wo10::Run run = Wo10::RunRung(spec);
    const Wo10::FrameStats st = Wo10::CheckFrames(spec, run);
    Wo10::AssertCommon(spec, run, st);
    const auto& rep = *run.rep; const auto& r = run.ref;
    const WO10FrameRecord& last = rep.frames[0];

    // Per-frame variable dt equals the declared policy within the float bar (origin 0).
    CHECK(st.framesOverFloatBar == 0);

    // Stored-float accumulators (Layer local time, Application uptime): within the
    // worst-case float accumulation bound over this many frames; observed error is
    // in the MESSAGE above.
    const double accBound = (double)run.frames * Wo10::Ulp(r.pluginLocal) / 2.0 + 1e-6;
    CHECK(std::abs((double)last.pluginLocalTime - r.pluginLocal) <= accBound);
    const double upBound = (double)run.frames * Wo10::Ulp(r.uptime) / 2.0 + 1e-6;
    CHECK(std::abs((double)last.absoluteTime - r.uptime) <= upBound);

    if (rung == "30hz" || rung == "60hz" || rung == "144hz" || rung == "irregular")
    {
        CHECK(r.ticks == 600);                                  // the declared count
        // No burst: a regular frame can never deliver more than ceil(dt/step)+1 ticks.
        const double maxDt = *std::max_element(spec.deltas.begin(), spec.deltas.end());
        CHECK(rep.maxDirectTicksInFrame.load() <= (int)std::ceil(maxDt * 60.0) + 1);
        CHECK(rep.pluginLocalScale.load() == 1.0f);
    }
    if (rung == "60hz")
    {
        // An exact 1/60 frame schedule delivers exactly ONE tick every frame — no
        // 0/2 jitter — once the frame delta is derived from a double sample
        // (before WO-10 the float sample quantised the delta above/below 1/60).
        CHECK(st.framesWithZeroTicks == 1);   // only the 1/480 tail frame
        CHECK(st.framesWithTwoPlus == 0);
        CHECK(rep.maxDirectTicksInFrame.load() == 1);
    }
    if (rung == "speed0")
    {
        CHECK(r.ticks == 0);
        CHECK(rep.directTicksTotal.load() == 0);
        CHECK(last.pluginLocalTime == 0.0f);
        CHECK(last.directUpdateDt == 0.0f);
        CHECK(last.absoluteTime > 10.0f);            // uptime is never scaled
    }
    if (rung == "speed025") { CHECK(r.ticks == 150); CHECK(rep.frames[100].directUpdateDt == doctest::Approx(1.0 / 240.0).epsilon(1e-5)); }
    if (rung == "speed4")   { CHECK(r.ticks == 2400); CHECK(rep.maxDirectTicksInFrame.load() <= 5); CHECK(rep.frames[100].directUpdateDt == doctest::Approx(4.0 / 60.0).epsilon(1e-5)); }
    if (rung == "pause")
    {
        CHECK(r.ticks == 480);
        // Frames 181..300 are paused: 0 ticks, dt 0, local time frozen, uptime running.
        CHECK(rep.frames[181].paused); CHECK(rep.frames[300].paused); CHECK_FALSE(rep.frames[301].paused);
        CHECK(rep.frames[181].directTicks == 0); CHECK(rep.frames[300].directTicks == 0);
        CHECK(rep.frames[181].directUpdateDt == 0.0f);
        CHECK(rep.frames[300].pluginLocalTime == rep.frames[181].pluginLocalTime);
        CHECK(rep.frames[300].absoluteTime > rep.frames[181].absoluteTime);
        // Resume: no catch-up burst (no pause debt) — at most the one tick a 1/60 frame earns
        // plus the carried sub-step residual.
        CHECK(rep.frames[301].directTicks <= 2);
        CHECK(rep.maxDirectTicksInFrame.load() <= 2);
    }
    if (rung == "stall")
    {
        // Declared clamp policy: the 0.4 s frame contributes min(0.4, 0.25) = 0.25 s of
        // fixed time (15 ticks); 0.15 s (9 ticks) is DROPPED, never repaid. The
        // variable pass still receives the unclamped 0.4 s (documented).
        CHECK(r.ticks == 591);
        CHECK(rep.frames[301].directTicks + rep.frames[302].directTicks >= 15);   // 15 delivered (float may split 14+2 / 15+1)
        CHECK(rep.frames[301].directTicks <= 15);
        CHECK(rep.frames[302].directTicks <= 2);
        CHECK(rep.maxDirectTicksInFrame.load() <= 15);
        CHECK(rep.frames[301].directUpdateDt == doctest::Approx(0.4).epsilon(1e-5));
        CHECK(rep.frames[303].directTicks <= 1);   // no double tick after the stall settles
        MESSAGE((std::string("[stall] ticks in the 0.4 s frame: ") + std::to_string(rep.frames[301].directTicks)
            + ", next frame: " + std::to_string(rep.frames[302].directTicks) + " (declared: 15 delivered, 9 dropped)"));
    }
    if (rung == "local-quarter")
    {
        // WorkspaceLayer applies the plugin-LOCAL scale to BOTH deltas it forwards;
        // the direct overlay sees the unscaled engine dispatch and only its own
        // local time (via UpdateLayerTime) is affected by ITS local scale (1 here).
        CHECK(r.ticks == 600);
        CHECK(rep.pluginLocalScale.load() == 0.25f);
        CHECK(rep.frames[100].pluginUpdateDt == doctest::Approx(1.0 / 240.0).epsilon(1e-5));
        CHECK(rep.frames[100].pluginFixedDtMin == doctest::Approx(1.0 / 240.0).epsilon(1e-5));
        CHECK(rep.frames[100].directUpdateDt == doctest::Approx(1.0 / 60.0).epsilon(1e-5));
        CHECK(rep.frames[100].directFixedDtMin == doctest::Approx(1.0 / 60.0).epsilon(1e-5));
        CHECK(last.pluginLocalTime == doctest::Approx(r.pluginLocal).epsilon(1e-4));   // 2.5 s
    }
    if (rung == "global-quarter")
    {
        CHECK(r.ticks == 150);
        CHECK(rep.frames[100].pluginUpdateDt == doctest::Approx(1.0 / 240.0).epsilon(1e-5));   // same dt as local-quarter...
        CHECK(rep.frames[100].directUpdateDt == doctest::Approx(1.0 / 240.0).epsilon(1e-5));
        // ...but the fixed delta stays 1/60 and only every fourth frame ticks.
        int ticked = 0; for (size_t k = 1; k <= 600; ++k) ticked += rep.frames[k].pluginTicks > 0 ? 1 : 0;
        CHECK(ticked == 150);
        for (size_t k = 1; k <= 600; ++k) if (rep.frames[k].pluginTicks > 0) { CHECK(rep.frames[k].pluginFixedDtMin == doctest::Approx(1.0 / 60.0).epsilon(1e-5)); break; }
    }
}
