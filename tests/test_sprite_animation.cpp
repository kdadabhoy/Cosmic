// test_sprite_animation.cpp — flipbook sprite animation (Phase 29 W2 / §9.2).
//
// Headless (no GL): SpriteAnimationComponent::SelectFrame / FrameUV are pure, and
// Scene::UpdateSpriteAnimations' time accumulation runs without a texture (the
// sheet resolve is skipped when the atlas is unavailable, which is exactly the
// headless case — the sprite keeps its last SourceRect and only Elapsed moves).
//
// The U4 pass had a round-trip + draw test but no coverage of the wrap/clamp
// edges; this is the net that keeps them fixed while Scene.cpp is split (W5).

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <glm/glm.hpp>

using namespace Cosmic;

TEST_SUITE("Sprite animation (U4) — SelectFrame")
{
    TEST_CASE("a looping clip advances one frame per 1/FPS and wraps")
    {
        const int   frames = 4;
        const float fps    = 8.0f;   // 0.125 s per frame

        CHECK(SpriteAnimationComponent::SelectFrame(0.0f,    fps, frames, true) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(0.124f,  fps, frames, true) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(0.125f,  fps, frames, true) == 1);
        CHECK(SpriteAnimationComponent::SelectFrame(0.375f,  fps, frames, true) == 3);
        CHECK(SpriteAnimationComponent::SelectFrame(0.5f,    fps, frames, true) == 0);   // wrapped
        CHECK(SpriteAnimationComponent::SelectFrame(0.625f,  fps, frames, true) == 1);
        CHECK(SpriteAnimationComponent::SelectFrame(100.0f,  fps, frames, true) == 0);   // 800 % 4
    }

    TEST_CASE("a one-shot clip clamps to the last frame and stays there")
    {
        const int   frames = 3;
        const float fps    = 10.0f;

        CHECK(SpriteAnimationComponent::SelectFrame(0.0f,   fps, frames, false) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(0.15f,  fps, frames, false) == 1);
        CHECK(SpriteAnimationComponent::SelectFrame(0.25f,  fps, frames, false) == 2);
        CHECK(SpriteAnimationComponent::SelectFrame(0.99f,  fps, frames, false) == 2);   // clamped
        CHECK(SpriteAnimationComponent::SelectFrame(1e6f,   fps, frames, false) == 2);
    }

    TEST_CASE("negative time (a reversed clip) wraps forward when looping, clamps otherwise")
    {
        const int   frames = 5;
        const float fps    = 4.0f;   // 0.25 s per frame

        // Looping: the double modulo maps a negative index back into [0, frames).
        CHECK(SpriteAnimationComponent::SelectFrame(-0.25f, fps, frames, true) == 4);
        CHECK(SpriteAnimationComponent::SelectFrame(-0.5f,  fps, frames, true) == 3);
        CHECK(SpriteAnimationComponent::SelectFrame(-1.25f, fps, frames, true) == 0);   // -5 % 5

        // One-shot: never below frame 0.
        CHECK(SpriteAnimationComponent::SelectFrame(-0.25f, fps, frames, false) == 0);
        CHECK(SpriteAnimationComponent::SelectFrame(-999.0f, fps, frames, false) == 0);
    }

    TEST_CASE("degenerate clips pin to frame 0 instead of dividing or modding by zero")
    {
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f,  8.0f,  0, true)  == 0);   // no frames
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f,  8.0f,  1, true)  == 0);   // single frame
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f,  8.0f, -4, true)  == 0);   // negative count
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f,  0.0f,  6, true)  == 0);   // stopped clock
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f, -8.0f,  6, true)  == 0);   // negative FPS
        CHECK(SpriteAnimationComponent::SelectFrame(3.0f,  0.0f,  6, false) == 0);
    }
}

