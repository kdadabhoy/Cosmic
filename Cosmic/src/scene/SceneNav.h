#pragma once
// scene/SceneNav.h
//
// ============================================================================
// Cosmic navigation — the Scene <-> NavWorld bake pipeline (Phase 26 / N2).
// ============================================================================
//
// Turns a NavMeshComponent's reflected recipe into a baked NavWorld by gathering
// the scene's COLLISION VIEW (the honest source — collider shapes via the
// ScenePhysics enumeration, terrain heightfields, voxel chunk meshes), filtered to
// the navmesh entity's children when SourceMode == FromChildren, and running the
// N1 Recast build over the world-space triangle soup.
//
// Two entry points, mirroring the E18 world-system pattern:
//   * BakeSync   — gather + build + install on the main thread (tests / small bakes).
//   * BeginBake / FinishBake — the one-shot async build (the WorldSystemsPanel
//     precedent): gather on the calling (main) thread, run Recast on a JobSystem
//     worker, poll the returned job, install when done — no frame stall.
//
// The built navmesh is big binary: it rides a `.cnav` sidecar (SaveSidecar /
// LoadSidecar), never the scene JSON. A BuiltSignature over the recipe + gathered
// geometry gates regeneration (rebake only when something actually changed).
//
// GL-free and headless-testable.
// ============================================================================

#include "core/Core.h"
#include "nav/NavTypes.h"

#include <glm/glm.hpp>

#include <entt/entt.hpp>

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Cosmic
{
    class Scene;
    class NavWorld;
    struct NavMeshComponent;

    // ------------------------------------------------------------------------
    // NavBakeJob — a one-shot async bake handle (the terrain-build pattern). The
    // Recast build runs on a JobSystem worker into `Result`; the caller polls
    // IsDone() each frame and installs via SceneNav::FinishBake.
    // ------------------------------------------------------------------------
    struct NavBakeJob
    {
        Ref<std::atomic<bool>> Done;      // set true by the worker when Result is ready
        Ref<NavWorld>          Result;    // worker builds here; main thread installs
        std::size_t            Signature = 0;
        bool                   Valid = false;

        bool IsDone() const { return Done && Done->load(std::memory_order_acquire); }
    };

    namespace SceneNav
    {
        // --- geometry + recipe -----------------------------------------------
        /** @brief Gather the world-space collision-view triangle soup for the navmesh
         *  entity `navEntity` (colliders + terrain heightfields + voxel chunks),
         *  filtered per the component's SourceMode. Appends into outVerts (xyz
         *  triples) / outTris (index triples). Main-thread (reads ECS + assets). */
        COSMIC_API void GatherGeometry(Scene& scene, entt::entity navEntity,
                                       std::vector<float>& outVerts, std::vector<int>& outTris);

        /** @brief Map the reflected recipe onto a NavBuildDesc. */
        COSMIC_API NavBuildDesc MakeBuildDesc(const NavMeshComponent& c);

        /** @brief Signature over the recipe + gathered geometry (the E18 regen gate);
         *  0 is reserved for "never built". */
        COSMIC_API std::size_t Signature(const NavMeshComponent& c,
                                         const std::vector<float>& verts, const std::vector<int>& tris);

        // --- bake ------------------------------------------------------------
        /** @brief Gather + build + install on the component synchronously. Returns
         *  true if a navmesh was produced (false = no walkable geometry). */
        COSMIC_API bool BakeSync(Scene& scene, entt::entity navEntity);

        /** @brief Begin a one-shot async bake: gather now (main thread), submit the
         *  Recast build to the JobSystem, return a handle to poll. Sets the
         *  component's Baking flag. If the JobSystem is not initialized the build
         *  runs inline and the returned job is already Done. */
        COSMIC_API NavBakeJob BeginBake(Scene& scene, entt::entity navEntity);

        /** @brief If `job` is done, install its result on the component (updating
         *  BuiltSignature), clear Baking, consume the job, and return true. */
        COSMIC_API bool FinishBake(Scene& scene, entt::entity navEntity, NavBakeJob& job);

        // --- `.cnav` sidecar -------------------------------------------------
        /** @brief The sidecar path for a navmesh entity: the component's SidecarPath
         *  if set, else derived beside `scenePath` (empty scenePath => empty). */
        COSMIC_API std::string SidecarPathFor(const NavMeshComponent& c, const std::string& scenePath);

        /** @brief Write the component's baked navmesh to `path` (via the VFS). No-op
         *  false when nothing is baked. */
        COSMIC_API bool SaveSidecar(const NavMeshComponent& c, const std::string& path);

        /** @brief Load a `.cnav` at `path` into the component's runtime NavWorld. */
        COSMIC_API bool LoadSidecar(NavMeshComponent& c, const std::string& path);
    }

    // ------------------------------------------------------------------------
    // SceneNavRuntime (N4) — the play-session agent/crowd binding, mirroring
    // ScenePhysics. Lives only while a play session runs: it borrows the scene's
    // primary baked navmesh, spins up its DetourCrowd, adds an agent per
    // NavAgentComponent, and each fixed step pushes targets -> steps the crowd ->
    // writes agent transforms back -> emits `nav.arrived` on the scene EventBus.
    // Torn down on Stop (the crowd is released; the baked navmesh is authored data
    // and survives). The Scene owns one (Scene::GetNav()); scripts reach it via the
    // ScriptableEntity Nav() proxy. GL-free, headless-testable, deterministic.
    // ------------------------------------------------------------------------
    class COSMIC_API SceneNavRuntime
    {
    public:
        explicit SceneNavRuntime(Scene& scene);
        ~SceneNavRuntime();

        /** @brief Bind the primary navmesh + crowd and add an agent per
         *  NavAgentComponent (world positions snapped to the mesh). */
        void BuildAgents();

        /** @brief One fixed step: (targets already pushed by scripts) advance the
         *  crowd, write agent transforms back, emit arrival signals. */
        void Step(float fixedDt);

        /** @brief Release the crowd + agents (the navmesh itself is untouched). */
        void Teardown();

        /** @brief The active navmesh used for agents + queries, or null. */
        NavWorld* Nav() const { return m_Nav; }
        bool HasNavmesh() const { return m_Nav != nullptr; }

        // ---- per-agent control (routed from the Nav() script proxy) ---------
        bool      HasAgent(entt::entity e) const;
        void      SetTarget(entt::entity e, const glm::vec3& target);
        void      Stop(entt::entity e);
        bool      HasArrived(entt::entity e) const;   // within StoppingDistance of its last target
        glm::vec3 AgentPosition(entt::entity e) const;
        glm::vec3 AgentVelocity(entt::entity e) const;

    private:
        void WriteBackWorldPos(entt::entity e, const glm::vec3& worldPos);

        Scene&    m_Scene;
        NavWorld* m_Nav = nullptr;              // borrowed (the primary NavMeshComponent's Nav)

        struct AgentSlot
        {
            int   Id = -1;             // DetourCrowd agent id
            float StopDist = 0.4f;     // arrival tolerance
            bool  HasTarget = false;
            bool  ArrivedLatched = false;   // nav.arrived emitted for the current target
        };
        std::unordered_map<entt::entity, AgentSlot> m_Agents;
    };
}
