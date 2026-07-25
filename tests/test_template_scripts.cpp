// test_template_scripts.cpp — compile smoke for the U8 sample scripts.
//
// The ForgePong template scripts (PaddleController / PongBall) only compile
// when a scaffolded project builds (the user's Ctrl+B) — so a template typo
// would surface on the USER's machine. Including them here builds them against
// the same SDK headers a project would, headless. `@PROJECT_NAME@` appears
// only in their comments, so the files compile verbatim.

#include <doctest.h>

#include "../Projects/Starforge/assets/templates/src/scripts/PaddleController.h"
#include "../Projects/Starforge/assets/templates/src/scripts/PongBall.h"
#include "../Projects/Starforge/assets/templates/src/scripts/StoryUiBinding.h"   // Q3
#ifndef COSMIC_2D_ONLY
#include "../Projects/Starforge/assets/templates/src/scripts/NavCritter.h"       // N5 — Nav() proxy, 3D only
#endif

TEST_CASE("U8: ForgePong template scripts compile and expose their tuned fields")
{
    PaddleController paddle;
    CHECK(paddle.Speed > 0.0f);
    CHECK(paddle.LimitY > 0.0f);
    CHECK_FALSE(paddle.UseArrows);   // left player default; the right one overrides

    PongBall ball;
    CHECK(ball.WinScore == 5);
    CHECK(ball.Speed > 0.0f);
    CHECK(ball.SpeedUp >= 1.0f);
    CHECK(ball.CourtHalfW > ball.CourtHalfH);   // a pong court is wide
}

TEST_CASE("Q3: the stock Story Graph UI binding template compiles")
{
    StoryUiBinding b;
    CHECK(StoryUiBinding::MaxOptions == 4);
    CHECK(b.TextTag == "StoryText");
    CHECK(b.OptionTagPrefix == "StoryOption");
}

#ifndef COSMIC_2D_ONLY
TEST_CASE("N5: the NavCritter sample system script compiles and exposes its fields")
{
    NavCritter critter;
    CHECK(critter.ChaseRadius > 0.0f);
    CHECK(critter.PatrolRadius > 0.0f);
}
#endif
