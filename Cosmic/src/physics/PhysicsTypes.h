#pragma once
// physics/PhysicsTypes.h
//
// ============================================================================
// Cosmic physics — public, Jolt-free value types (Phase 15 / J2).
// ============================================================================
//
// This header is the compile-time firewall in front of Jolt (like nlohmann/json
// or windows.h elsewhere in the engine): it declares the plain POD/reflected
// types the rest of the engine and game scripts pass to PhysicsWorld, and it
// includes NO Jolt header. All JPH:: types live in PhysicsWorld.cpp behind the
// pimpl. Everything here is header-only, GL-free, and headless-testable.
//
// Coordinate/quaternion convention matches TransformComponent (glm, right-handed,
// metres, quaternion {w,x,y,z}). Rotations are glm::quat throughout — the engine
// physics path uses the quaternion slot, not Euler.
// ============================================================================

#include "core/Core.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // MotionType — how a body is driven. Reflected on RigidBodyComponent (J3);
    // an `enum class : int32_t` so the reflection registry boxes it as Enum.
    //   Static    — never moves; infinite mass; the world/ground.
    //   Kinematic — moved by script/transform (MoveKinematic), pushes dynamics,
    //               is not pushed back.
    //   Dynamic   — fully simulated (gravity, forces, contacts).
    // ------------------------------------------------------------------------
    enum class MotionType : int32_t { Static = 0, Kinematic = 1, Dynamic = 2 };

    // ------------------------------------------------------------------------
    // Object layers — the coarse broadphase category derived from a body's
    // MotionType / trigger / character role (NOT the fine 16-bit CollisionMask,
    // which is applied on top). These are the standard Jolt sample layers.
    // ------------------------------------------------------------------------
    namespace PhysicsObjectLayer
    {
        enum : uint16_t
        {
            Static    = 0,   // non-moving world geometry
            Dynamic   = 1,   // simulated / kinematic bodies
            Trigger   = 2,   // sensors: report overlap, no contact response
            Character  = 3,  // character-controller capsules

            Count      = 4
        };
    }

    // ------------------------------------------------------------------------
    // CollisionShapeDesc — one collider primitive. A body carries one or more
    // (multiple => a compound shape). Offset/OffsetRotation place the shape in
    // the body's local frame; Scale bakes the entity's world scale into the
    // primitive at build time. Convex/Mesh/HeightField carry their geometry.
    // ------------------------------------------------------------------------
    struct CollisionShapeDesc
    {
        enum class Kind { Box, Sphere, Capsule, ConvexHull, Mesh, HeightField };

        Kind      Shape       = Kind::Box;
        glm::vec3 HalfExtents{ 0.5f };               // Box: half-size per axis
        float     Radius      = 0.5f;                // Sphere / Capsule
        float     HalfHeight  = 0.5f;                // Capsule: half the cylinder part (excl. caps)
        glm::vec3 Offset{ 0.0f };                    // local translation
        glm::quat OffsetRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };                     // baked world scale

        // ConvexHull (points) / Mesh (triangle soup: Vertices + triangle Indices).
        std::vector<glm::vec3> Vertices;
        std::vector<uint32_t>  Indices;

        // HeightField: HeightFieldSize x HeightFieldSize row-major samples, world
        // height already applied. Offset0 = world position of sample (0,0);
        // CellSize = world metres between adjacent samples along X and Z.
        std::vector<float> HeightSamples;
        uint32_t  HeightFieldSize = 0;
        glm::vec3 HeightFieldOffset{ 0.0f };
        float     HeightFieldCellSize = 1.0f;
    };

    // ------------------------------------------------------------------------
    // BodyDesc — everything PhysicsWorld::CreateBody needs. Built from a
    // RigidBodyComponent + its sibling collider components at scene-start (J4).
    // ------------------------------------------------------------------------
    struct BodyDesc
    {
        MotionType Motion = MotionType::Dynamic;
        glm::vec3  Position{ 0.0f };
        glm::quat  Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

        // Material / dynamics.
        float Mass           = 1.0f;
        float Friction       = 0.5f;
        float Restitution    = 0.1f;
        float LinearDamping  = 0.05f;
        float AngularDamping = 0.05f;
        float GravityFactor  = 1.0f;
        bool  CCD            = false;   // continuous collision (fast small bodies)
        bool  StartAsleep    = false;
        bool  IsTrigger      = false;   // sensor: overlap events, no contact forces

        // Fine gameplay filter (category/mask, Box2D-style). Two bodies collide iff
        // (A.Category & B.CollidesWith) && (B.Category & A.CollidesWith). Applied on
        // top of the coarse object layer, in OnContactValidate + query filters.
        uint16_t Category    = 0x0001;
        uint16_t CollidesWith = 0xFFFF;

        uint64_t EntityId = 0;   // owning entity UUID -> Jolt body userData (query round-trip)

        std::vector<CollisionShapeDesc> Shapes;   // >= 1 required
    };

    // ------------------------------------------------------------------------
    // Query results.
    // ------------------------------------------------------------------------
    struct RayHit
    {
        uint64_t  EntityId = 0;        // owning entity UUID (0 = the query missed)
        glm::vec3 Point{ 0.0f };       // world hit point
        glm::vec3 Normal{ 0.0f };      // world surface normal at the hit
        float     Distance = 0.0f;     // along the ray from its origin
        bool      Hit = false;
    };

    // ------------------------------------------------------------------------
    // Contact events — queued by the Jolt contact listener (worker threads),
    // drained on the main thread after Step and dispatched to scripts (J5).
    // ------------------------------------------------------------------------
    enum class ContactKind { CollisionEnter, CollisionExit, TriggerEnter, TriggerExit };

    struct ContactEvent
    {
        uint64_t    EntityA = 0;   // for Trigger* events, A is the sensor's entity
        uint64_t    EntityB = 0;
        ContactKind Kind = ContactKind::CollisionEnter;
    };

    // ------------------------------------------------------------------------
    // PhysicsSettings — one-time world configuration.
    // ------------------------------------------------------------------------
    struct PhysicsSettings
    {
        glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
        uint32_t  MaxBodies            = 10240;
        uint32_t  MaxBodyPairs         = 65536;
        uint32_t  MaxContactConstraints = 20480;

        // Worker threads for the backend's own pool. -1 => auto (min(hw-1, 4)); 0 =>
        // single-threaded (the determinism proof pins this so two runs bit-match).
        // A third-party backend either honours 0 or documents that it ignores it.
        int32_t ThreadCount = -1;

        // Which IPhysicsBackend this world runs on (Phase 29 W3, ABI-appended).
        // Empty => PhysicsBackendRegistry::Default(), which is "jolt" unless an app
        // called SetDefault. An unregistered name logs and falls back to "null";
        // see physics/PhysicsBackend.h for how to register your own.
        std::string Backend;
    };

    // ------------------------------------------------------------------------
    // Statistics — cheap per-step counters for the editor HUD (J8).
    // ------------------------------------------------------------------------
    struct PhysicsStats
    {
        uint32_t BodyCount   = 0;
        uint32_t ActiveBodies = 0;   // awake (non-sleeping) dynamic/kinematic bodies
    };

    // ------------------------------------------------------------------------
    // CharacterDesc — a CharacterVirtual capsule (J6).
    // ------------------------------------------------------------------------
    struct CharacterDesc
    {
        glm::vec3 Position{ 0.0f };
        float     Height      = 1.8f;   // total capsule height (incl. both caps)
        float     Radius      = 0.3f;
        float     MaxSlopeDeg = 45.0f;
        float     StepHeight  = 0.35f;  // max obstacle height auto-stepped onto
        float     Mass        = 80.0f;
        uint64_t  EntityId    = 0;
    };
}
