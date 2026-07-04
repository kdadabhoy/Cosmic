// test_hierarchy.cpp — parent/child hierarchy + world transforms (Phase 13 / E3).
// Headless: pure transform math + entt, no GL.
//
// Acceptance (plan doc 11 E3): three-deep chain world position; reparent keeps
// world pose within 1e-4 absolute; destroy-with-children; cycle refused; flat
// entities render from their local transform (compat).

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneSerializer.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace Cosmic;

static bool Mat4Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (std::abs(a[c][r] - b[c][r]) > eps)
                return false;
    return true;
}

static glm::vec3 WorldPos(Scene& s, Entity e)
{
    const glm::mat4 m = s.GetWorldTransform(e);
    return glm::vec3(m[3]);
}

TEST_CASE("E3: three-deep chain composes world position")
{
    Scene s;
    Entity a = s.CreateEntity("A");
    Entity b = s.CreateEntity("B");
    Entity c = s.CreateEntity("C");

    s.SetParent(b, a, /*keepWorldPose=*/false);
    s.SetParent(c, b, /*keepWorldPose=*/false);

    a.GetComponent<TransformComponent>().Position = { 10.0f, 0.0f, 0.0f };
    b.GetComponent<TransformComponent>().Position = { 5.0f, 0.0f, 0.0f };  // local
    c.GetComponent<TransformComponent>().Position = { 0.0f, 3.0f, 0.0f };  // local

    const glm::vec3 w = WorldPos(s, c);
    CHECK(w.x == doctest::Approx(15.0f));
    CHECK(w.y == doctest::Approx(3.0f));
    CHECK(w.z == doctest::Approx(0.0f));
}

TEST_CASE("E3: reparent with keepWorldPose preserves world transform (rot+scale)")
{
    Scene s;
    Entity p = s.CreateEntity("Parent");
    auto& pt = p.GetComponent<TransformComponent>();
    pt.Position = { 0.0f, 10.0f, 0.0f };
    pt.Rotation = { 0.0f, 45.0f, 0.0f };
    pt.Scale    = { 2.0f, 2.0f, 2.0f };

    Entity x = s.CreateEntity("X");
    auto& xt = x.GetComponent<TransformComponent>();
    xt.Position = { 3.0f, 4.0f, 5.0f };
    xt.Rotation = { 0.0f, 0.0f, 30.0f };

    const glm::mat4 before = s.GetWorldTransform(x);
    REQUIRE(s.SetParent(x, p, /*keepWorldPose=*/true));
    const glm::mat4 after = s.GetWorldTransform(x);

    CHECK(Mat4Near(before, after, 1e-4f));
    // And it really is parented now.
    CHECK(x.GetComponent<RelationshipComponent>().Parent == p.GetComponent<IDComponent>().ID);
}

TEST_CASE("E3: destroy-with-children removes the subtree; orphan mode keeps them")
{
    // destroyChildren = true (default): the whole subtree goes.
    {
        Scene s;
        Entity root  = s.CreateEntity("root");
        Entity child = s.CreateEntity("child");
        Entity grand = s.CreateEntity("grand");
        s.SetParent(child, root, false);
        s.SetParent(grand, child, false);

        const UUID rootID = root.GetComponent<IDComponent>().ID;
        const UUID childID = child.GetComponent<IDComponent>().ID;
        const UUID grandID = grand.GetComponent<IDComponent>().ID;

        s.DestroyEntity(root, /*destroyChildren=*/true);
        CHECK_FALSE(s.FindByUUID(rootID));
        CHECK_FALSE(s.FindByUUID(childID));
        CHECK_FALSE(s.FindByUUID(grandID));
    }

    // destroyChildren = false: children survive, detached to root.
    {
        Scene s;
        Entity root  = s.CreateEntity("root");
        Entity child = s.CreateEntity("child");
        s.SetParent(child, root, false);
        const UUID childID = child.GetComponent<IDComponent>().ID;

        s.DestroyEntity(root, /*destroyChildren=*/false);
        Entity survivor = s.FindByUUID(childID);
        REQUIRE(survivor);
        CHECK_FALSE(survivor.GetComponent<RelationshipComponent>().Parent.IsValid());
    }
}

