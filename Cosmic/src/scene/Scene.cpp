// Optimized Material Grouping System implemented 5/21/2026
// Scene::OnRender camera parameter + BeginScene/EndScene ownership added 5/26/2026
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "renderer/Renderer2D.h"
#include "renderer/Renderer3D.h"
#include "terrain/Terrain.h"
#include "camera/OrthographicCamera.h"
#include "camera/Camera.h"
#include "jobs/JobSystem.h"
#include "core/Log.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace Cosmic
{
	Scene::Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_Registry.destroy((entt::entity)entity);
	}

	void Scene::OnUpdate(float deltaTime)
	{
		// PASS A — Sequential systems (main thread)
		for (auto& system : m_Systems)
			system->OnUpdate(*this, deltaTime);

		// PASS B/C/D — Parallel systems (skipped entirely if none registered)
		if (!m_ParallelSystems.empty())
		{
			// PASS B — Stage queries (engine), then optional user setup (main thread)
			for (auto* ps : m_ParallelSystems)
			{
				ps->StageQueries(*this);
				ps->OnPrepare(*this, deltaTime);
			}

			// PASS C — All systems submit async jobs before any barrier (main thread)
			for (auto* ps : m_ParallelSystems)
				ps->OnParallelExecute(*this, deltaTime);

			// Single barrier: every job from every system completes here
			JobSystem::Get().WaitIdle();

			// PASS D — Optional user finalization, then commit queries (main thread)
			for (auto* ps : m_ParallelSystems)
			{
				ps->OnMerge(*this, deltaTime);
				ps->CommitQueries(*this);
			}
		}
	}

	void Scene::OnFixedUpdate(float fixedDeltaTime)
	{
		// PASS A — Sequential fixed-step systems (main thread)
		for (auto& system : m_Systems)
			system->OnFixedUpdate(*this, fixedDeltaTime);

		// PASS B/C/D — Fixed-timestep parallel systems
		if (!m_ParallelSystems.empty())
		{
			for (auto* ps : m_ParallelSystems)
			{
				ps->StageQueries(*this);
				ps->OnFixedPrepare(*this, fixedDeltaTime);
			}

			for (auto* ps : m_ParallelSystems)
				ps->OnFixedParallelExecute(*this, fixedDeltaTime);

			JobSystem::Get().WaitIdle();

			for (auto* ps : m_ParallelSystems)
			{
				ps->OnFixedMerge(*this, fixedDeltaTime);
				ps->CommitQueries(*this);
			}
		}
	}

	void Scene::OnRender(const OrthographicCamera& camera)
	{
		// The scene owns the full render pass. Callers must NOT wrap this
		// call in their own BeginScene/EndScene.
		Renderer2D::BeginScene(camera);

		// 1. Gather all entities containing rendering properties
		auto view = m_Registry.view<TransformComponent, SpriteRendererComponent>();

		// 2. Reuse persistent sorting buckets to minimize batch-breaking state changes
		//    AND avoid per-frame heap allocation. Clear the inner vectors (capacity is
		//    retained); stale keys from a previous frame survive with empty vectors and
		//    are skipped below. The fallback bucket is cleared the same way.
		auto& materialBuckets = m_RenderMaterialBuckets;
		auto& flatColorFallbackBucket = m_RenderFlatColorBucket;

		for (auto& bucket : materialBuckets)
			bucket.second.clear();
		flatColorFallbackBucket.clear();

		// Use EnTT's native .each() layout to cleanly extract entity IDs and references
		view.each([&](auto entity, const auto& transform, const auto& sprite)
			{
				if (sprite.ActiveMaterial)
				{
					materialBuckets[sprite.ActiveMaterial.get()].push_back(entity);
				}
				else
				{
					flatColorFallbackBucket.push_back(entity);
				}
			});

		// 3. Dispatch Material-Batched Quads
		for (auto& [materialPtr, entities] : materialBuckets)
		{
			// Skip buckets left empty this frame (material no longer in use, key retained).
			if (entities.empty())
				continue;

			// Safely extract the shared_ptr reference from the first entity in the bucket
			auto& firstSprite = view.get<SpriteRendererComponent>(entities[0]);
			Ref<Material> activeMaterial = firstSprite.ActiveMaterial;

			// Sort ascending by z so sprites within the same material bucket are drawn
			// back-to-front regardless of entity creation order.
			// PERFORMANCE NOTE: This is O(n log n) per bucket per frame, where n is the
			// number of entities sharing this material. For small-to-medium bucket sizes
			// (< ~1000 entities) the cost is negligible. If a single material bucket grows
			// very large, consider one of these mitigations:
			//   1. Dirty-flag: skip the sort when no TransformComponent z-values changed.
			//   2. Pre-sorted container: maintain a z-ordered structure (e.g. std::multiset)
			//      that is updated incrementally rather than re-sorted every frame.
			std::sort(entities.begin(), entities.end(), [&](entt::entity a, entt::entity b)
			{
				return view.get<TransformComponent>(a).Position.z <
				       view.get<TransformComponent>(b).Position.z;
			});

			for (auto entity : entities)
			{
				auto& transform = view.get<TransformComponent>(entity);
				auto& sprite = view.get<SpriteRendererComponent>(entity);

				// INTERCEPT & APPLY ORIENTATION FLIPS VIA NEGATIVE MATRIX SCALING ADJUSTMENTS
				glm::vec2 drawScale = {
					transform.Scale.x * (sprite.FlipX ? -1.0f : 1.0f),
					transform.Scale.y * (sprite.FlipY ? -1.0f : 1.0f)
				};

				// FIX: TransformComponent stores rotation in degrees; DrawRotatedQuad expects radians.
				// NOTE: Only Rotation.z is used here. Rotation.x and Rotation.y are reserved for
				// future 3D use and are intentionally ignored by this 2D render path. This means
				// OnRender diverges from TransformComponent::GetTransform() for non-zero X/Y rotation.
				Renderer2D::DrawRotatedQuad(
					transform.Position,
					drawScale,
					glm::radians(transform.Rotation.z),
					activeMaterial
				);
			}
		}

		// 4. Dispatch Fallback Flat-Color Quads
		for (auto entity : flatColorFallbackBucket)
		{
			auto& transform = view.get<TransformComponent>(entity);
			auto& sprite = view.get<SpriteRendererComponent>(entity);

			// INTERCEPT & APPLY ORIENTATION FLIPS VIA NEGATIVE MATRIX SCALING ADJUSTMENTS
			glm::vec2 drawScale = {
				transform.Scale.x * (sprite.FlipX ? -1.0f : 1.0f),
				transform.Scale.y * (sprite.FlipY ? -1.0f : 1.0f)
			};

			// FIX: TransformComponent stores rotation in degrees; DrawRotatedQuad expects radians.
			Renderer2D::DrawRotatedQuad(
				transform.Position,
				drawScale,
				glm::radians(transform.Rotation.z),
				sprite.Color
			);
		}

		Renderer2D::EndScene();

	} // Closes void Scene::OnRender()

	void Scene::OnRender3D(const Camera& camera)
	{
		// The scene owns the full 3D pass. Callers must NOT wrap this in their own
		// BeginScene/EndScene. Sorting + frustum culling happen inside Renderer3D's
		// queue (S12.1/S12.2) — this method only decides WHAT to submit.

		// --- Gather scene lights (S4.5) and upload before drawing. ---
		Renderer3D::SceneLightsDesc lights;
		lights.Ambient = Renderer3D::GetAmbient();

		// First directional light wins as the sun.
		{
			auto dirView = m_Registry.view<DirectionalLightComponent>();
			for (auto entity : dirView)
			{
				const auto& dl = dirView.get<DirectionalLightComponent>(entity);
				lights.SunDirection = dl.Direction;
				lights.SunColor     = dl.Color;
				lights.SunIntensity = dl.Intensity;
				break;
			}
		}

		// Point lights (position from the TransformComponent). All are gathered;
		// SetLights is the single truncation point — it uploads the first
		// Renderer3D::kMaxPointLights and warns once when over the cap.
		{
			auto ptView = m_Registry.view<TransformComponent, PointLightComponent>();
			ptView.each([&](auto /*entity*/, const TransformComponent& t, const PointLightComponent& pl)
			{
				Renderer3D::PointLightDesc d;
				d.Position  = t.Position;
				d.Radius    = pl.Radius;
				d.Color     = pl.Color;
				d.Intensity = pl.Intensity;
				lights.Points.push_back(d);
			});
		}

		Renderer3D::SetLights(lights);

		Renderer3D::BeginScene(camera);

		// Terrain first (S8.1) — world geometry with its own quadtree LOD around
		// the pass camera. Water/particle components are app-sequenced (multi-pass
		// / per-frame-dt needs — see their notes in Components.h).
		{
			auto terrainView = m_Registry.view<TerrainComponent>();
			terrainView.each([&](auto entity, const TerrainComponent& tc)
			{
				if (tc.TerrainAsset)
					tc.TerrainAsset->Render(camera.GetPosition(), (int)(uint32_t)entity);
			});
		}

		auto view = m_Registry.view<TransformComponent, MeshRendererComponent>();
		view.each([&](auto entity, const TransformComponent& transform, const MeshRendererComponent& mr)
		{
			if (!mr.MeshAsset)
				return;

			const int entityID = (int)(uint32_t)entity;
			const glm::mat4 xform = transform.GetTransform();
			if (mr.MaterialAsset)
				Renderer3D::DrawMesh(mr.MeshAsset, xform, mr.MaterialAsset, entityID);
			else
				Renderer3D::DrawMesh(mr.MeshAsset, xform, mr.Color, entityID);
		});

		// LOD groups (S12.4): one level per entity, picked by camera distance.
		// Beyond the last level's MaxDistance the entity draws nothing at all.
		{
			auto lodView = m_Registry.view<TransformComponent, LODGroupComponent>();
			lodView.each([&](auto entity, const TransformComponent& transform, const LODGroupComponent& lod)
			{
				const float dist = glm::distance(camera.GetPosition(), transform.Position);
				const int level = LODGroupComponent::SelectLevel(lod.Levels, dist);
				if (level < 0 || !lod.Levels[level].MeshAsset)
					return;

				const int entityID = (int)(uint32_t)entity;
				const glm::mat4 xform = transform.GetTransform();
				if (lod.MaterialAsset)
					Renderer3D::DrawMesh(lod.Levels[level].MeshAsset, xform, lod.MaterialAsset, entityID);
				else
					Renderer3D::DrawMesh(lod.Levels[level].MeshAsset, xform, lod.Color, entityID);
			});
		}

		Renderer3D::EndScene();
	}
} // Closes namespace Cosmic