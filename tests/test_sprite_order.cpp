// test_sprite_order.cpp — the 2D painter order (Phase 29 W2 / §9.2).
//
// Headless (no GL): drives Scene::BuildSpriteDrawList — the pure list builder
// extracted from Scene::OnRenderSprites in W2. That extraction is the ONLY way
// the sprite/tilemap draw order can be asserted without a GPU, and this suite is
// the regression net that keeps the order identical as the 2D and 3D halves of
// Scene.cpp are pulled apart (W5).
//
// The invariant under test: ascending (ZOrder, key, entity id), where the key is
// -Position.y for a YSort sprite, Position.z otherwise, and a tilemap always
// keys on Position.z. Disabled sprites (T12) and entities under an inactive
// ancestor (T13) never appear.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    // A sprite entity at (x, y, z) with the given sort settings.
    Entity MakeSprite(Scene& s, const std::string& name, glm::vec3 pos,
                      int32_t zOrder, bool ySort = false)
    {
        Entity e = s.CreateEntity(name);
        e.GetComponent<TransformComponent>().Position = pos;
        auto& sr  = e.AddComponent<SpriteRendererComponent>();
        sr.ZOrder = zOrder;
        sr.YSort  = ySort;
        return e;
    }

    Entity MakeTilemap(Scene& s, const std::string& name, glm::vec3 pos, int32_t zOrder)
    {
        Entity e = s.CreateEntity(name);
        e.GetComponent<TransformComponent>().Position = pos;
        auto& tm  = e.AddComponent<TilemapComponent>();
        tm.ZOrder = zOrder;
        return e;
    }

    // The draw list as entity-name strings, so the assertions read as the order
    // a human would see on screen (back to front).
    std::vector<std::string> Order(Scene& s)
    {
        std::vector<std::string> out;
        for (const auto& item : s.BuildSpriteDrawList())
            out.push_back(s.GetRegistry().get<TagComponent>(item.E).Tag);
        return out;
    }
}

