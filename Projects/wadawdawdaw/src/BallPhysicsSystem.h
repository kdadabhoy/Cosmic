#pragma once
// BallPhysicsSystem.h
// Last Modified: 5/28/2026

/**
 * ============================================================================
 * BallPhysicsSystem
 * ============================================================================
 *
 * A parallel physics system that integrates all PhysicsBody components each
 * fixed timestep and syncs the computed positions back to TransformComponent
 * for rendering.
 *
 * HOW IT WORKS
 * ------------
 * The system declares a single ReadWriteQuery<PhysicsBody> member. The engine
 * automatically stages (snapshots) all PhysicsBody components before each
 * tick and commits the computed results back after the merge phase.
 *
 * The only methods you need to override:
 *
 *   OnFixedParallelExecute — submit the integration jobs via ForEachAsync.
 *   OnFixedMerge           — sync computed positions to TransformComponent.
 *
 * Everything else — double-buffering, entity mapping, registry reads/writes,
 * WaitIdle barriers — is handled by the engine.
 *
 * REGISTRATION
 * ------------
 *   scene->AddSystem<BallPhysicsSystem>();
 *
 * That is all. No manual Prepare/Merge wiring needed.
 *
 * SIMULATION PARAMETERS
 * ---------------------
 * Gravity, Damping, BoundsX, and BoundsY are public fields you can adjust
 * at runtime from an ImGui panel or from the owning Layer.
 *
 * PROFILING
 * ---------
 * TimePrepareMs, TimeExecuteMs, and TimeMergeMs are populated each fixed tick
 * so you can display them in the ImGui panel for live telemetry.
 * ============================================================================
 */

#include <Cosmic.h>
#include "Components.h"
#include <glm/glm.hpp>
#include <chrono>

namespace Workspace
{
    class BallPhysicsSystem : public Cosmic::ParallelSystem
    {
    public:
        // =====================================================================
        // Public simulation parameters — set from Layer or ImGui
        // =====================================================================
        float Gravity = -9.8f;
        float Damping = 0.85f;
        float BoundsX = 6.0f;
        float BoundsY = 4.0f;

        // Profiling telemetry (milliseconds, updated each fixed tick)
        float TimePrepareMs = 0.0f;
        float TimeExecuteMs = 0.0f;
        float TimeMergeMs   = 0.0f;

    private:
        // =====================================================================
        // Query declarations
        //
        // Passing `this` registers each query with this system. The engine
        // calls Stage() before OnFixedPrepare and Commit() after OnFixedMerge
        // automatically — no manual snapshotting or writeback code needed.
        // =====================================================================
        Cosmic::ReadWriteQuery<PhysicsBody> m_Bodies{ this };

    public:
        BallPhysicsSystem() = default;

        // =====================================================================
        // PASS B — OnFixedPrepare  (optional override)
        //
        // The engine has already staged m_Bodies before this is called.
        // Override only if you need custom per-tick setup — for this system
        // we just record timing.
        // =====================================================================
        void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
        {
            m_PrepareStart = Clock::now();
        }

        // =====================================================================
        // PASS C — OnFixedParallelExecute
        //
        // Submit parallel integration jobs. The Scene calls WaitIdle() once
        // after ALL systems have submitted — do not call it here.
        // ForEachAsync submits jobs and returns immediately.
        // =====================================================================
        void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
        {
            TimePrepareMs = ElapsedMs(m_PrepareStart, Clock::now());

            if (m_Bodies.IsEmpty()) return;

            auto execStart = Clock::now();

            // Capture constants by value — safe cross-thread.
            const float gravity = Gravity;
            const float damping = Damping;
            const float boundsX = BoundsX;
            const float boundsY = BoundsY;
            const float dt      = fixedDt;

            m_Bodies.ForEachAsync([gravity, damping, boundsX, boundsY, dt](PhysicsBody& body)
            {
                // --- Integrate gravity ---
                body.Velocity.y += gravity * dt;

                // --- Air drag ---
                float airDrag = 1.0f - (damping * body.LinearDrag * dt);
                body.Velocity *= glm::clamp(airDrag, 0.0f, 1.0f);

                // --- Integrate position ---
                body.Position.x += body.Velocity.x * dt;
                body.Position.y += body.Velocity.y * dt;

                // --- Bounce energy retention ---
                const float retention = glm::clamp(body.Restitution - damping, 0.0f, 1.0f);

                // --- X bounds ---
                if (body.Position.x + body.Radius > boundsX)
                {
                    body.Position.x  = boundsX - body.Radius;
                    body.Velocity.x *= -retention;
                }
                else if (body.Position.x - body.Radius < -boundsX)
                {
                    body.Position.x  = -boundsX + body.Radius;
                    body.Velocity.x *= -retention;
                }

                // --- Y bounds: floor ---
                if (body.Position.y - body.Radius < -boundsY)
                {
                    body.Position.y  = -boundsY + body.Radius;
                    body.Velocity.y *= -retention;

                    // Floor friction
                    float friction = glm::clamp(1.0f - damping * 2.0f * dt, 0.0f, 1.0f);
                    body.Velocity.x *= friction;

                    // Micro-jitter mitigation
                    if (glm::abs(body.Velocity.y) < 0.15f && gravity < 0.0f)
                        body.Velocity.y = 0.0f;
                }
                // --- Y bounds: ceiling ---
                else if (body.Position.y + body.Radius > boundsY)
                {
                    body.Position.y  = boundsY - body.Radius;
                    body.Velocity.y *= -retention;
                }
            }, 32); // minChunkSize = 32 for fine-grained distribution

            TimeExecuteMs = ElapsedMs(execStart, Clock::now());
        }

        // =====================================================================
        // PASS D — OnFixedMerge
        //
        // The engine has called WaitIdle() — all jobs are done.
        // m_Bodies now holds the computed results for this tick.
        //
        // The engine will auto-commit m_Bodies → PhysicsBody registry AFTER
        // this function returns. Here we just sync positions to TransformComponent
        // for the renderer.
        // =====================================================================
        void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
        {
            auto mergeStart = Clock::now();

            auto& reg = scene.GetRegistry();

            m_Bodies.ForEachWithEntity([&reg](PhysicsBody& body, entt::entity entity)
            {
                if (!reg.valid(entity)) return;
                auto& transform = reg.get<Cosmic::TransformComponent>(entity);
                transform.Position.x = body.Position.x;
                transform.Position.y = body.Position.y;
            });

            TimeMergeMs = ElapsedMs(mergeStart, Clock::now());
        }

    private:
        using Clock     = std::chrono::high_resolution_clock;
        using TimePoint = std::chrono::time_point<Clock>;

        TimePoint m_PrepareStart = Clock::now();

        static float ElapsedMs(TimePoint start, TimePoint end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    };

} // namespace Workspace
