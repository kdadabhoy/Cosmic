// test_sockets.cpp — Phase 24 / M4: joint sockets. Headless (no GL): the pose
// palette + GetWorldTransform composition + serializer round-trip. The expected
// world positions are hand-computed, not derived from the implementation.
//
// Rig: a two-joint chain — root at the origin, tip bound 1 m above it — with a
// clip that lifts the tip's LOCAL translation from (0,1,0) to (0,3,0) over 2 s.
// The rig entity sits at world (5,0,0). A "Sword" socketed to "tip" with offset
// (0,0,1) must therefore sit at (5, 1+tipLift, 1).

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"
#endif
#include "scene/SceneSerializer.h"
#include "graphics/Skeleton.h"
#include "graphics/AnimationClip.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

using namespace Cosmic;

namespace
{
    bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f)
    {
        return glm::length(a - b) <= eps;
    }

    glm::vec3 WPos(Scene& s, Entity e)
    {
        return glm::vec3(s.GetWorldTransform(e)[3]);
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

    // Lifts the tip's local translation from (0,1,0) → (0,3,0) over 2 seconds.
    Ref<AnimationClip> MakeLiftClip()
    {
        auto c = std::make_shared<AnimationClip>();
        c->Duration = 2.0f;
        AnimationChannel ch;
        ch.JointIndex = 1;
        ch.PosTimes   = { 0.0f, 2.0f };
        ch.PosValues  = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 3.0f, 0.0f } };
        c->Channels.push_back(ch);
        return c;
    }
}

