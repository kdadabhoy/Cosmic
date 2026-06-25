#pragma once

// AgentSystem.h
// Last Modified: 5/29/2026

/**
 * Self-contained steering-agent simulation decoupled from TemplateTelemetryLayer.
 *
 * AgentComponent holds all worker-owned state. The parallel system integrates
 * position, steers toward random targets, and records 5 telemetry channels.
 * OnFixedMerge copies agent.position back to TransformComponent for rendering.
 *
 * Component layout (48 bytes, fits in one cache line):
 *   velocity  8   worker-owned XY velocity
 *   target    8   current steering target in world space
 *   position  8   worker-owned XY; merged into TransformComponent in OnFixedMerge
 *   speed     4   max movement speed (units/s)
 *   power     4   sinusoidal cosmetic channel
 *   heading   4   atan2(vy, vx) in radians
 *   recordId  4   DataRecorder registration ID
 *   _pad      8   explicit pad to align to 48 bytes
 */

#include <Cosmic.h>
#include <glm/glm.hpp>
#include <random>
#include <chrono>
#include <cmath>

namespace Workspace
{
    // =========================================================================
    // AgentComponent
    // =========================================================================

    struct AgentComponent
    {
        glm::vec2 velocity = { 0.0f, 0.0f }; //  8
        glm::vec2 target   = { 0.0f, 0.0f }; //  8
        glm::vec2 position = { 0.0f, 0.0f }; //  8  worker-owned; synced in Merge
        float     speed    = 3.0f;            //  4
        float     power    = 1.0f;            //  4
        float     heading  = 0.0f;            //  4
        uint32_t  recordId = 0;               //  4  DataRecorder ID
        float     _pad[2]  = {};              //  8  alignment pad → 48 bytes total
    };

    // =========================================================================
    // AgentSystem
    // =========================================================================

    class AgentSystem : public Cosmic::ParallelSystem
    {
    public:
        // Public profiling readouts — updated each fixed tick.
        float TimePrepareMs = 0.0f;
        float TimeExecuteMs = 0.0f;
        float TimeMergeMs   = 0.0f;

        AgentSystem(Cosmic::DataRecorder* recorder, float bounds)
            : m_Recorder(recorder), m_Bounds(bounds)
        {}

        // -------------------------------------------------------------------
        // PASS B — OnFixedPrepare  (main thread)
        // -------------------------------------------------------------------
        void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
        {
            m_PrepareStart = Clock::now();
        }

        // -------------------------------------------------------------------
        // PASS C — OnFixedParallelExecute  (worker threads)
        //
        // Rules:
        //   - ForEachAsync only (no WaitIdle).
        //   - No EnTT registry access — read/write AgentComponent only.
        //   - All captures by value (closures outlive this stack frame).
        // -------------------------------------------------------------------
        void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
        {
            TimePrepareMs = ElapsedMs(m_PrepareStart, Clock::now());
            if (m_Agents.IsEmpty()) return;

            auto execStart = Clock::now();

            Cosmic::DataRecorder* recorder = m_Recorder;
            const float bounds = m_Bounds;
            const float dt     = fixedDt;

            m_Agents.ForEachAsync([recorder, bounds, dt](AgentComponent& agent)
            {
                thread_local std::mt19937 rng{ std::random_device{}() };
                thread_local std::uniform_real_distribution<float> tgtDist(-1.0f, 1.0f);

                // Steer toward target; pick a new one when close enough.
                glm::vec2 toTarget = agent.target - agent.position;
                float dist = glm::length(toTarget);

                if (dist < 0.2f)
                {
                    const float r = bounds * 0.9f;
                    agent.target = { tgtDist(rng) * r, tgtDist(rng) * r };
                }
                else
                {
                    agent.velocity = (toTarget / dist) * agent.speed;
                }

                agent.position += agent.velocity * dt;
                agent.position.x = glm::clamp(agent.position.x, -bounds, bounds);
                agent.position.y = glm::clamp(agent.position.y, -bounds, bounds);

                float vLen = glm::length(agent.velocity);
                if (vLen > 0.01f)
                    agent.heading = std::atan2(agent.velocity.y, agent.velocity.x);

                agent.power = 0.5f + 0.5f * std::sin(agent.heading * 3.0f + agent.position.x);

                recorder->Record(agent.recordId, {
                    agent.position.x,
                    agent.position.y,
                    vLen,
                    agent.heading,
                    agent.power
                });

            }, 2); // minChunkSize=2 → up to 10 parallel chunks for 20 agents

            TimeExecuteMs = ElapsedMs(execStart, Clock::now());
        }

        // -------------------------------------------------------------------
        // PASS D — OnFixedMerge  (main thread)
        //
        // All jobs done. Copy agent.position → TransformComponent.
        // -------------------------------------------------------------------
        void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
        {
            auto mergeStart = Clock::now();

            auto& reg = scene.GetRegistry();
            m_Agents.ForEachWithEntity([&reg](AgentComponent& agent, entt::entity entity)
            {
                if (!reg.valid(entity)) return;
                auto& t = reg.get<Cosmic::TransformComponent>(entity);
                t.Position.x = agent.position.x;
                t.Position.y = agent.position.y;
            });

            TimeMergeMs = ElapsedMs(mergeStart, Clock::now());
        }

    private:
        using Clock     = std::chrono::high_resolution_clock;
        using TimePoint = std::chrono::time_point<Clock>;

        Cosmic::DataRecorder*                  m_Recorder = nullptr;
        float                                  m_Bounds   = 5.0f;
        Cosmic::ReadWriteQuery<AgentComponent> m_Agents{ this };

        TimePoint m_PrepareStart = Clock::now();

        static float ElapsedMs(TimePoint start, TimePoint end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    };

} // namespace Workspace

// Stable cross-DLL type hash — must be at global scope after the struct.
CS_REGISTER_COMPONENT(Workspace::AgentComponent)
