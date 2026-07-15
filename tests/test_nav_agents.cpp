// test_nav_agents.cpp — N4 acceptance. Bakes a navmesh, spawns NavAgentComponents,
// and drives the play-session crowd (Scene::OnNavStart/Step/Stop) headlessly: an
// agent walks to a target within tolerance and emits nav.arrived; an agent routes
// around a wall; two crowd agents don't interpenetrate; two identical runs produce
// bit-for-bit identical agent traces (the J9 determinism proof pattern). GL-free.

#include "doctest.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/SceneNav.h"
#include "nav/NavWorld.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Cosmic;

namespace
{
    Entity AddGround(Scene& s, const char* name, const glm::vec3& pos, const glm::vec3& he)
    {
        Entity g = s.CreateEntity(name);
        g.GetComponent<TransformComponent>().Position = pos;
        g.AddComponent<BoxColliderComponent>().HalfExtents = he;
        return g;
    }

    Entity BakeNav(Scene& s)
    {
        Entity nav = s.CreateEntity("Nav");
        auto& nm = nav.AddComponent<NavMeshComponent>();
        nm.SourceMode = NavSourceMode::WholeScene;
        nm.CellSize = 0.2f; nm.CellHeight = 0.2f;
        nm.AgentRadius = 0.4f; nm.AgentHeight = 1.8f; nm.AgentMaxClimb = 0.4f;
        REQUIRE(SceneNav::BakeSync(s, (entt::entity)nav));
        return nav;
    }

    Entity AddAgent(Scene& s, const glm::vec3& pos, float stopDist = 0.4f)
    {
        Entity a = s.CreateEntity("Agent");
        a.GetComponent<TransformComponent>().Position = pos;
        auto& ac = a.AddComponent<NavAgentComponent>();
        ac.Radius = 0.4f; ac.MaxSpeed = 3.5f; ac.StoppingDistance = stopDist;
        return a;
    }

    glm::vec3 XZ(const glm::vec3& v) { return { v.x, 0.0f, v.z }; }
    float DistXZ(const glm::vec3& a, const glm::vec3& b) { return glm::length(XZ(a) - XZ(b)); }
}

