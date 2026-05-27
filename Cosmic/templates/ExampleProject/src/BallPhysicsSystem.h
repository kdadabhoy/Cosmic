#pragma once

#include <Cosmic.h>
#include "Components.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // Required for GetCurrentThreadId()

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


		// =====================================================================
		// 1. PASS B — Prepare
		// =====================================================================
		virtual void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
		{
			entt::registry& reg = scene.GetRegistry();

			// Query elements matching BOTH required components simultaneously to guarantee aligned paged memory tables
			auto matchedView = reg.view<Cosmic::TransformComponent, BallComponent>();

			// Synchronize both buffers onto identical structural indices mapped cleanly by view allocation order
			m_Transforms.PrepareFromView(reg, matchedView);
			m_PhysicsData.PrepareFromView(reg, matchedView);
		}

		// =====================================================================
		// 2. PASS C — Parallel Execute
		// =====================================================================
		virtual void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
		{
			size_t elementCount = m_PhysicsData.Count();
			if (elementCount == 0 || m_Transforms.IsEmpty()) return;

			// Active Thread Optimization Log: Dynamic runtime worker-pool tracking
			// Accumulate time and log only every 5 seconds
			m_LogTimer += fixedDt;
			if (m_LogTimer >= 5.0f)
			{
				uint32_t activeWorkers = Cosmic::JobSystem::Get().GetWorkerCount();
				CS_INFO("BallPhysicsSystem: Processing {0} objects across {1} workers.",
					elementCount, activeWorkers);

				m_LogTimer = 0.0f; // Reset the timer
			}

			float g = Gravity;
			float d = Damping;
			float bx = BoundsX;
			float by = BoundsY;

			Cosmic::TransformComponent* transformRaw = m_Transforms.Data();
			BallComponent* physicsRaw = m_PhysicsData.Data();

			Cosmic::ParallelFor(elementCount, [transformRaw, physicsRaw, g, d, bx, by, fixedDt](size_t begin, size_t end)
				{
					// Thread Chunk Audit: Identifies exactly which Win32 Thread Handle is crunching which block
					//CS_TRACE("Thread [ID: {0}] claimed index slice [{1} -> {2})", GetCurrentThreadId(), begin, end);

					for (size_t i = begin; i < end; ++i)
					{
						auto& t = transformRaw[i];
						auto& b = physicsRaw[i];

						// Integrate Gravity & Position steps
						b.Velocity.y += g * fixedDt;
						t.Position.x += b.Velocity.x * fixedDt;
						t.Position.y += b.Velocity.y * fixedDt;

						// Hard boundary collision checks (X Axis)
						if (t.Position.x + b.Radius > bx) { t.Position.x = bx - b.Radius;  b.Velocity.x = -b.Velocity.x * d; }
						else if (t.Position.x - b.Radius < -bx) { t.Position.x = -bx + b.Radius; b.Velocity.x = -b.Velocity.x * d; }

						// Hard boundary collision checks (Y Axis)
						if (t.Position.y - b.Radius < -by) { t.Position.y = -by + b.Radius; b.Velocity.y = -b.Velocity.y * d; b.Velocity.x *= d; }
						else if (t.Position.y + b.Radius > by) { t.Position.y = by - b.Radius;  b.Velocity.y = -b.Velocity.y * d; }
					}
				}, 32);
		}

		// =====================================================================
		// 3. PASS D — Merge
		// =====================================================================
		virtual void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
		{
			// Safely commit flat pointer changes directly back into internal stable EnTT memory pages
			m_Transforms.WriteBack(scene.GetRegistry());
			m_PhysicsData.WriteBack(scene.GetRegistry());
		}


	private:
		Cosmic::FlatComponentArray<Cosmic::TransformComponent> m_Transforms;
		Cosmic::FlatComponentArray<BallComponent> m_PhysicsData;
		float m_LogTimer = 0.0f; // Track time here
	};
}