TEST_SUITE("Sprite animation (U4) — FrameUV")
{
    TEST_CASE("frames walk the row left to right in normalized, top-left-origin UV")
    {
        // A 64x32 sheet of 16x16 cells: 4 columns, 2 rows.
        const glm::vec4 f0 = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, 0);
        CHECK(f0.x == doctest::Approx(0.0f));
        CHECK(f0.y == doctest::Approx(0.0f));    // row 0 = the TOP of the sheet
        CHECK(f0.z == doctest::Approx(0.25f));
        CHECK(f0.w == doctest::Approx(0.5f));

        const glm::vec4 f3 = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, 3);
        CHECK(f3.x == doctest::Approx(0.75f));
        CHECK(f3.z == doctest::Approx(1.0f));

        // Row 1 is the lower half.
        const glm::vec4 r1 = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 1, 1);
        CHECK(r1.x == doctest::Approx(0.25f));
        CHECK(r1.y == doctest::Approx(0.5f));
        CHECK(r1.w == doctest::Approx(1.0f));

        // Every cell is exactly one cell wide/tall.
        for (int i = 0; i < 4; ++i)
        {
            const glm::vec4 f = SpriteAnimationComponent::FrameUV(64, 32, 16, 16, 0, i);
            CHECK((f.z - f.x) == doctest::Approx(0.25f));
            CHECK((f.w - f.y) == doctest::Approx(0.5f));
        }
    }

    TEST_CASE("a degenerate sheet or cell size falls back to the whole image")
    {
        for (glm::vec4 uv : { SpriteAnimationComponent::FrameUV(0, 32, 16, 16, 0, 0),
                              SpriteAnimationComponent::FrameUV(64, 0, 16, 16, 0, 0),
                              SpriteAnimationComponent::FrameUV(64, 32,  0, 16, 0, 0),
                              SpriteAnimationComponent::FrameUV(64, 32, 16,  0, 0, 0) })
        {
            CHECK(uv.x == 0.0f);
            CHECK(uv.y == 0.0f);
            CHECK(uv.z == 1.0f);
            CHECK(uv.w == 1.0f);
        }
    }
}

TEST_SUITE("Sprite animation (U4) — Scene::UpdateSpriteAnimations")
{
    TEST_CASE("Elapsed accumulates across variable dt and only while Playing")
    {
        Scene s;
        Entity e = s.CreateEntity("hero");
        e.AddComponent<SpriteRendererComponent>();
        auto& anim = e.AddComponent<SpriteAnimationComponent>();
        anim.Frames = 4; anim.FPS = 8.0f; anim.Playing = true;

        // A ragged frame-time sequence: the accumulation must be the plain sum.
        float expected = 0.0f;
        for (float dt : { 1.0f / 60.0f, 1.0f / 30.0f, 0.004f, 1.0f / 144.0f, 0.25f })
        {
            s.UpdateSpriteAnimations(dt);
            expected += dt;
        }
        CHECK(anim.Elapsed == doctest::Approx(expected));

        // Paused: the clock stops dead.
        anim.Playing = false;
        const float held = anim.Elapsed;
        s.UpdateSpriteAnimations(1.0f);
        s.UpdateSpriteAnimations(1.0f);
        CHECK(anim.Elapsed == held);

        // Resumed: it picks up where it left off, and the frame follows Elapsed.
        anim.Playing = true;
        s.UpdateSpriteAnimations(0.5f);
        CHECK(anim.Elapsed == doctest::Approx(held + 0.5f));
        CHECK(SpriteAnimationComponent::SelectFrame(anim.Elapsed, anim.FPS, anim.Frames, anim.Loop)
              == (int)((int)(anim.Elapsed * anim.FPS) % anim.Frames));
    }

    TEST_CASE("the SourceRect is left alone when the sheet cannot be resolved")
    {
        // Headless: AssetLibrary has no GL texture to hand back, so the pass must
        // leave the authored rect untouched rather than writing a garbage UV.
        Scene s;
        Entity e = s.CreateEntity("hero");
        auto& sr = e.AddComponent<SpriteRendererComponent>();
        sr.SourceRect = { 0.1f, 0.2f, 0.3f, 0.4f };
        auto& anim = e.AddComponent<SpriteAnimationComponent>();
        anim.SheetPath = "";               // nothing to resolve
        anim.Frames = 4; anim.FPS = 8.0f;

        s.UpdateSpriteAnimations(1.0f);
        CHECK(sr.SourceRect.x == doctest::Approx(0.1f));
        CHECK(sr.SourceRect.y == doctest::Approx(0.2f));
        CHECK(sr.SourceRect.z == doctest::Approx(0.3f));
        CHECK(sr.SourceRect.w == doctest::Approx(0.4f));
        CHECK(anim.Elapsed == doctest::Approx(1.0f));   // the clock still ran
    }

    TEST_CASE("a scene with no sprite animations is untouched (compat gate)")
    {
        Scene s;
        Entity e = s.CreateEntity("plain");
        auto& sr = e.AddComponent<SpriteRendererComponent>();
        sr.SourceRect = { 0.0f, 0.0f, 1.0f, 1.0f };
        s.UpdateSpriteAnimations(1.0f);
        CHECK(sr.SourceRect.z == 1.0f);
    }
}