TEST_SUITE("2D painter order (W2 / BuildSpriteDrawList)")
{
    TEST_CASE("ZOrder is the primary key and beats every position term")
    {
        Scene s;
        // Created in the WRONG order and with positions that would sort the other
        // way on Z, so only ZOrder can produce the expected result.
        MakeSprite(s, "front", { 0, 0, -100.0f }, 10);
        MakeSprite(s, "back",  { 0, 0,  100.0f }, -10);
        MakeSprite(s, "mid",   { 0, 0,    0.0f },  0);

        const auto order = Order(s);
        REQUIRE(order.size() == 3u);
        CHECK(order[0] == "back");
        CHECK(order[1] == "mid");
        CHECK(order[2] == "front");
    }

    TEST_CASE("within one ZOrder, the default key is Position.z")
    {
        Scene s;
        MakeSprite(s, "far",  { 0, 0, 5.0f }, 0);
        MakeSprite(s, "near", { 0, 0, 1.0f }, 0);

        const auto order = Order(s);
        REQUIRE(order.size() == 2u);
        CHECK(order[0] == "near");   // smaller z draws first
        CHECK(order[1] == "far");
    }

    TEST_CASE("YSort keys on -Position.y so a lower sprite draws in front")
    {
        Scene s;
        // Same ZOrder, same z — only Y differs. The top-down convention: the
        // sprite with the SMALLER y is closer to the viewer and must draw LAST.
        MakeSprite(s, "top",    { 0,  4.0f, 0 }, 0, /*ySort*/ true);
        MakeSprite(s, "bottom", { 0, -2.0f, 0 }, 0, /*ySort*/ true);
        MakeSprite(s, "middle", { 0,  1.0f, 0 }, 0, /*ySort*/ true);

        const auto order = Order(s);
        REQUIRE(order.size() == 3u);
        CHECK(order[0] == "top");      // key = -4
        CHECK(order[1] == "middle");   // key = -1
        CHECK(order[2] == "bottom");   // key = +2

        // The key really is the NEGATED y: a YSort sprite and a plain one with
        // key == -y interleave as equals (then tie-break by entity id).
        Scene s2;
        Entity a = MakeSprite(s2, "ysort", { 0, 3.0f, 0.0f }, 0, /*ySort*/ true);   // key -3
        Entity b = MakeSprite(s2, "plain", { 0, 0.0f, -3.0f }, 0, /*ySort*/ false); // key -3
        const auto tied = Order(s2);
        REQUIRE(tied.size() == 2u);
        CHECK((uint32_t)(entt::entity)a < (uint32_t)(entt::entity)b);
        CHECK(tied[0] == "ysort");     // equal keys -> lower entity id first
        CHECK(tied[1] == "plain");
    }

    TEST_CASE("tilemaps interleave with sprites through the same keys")
    {
        Scene s;
        MakeSprite(s,  "sprite_bg", { 0, 0, 0 }, -5);
        MakeTilemap(s, "map_ground",{ 0, 0, 0 }, -1);
        MakeSprite(s,  "sprite_fg", { 0, 0, 0 },  3);
        MakeTilemap(s, "map_roof",  { 0, 0, 0 },  9);

        const auto order = Order(s);
        REQUIRE(order.size() == 4u);
        CHECK(order[0] == "sprite_bg");
        CHECK(order[1] == "map_ground");
        CHECK(order[2] == "sprite_fg");
        CHECK(order[3] == "map_roof");

        // The Map flag distinguishes the two kinds for the draw loop.
        const auto items = s.BuildSpriteDrawList();
        CHECK_FALSE(items[0].Map);
        CHECK(items[1].Map);
        CHECK_FALSE(items[2].Map);
        CHECK(items[3].Map);

        // A tilemap always keys on Position.z, never on -y (it has no YSort).
        Scene s2;
        MakeTilemap(s2, "high_y", { 0, 100.0f, 2.0f }, 0);
        MakeTilemap(s2, "low_y",  { 0,   0.0f, 1.0f }, 0);
        const auto byZ = Order(s2);
        CHECK(byZ[0] == "low_y");
        CHECK(byZ[1] == "high_y");
    }

    TEST_CASE("T12: a disabled sprite is excluded from the list")
    {
        Scene s;
        Entity off = MakeSprite(s, "off", { 0, 0, 0 }, 0);
        MakeSprite(s, "on", { 0, 0, 1.0f }, 0);
        off.GetComponent<SpriteRendererComponent>().Enabled = false;

        const auto order = Order(s);
        REQUIRE(order.size() == 1u);
        CHECK(order[0] == "on");
    }

    TEST_CASE("T13: an inactive entity or an inactive ancestor is excluded")
    {
        Scene s;
        Entity parent = s.CreateEntity("parent");
        Entity child  = MakeSprite(s, "child", { 0, 0, 0 }, 0);
        Entity solo   = MakeSprite(s, "solo",  { 0, 0, 1.0f }, 0);
        Entity map    = MakeTilemap(s, "map",  { 0, 0, 2.0f }, 0);
        REQUIRE(s.SetParent(child, parent));

        CHECK(Order(s).size() == 3u);

        // Deactivating the PARENT (which owns no sprite of its own) hides the child.
        parent.GetComponent<TagComponent>().Active = false;
        auto order = Order(s);
        REQUIRE(order.size() == 2u);
        CHECK(order[0] == "solo");
        CHECK(order[1] == "map");

        // Tilemaps honour the same gate.
        map.GetComponent<TagComponent>().Active = false;
        order = Order(s);
        REQUIRE(order.size() == 1u);
        CHECK(order[0] == "solo");

        // Reactivating restores the full list.
        parent.GetComponent<TagComponent>().Active = true;
        map.GetComponent<TagComponent>().Active    = true;
        CHECK(Order(s).size() == 3u);
    }

    TEST_CASE("fully-tied items break by entity id, and the list is deterministic")
    {
        Scene s;
        std::vector<entt::entity> created;
        for (int i = 0; i < 16; ++i)
            created.push_back((entt::entity)MakeSprite(s, "tie" + std::to_string(i), { 0, 0, 0 }, 0));

        const auto a = s.BuildSpriteDrawList();
        const auto b = s.BuildSpriteDrawList();
        REQUIRE(a.size() == 16u);
        REQUIRE(b.size() == 16u);

        for (size_t i = 0; i < a.size(); ++i)
        {
            CHECK(a[i].E == b[i].E);                       // repeatable
            if (i > 0)
                CHECK(a[i - 1].E < a[i].E);                // ascending entity id
        }
    }

    TEST_CASE("a scene with no 2D content yields an empty list")
    {
        Scene s;
        s.CreateEntity("bare");
        CHECK(s.BuildSpriteDrawList().empty());
    }
}