TEST_CASE("E3: cycles are refused")
{
    Scene s;
    Entity a = s.CreateEntity("A");
    Entity b = s.CreateEntity("B");
    REQUIRE(s.SetParent(b, a, false));      // B under A

    CHECK_FALSE(s.SetParent(a, b));         // A under its own descendant -> refused
    CHECK_FALSE(s.SetParent(a, a));         // self-parent -> refused

    // The valid relationship is intact.
    CHECK(b.GetComponent<RelationshipComponent>().Parent == a.GetComponent<IDComponent>().ID);
    CHECK_FALSE(a.GetComponent<RelationshipComponent>().Parent.IsValid());
}

TEST_CASE("E3: flat entities use their local transform and gain no components")
{
    Scene s;
    Entity e = s.CreateEntity("Flat");
    auto& t = e.GetComponent<TransformComponent>();
    t.Position = { 1.0f, 2.0f, 3.0f };
    t.Rotation = { 10.0f, 20.0f, 30.0f };
    t.Scale    = { 2.0f, 1.0f, 0.5f };

    CHECK(Mat4Near(s.GetWorldTransform(e), t.GetTransform()));
    CHECK_FALSE(e.HasComponent<RelationshipComponent>());   // compat: nothing added
}

TEST_CASE("E3: hierarchy survives serialization with children order preserved")
{
    Scene s;
    Entity parent = s.CreateEntity("Parent");
    Entity c1 = s.CreateEntity("C1");
    Entity c2 = s.CreateEntity("C2");
    Entity c3 = s.CreateEntity("C3");

    s.SetParent(c1, parent, false);
    s.SetParent(c2, parent, false);
    s.SetParent(c3, parent, false);

    parent.GetComponent<TransformComponent>().Position = { 0.0f, 5.0f, 0.0f };
    c1.GetComponent<TransformComponent>().Position = { 1.0f, 0.0f, 0.0f };
    c2.GetComponent<TransformComponent>().Position = { 2.0f, 0.0f, 0.0f };
    c3.GetComponent<TransformComponent>().Position = { 3.0f, 0.0f, 0.0f };

    const UUID parentID = parent.GetComponent<IDComponent>().ID;
    const UUID c1ID = c1.GetComponent<IDComponent>().ID;
    const UUID c2ID = c2.GetComponent<IDComponent>().ID;
    const UUID c3ID = c3.GetComponent<IDComponent>().ID;
    const glm::vec3 c2WorldBefore = WorldPos(s, c2);

    const std::string save1 = SceneSerializer::SaveToString(s);

    Scene s2;
    REQUIRE(SceneSerializer::LoadFromString(s2, save1));

    Entity p2 = s2.FindByUUID(parentID);
    REQUIRE(p2);
    const auto& kids = p2.GetComponent<RelationshipComponent>().Children;
    REQUIRE(kids.size() == 3);
    CHECK(kids[0] == c1ID);
    CHECK(kids[1] == c2ID);
    CHECK(kids[2] == c3ID);

    Entity c2b = s2.FindByUUID(c2ID);
    REQUIRE(c2b);
    CHECK(c2b.GetComponent<RelationshipComponent>().Parent == parentID);

    // World transform is preserved through the round-trip.
    const glm::vec3 c2WorldAfter = WorldPos(s2, c2b);
    CHECK(c2WorldAfter.x == doctest::Approx(c2WorldBefore.x));
    CHECK(c2WorldAfter.y == doctest::Approx(c2WorldBefore.y));

    // And save/load/save is byte-identical.
    const std::string save2 = SceneSerializer::SaveToString(s2);
    CHECK(save1 == save2);
}
