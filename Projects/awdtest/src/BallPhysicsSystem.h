#pragma once

// BallPhysicsSystem.h
// Last Modified: 5/27/2026

/**
 * BallPhysicsSystem — Approach B Parallel Physics Implementation
 * ==============================================================
 *
 * See original header documentation in the design document for the full
 * architectural overview.  This file contains only the implementation.
 *
 * Key fix notes vs. earlier draft:
 *   - Explicit #include of DoubleBuffer.h (not pulled in transitively by Cosmic.h)
 *   - view.size() used instead of view.size_hint() — size_hint() is only
 *     available on multi-component views in this version of EnTT; single-
 *     component views expose size() directly.
 *   - PhysicsBody padding fields removed (see Components.h) so the
 *     static_assert(sizeof == 32) passes.
 */

#include <Cosmic.h>
#include "jobs/DoubleBuffer.h"   // Not included transitively via Cosmic.h — must be explicit
#include "Components.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Workspace
{
    class BallPhysicsSystem : public Cosmic::ParallelSystem
    {
    public:
        BallPhysicsSystem() = default;

        // =====================================================================
        // Simulation parameters — written by ImGui, read by parallel workers.
        // Workers capture these by value inside the lambda at submit time, so
        // there is no live data race.  Changes take effect on the next tick.
        // =====================================================================
        float Gravity = -9.8f;
        float Damping = 0.85f;
        float BoundsX = 6.0f;
        float BoundsY = 4.0f;

        // =====================================================================
        // PASS B — OnFixedPrepare
        // =====================================================================
        virtual void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
        {
            entt::registry& reg = scene.GetRegistry();
            auto view = reg.view<PhysicsBody>();

            // view.size() is correct for a single-component view.
            // view.size_hint() requires a multi-component view (storage_view) in
            // this version of EnTT and will fail to compile on single-component views.
            const size_t count = view.size();

            if (m_Buffer.Count() != count)
            {
                m_Buffer.Resize(count);
                m_EntitySlots.clear();
                m_EntitySlots.reserve(count);
            }

            m_EntitySlots.clear();
            size_t slot = 0;
            for (auto entity : view)
            {
                m_Buffer.WriteAt(slot) = reg.get<PhysicsBody>(entity);
                m_EntitySlots.push_back(entity);
                ++slot;
            }

            // Promote write → read.  Workers will read this stable snapshot.
            m_Buffer.Swap();
        }

        // =====================================================================
        // PASS C — OnFixedParallelExecute
        // =====================================================================
        virtual void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
        {
            const size_t count = m_Buffer.Count();
            if (count == 0) return;

            m_LogTimer += fixedDt;
            if (m_LogTimer >= 5.0f)
            {
                CS_INFO("BallPhysicsSystem: Integrating {0} bodies across {1} workers.",
                    count, Cosmic::JobSystem::Get().GetWorkerCount());
                m_LogTimer = 0.0f;
            }

            // Capture by value — workers have no dependency on 'this'.
            const float gravity = Gravity;
            const float damping = Damping;
            const float boundsX = BoundsX;
            const float boundsY = BoundsY;
            const float dt = fixedDt;

            const PhysicsBody* readBuf = m_Buffer.GetReadBuffer();
            PhysicsBody* writeBuf = m_Buffer.GetWriteBuffer();

            Cosmic::ParallelFor(count,
                [readBuf, writeBuf, gravity, damping, boundsX, boundsY, dt]
                (size_t begin, size_t end)
                {
                    for (size_t i = begin; i < end; ++i)
                    {
                        PhysicsBody body = readBuf[i];

                        // Gravity integration
                        body.Velocity.y += gravity * dt;

                        // Position integration
                        body.Position.x += body.Velocity.x * dt;
                        body.Position.y += body.Velocity.y * dt;

                        // Per-body effective damping
                        const float ed = damping * body.Restitution * body.LinearDrag;

                        // X boundary
                        if (body.Position.x + body.Radius > boundsX)
                        {
                            body.Position.x = boundsX - body.Radius;
                            body.Velocity.x = -body.Velocity.x * ed;
                        }
                        else if (body.Position.x - body.Radius < -boundsX)
                        {
                            body.Position.x = -boundsX + body.Radius;
                            body.Velocity.x = -body.Velocity.x * ed;
                        }

                        // Y boundary
                        if (body.Position.y - body.Radius < -boundsY)
                        {
                            body.Position.y = -boundsY + body.Radius;
                            body.Velocity.y = -body.Velocity.y * ed;
                            body.Velocity.x *= ed; // floor friction
                        }
                        else if (body.Position.y + body.Radius > boundsY)
                        {
                            body.Position.y = boundsY - body.Radius;
                            body.Velocity.y = -body.Velocity.y * ed;
                        }

                        writeBuf[i] = body;
                    }
                },
                32
            );
        }

        // =====================================================================
        // PASS D — OnFixedMerge
        // =====================================================================
        virtual void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
        {
            if (m_Buffer.Count() == 0) return;

            // Promote workers' write buffer → read buffer.
            m_Buffer.Swap();

            entt::registry& reg = scene.GetRegistry();
            const PhysicsBody* results = m_Buffer.GetReadBuffer();
            const size_t count = m_EntitySlots.size();

            for (size_t i = 0; i < count; ++i)
            {
                const entt::entity entity = m_EntitySlots[i];
                if (!reg.valid(entity)) continue;

                // Pass 1: write back integrated PhysicsBody state
                auto* body = reg.try_get<PhysicsBody>(entity);
                if (body) *body = results[i];

                // Pass 2: sync position into TransformComponent for the renderer
                auto* transform = reg.try_get<Cosmic::TransformComponent>(entity);
                if (transform)
                {
                    transform->Position.x = results[i].Position.x;
                    transform->Position.y = results[i].Position.y;
                    // Z (depth / layering) is not owned by the physics system.
                }
            }
        }

    private:
        Cosmic::DoubleBuffer<PhysicsBody> m_Buffer;
        std::vector<entt::entity>         m_EntitySlots;
        float                             m_LogTimer = 0.0f;
    };

} // namespace Workspace