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
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

namespace Cosmic
{
	Scene::Scene()
	{
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID id, const std::string& name)
	{
		if (!id.IsValid())
			id = UUID();

		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(id);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<TagComponent>(name.empty() ? "Entity" : name);

		m_UUIDMap[id] = (entt::entity)entity;
		return entity;
	}

	Entity Scene::FindByUUID(UUID id)
	{
		auto it = m_UUIDMap.find(id);
		if (it == m_UUIDMap.end() || !m_Registry.valid(it->second))
			return Entity{};
		return Entity{ it->second, this };
	}

	void Scene::DestroyEntity(Entity entity, bool destroyChildren)
	{
		if (!entity)
			return;
		const entt::entity handle = (entt::entity)entity;
		if (!m_Registry.valid(handle))
			return;

		// Read everything we need up front — destroying children below mutates
		// the RelationshipComponent pool and would dangle a held reference.
		const UUID myID = m_Registry.all_of<IDComponent>(handle)
			? m_Registry.get<IDComponent>(handle).ID : UUID(0);
		UUID parentID(0);
		std::vector<UUID> kids;
		if (m_Registry.all_of<RelationshipComponent>(handle))
		{
			const auto& rel = m_Registry.get<RelationshipComponent>(handle);
			parentID = rel.Parent;
			kids     = rel.Children;
		}

		// Detach from the parent's Children list.
		if (parentID.IsValid())
		{
			Entity parent = FindByUUID(parentID);
			if (parent && parent.HasComponent<RelationshipComponent>())
			{
				auto& pc = parent.GetComponent<RelationshipComponent>().Children;
				pc.erase(std::remove(pc.begin(), pc.end(), myID), pc.end());
			}
		}

		// Recurse into (or orphan) the children.
		for (UUID k : kids)
		{
			Entity ke = FindByUUID(k);
			if (!ke)
				continue;
			if (destroyChildren)
				DestroyEntity(ke, true);
			else if (ke.HasComponent<RelationshipComponent>())
				ke.GetComponent<RelationshipComponent>().Parent = UUID(0);
		}

		if (myID.IsValid())
			m_UUIDMap.erase(myID);
		m_Registry.destroy(handle);
	}

	bool Scene::IsAncestor(Entity ancestor, Entity node)
	{
		if (!ancestor || !node)
			return false;
		const entt::entity ancestorHandle = (entt::entity)ancestor;

		entt::entity cur = (entt::entity)node;
		while (m_Registry.valid(cur) && m_Registry.all_of<RelationshipComponent>(cur))
		{
			const UUID p = m_Registry.get<RelationshipComponent>(cur).Parent;
			if (!p.IsValid())
				break;
			auto it = m_UUIDMap.find(p);
			if (it == m_UUIDMap.end() || !m_Registry.valid(it->second))
				break;
			if (it->second == ancestorHandle)
				return true;
			cur = it->second;
		}
		return false;
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		if (!entity)
			return glm::mat4(1.0f);
		return WorldOf((entt::entity)entity);
	}

	glm::mat4 Scene::WorldOf(entt::entity handle)
	{
		glm::mat4 local(1.0f);
		if (m_Registry.all_of<TransformComponent>(handle))
			local = m_Registry.get<TransformComponent>(handle).GetTransform();

		if (m_Registry.all_of<RelationshipComponent>(handle))
		{
			const UUID p = m_Registry.get<RelationshipComponent>(handle).Parent;
			if (p.IsValid())
			{
				auto it = m_UUIDMap.find(p);
				if (it != m_UUIDMap.end() && m_Registry.valid(it->second))
					return WorldOf(it->second) * local;
			}
		}
		return local;
	}

	bool Scene::SetParent(Entity child, Entity parent, bool keepWorldPose)
	{
		if (!child)
			return false;
		if (parent && (parent == child || IsAncestor(child, parent)))
		{
			CS_CORE_WARN("Scene::SetParent refused: the operation would create a cycle.");
			return false;
		}

		glm::mat4 worldBefore(1.0f);
		if (keepWorldPose)
			worldBefore = GetWorldTransform(child);

		const UUID childID = child.GetComponent<IDComponent>().ID;

		// Ensure both endpoints own a RelationshipComponent BEFORE taking any
		// references — the emplaces may reallocate the pool.
		if (!child.HasComponent<RelationshipComponent>())
			child.AddComponent<RelationshipComponent>();
		if (parent && !parent.HasComponent<RelationshipComponent>())
			parent.AddComponent<RelationshipComponent>();

		// Detach from the previous parent (read the old id by value first).
		const UUID oldParentID = child.GetComponent<RelationshipComponent>().Parent;
		if (oldParentID.IsValid())
		{
			Entity oldParent = FindByUUID(oldParentID);
			if (oldParent && oldParent.HasComponent<RelationshipComponent>())
			{
				auto& oc = oldParent.GetComponent<RelationshipComponent>().Children;
				oc.erase(std::remove(oc.begin(), oc.end(), childID), oc.end());
			}
		}

		if (parent)
		{
			const UUID parentID = parent.GetComponent<IDComponent>().ID;
			child.GetComponent<RelationshipComponent>().Parent = parentID;
			auto& pc = parent.GetComponent<RelationshipComponent>().Children;
			if (std::find(pc.begin(), pc.end(), childID) == pc.end())
				pc.push_back(childID);
		}
		else
		{
			child.GetComponent<RelationshipComponent>().Parent = UUID(0);
		}

		if (keepWorldPose)
		{
			const glm::mat4 parentWorld = parent ? GetWorldTransform(parent) : glm::mat4(1.0f);
			const glm::mat4 newLocal    = glm::inverse(parentWorld) * worldBefore;

			glm::vec3 scale, translation, skew;
			glm::vec4 perspective;
			glm::quat orientation;
			if (glm::decompose(newLocal, scale, orientation, translation, skew, perspective))
			{
				auto& t = child.GetComponent<TransformComponent>();
				t.Position        = translation;
				t.Scale           = scale;
				t.RotationQuat    = orientation;
				t.UseQuatRotation = true;   // preserve the exact recovered rotation
			}
		}
		return true;
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
		view.each([&](auto entity, const TransformComponent& /*transform*/, const MeshRendererComponent& mr)
		{
			if (!mr.MeshAsset)
				return;

			const int entityID = (int)(uint32_t)entity;
			// World transform (E3): parent-world x local. Flat entities (no
			// RelationshipComponent) resolve to their local transform, so every
			// shipped flat scene renders identically.
			const glm::mat4 xform = WorldOf(entity);
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
				const glm::mat4 xform = WorldOf(entity);
				if (lod.MaterialAsset)
					Renderer3D::DrawMesh(lod.Levels[level].MeshAsset, xform, lod.MaterialAsset, entityID);
				else
					Renderer3D::DrawMesh(lod.Levels[level].MeshAsset, xform, lod.Color, entityID);
			});
		}

		Renderer3D::EndScene();
	}
} // Closes namespace Cosmic