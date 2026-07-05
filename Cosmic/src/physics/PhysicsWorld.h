#pragma once
// physics/PhysicsWorld.h
//
// ============================================================================
// Cosmic physics — the engine rigid-body / query / character service (Phase 15).
// ============================================================================
//
// Wraps ONE Jolt PhysicsSystem behind a pimpl so no JPH:: type ever appears in a
// public header (the compile-time firewall; Jolt is linked PRIVATE into the DLL).
// Owned by whoever runs a simulation session — the Starforge editor's play mode
// and the standalone PlayerLayer (the SerialLink/SceneManager ownership pattern).
// Bodies exist only while a session runs; edit mode holds no Jolt objects.
//
// FIXED-STEP CONTRACT (J4): Step() is called exactly on the engine fixed timestep,
// once per accumulated fixed-dt, AFTER scripts' OnFixedUpdate and BEFORE transform
// write-back + contact-event dispatch. Never step from a variable OnUpdate.
//
// THREADING: Jolt runs its solver on an internal pool; all PhysicsWorld calls are
// main-thread (v1). The contact listener fires on worker threads during Step and
// only pushes into a mutex-guarded queue drained by DrainContactEvents afterwards.
// The body metadata map is mutated only outside Step (main thread), so the
// listener reads it lock-free.
//
// GL-free and headless-testable: CosmicTests constructs a real PhysicsWorld and
// simulates without a window (PhysicsDebug is the only GL-adjacent piece, J8).
// ============================================================================

#include "core/Core.h"
#include "physics/PhysicsTypes.h"
#include "physics/PhysicsBody.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Cosmic
{
    class Renderer3DDebugSink;   // fwd (J8) — not used publicly here

    class COSMIC_API PhysicsWorld
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&)            = delete;   // owns the Jolt system
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        // ---- lifecycle ------------------------------------------------------
        void Init(const PhysicsSettings& settings = {});
        void Shutdown();
        bool IsInitialized() const;

        /** @brief Advance the simulation by exactly one fixed step (see contract). */
        void Step(float fixedDt);

        // ---- bodies ---------------------------------------------------------
        PhysicsBody CreateBody(const BodyDesc& desc);
        void        DestroyBody(PhysicsBody body);

        /** @brief Teleport (position + rotation). Use only on session start / hard
         *  resets — for per-step kinematic movement use MoveKinematic. */
        void SetBodyTransform(PhysicsBody body, const glm::vec3& position, const glm::quat& rotation);
        void GetBodyTransform(PhysicsBody body, glm::vec3& outPosition, glm::quat& outRotation) const;

        /** @brief Velocity-consistent kinematic move: sets the body's velocity so it
         *  reaches (position, rotation) after `dt`, so it pushes dynamic bodies
         *  correctly (the script-driven-mover path). */
        void MoveKinematic(PhysicsBody body, const glm::vec3& position, const glm::quat& rotation, float dt);

        void      SetLinearVelocity(PhysicsBody body, const glm::vec3& v);
        glm::vec3 GetLinearVelocity(PhysicsBody body) const;
        void      SetAngularVelocity(PhysicsBody body, const glm::vec3& w);
        glm::vec3 GetAngularVelocity(PhysicsBody body) const;

        void AddForce(PhysicsBody body, const glm::vec3& force);            // N, this step
        void AddImpulse(PhysicsBody body, const glm::vec3& impulse);        // N*s, instantaneous
        void AddTorque(PhysicsBody body, const glm::vec3& torque);

        /** @brief True while the body is awake (not sleeping). */
        bool IsActive(PhysicsBody body) const;
        void Activate(PhysicsBody body);

        // ---- queries (main thread) -----------------------------------------
        // layerMask filters by each body's fine Category bits (default = all);
        // ignoreEntity (a UUID, 0 = none) drops that entity's body from the result
        // — the "don't hit myself" convenience (e.g. an IsGrounded down-ray).
        std::optional<RayHit> RayCast(const glm::vec3& origin, const glm::vec3& direction,
                                      float maxDistance, uint16_t layerMask = 0xFFFF,
                                      uint64_t ignoreEntity = 0) const;
        std::optional<RayHit> SphereCast(const glm::vec3& origin, const glm::vec3& direction,
                                         float radius, float maxDistance, uint16_t layerMask = 0xFFFF,
                                         uint64_t ignoreEntity = 0) const;
        void OverlapSphere(const glm::vec3& center, float radius,
                           std::vector<uint64_t>& outEntities, uint16_t layerMask = 0xFFFF,
                           uint64_t ignoreEntity = 0) const;
        void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                        std::vector<uint64_t>& outEntities, uint16_t layerMask = 0xFFFF,
                        uint64_t ignoreEntity = 0) const;

        // ---- character controllers (J6) ------------------------------------
        CharacterHandle CreateCharacter(const CharacterDesc& desc);
        void            DestroyCharacter(CharacterHandle ch);
        /** @brief Integrate one character against the world (called by the session
         *  AFTER Step()). desiredVelocity is the horizontal walk + vertical (jump/
         *  gravity) velocity the script set this step. */
        void  UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt);
        void  GetCharacterTransform(CharacterHandle ch, glm::vec3& outPosition, glm::quat& outRotation) const;
        void  SetCharacterPosition(CharacterHandle ch, const glm::vec3& position);
        bool  IsCharacterGrounded(CharacterHandle ch) const;
        glm::vec3 GetCharacterGroundNormal(CharacterHandle ch) const;
        glm::vec3 GetCharacterVelocity(CharacterHandle ch) const;

        // ---- events ---------------------------------------------------------
        /** @brief Move queued contact events into `out` (clears the internal queue).
         *  Call once per fixed step after Step(); dispatch to scripts (J5). */
        void DrainContactEvents(std::vector<ContactEvent>& out);

        // ---- introspection --------------------------------------------------
        PhysicsStats GetStatistics() const;

        // ---- debug draw (J8) ------------------------------------------------
        /** @brief Emit live body/character wireframes + contact points to the
         *  Renderer3D line batch (call between BeginScene/EndScene). Sleeping
         *  bodies draw grey, awake green, triggers cyan, characters yellow.
         *  Debug-config only (needs JPH_DEBUG_RENDERER); a no-op in Release. */
        void DebugDraw() const;

    public:
        // Opaque — the full definition (all JPH:: state) lives in PhysicsWorld.cpp.
        // Public only so the .cpp's file-local contact-listener / query filters can
        // name the type; clients see an incomplete type and can do nothing with it.
        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;   // all JPH:: state lives here
    };
}
