#pragma once
// physics/CharacterController.h
//
// ============================================================================
// Cosmic physics — character controller wrapper (Phase 15 / J6).
// ============================================================================
//
// An ergonomic, Jolt-free handle around a PhysicsWorld CharacterVirtual (the
// kinematic capsule with slope/step handling). It owns the walk model the raw
// CharacterVirtual does NOT: gravity integration, jump, and stick-to-floor, so a
// gameplay script only has to say Move(dir) / Jump(v). Header-only (thin inline
// calls into the exported PhysicsWorld API); no Jolt types leak.
//
// Lifecycle (J4/J6): the Scene creates one per CharacterControllerComponent at
// play-start and calls Tick(dt) each fixed step AFTER PhysicsWorld::Step. The
// script drives it through ScriptableEntity::Character().
// ============================================================================

#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBody.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Cosmic
{
    class CharacterController
    {
    public:
        CharacterController() = default;
        CharacterController(PhysicsWorld* world, CharacterHandle handle)
            : m_World(world), m_Handle(handle) {}

        bool IsValid() const { return m_World != nullptr && m_Handle.IsValid(); }
        CharacterHandle GetHandle() const { return m_Handle; }

        // ---- script surface -------------------------------------------------
        /** @brief Set the desired horizontal (X/Z) walk velocity for this step.
         *  Persists until changed — set it to zero when there's no input. The Y
         *  component is ignored (gravity + Jump own the vertical axis). */
        void Move(const glm::vec3& horizontalVelocity)
        {
            m_DesiredHorizontal = glm::vec3(horizontalVelocity.x, 0.0f, horizontalVelocity.z);
        }

        /** @brief Request a jump: takes effect next Tick if grounded. `speed` is the
         *  launch velocity (m/s), e.g. sqrt(2*g*height) for a target apex height. */
        void Jump(float speed) { m_PendingJump = speed; m_JumpQueued = true; }

        bool      IsGrounded()     const { return IsValid() && m_World->IsCharacterGrounded(m_Handle); }
        glm::vec3 GetGroundNormal() const { return IsValid() ? m_World->GetCharacterGroundNormal(m_Handle) : glm::vec3(0, 1, 0); }
        glm::vec3 GetVelocity()    const { return IsValid() ? m_World->GetCharacterVelocity(m_Handle) : glm::vec3(0.0f); }

        glm::vec3 GetPosition() const
        {
            glm::vec3 p(0.0f); glm::quat r(1, 0, 0, 0);
            if (IsValid()) m_World->GetCharacterTransform(m_Handle, p, r);
            return p;
        }
        void SetPosition(const glm::vec3& p) { if (IsValid()) m_World->SetCharacterPosition(m_Handle, p); }

        void SetGravity(float accelY) { m_Gravity = accelY; }

        // ---- driven by the Scene each fixed step (after PhysicsWorld::Step) --
        void Tick(float dt)
        {
            if (!IsValid() || dt <= 0.0f) return;

            const bool grounded = m_World->IsCharacterGrounded(m_Handle);
            if (grounded && m_VerticalVelocity < 0.0f)
                m_VerticalVelocity = 0.0f;             // rest on the floor

            if (m_JumpQueued)
            {
                if (grounded) m_VerticalVelocity = m_PendingJump;
                m_JumpQueued = false;
            }

            m_VerticalVelocity += m_Gravity * dt;      // m_Gravity is negative (down)

            const glm::vec3 v(m_DesiredHorizontal.x, m_VerticalVelocity, m_DesiredHorizontal.z);
            m_World->UpdateCharacter(m_Handle, v, dt);

            // Read back the achieved vertical velocity so a ceiling hit / ground
            // contact clamps it for next step.
            m_VerticalVelocity = m_World->GetCharacterVelocity(m_Handle).y;
        }

    private:
        PhysicsWorld*   m_World = nullptr;
        CharacterHandle m_Handle;
        glm::vec3 m_DesiredHorizontal{ 0.0f };
        float     m_VerticalVelocity = 0.0f;
        float     m_Gravity = -9.81f;
        float     m_PendingJump = 0.0f;
        bool      m_JumpQueued = false;
    };
}
