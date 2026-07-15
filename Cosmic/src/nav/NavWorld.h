#pragma once
// nav/NavWorld.h
//
// ============================================================================
// Cosmic navigation — the engine navmesh bake / query / crowd service (Phase 26).
// ============================================================================
//
// Wraps Recast (bake) + Detour (navmesh + query) + DetourCrowd (agents) behind a
// pimpl so no rc*/dt* type ever appears in a public header (the compile-time
// firewall; RecastNavigation is linked PRIVATE into the DLL — the Jolt playbook,
// doc 14 J1). Everything is GL-free and headless-testable: CosmicTests bakes a
// greybox and runs queries + a crowd with no window.
//
// LIFETIME: like PhysicsWorld, a NavWorld's *crowd* exists only while a play
// session runs (SceneNav owns it — the physics-body lifetime rule). The *baked
// navmesh* itself is authored data: it loads from a `.cnav` sidecar and can be
// rebaked in edit mode. A NavWorld holds ONE navmesh (single-tile "solo" build in
// v1) plus an optional crowd bound to it.
//
// THREADING: a NavWorld is single-consumer. The N2 async bake runs Build on a
// JobSystem worker into a *separate* NavWorld/NavMeshData and hands the result
// back on the main thread (the WorldSystemsPanel one-shot pattern) — no NavWorld
// instance is touched from two threads at once.
// ============================================================================

#include "core/Core.h"
#include "nav/NavTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Cosmic
{
    class COSMIC_API NavWorld
    {
    public:
        NavWorld();
        ~NavWorld();

        NavWorld(const NavWorld&)            = delete;   // owns the Detour navmesh
        NavWorld& operator=(const NavWorld&) = delete;

        // ---- bake / load / save --------------------------------------------
        /** @brief Rasterize `geometry` and build a walkable navmesh per `desc`.
         *  Replaces any previously held mesh. Returns false (and fills outError,
         *  logs) on failure — e.g. empty geometry or no walkable surface. */
        bool Build(const NavBuildDesc& desc, const NavGeometryInput& geometry,
                   std::string* outError = nullptr);

        /** @brief Load a previously baked navmesh (a `.cnav` payload). Rebuilds the
         *  query. Returns false on a bad/empty/incompatible blob. */
        bool Load(const NavMeshData& data);

        /** @brief Serialize the held navmesh to a `.cnav` payload (empty if none).
         *  Byte-stable for a given built mesh (the round-trip test). */
        NavMeshData Serialize() const;

        bool IsBuilt() const;
        void Clear();

        /** @brief World AABB of the baked mesh (min/max). Zeroed if not built. */
        void GetBounds(glm::vec3& outMin, glm::vec3& outMax) const;

        // ---- queries (single-consumer) -------------------------------------
        /** @brief Straightened path from `a` to `b`. Reached=false (cleanly) when
         *  either endpoint has no navmesh polygon within the search box, or the
         *  goal is unreachable (then Partial=true and Corners stop at the frontier). */
        NavPath FindPath(const glm::vec3& a, const glm::vec3& b) const;

        /** @brief Surface wall-raycast: does the straight segment a->b stay on the
         *  navmesh? Hit=false => clear to b. */
        NavRayHit Raycast(const glm::vec3& a, const glm::vec3& b) const;

        /** @brief Nearest navmesh point to `p` within `halfExtents`, or nullopt. */
        std::optional<glm::vec3> NearestPoint(const glm::vec3& p,
                                              const glm::vec3& halfExtents = glm::vec3(2.0f, 4.0f, 2.0f)) const;

        /** @brief A random navmesh point within `radius` of `center`. `rngState` is
         *  advanced by the caller-owned xorshift so the draw is deterministic and
         *  reproducible (seed it once per run for the two-run bit-match proof). */
        std::optional<glm::vec3> RandomPointAround(const glm::vec3& center, float radius,
                                                   uint32_t& rngState) const;

        /** @brief Append every walkable detail triangle (world space) to `out` for
         *  the N3 debug overlay. Clears nothing — caller resets `out`. */
        void GetDebugTriangles(std::vector<NavDebugTri>& out) const;

        // ---- crowd (N4) — a play-session agent manager over this navmesh -----
        /** @brief (Re)create the crowd bound to the held navmesh. `maxAgentRadius`
         *  sizes the internal obstacle-avoidance grid. No-op if not built. */
        void CrowdInit(float maxAgentRadius = 1.0f, int maxAgents = 128);
        void CrowdShutdown();
        bool CrowdReady() const;

        /** @brief Spawn a crowd agent at `pos` (snapped to the navmesh). Returns a
         *  stable agent id (>= 0), or -1 on failure. */
        int  AddAgent(const glm::vec3& pos, const NavAgentParams& params);
        void RemoveAgent(int agentId);

        /** @brief Request the agent steer toward `target` (snapped to the navmesh). */
        void SetAgentTarget(int agentId, const glm::vec3& target);
        /** @brief Clear the agent's move request (it decelerates and holds). */
        void ResetAgentTarget(int agentId);

        /** @brief Advance every agent by `dt` (fixed step). Deterministic. */
        void UpdateCrowd(float dt);

        glm::vec3 GetAgentPosition(int agentId) const;
        glm::vec3 GetAgentVelocity(int agentId) const;
        bool      AgentHasTarget(int agentId) const;
        /** @brief Straight-line distance from the agent to its requested target
         *  (or a large value when it has none). Arrival test lives in SceneNav. */
        float     AgentDistanceToTarget(int agentId) const;

    public:
        // Opaque — the full definition (all rc*/dt* state) lives in NavWorld.cpp.
        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
    };
}
