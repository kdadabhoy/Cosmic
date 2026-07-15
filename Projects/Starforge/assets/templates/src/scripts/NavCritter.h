#pragma once
// scripts/NavCritter.h — Phase 26 / N5 nav sample.
//
// A SystemScript (H9 — class-of-entities logic, decision #5) that steers every
// "Critter"-tagged NavAgent over the scene's baked navmesh: it patrols a square
// waypoint loop around its spawn, and chases the "Player" entity when the player
// comes within ChaseRadius. One instance drives all critters (attach a
// SystemScriptComponent naming "NavCritter" to any holder entity).
//
// It reaches the agents through the scene nav runtime (GetScene().GetNav()) —
// SetTarget / HasAgent / AgentPosition — the same surface the per-entity Nav()
// proxy exposes. Pure logic; the DetourCrowd does the steering + avoidance.

#include <Cosmic.h>

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>

class NavCritter : public Cosmic::SystemScript
{
public:
    float ChaseRadius  = 6.0f;    // start chasing the Player within this range (m)
    float PatrolRadius = 6.0f;    // half-size of the square patrol loop around spawn (m)

protected:
    void OnFixedUpdateAll(std::span<Cosmic::Entity> critters, float /*fixedDt*/) override
    {
        Cosmic::SceneNavRuntime* nav = GetScene().GetNav();
        if (!nav || !nav->HasNavmesh())
            return;

        // The player's world position (first entity tagged "Player"), if present.
        auto& reg = GetScene().GetRegistry();
        bool hasPlayer = false;
        glm::vec3 playerPos(0.0f);
        for (auto e : reg.view<Cosmic::TagComponent, Cosmic::TransformComponent>())
        {
            if (reg.get<Cosmic::TagComponent>(e).Tag == "Player")
            {
                playerPos = glm::vec3(GetScene().GetWorldTransform(Cosmic::Entity(e, &GetScene()))[3]);
                hasPlayer = true;
                break;
            }
        }

        for (Cosmic::Entity critter : critters)
        {
            const entt::entity h = static_cast<entt::entity>(critter);
            if (!nav->HasAgent(h))
                continue;

            const uint64_t id = critter.GetComponent<Cosmic::IDComponent>().ID.Value();
            State& st = m_State[id];
            const glm::vec3 pos = nav->AgentPosition(h);
            if (!st.Spawned) { st.Spawn = pos; st.Spawned = true; }

            const bool chase = hasPlayer &&
                glm::length(glm::vec2(playerPos.x - pos.x, playerPos.z - pos.z)) < ChaseRadius;

            if (chase)
            {
                nav->SetTarget(h, playerPos);      // re-issue each tick — the crowd repaths
                st.Targeted = false;               // force a fresh patrol target when the chase ends
            }
            else
            {
                const glm::vec3 wp = Waypoint(st);
                if (glm::length(glm::vec2(wp.x - pos.x, wp.z - pos.z)) < 1.2f)   // reached the corner
                {
                    st.Wp = (st.Wp + 1) & 3;
                    st.Targeted = false;
                }
                if (!st.Targeted)
                {
                    nav->SetTarget(h, Waypoint(st));
                    st.Targeted = true;
                }
            }
        }
    }

private:
    struct State
    {
        glm::vec3 Spawn{ 0.0f };
        int  Wp = 0;
        bool Spawned = false;
        bool Targeted = false;
    };

    glm::vec3 Waypoint(const State& st) const
    {
        static const glm::vec3 kCorners[4] = { { 1, 0, 1 }, { -1, 0, 1 }, { -1, 0, -1 }, { 1, 0, -1 } };
        return st.Spawn + kCorners[st.Wp & 3] * PatrolRadius;
    }

    std::unordered_map<uint64_t, State> m_State;
};
