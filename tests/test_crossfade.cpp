// test_crossfade.cpp — Phase 24 / M6: the Animator crossfade tier. Headless (no
// GL): the pose-space blend math (AnimationClip::BlendLocals) and the end-to-end
// fade through Scene::UpdateAnimators — the pose at t=0 / half / end is the
// expected lerp, and the fade PROMOTES the next clip when it completes.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif
#include "graphics/Skeleton.h"
#include "graphics/AnimationClip.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

using namespace Cosmic;

namespace
{
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f)
    {
        return glm::length(a - b) <= eps;
    }
    glm::vec3 XformPoint(const glm::mat4& m, const glm::vec3& p)
    {
        return glm::vec3(m * glm::vec4(p, 1.0f));
    }

    Ref<Skeleton> MakeChain()
    {
        auto s = std::make_shared<Skeleton>();
        SkeletonJoint root; root.Name = "root";
        s->Joints.push_back(root);
        SkeletonJoint tip;
        tip.Name        = "tip";
        tip.Parent      = 0;
        tip.LocalBind   = glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, 0.0f });
        tip.InverseBind = glm::translate(glm::mat4(1.0f), { 0.0f, -1.0f, 0.0f });
        s->Joints.push_back(tip);
        return s;
    }

    // A clip that holds the tip's local translation at (0, y, 0) (one key).
    Ref<AnimationClip> ConstClip(float y)
    {
        auto c = std::make_shared<AnimationClip>();
        c->Duration = 1.0f;
        AnimationChannel ch;
        ch.JointIndex = 1;
        ch.PosTimes   = { 0.0f };
        ch.PosValues  = { { 0.0f, y, 0.0f } };
        c->Channels.push_back(ch);
        return c;
    }
}

TEST_SUITE("Animator crossfade (M6)")
{
    TEST_CASE("BlendLocals lerps translation and slerps rotation")
    {
        std::vector<glm::mat4> a = { glm::translate(glm::mat4(1.0f), { 0.0f, 1.0f, 0.0f }) };
        std::vector<glm::mat4> b = { glm::translate(glm::mat4(1.0f), { 0.0f, 3.0f, 0.0f }) };
        std::vector<glm::mat4> out;

        AnimationClip::BlendLocals(a, b, 0.0f, out);
        CHECK(Near(glm::vec3(out[0][3]), { 0.0f, 1.0f, 0.0f }));
        AnimationClip::BlendLocals(a, b, 0.5f, out);
        CHECK(Near(glm::vec3(out[0][3]), { 0.0f, 2.0f, 0.0f }));
        AnimationClip::BlendLocals(a, b, 1.0f, out);
        CHECK(Near(glm::vec3(out[0][3]), { 0.0f, 3.0f, 0.0f }));

        // Rotation: 0° → 90° about Z, blended halfway = 45°.
        std::vector<glm::mat4> ra = { glm::mat4(1.0f) };
        std::vector<glm::mat4> rb = { glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1))) };
        AnimationClip::BlendLocals(ra, rb, 0.5f, out);
        const glm::vec3 x = glm::vec3(out[0] * glm::vec4(1, 0, 0, 1));
        const float r22 = std::sqrt(2.0f) / 2.0f;
        CHECK(Near(x, { r22, r22, 0.0f }));
    }

    TEST_CASE("a 0.3 s fade blends the pose at t=0 / half / end, then promotes")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef          = MakeChain();
        an.ClipRef          = ConstClip(1.0f);     // clip A: tip at (0,1,0) == bind
        an.ClipPath         = "A";
        an.ResolvedClipPath = "A";
        an.Playing          = true;

        // Wire the crossfade to clip B directly (skip AssetLibrary): fade over 0.3 s.
        an.NextClipRef          = ConstClip(3.0f);  // clip B: tip at (0,3,0)
        an.NextClipPath         = "B";
        an.ResolvedNextClipPath = "B";
        an.FadeDuration         = 0.3f;
        an.FadeElapsed          = 0.0f;

        // t=0 (w=0) → pure clip A: bind tip (0,1,0) stays at (0,1,0).
        s.UpdateAnimators(0.0f);
        REQUIRE(an.Palette.size() == 2);
        CHECK(Near(XformPoint(an.Palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f }));

        // half (w=0.5) → blended tip local (0,2,0): moves bind tip to (0,2,0).
        s.UpdateAnimators(0.15f);
        CHECK(Near(XformPoint(an.Palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, 2.0f, 0.0f }));

        // end (w>=1) → promoted to clip B: moves bind tip to (0,3,0).
        s.UpdateAnimators(0.15f);
        CHECK(Near(XformPoint(an.Palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, 3.0f, 0.0f }));

        // The fade completed → clip B is now the current clip, no pending fade.
        CHECK(an.ClipPath == "B");
        CHECK(an.NextClipPath.empty());
        CHECK(an.FadeDuration == doctest::Approx(0.0f));
    }

    TEST_CASE("CrossfadeTo with seconds<=0 hard-switches; same/empty clip cancels")
    {
        AnimatorComponent an;
        an.ClipPath = "Idle";

        an.CrossfadeTo("Walk", 0.0f);              // hard switch
        CHECK(an.ClipPath == "Walk");
        CHECK(an.NextClipPath.empty());
        CHECK(an.FadeDuration == doctest::Approx(0.0f));

        an.CrossfadeTo("Run", 0.3f);               // start a fade
        CHECK(an.NextClipPath == "Run");
        CHECK(an.FadeDuration == doctest::Approx(0.3f));

        an.CrossfadeTo("Walk", 0.3f);              // same as current → cancel the fade
        CHECK(an.NextClipPath.empty());
        CHECK(an.FadeDuration == doctest::Approx(0.0f));
    }

    TEST_CASE("a paused animator does not advance the fade")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef              = MakeChain();
        an.ClipRef              = ConstClip(1.0f);
        an.ClipPath             = "A";
        an.ResolvedClipPath     = "A";
        an.Playing              = false;           // paused
        an.NextClipRef          = ConstClip(3.0f);
        an.NextClipPath         = "B";
        an.ResolvedNextClipPath = "B";
        an.FadeDuration         = 0.3f;

        s.UpdateAnimators(1.0f);   // a big dt, but paused → fade frozen at w=0
        CHECK(an.FadeElapsed == doctest::Approx(0.0f));
        CHECK(Near(XformPoint(an.Palette[1], { 0.0f, 1.0f, 0.0f }), { 0.0f, 1.0f, 0.0f }));  // still clip A
    }
}
