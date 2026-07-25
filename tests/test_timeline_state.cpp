// test_timeline_state.cpp — the Starforge timeline transport (Phase 29 W2 / §9.2).
//
// TimelineState is the pure clock behind the Animation Editor's scrub head (M2).
// It lives in the EDITOR, so a regression only ever surfaced when a human dragged
// the play head — include the header the same way test_template_scripts.cpp
// includes the project templates and drive it from a fake clock instead.
//
// Timeline.h includes <imgui.h> for the widget half; imgui is a PUBLIC link of
// the engine, so the include resolves without touching the test target's
// configuration. Only TimelineState (no ImGui calls) is exercised here.

#include <doctest.h>

#include "../Projects/Starforge/src/widgets/Timeline.h"

#include <cmath>

using Starforge::TimelineState;

TEST_SUITE("M2: TimelineState transport")
{
    TEST_CASE("Advance moves the head only while Playing")
    {
        TimelineState st;
        st.Duration = 2.0f;

        st.Advance(0.5f);                       // stopped
        CHECK(st.Time == doctest::Approx(0.0f));

        st.Play();
        CHECK(st.Playing);
        st.Advance(0.5f);
        CHECK(st.Time == doctest::Approx(0.5f));

        st.Pause();
        st.Advance(1.0f);
        CHECK(st.Time == doctest::Approx(0.5f));   // frozen
        CHECK_FALSE(st.Playing);
    }

    TEST_CASE("a looping clip wraps into [0, Duration) and never lands ON Duration")
    {
        TimelineState st;
        st.Duration = 1.0f;
        st.Loop     = true;
        st.Play();

        st.Advance(0.75f);
        CHECK(st.Time == doctest::Approx(0.75f));

        st.Advance(0.5f);                       // 1.25 -> 0.25
        CHECK(st.Time == doctest::Approx(0.25f));
        CHECK(st.Playing);                      // looping never stops

        // An enormous dt still lands inside the clip in one step.
        st.Advance(1000.0f);
        CHECK(st.Time >= 0.0f);
        CHECK(st.Time < st.Duration);
        CHECK(st.Playing);

        // Exactly one period is a no-op modulo the clip length.
        st.Scrub(0.4f);
        st.Advance(1.0f);
        CHECK(st.Time == doctest::Approx(0.4f).epsilon(1e-4));
    }

    TEST_CASE("a non-looping clip clamps to an end and stops itself")
    {
        TimelineState st;
        st.Duration = 2.0f;
        st.Loop     = false;
        st.Play();

        st.Advance(5.0f);
        CHECK(st.Time == doctest::Approx(2.0f));
        CHECK_FALSE(st.Playing);                // auto-stop at the end

        // Play() from a finished one-shot rewinds instead of sticking.
        st.Play();
        CHECK(st.Time == doctest::Approx(0.0f));
        CHECK(st.Playing);
    }

    TEST_CASE("negative Speed runs the clip backwards: looping wraps, one-shot stops at 0")
    {
        TimelineState st;
        st.Duration = 1.0f;
        st.Speed    = -1.0f;
        st.Loop     = true;
        st.Scrub(0.25f);
        st.Play();

        st.Advance(0.5f);                       // -0.25 -> wraps to 0.75
        CHECK(st.Time == doctest::Approx(0.75f));
        CHECK(st.Time >= 0.0f);
        CHECK(st.Playing);

        st.Loop = false;
        st.Scrub(0.25f);
        st.Playing = true;
        st.Advance(1.0f);                       // -0.75 -> clamped
        CHECK(st.Time == doctest::Approx(0.0f));
        CHECK_FALSE(st.Playing);
    }

    TEST_CASE("Speed scales the advance and zero freezes without stopping")
    {
        TimelineState st;
        st.Duration = 10.0f;
        st.Loop     = false;
        st.Speed    = 2.5f;
        st.Play();
        st.Advance(1.0f);
        CHECK(st.Time == doctest::Approx(2.5f));

        st.Speed = 0.0f;
        st.Advance(100.0f);
        CHECK(st.Time == doctest::Approx(2.5f));
        CHECK(st.Playing);                      // still "playing", just at 0x
    }

    TEST_CASE("Scrub clamps to the clip in both directions")
    {
        TimelineState st;
        st.Duration = 3.0f;

        st.Scrub(1.5f);
        CHECK(st.Time == doctest::Approx(1.5f));
        st.Scrub(99.0f);
        CHECK(st.Time == doctest::Approx(3.0f));
        st.Scrub(-99.0f);
        CHECK(st.Time == doctest::Approx(0.0f));
        st.Scrub(3.0f);
        CHECK(st.Time == doctest::Approx(3.0f));   // the end is reachable by scrub
    }

    TEST_CASE("Duration <= 0 pins the head at 0 through every entry point")
    {
        for (float duration : { 0.0f, -5.0f })
        {
            TimelineState st;
            st.Duration = duration;
            st.Time     = 7.0f;      // a stale head from a previously loaded clip
            st.Play();

            CHECK(st.Advance(1.0f) == 0.0f);
            CHECK(st.Time == 0.0f);

            st.Scrub(2.0f);
            CHECK(st.Time == 0.0f);

            CHECK(st.Normalized() == 0.0f);   // no divide by zero
            st.SetNormalized(0.5f);
            CHECK(st.Time == 0.0f);
        }
    }

    TEST_CASE("Normalized and SetNormalized are inverses inside the clip")
    {
        TimelineState st;
        st.Duration = 4.0f;

        st.Scrub(1.0f);
        CHECK(st.Normalized() == doctest::Approx(0.25f));
        st.SetNormalized(0.75f);
        CHECK(st.Time == doctest::Approx(3.0f));
        CHECK(st.Normalized() == doctest::Approx(0.75f));

        // Out-of-range normals clamp through Scrub.
        st.SetNormalized(2.0f);
        CHECK(st.Time == doctest::Approx(4.0f));
        st.SetNormalized(-1.0f);
        CHECK(st.Time == doctest::Approx(0.0f));
    }

    TEST_CASE("Stop rewinds and pauses; Pause holds the head")
    {
        TimelineState st;
        st.Duration = 5.0f;
        st.Play();
        st.Advance(2.0f);

        st.Pause();
        CHECK(st.Time == doctest::Approx(2.0f));
        CHECK_FALSE(st.Playing);

        st.Playing = true;
        st.Stop();
        CHECK(st.Time == 0.0f);
        CHECK_FALSE(st.Playing);
    }

    TEST_CASE("a variable frame rate reaches the same head as one big step")
    {
        TimelineState fine, coarse;
        fine.Duration = coarse.Duration = 100.0f;
        fine.Loop = coarse.Loop = false;
        fine.Play();
        coarse.Play();

        for (int i = 0; i < 600; ++i)
            fine.Advance(1.0f / 60.0f);
        coarse.Advance(10.0f);

        CHECK(fine.Time == doctest::Approx(coarse.Time).epsilon(1e-4));
        CHECK(std::isfinite(fine.Time));
    }
}
