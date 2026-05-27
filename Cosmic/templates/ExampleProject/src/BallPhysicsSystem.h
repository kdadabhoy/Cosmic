#pragma once
#include <Cosmic.h>
#include "Components.h"

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

		// 1. Snapshot component records sequentially into flat buffers before threading starts
		virtual void OnFixedPrepare(Cosmic::Scene& scene, float fixedDt) override
		{
			entt::registry& reg = scene.GetRegistry();

			// Query elements matching BOTH required components simultaneously
			auto matchedView = reg.view<Cosmic::TransformComponent, BallComponent>();

			// Synchronize both arrays onto identical indices mapped by view order
			m_Transforms.PrepareFromView(reg, matchedView);
			m_PhysicsData.PrepareFromView(reg, matchedView);

			CS_INFO("BallPhysicsSystem::OnFixedPrepare -> Synced {0} index-aligned balls into FlatComponentArrays.", m_PhysicsData.Count());
		}

		// 2. Compute steps safely in parallel chunks
		virtual void OnFixedParallelExecute(Cosmic::Scene& scene, float fixedDt) override
		{
			size_t elementCount = m_PhysicsData.Count();

			// DIAGNOSTIC LOG 2
			CS_INFO("BallPhysicsSystem::OnFixedParallelExecute -> Submitting batch range for {0} elements.", elementCount);

			if (elementCount == 0 || m_Transforms.IsEmpty()) return;

			float g = Gravity;
			float d = Damping;
			float bx = BoundsX;
			float by = BoundsY;

			Cosmic::TransformComponent* transformRaw = m_Transforms.Data();
			BallComponent* physicsRaw = m_PhysicsData.Data();

			Cosmic::ParallelFor(elementCount, [transformRaw, physicsRaw, g, d, bx, by, fixedDt](size_t begin, size_t end)
				{
					for (size_t i = begin; i < end; ++i)
					{
						auto& t = transformRaw[i];
						auto& b = physicsRaw[i];

						b.Velocity.y += g * fixedDt;
						t.Position.x += b.Velocity.x * fixedDt;
						t.Position.y += b.Velocity.y * fixedDt;

						if (t.Position.x + b.Radius > bx) { t.Position.x = bx - b.Radius;  b.Velocity.x = -b.Velocity.x * d; }
						else if (t.Position.x - b.Radius < -bx) { t.Position.x = -bx + b.Radius; b.Velocity.x = -b.Velocity.x * d; }

						if (t.Position.y - b.Radius < -by) { t.Position.y = -by + b.Radius; b.Velocity.y = -b.Velocity.y * d; b.Velocity.x *= d; }
						else if (t.Position.y + b.Radius > by) { t.Position.y = by - b.Radius;  b.Velocity.y = -b.Velocity.y * d; }
					}
				}, 32);
		}

		// 3. Commit the updated states back to EnTT's primary storage allocations
		virtual void OnFixedMerge(Cosmic::Scene& scene, float fixedDt) override
		{
			// DIAGNOSTIC LOG 3
			CS_INFO("BallPhysicsSystem::OnFixedMerge -> Flushing updates back to main EnTT registry.");

			m_Transforms.WriteBack(scene.GetRegistry());
			m_PhysicsData.WriteBack(scene.GetRegistry());
		}

	private:
		Cosmic::FlatComponentArray<Cosmic::TransformComponent> m_Transforms;
		Cosmic::FlatComponentArray<BallComponent> m_PhysicsData;
	};
}