TEST_SUITE("Joint sockets (M4)")
{
    TEST_CASE("socket = ancestorWorld · jointFrame · offset, at t=0 and mid")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        rig.GetComponent<TransformComponent>().Position = { 5.0f, 0.0f, 0.0f };

        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef         = MakeChain();
        an.ClipRef         = MakeLiftClip();
        an.ClipPath        = "mem";   // == ResolvedClipPath ⇒ UpdateAnimators skips AssetLibrary
        an.ResolvedClipPath = "mem";
        an.Playing         = false;   // the scrubbed NormalizedTime is authoritative
        an.Loop            = false;   // clamp at the ends (so t=2 holds the end pose, not wrap)

        Entity sword = s.CreateEntity("Sword");
        auto& sc = sword.AddComponent<SocketComponent>();
        sc.Joint    = "tip";
        sc.Position = { 0.0f, 0.0f, 1.0f };
        s.SetParent(sword, rig, /*keepWorldPose=*/false);

        // t=0 → tip local (0,1,0) ⇒ joint frame T(0,1,0) ⇒ world (5,1,1).
        rig.GetComponent<AnimatorComponent>().NormalizedTime = 0.0f;
        s.UpdateAnimators(0.0f);
        CHECK(Near(WPos(s, sword), { 5.0f, 1.0f, 1.0f }));

        // mid (t=1) → tip local (0,2,0) ⇒ joint frame T(0,2,0) ⇒ world (5,2,1).
        rig.GetComponent<AnimatorComponent>().NormalizedTime = 0.5f;
        s.UpdateAnimators(0.0f);
        CHECK(Near(WPos(s, sword), { 5.0f, 2.0f, 1.0f }));

        // end (t=2, non-loop) → tip local (0,3,0) ⇒ world (5,3,1).
        rig.GetComponent<AnimatorComponent>().NormalizedTime = 1.0f;
        s.UpdateAnimators(0.0f);
        CHECK(Near(WPos(s, sword), { 5.0f, 3.0f, 1.0f }));
    }

    TEST_CASE("a rotation+scale offset composes correctly")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef          = MakeChain();
        an.ClipRef          = MakeLiftClip();
        an.ClipPath         = "mem";
        an.ResolvedClipPath = "mem";
        an.Playing          = false;
        an.NormalizedTime   = 0.0f;   // tip at (0,1,0)

        Entity prop = s.CreateEntity("Prop");
        auto& sc = prop.AddComponent<SocketComponent>();
        sc.Joint    = "tip";
        sc.Position = { 1.0f, 0.0f, 0.0f };
        sc.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));   // +X → +Y
        s.SetParent(prop, rig, false);
        s.UpdateAnimators(0.0f);

        // Joint frame is a pure translation T(0,1,0); the offset's translation
        // (1,0,0) is unrotated by the joint (identity rotation) ⇒ prop at (1,1,0).
        CHECK(Near(WPos(s, prop), { 1.0f, 1.0f, 0.0f }));
        // A local +X axis at the prop is rotated to +Y by the socket rotation.
        const glm::mat4 w = s.GetWorldTransform(prop);
        const glm::vec3 xAxis = glm::normalize(glm::vec3(w[0]));
        CHECK(Near(xAxis, { 0.0f, 1.0f, 0.0f }, 1e-4f));
    }

    TEST_CASE("socket follows the bind pose when the rig has a skeleton but no clip")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef = MakeChain();   // no ClipRef; ClipPath "" == ResolvedClipPath "" ⇒ stays null

        Entity sock = s.CreateEntity("Sock");
        auto& sc = sock.AddComponent<SocketComponent>();
        sc.Joint = "tip";
        s.SetParent(sock, rig, false);

        s.UpdateAnimators(0.0f);   // bind-pose joint frames published
        CHECK(Near(WPos(s, sock), { 0.0f, 1.0f, 0.0f }));   // bind tip
    }

    TEST_CASE("socket without an animating ancestor is a plain child (compat)")
    {
        Scene s;
        Entity parent = s.CreateEntity("Parent");
        parent.GetComponent<TransformComponent>().Position = { 3.0f, 0.0f, 0.0f };

        Entity child = s.CreateEntity("Child");
        child.GetComponent<TransformComponent>().Position = { 0.0f, 1.0f, 0.0f };
        auto& sc = child.AddComponent<SocketComponent>();
        sc.Joint    = "hand.r";
        sc.Position = { 9.0f, 9.0f, 9.0f };   // ignored while unresolved
        s.SetParent(child, parent, false);

        // No animator ⇒ the socket does not resolve ⇒ ordinary parent·local.
        CHECK(Near(WPos(s, child), { 3.0f, 1.0f, 0.0f }));
    }

    TEST_CASE("unknown joint name falls back to the ordinary transform")
    {
        Scene s;
        Entity rig = s.CreateEntity("Rig");
        auto& an = rig.AddComponent<AnimatorComponent>();
        an.SkelRef = MakeChain();

        Entity child = s.CreateEntity("Child");
        child.GetComponent<TransformComponent>().Position = { 0.0f, 4.0f, 0.0f };
        child.AddComponent<SocketComponent>().Joint = "does_not_exist";
        s.SetParent(child, rig, false);
        s.UpdateAnimators(0.0f);

        CHECK(Near(WPos(s, child), { 0.0f, 4.0f, 0.0f }));   // its own local
    }

    TEST_CASE("SocketComponent serializes + round-trips")
    {
        Scene s;
        Entity e = s.CreateEntity("Sword");
        auto& sc = e.AddComponent<SocketComponent>();
        sc.Joint    = "hand.r";
        sc.Position = { 0.1f, 0.2f, 0.3f };
        sc.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
        sc.Scale    = { 2.0f, 2.0f, 2.0f };

        const std::string text = SceneSerializer::SaveToString(s);
        CHECK(text.find("Socket") != std::string::npos);
        CHECK(text.find("hand.r") != std::string::npos);

        Scene s2;
        REQUIRE(SceneSerializer::LoadFromString(s2, text));
        bool found = false;
        for (auto ent : s2.GetRegistry().view<SocketComponent>())
        {
            const auto& r = s2.GetRegistry().get<SocketComponent>(ent);
            CHECK(r.Joint == "hand.r");
            CHECK(r.Position.x == doctest::Approx(0.1f));
            CHECK(r.Position.z == doctest::Approx(0.3f));
            CHECK(r.Scale.y == doctest::Approx(2.0f));
            found = true;
        }
        CHECK(found);
    }
}
