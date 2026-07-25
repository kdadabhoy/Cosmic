#pragma once
// physics/ScenePhysics.h
//
// ============================================================================
// Cosmic physics — the Scene <-> PhysicsWorld runtime binding (Phase 15 / J4).
// ============================================================================
//
// Lives only while a simulation session runs. Built by Scene::OnPhysicsStart from
// the authored components; each fixed step it pushes kinematic targets, steps the
// world, writes dynamic transforms back, advances character controllers, and
// (J5) dispatches collision events to scripts. Torn down by Scene::OnPhysicsStop.
//
// FIXED-STEP CONTRACT (documented, load-bearing): per fixed step the session runs
//   scripts OnFixedUpdate  ->  ScenePhysics::Step  ->  ScenePhysics::DispatchEvents
// Step itself is: kinematic read -> PhysicsWorld::Step -> dynamic write-back ->
// character update. Never step from a variable OnUpdate.
//
// The PhysicsWorld is BORROWED (owned by the session — Starforge play mode /
// PlayerLayer); ScenePhysics owns only the entity<->body/character maps. Jolt-free
// header (PhysicsBody / CharacterController are Jolt-free).
// ============================================================================

#include "core/Core.h"
#include "physics/PhysicsBody.h"
#include "physics/PhysicsTypes.h"
#include "physics/CharacterController.h"
#ifndef COSMIC_2D_ONLY
#include "voxel/VoxelVolume.h"           // V5 — IVec3Hash for the per-chunk body map
#endif

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>
#include <unordered_map>
#include <vector>

namespace Cosmic
{
    class Scene;
    class PhysicsWorld;
    class ScriptHost;

    class COSMIC_API ScenePhysics
    {
    public:
        ScenePhysics(Scene& scene, PhysicsWorld& world);
        ~ScenePhysics();

        /** @brief Create bodies + character controllers from the scene's components
         *  (world transforms via Scene::GetWorldTransform). Called once at start. */
        void BuildBodies();

        /** @brief One fixed step: push kinematic targets -> PhysicsWorld::Step ->
         *  write dynamic transforms back -> advance characters. */
        void Step(float fixedDt);

        /** @brief Drain queued contact events and dispatch the OnCollision / OnTrigger
         *  callbacks to the matching script instances (J5). Call after Step each step. */
        void DispatchEvents(ScriptHost& scripts);

        /** @brief Destroy every body + character (called at stop). */
        void Teardown();

        PhysicsWorld& World() { return m_World; }

        /** @brief Build a BodyDesc (collider shapes + world pose) for `e` from
         *  `scene`, edit-mode safe (reads components + assets only; creates no Jolt
         *  objects). Returns false when the entity carries no collider shape. This is
         *  the scene's collision-view enumeration — shared by the play-session body
         *  build (BuildBodyDesc wraps it) and the N2 navmesh bake (SceneNav gathers
         *  triangles through it, the "honest physics source" of the navmesh). */
        static bool BuildColliderDesc(Scene& scene, entt::entity e, BodyDesc& out);

        /** @brief The body bound to `entity`, or an invalid handle. */
        PhysicsBody GetBody(entt::entity entity) const;
        /** @brief The character controller bound to `entity`, or nullptr. */
        CharacterController* GetCharacter(entt::entity entity);

    private:
        bool BuildBodyDesc(entt::entity e, BodyDesc& out) const;
        void WriteBackWorldPose(entt::entity e, const glm::vec3& worldPos, const glm::quat& worldRot);

#ifndef COSMIC_2D_ONLY
        // Voxel collision (V5): one static triangle-mesh body per resident chunk.
        // 3D-only — voxel volumes do not exist in the 2D engine (plan doc 28 §6.4).
        void        BuildVoxelBodies();
        void        RebuildDirtyVoxelChunks();
        PhysicsBody MakeVoxelChunkBody(entt::entity e, const glm::ivec3& chunk);
#endif

        Scene&        m_Scene;
        PhysicsWorld& m_World;

        std::unordered_map<entt::entity, PhysicsBody>          m_Bodies;
        std::unordered_map<entt::entity, CharacterController>  m_Characters;

#ifndef COSMIC_2D_ONLY
        // entity -> (chunk coord -> static mesh body). IVec3Hash/IVec3Eq are the
        // only reason this header includes voxel/VoxelVolume.h at all.
        using ChunkBodyMap = std::unordered_map<glm::ivec3, PhysicsBody, IVec3Hash, IVec3Eq>;
        std::unordered_map<entt::entity, ChunkBodyMap> m_VoxelBodies;
#endif

        std::vector<ContactEvent> m_EventScratch;
        bool m_WarnedMovingParent = false;
    };
}
