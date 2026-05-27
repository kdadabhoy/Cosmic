#pragma once

// Components.h
// Last Modified: 5/27/2026

/**
 * Components.h
 * ============
 * Defines all client-side ECS components used by the awdtest.
 * Every component that crosses the engine/DLL boundary MUST be registered
 * with CS_REGISTER_COMPONENT so EnTT uses a stable compile-time hash
 * instead of a sequential counter that differs between binaries.
 *
 * Component Inventory
 * -------------------
 * BallComponent   — Visual/identity data for a simulated ball entity.
 *                   Contains rendering color and the physical radius used
 *                   for drawing.  NOT read during the parallel physics pass.
 *
 * PhysicsBody     — The dedicated simulation state component (Approach B).
 *                   Contains EVERYTHING the physics parallel pass needs:
 *                   position, velocity, radius, mass, restitution, and
 *                   per-body damping override.  Kept intentionally separate
 *                   from TransformComponent (renderer concern) and from
 *                   BallComponent (visual concern) so the parallel workers
 *                   touch a single, self-contained struct with no pointer
 *                   chasing or multi-pool alignment dependencies.
 *
 * Design rules:
 *   - All fields are plain value types (no pointers, no Ref<>, no std::string).
 *   - The struct is trivially copyable so DoubleBuffer<PhysicsBody> can use
 *     std::memcpy safely and the compiler can auto-vectorise inner loops.
 *   - Any field the renderer or UI needs is synced back to TransformComponent
 *     by BallPhysicsSystem::OnFixedMerge — the single authoritative sync point.
 *
 * Memory layout (PhysicsBody = exactly 32 bytes):
 *   Position   glm::vec2   8 bytes   offset  0
 *   Velocity   glm::vec2   8 bytes   offset  8
 *   Radius     float       4 bytes   offset 16
 *   Mass       float       4 bytes   offset 20
 *   Restitution float      4 bytes   offset 24
 *   LinearDrag float       4 bytes   offset 28
 *                         --------
 *   Total                 32 bytes   (one cache-line half, SIMD-friendly)
 *
 * NOTE: The two _pad fields that appeared in earlier drafts were removed.
 *   glm::vec2 (8) + glm::vec2 (8) + 4 floats (16) = 32 exactly.
 *   Adding padding fields pushed the size to 40, breaking the static_assert.
 */

#include <glm/glm.hpp>
#include <Cosmic.h>

namespace Workspace
{
    // =========================================================================
    // BallComponent
    // Visual / identity data.  Read only by the render loop and ImGui.
    // Never touched by the parallel physics workers.
    // =========================================================================
    struct BallComponent
    {
        float     Radius = 0.2f;     // World-unit radius used for DrawCircle size
        float     Mass = 1.0f;     // Kept here for UI display; authoritative copy
        // lives in PhysicsBody
        glm::vec4 Color = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // =========================================================================
    // PhysicsBody
    // The dedicated parallel-physics component (Approach B).
    //
    // Layout rules:
    //   - Trivially copyable: DoubleBuffer uses std::memcpy internally.
    //   - All simulation state lives here so the parallel pass touches
    //     exactly ONE component type with NO cross-pool index alignment.
    //   - Position is mirrored INTO TransformComponent by the merge pass;
    //     it is NOT read FROM TransformComponent during the physics pass.
    //
    // When to add this component:
    //   Add it at the same time as BallComponent (in SpawnBall).
    //   PhysicsBody::Position must be seeded from the desired spawn
    //   position before the first physics tick runs.
    // =========================================================================
    struct PhysicsBody
    {
        // --- Kinematic state (read + write by parallel workers) ---
        glm::vec2 Position = { 0.0f, 0.0f }; // World-space XY; Z handled by TransformComponent
        glm::vec2 Velocity = { 0.0f, 0.0f }; // World units per second

        // --- Physical properties (read-only by parallel workers) ---
        float Radius = 0.2f;   // Collision radius in world units
        float Mass = 1.0f;   // Used for future impulse/response calculations
        float Restitution = 0.85f;  // Coefficient of restitution (bounce factor, 0-1)
        float LinearDrag = 1.0f;   // Per-body drag multiplier (1.0 = use system default)

        // Total: 8 + 8 + 4 + 4 + 4 + 4 = 32 bytes exactly.
        // DO NOT add fields without updating the static_assert below.
    };

    static_assert(sizeof(PhysicsBody) == 32,
        "PhysicsBody must be exactly 32 bytes for cache-line alignment in parallel loops.");

} // namespace Workspace

// =============================================================================
// DLL-safe component registration
// =============================================================================
CS_REGISTER_COMPONENT(Workspace::BallComponent)
CS_REGISTER_COMPONENT(Workspace::PhysicsBody)
