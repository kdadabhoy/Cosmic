#pragma once
// BallPhysicsSystem.h
// Last Modified: 5/27/2026

#include <Cosmic.h>
#include "jobs/DoubleBuffer.h"   
#include "Components.h"
#include <chrono>
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

		float Gravity = -9.8f;
		float Damping = 0.85f;
		float BoundsX = 6.0f;
		float BoundsY = 4.0f;

		// Profiling Telemetry Data
		float TimePrepareMs = 0.0f;
		float TimeExecuteMs = 0.0f;
		float TimeMergeMs = 0.0f;

		// =====================================================================
		// PASS B — OnFixedPrepare
		// =====================================================================
		virtual void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
		{
			auto start = std::chrono::high_resolution_clock::now();

			entt::registry& reg = scene.GetRegistry();

			// OPTIMIZATION: Initialize / enforce an owning group layout.
			// This tells EnTT to tightly pack matching components sequentially.
			auto group = reg.group<PhysicsBody>(entt::get<Cosmic::TransformComponent>);

			const size_t count = group.size();

			if (m_Buffer.Count() != count)
			{
				m_Buffer.Resize(count);
				m_EntitySlots.clear();
				m_EntitySlots.reserve(count);
			}

			m_EntitySlots.clear();
			size_t slot = 0;

			// Group iteration is exceptionally fast and perfectly ordered
			for (auto entity : group)
			{
				m_Buffer.WriteAt(slot) = group.get<PhysicsBody>(entity);
				m_EntitySlots.push_back(entity);
				++slot;
			}

			m_Buffer.Swap();

			auto end = std::chrono::high_resolution_clock::now();
			TimePrepareMs = std::chrono::duration<float, std::milli>(end - start).count();
		}

		// =====================================================================
		// PASS C — OnFixedParallelExecute
		// =====================================================================
		virtual void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
		{
			auto start = std::chrono::high_resolution_clock::now();

			const size_t count = m_Buffer.Count();
			if (count == 0)
			{
				TimeExecuteMs = 0.0f;
				return;
			}

			m_LogTimer += fixedDt;
			if (m_LogTimer >= 5.0f)
			{
				CS_INFO("BallPhysicsSystem: Integrating {0} bodies across {1} workers.",
					count, Cosmic::JobSystem::Get().GetWorkerCount());
				m_LogTimer = 0.0f;
			}

			const float gravity = Gravity;
			const float damping = Damping; // ImGui Slider (0.0 = bounce forever, 1.0 = stop instantly)
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

						// 1. Integrate forces (Gravity) and apply linear drag in flight
						body.Velocity.y += gravity * dt;

						// Apply standard atmospheric air resistance drag while moving
						// If drag multiplier is 1.0 and damping is 0, this does nothing.
						float airDragFactor = 1.0f - (damping * body.LinearDrag * dt);
						body.Velocity *= glm::clamp(airDragFactor, 0.0f, 1.0f);

						// Update positions based on newly calculated velocities
						body.Position.x += body.Velocity.x * dt;
						body.Position.y += body.Velocity.y * dt;

						// 2. Compute true structural energy conservation ratio during impacts.
						// If damping slider is 0.0, energy retention is driven 100% by Restitution.
						// If Restitution is 1.0 and Damping is 0.0, bounces are completely lossless.
						const float totalBounceRetention = glm::clamp(body.Restitution - damping, 0.0f, 1.0f);

						// --- X Axis Bounds Collision ---
						if (body.Position.x + body.Radius > boundsX)
						{
							body.Position.x = boundsX - body.Radius;
							body.Velocity.x = -body.Velocity.x * totalBounceRetention;
						}
						else if (body.Position.x - body.Radius < -boundsX)
						{
							body.Position.x = -boundsX + body.Radius;
							body.Velocity.x = -body.Velocity.x * totalBounceRetention;
						}

						// --- Y Axis Bounds Collision ---
						if (body.Position.y - body.Radius < -boundsY) // Floor
						{
							body.Position.y = -boundsY + body.Radius;
							body.Velocity.y = -body.Velocity.y * totalBounceRetention;

							// Apply horizontal floor friction (only when contacting the floor)
							// Scales smoothly alongside the core damping factor.
							float floorFriction = glm::clamp(1.0f - (damping * 2.0f * dt), 0.0f, 1.0f);
							body.Velocity.x *= floorFriction;

							// Micro-jitter mitigation: Kill tiny velocities to allow resting states
							if (glm::abs(body.Velocity.y) < 0.15f && gravity < 0.0f)
							{
								body.Velocity.y = 0.0f;
							}
						}
						else if (body.Position.y + body.Radius > boundsY) // Ceiling
						{
							body.Position.y = boundsY - body.Radius;
							body.Velocity.y = -body.Velocity.y * totalBounceRetention;
						}

						writeBuf[i] = body;
					}
				},
				32
			);

			auto end = std::chrono::high_resolution_clock::now();
			TimeExecuteMs = std::chrono::duration<float, std::milli>(end - start).count();
		}


		// =====================================================================
        // PASS D — OnFixedMerge
        // =====================================================================
        virtual void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
        {
            auto start = std::chrono::high_resolution_clock::now();

            if (m_Buffer.Count() == 0)
            {
                TimeMergeMs = 0.0f;
                return;
            }

            // Promote workers' write buffer → read buffer.
            m_Buffer.Swap();

            entt::registry& reg = scene.GetRegistry();
            const PhysicsBody* results = m_Buffer.GetReadBuffer();
            const size_t count = m_EntitySlots.size();

            // Re-acquire the exact group layout context used in Prepare
            auto group = reg.group<PhysicsBody>(entt::get<Cosmic::TransformComponent>);

            for (size_t i = 0; i < count; ++i)
            {
                const entt::entity entity = m_EntitySlots[i];

                // Fast group-member lookup. Avoids registry hash-mapping completely.
                if (group.contains(entity))
                {
                    // Update the sorted PhysicsBody
                    group.get<PhysicsBody>(entity) = results[i];

                    // Sync data over to the paired TransformComponent
                    auto& transform = group.get<Cosmic::TransformComponent>(entity);
                    transform.Position.x = results[i].Position.x;
                    transform.Position.y = results[i].Position.y;
                }
            }

            auto end = std::chrono::high_resolution_clock::now();
            TimeMergeMs = std::chrono::duration<float, std::milli>(end - start).count();
        }

	private:
		Cosmic::DoubleBuffer<PhysicsBody> m_Buffer;
		std::vector<entt::entity>         m_EntitySlots;
		float                             m_LogTimer = 0.0f;
	};

} // namespace Workspace