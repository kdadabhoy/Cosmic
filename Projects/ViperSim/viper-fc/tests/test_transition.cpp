// test_transition.cpp — mode-machine transition table (doc 04 §5).

#include "doctest.h"
#include "viperfc/Transition.h"

using namespace viperfc;

TEST_CASE("forward transition: ACCEL -> BLEND -> CRUISE as airspeed builds")
{
    FcParams p;
    TransitionMachine tm;
    tm.StartForward(0.0f);

    const float dt = 1.0f / 240.0f;
    float airspeed = 0.0f;
    bool sawAccel = false, sawBlend = false, done = false;

    for (int i = 0; i < 240 * 10 && !done; ++i)
    {
        airspeed += 2.5f * dt;   // steady acceleration to ~25 m/s
        const auto s = tm.Update(airspeed, p, dt);
        if (s.phase == TransitionPhase::Accel) sawAccel = true;
        if (s.phase == TransitionPhase::Blend) sawBlend = true;
        if (s.done) done = true;

        CHECK(s.blend >= 0.0f);
        CHECK(s.blend <= 1.0f);
    }

    CHECK(sawAccel);
    CHECK(sawBlend);
    CHECK(done);
    CHECK_FALSE(tm.Active());
}

TEST_CASE("forward transition: blend is monotonic through gust lulls")
{
    FcParams p;
    TransitionMachine tm;
    tm.StartForward(0.0f);

    const float dt = 1.0f / 240.0f;
    float prevBlend = 0.0f;
    for (int i = 0; i < 240 * 4; ++i)
    {
        // Airspeed oscillates while trending up (gusty acceleration).
        const float t = i * dt;
        const float airspeed = 3.0f * t + 2.0f * std::sin(t * 9.0f);
        const auto s = tm.Update(airspeed, p, dt);
        CHECK(s.blend >= prevBlend);   // never snaps back
        prevBlend = s.blend;
        if (s.done) break;
    }
}

TEST_CASE("forward transition aborts when airspeed never builds")
{
    FcParams p;
    TransitionMachine tm;
    tm.StartForward(0.0f);

    const float dt = 1.0f / 240.0f;
    bool aborted = false;
    for (int i = 0; i < 240 * 20 && !aborted; ++i)
        aborted = tm.Update(2.0f /* stuck slow */, p, dt).aborted;

    CHECK(aborted);
    CHECK_FALSE(tm.Active());
}

TEST_CASE("back transition: DECEL -> FLARE as airspeed decays")
{
    FcParams p;
    TransitionMachine tm;
    tm.StartBack(0.0f);

    const float dt = 1.0f / 240.0f;
    float airspeed = 20.0f;
    bool sawDecel = false, sawFlare = false, done = false;
    float prevBlend = 1.0f;

    for (int i = 0; i < 240 * 10 && !done; ++i)
    {
        airspeed = airspeed > 0.0f ? airspeed - 4.0f * dt : 0.0f;
        const auto s = tm.Update(airspeed, p, dt);
        if (s.phase == TransitionPhase::Decel) sawDecel = true;
        if (s.phase == TransitionPhase::Flare) sawFlare = true;
        CHECK(s.blend <= prevBlend);   // min-latched on the way back
        prevBlend = s.blend;
        if (s.done) done = true;
    }

    CHECK(sawDecel);
    CHECK(sawFlare);
    CHECK(done);
    CHECK(prevBlend == doctest::Approx(0.0f));
}