TEST_SUITE("Nav / agents + crowd (N4)")
{
    TEST_CASE("An agent walks a baked greybox to its target and emits nav.arrived")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        BakeNav(scene);
        Entity agent = AddAgent(scene, { -8, 0, -8 });

        bool arrived = false;
        scene.Events().Connect("nav.arrived", [&](Entity src) { if (src == agent) arrived = true; });

        scene.OnNavStart();
        auto* nav = scene.GetNav();
        REQUIRE(nav);
        REQUIRE(nav->HasNavmesh());
        REQUIRE(nav->HasAgent((entt::entity)agent));

        const glm::vec3 target{ 8, 0, 8 };
        nav->SetTarget((entt::entity)agent, target);

        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 900 && !arrived; ++i)
            scene.OnNavStep(dt);

        const glm::vec3 finalPos = agent.GetComponent<TransformComponent>().Position;
        CHECK(arrived);
        CHECK(DistXZ(finalPos, target) < 1.0f);
        scene.OnNavStop();
        CHECK(scene.GetNav() == nullptr);
    }

    TEST_CASE("An agent routes around a wall to reach the far side")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        // A long thin wall across the middle, gaps at |z| > 8.
        Entity wall = scene.CreateEntity("Wall");
        wall.GetComponent<TransformComponent>().Position = { 0, 1.0f, 0 };
        wall.AddComponent<BoxColliderComponent>().HalfExtents = { 0.3f, 1.0f, 8.0f };
        BakeNav(scene);
        Entity agent = AddAgent(scene, { -8, 0, 0 });

        scene.OnNavStart();
        auto* nav = scene.GetNav();
        nav->SetTarget((entt::entity)agent, { 8, 0, 0 });

        const float dt = 1.0f / 60.0f;
        float maxAbsZ = 0.0f;   // it must swing wide (|z| grows) to clear the wall ends
        for (int i = 0; i < 1200; ++i)
        {
            scene.OnNavStep(dt);
            maxAbsZ = std::max(maxAbsZ, std::abs(agent.GetComponent<TransformComponent>().Position.z));
            if (nav->HasArrived((entt::entity)agent)) break;
        }

        CHECK(nav->HasArrived((entt::entity)agent));
        CHECK(DistXZ(agent.GetComponent<TransformComponent>().Position, { 8, 0, 0 }) < 1.0f);
        CHECK(maxAbsZ > 5.0f);   // it detoured around the wall rather than through it
        scene.OnNavStop();
    }

    TEST_CASE("Two crowd agents swapping positions do not interpenetrate")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
        BakeNav(scene);
        Entity a = AddAgent(scene, { -3, 0, 0 });
        Entity b = AddAgent(scene, {  3, 0, 0 });

        scene.OnNavStart();
        auto* nav = scene.GetNav();
        nav->SetTarget((entt::entity)a, {  3, 0, 0 });
        nav->SetTarget((entt::entity)b, { -3, 0, 0 });

        const float dt = 1.0f / 60.0f;
        float minSep = 1e9f;
        for (int i = 0; i < 600; ++i)
        {
            scene.OnNavStep(dt);
            const float d = DistXZ(nav->AgentPosition((entt::entity)a), nav->AgentPosition((entt::entity)b));
            minSep = std::min(minSep, d);
        }
        // Radii sum to 0.8; obstacle avoidance must keep their centers from
        // collapsing together (allow some squeeze, but never deep overlap).
        CHECK(minSep > 0.45f);
        scene.OnNavStop();
    }

    // Mirrors the N5 NavCritter sample driver (a template SystemScript, so it isn't
    // linked into CosmicTests) to prove the patrol-loop + chase logic headlessly.
    TEST_CASE("N5 sample logic: a critter loops patrol waypoints, then chases a near player")
    {
        Scene scene;
        AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 16, 0.5f, 16 });
        BakeNav(scene);
        Entity critter = AddAgent(scene, { 0, 0, 0 });

        scene.OnNavStart();
        auto* nav = scene.GetNav();
        const entt::entity h = (entt::entity)critter;
        REQUIRE(nav->HasAgent(h));

        const glm::vec3 spawn = nav->AgentPosition(h);
        const float R = 6.0f;
        const glm::vec3 corners[4] = { { 1, 0, 1 }, { -1, 0, 1 }, { -1, 0, -1 }, { 1, 0, -1 } };
        int  wp = 0;
        bool targeted = false, visitedMask[4] = { false, false, false, false };
        int  visited = 0;
        const float dt = 1.0f / 60.0f;

        // Phase 1: patrol — cycle waypoints; expect to reach several distinct corners.
        for (int i = 0; i < 2400; ++i)
        {
            const glm::vec3 target = spawn + corners[wp] * R;
            const glm::vec3 pos    = nav->AgentPosition(h);
            if (glm::length(glm::vec2(target.x - pos.x, target.z - pos.z)) < 1.2f)
            {
                if (!visitedMask[wp]) { visitedMask[wp] = true; ++visited; }
                wp = (wp + 1) & 3;
                targeted = false;
            }
            if (!targeted) { nav->SetTarget(h, spawn + corners[wp] * R); targeted = true; }
            scene.OnNavStep(dt);
        }
        CHECK(visited >= 3);   // it looped the patrol, not stuck on one waypoint

        // Phase 2: chase — a "player" 4 m away; the critter closes the distance.
        const glm::vec3 player = spawn + glm::vec3(4.0f, 0, 0);
        auto planarDist = [&](const glm::vec3& a, const glm::vec3& b)
        { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); };
        const float startDist = planarDist(player, nav->AgentPosition(h));
        for (int i = 0; i < 500; ++i)
        {
            nav->SetTarget(h, player);   // re-issue each tick, like NavCritter's chase
            scene.OnNavStep(dt);
        }
        const float endDist = planarDist(player, nav->AgentPosition(h));
        CHECK(endDist < startDist);
        CHECK(endDist < 1.0f);
        scene.OnNavStop();
    }

    TEST_CASE("Determinism: two identical agent runs are bit-for-bit identical")
    {
        auto runOnce = []()
        {
            Scene scene;
            AddGround(scene, "Ground", { 0, -0.5f, 0 }, { 12, 0.5f, 12 });
            BakeNav(scene);
            Entity agent = AddAgent(scene, { -8, 0, -6 });

            scene.OnNavStart();
            scene.GetNav()->SetTarget((entt::entity)agent, { 7, 0, 5 });

            std::vector<glm::vec3> trace;
            const float dt = 1.0f / 60.0f;
            for (int i = 0; i < 500; ++i)
            {
                scene.OnNavStep(dt);
                trace.push_back(agent.GetComponent<TransformComponent>().Position);
            }
            scene.OnNavStop();
            return trace;
        };

        std::vector<glm::vec3> a = runOnce();
        std::vector<glm::vec3> b = runOnce();
        REQUIRE(a.size() == b.size());
        REQUIRE(a.size() == 500u);

        bool identical = true;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z)
                { identical = false; break; }
        CHECK(identical);
        // And it actually moved toward the goal (not a trivial all-zeros match).
        CHECK(DistXZ(a.back(), { 7, 0, 5 }) < 1.5f);
    }
}
