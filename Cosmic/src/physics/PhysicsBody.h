#pragma once
// physics/PhysicsBody.h
//
// ============================================================================
// Cosmic physics — thin body handle (Phase 15 / J2).
// ============================================================================
//
// A trivially-copyable, Jolt-free handle to a body living inside a PhysicsWorld.
// It stores only the packed Jolt BodyID value; every operation goes back through
// PhysicsWorld (SetVelocity, AddForce, GetBodyTransform, ...). Held by
// RigidBodyComponent at runtime (not reflected/serialized) and returned to
// scripts through the Physics() proxy.
// ============================================================================

#include <cstdint>

namespace Cosmic
{
    struct PhysicsBody
    {
        // Packed JPH::BodyID index+sequence. 0xFFFFFFFF == JPH::BodyID::cInvalidBodyID.
        uint32_t Id = 0xFFFFFFFFu;

        bool IsValid() const { return Id != 0xFFFFFFFFu; }

        bool operator==(const PhysicsBody& o) const { return Id == o.Id; }
        bool operator!=(const PhysicsBody& o) const { return Id != o.Id; }
    };

    // Handle to a CharacterVirtual owned by a PhysicsWorld (J6). Like PhysicsBody,
    // a plain index into the world's character pool.
    struct CharacterHandle
    {
        uint32_t Id = 0xFFFFFFFFu;

        bool IsValid() const { return Id != 0xFFFFFFFFu; }

        bool operator==(const CharacterHandle& o) const { return Id == o.Id; }
        bool operator!=(const CharacterHandle& o) const { return Id != o.Id; }
    };
}
