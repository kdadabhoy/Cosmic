// Optimized Material Grouping System implemented 5/21/2026
// Scene::OnRender camera parameter + BeginScene/EndScene ownership added 5/26/2026
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
// W5 — the ONE 3D dependency left in this file: WorldOf's M4 socket override reads
// SocketComponent + AnimatorComponent (both fenced with it below).
#include "scene/Components3D.h"
#endif
#include "renderer/Renderer2D.h"
#ifdef COSMIC_2D_ONLY
// W6 — only the 2D configuration defines BuildRenderDesc here (the 3D one is in
// Scene3D.cpp, which this build excludes), and it needs SceneRenderDesc complete.
#include "renderer/SceneRenderer.h"
#endif
#include "renderer/Light2DRenderer.h"   // X5 — 2D lighting composite
#include "renderer/RenderCommand.h"   // U3 — sprite-pass depth/blend state
#include "graphics/SubTexture2D.h"    // U3 — SourceRect sub-rect draws
#include "assets/AssetLibrary.h"
#include "physics/ScenePhysics.h"     // J4 — runtime body binding (Scene owns m_Physics)
#include "physics/PhysicsWorld.h"     // J4 — the play-session physics service
#ifndef COSMIC_2D_ONLY
// W5 — ~Scene destroys the m_NavRuntime unique_ptr, so SceneNavRuntime must be a
// complete type HERE even though every nav method now lives in Scene3D.cpp.
#include "scene/SceneNav.h"           // N4 — the play-session crowd binding
#endif
#include "camera/OrthographicCamera.h"
#include "jobs/JobSystem.h"
#include "core/Log.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>   // M4 — glm::mat4_cast for the socket offset
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

namespace Cosmic
{

	Scene::Scene()
	{
	}

	// Out-of-line so the unique_ptr<ScenePhysics> member can destroy a now-complete type.
	Scene::~Scene() = default;

	// ------------------------------------------------------------------------
	// Physics session (Phase 15 / J4)
	// ------------------------------------------------------------------------
	void Scene::OnPhysicsStart(PhysicsWorld& world)
	{
		m_Physics = std::make_unique<ScenePhysics>(*this, world);
		m_Physics->BuildBodies();
	}

	void Scene::OnPhysicsStep(float fixedDeltaTime)
	{
		if (m_Physics)
			m_Physics->Step(fixedDeltaTime);
	}

	void Scene::DispatchPhysicsEvents(ScriptHost& scripts)
	{
		if (m_Physics)
			m_Physics->DispatchEvents(scripts);
	}

	void Scene::OnPhysicsStop(PhysicsWorld& /*world*/)
	{
		if (m_Physics)
		{
			m_Physics->Teardown();
			m_Physics.reset();
		}
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
#ifndef COSMIC_2D_ONLY
		// Socket override (M4): an entity with a SocketComponent follows a named
		// joint of the NEAREST animated ancestor whose skeleton has that joint —
		// socketWorld = ancestorWorld · jointFrame · offset. It bypasses the
		// normal parent-relative local (the offset lives on the component). Falls
		// through to the ordinary path when no ancestor animates the joint yet, so
		// a socket behaves as a plain child until its rig poses (compat).
		if (const SocketComponent* sock = m_Registry.try_get<SocketComponent>(handle))
		{
			entt::entity cur = handle;
			for (int guard = 0; m_Registry.valid(cur) && guard < 4096; ++guard)
			{
				const auto* rel = m_Registry.try_get<RelationshipComponent>(cur);
				if (!rel || !rel->Parent.IsValid())
					break;
				auto it = m_UUIDMap.find(rel->Parent);
				if (it == m_UUIDMap.end() || !m_Registry.valid(it->second))
					break;
				const entt::entity parent = it->second;

				if (const auto* an = m_Registry.try_get<AnimatorComponent>(parent);
				    an && an->SkelRef && !an->JointModelMatrices.empty())
				{
					const int j = an->SkelRef->Find(sock->Joint);
					if (j >= 0 && (size_t)j < an->JointModelMatrices.size())
					{
						const glm::mat4 offset =
							glm::translate(glm::mat4(1.0f), sock->Position) *
							glm::mat4_cast(sock->Rotation) *
							glm::scale(glm::mat4(1.0f), sock->Scale);
						return WorldOf(parent) * an->JointModelMatrices[(size_t)j] * offset;
					}
				}
				cur = parent;
			}
			// Unresolved — fall through to the ordinary transform below.
		}
#endif   // COSMIC_2D_ONLY — no skeletons, so no sockets to resolve (pre-M4 path)

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

	bool Scene::IsActiveInHierarchy(Entity entity)
	{
		return entity ? IsActiveInHierarchy((entt::entity)entity) : false;
	}

	bool Scene::IsActiveInHierarchy(entt::entity handle)
	{
		// Walk up the parent chain (like WorldOf): false if self or any ancestor
		// is inactive. Guarded against a malformed cycle.
		entt::entity cur = handle;
		for (int guard = 0; m_Registry.valid(cur) && guard < 4096; ++guard)
		{
			if (const auto* tag = m_Registry.try_get<TagComponent>(cur); tag && !tag->Active)
				return false;
			const auto* rel = m_Registry.try_get<RelationshipComponent>(cur);
			if (!rel || !rel->Parent.IsValid())
				break;
			auto it = m_UUIDMap.find(rel->Parent);
			if (it == m_UUIDMap.end() || !m_Registry.valid(it->second))
				break;
			cur = it->second;
		}
		return true;
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
		// A2 — advance + sample skeletal animators for this play frame (the
		// editor calls UpdateAnimators itself in edit mode, where OnUpdate
		// never runs). W5 — skeletal animation is 3D only; UpdateAnimators
		// lives in Scene3D.cpp and is not linked into a 2D build.
#ifndef COSMIC_2D_ONLY
		UpdateAnimators(deltaTime);
#endif

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
				if (!sprite.Enabled)   // T12
					return;
				if (!IsActiveInHierarchy(entity))   // T13
					return;
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

	void Scene::UpdateSpriteAnimations(float deltaTime)
	{
		auto view = m_Registry.view<SpriteAnimationComponent, SpriteRendererComponent>();
		for (auto e : view)
		{
			auto& anim   = view.get<SpriteAnimationComponent>(e);
			auto& sprite = view.get<SpriteRendererComponent>(e);

			if (anim.Playing)
				anim.Elapsed += deltaTime;

			const int frame = SpriteAnimationComponent::SelectFrame(
				anim.Elapsed, anim.FPS, anim.Frames, anim.Loop);

			// Resolve the sheet for its pixel size, then map frame -> UV. Skipped
			// when the sheet is unavailable (e.g. headless / not yet loaded) — the
			// sprite keeps its last SourceRect.
			Ref<Texture2D> sheet = anim.SheetPath.empty() ? nullptr
			                                              : AssetLibrary::GetTexture(anim.SheetPath);
			if (!sheet || sheet->GetWidth() == 0 || sheet->GetHeight() == 0)
				continue;

			sprite.SourceRect = SpriteAnimationComponent::FrameUV(
				(int)sheet->GetWidth(), (int)sheet->GetHeight(),
				anim.FrameW, anim.FrameH, anim.Row, frame);
		}
	}

	// Painter order: ascending ZOrder, then the per-item key (sprite YSort
	// sorts by -Y so a lower-on-screen sprite draws in front; default =
	// Position.z, the legacy 2D-path tie-break), then entity id. Tilemaps
	// interleave with sprites through the same (ZOrder, Z) keys.
	std::vector<Scene::SpriteDrawItem> Scene::BuildSpriteDrawList()
	{
		auto view   = m_Registry.view<TransformComponent, SpriteRendererComponent>();
		auto tmView = m_Registry.view<TransformComponent, TilemapComponent>();

		std::vector<SpriteDrawItem> items;
		for (auto e : view)
		{
			const auto& t = view.get<TransformComponent>(e);
			const auto& s = view.get<SpriteRendererComponent>(e);
			if (!s.Enabled)   // T12
				continue;
			if (!IsActiveInHierarchy(e))   // T13
				continue;
			items.push_back({ e, s.ZOrder, s.YSort ? -t.Position.y : t.Position.z, false });
		}
		for (auto e : tmView)
		{
			const auto& t  = tmView.get<TransformComponent>(e);
			const auto& tm = tmView.get<TilemapComponent>(e);
			if (!IsActiveInHierarchy(e))   // T13
				continue;
			items.push_back({ e, tm.ZOrder, t.Position.z, true });
		}
		std::sort(items.begin(), items.end(), [](const SpriteDrawItem& a, const SpriteDrawItem& b)
		{
			if (a.Z   != b.Z)   return a.Z   < b.Z;
			if (a.Key != b.Key) return a.Key < b.Key;
			return a.E < b.E;
		});
		return items;
	}

	void Scene::OnRenderSprites(const glm::mat4& viewProjection,
	                            uint32_t viewportWidth, uint32_t viewportHeight)
	{
		auto view    = m_Registry.view<TransformComponent, SpriteRendererComponent>();
		auto tmView  = m_Registry.view<TransformComponent, TilemapComponent>();
		const bool anySprites  = view.begin() != view.end();
		const bool anyTilemaps = tmView.begin() != tmView.end();
		if (!anySprites && !anyTilemaps)
			return;   // compat gate: a scene without 2D content makes NO GL calls here

		const std::vector<SpriteDrawItem> items = BuildSpriteDrawList();

		// World-space XY bounds of the view frustum (invVP over the NDC cube) —
		// exact for the ortho 2D camera, conservative for perspective. Culls the
		// tilemap cell walk to the visible range. Computed once per call.
		glm::vec2 cullMin(0.0f), cullMax(0.0f);
		bool haveCull = false;
		if (anyTilemaps)
		{
			const glm::mat4 invVP = glm::inverse(viewProjection);
			for (int i = 0; i < 8; ++i)
			{
				glm::vec4 c = invVP * glm::vec4((i & 1) ? 1.0f : -1.0f,
				                                (i & 2) ? 1.0f : -1.0f,
				                                (i & 4) ? 1.0f : -1.0f, 1.0f);
				if (std::abs(c.w) < 1e-9f) continue;
				c /= c.w;
				const glm::vec2 p{ c.x, c.y };
				if (!haveCull) { cullMin = cullMax = p; haveCull = true; }
				else           { cullMin = glm::min(cullMin, p); cullMax = glm::max(cullMax, p); }
			}
		}

		// Transparent-queue contract: depth test ON (3D occludes sprites in a
		// 2.5D scene), depth write OFF, straight alpha. Restore write on exit.
		RenderCommand::SetDepthTest(true);
		RenderCommand::SetDepthWrite(false);
		RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);

		Renderer2D::PushRenderPass(viewProjection,
			{ 0.0f, 0.0f, (float)viewportWidth, (float)viewportHeight });

		for (const SpriteDrawItem& it : items)
		{
			if (it.Map)
			{
				// ---- tilemap draw: a culled cell walk in one batched pass ------
				auto& t  = tmView.get<TransformComponent>(it.E);
				auto& tm = tmView.get<TilemapComponent>(it.E);

				if (tm.TilesetPath != tm.ResolvedPath)
				{
					tm.ResolvedPath = tm.TilesetPath;
					tm.Resolved = tm.TilesetPath.empty() ? nullptr
					                                     : AssetLibrary::GetTexture(tm.TilesetPath);
				}
				if (!tm.Resolved || tm.Resolved->GetWidth() <= 0 || tm.TileW <= 0 || tm.TileH <= 0)
					continue;
				tm.EnsureCells();

				const float texW = (float)tm.Resolved->GetWidth();
				const float texH = (float)tm.Resolved->GetHeight();
				const int   cols = tm.Columns > 0 ? tm.Columns
				                                  : std::max(1, (int)(texW / (float)tm.TileW));

				// Visible cell range (one cell = one world unit off Position).
				int x0 = 0, x1 = tm.GridW - 1, y0 = 0, y1 = tm.GridH - 1;
				if (haveCull)
				{
					x0 = std::max(0,          (int)std::floor(cullMin.x - t.Position.x));
					x1 = std::min(tm.GridW - 1, (int)std::ceil (cullMax.x - t.Position.x));
					y0 = std::max(0,          (int)std::floor(cullMin.y - t.Position.y));
					y1 = std::min(tm.GridH - 1, (int)std::ceil (cullMax.y - t.Position.y));
				}

				// One SubTexture per distinct tile id used this draw.
				std::unordered_map<uint16_t, Ref<SubTexture2D>> tiles;
				for (int cy = y0; cy <= y1; ++cy)
				{
					for (int cx = x0; cx <= x1; ++cx)
					{
						const uint16_t v = tm.Cells[(size_t)cy * tm.GridW + cx];
						if (v == 0)
							continue;
						Ref<SubTexture2D>& sub = tiles[v];
						if (!sub)
						{
							const int idx = (int)v - 1;
							const int col = idx % cols, row = idx / cols;
							const float u0 = col * tm.TileW / texW;
							const float u1 = (col + 1) * tm.TileW / texW;
							const float vTop = row * tm.TileH / texH;        // atlas row 0 = top
							const float vBot = (row + 1) * tm.TileH / texH;
							sub = CreateRef<SubTexture2D>(tm.Resolved,
								glm::vec2{ u0, 1.0f - vBot },   // uvMin (bottom-left)
								glm::vec2{ u1, 1.0f - vTop });  // uvMax (top-right)
						}
						Renderer2D::DrawQuad(
							glm::vec3{ t.Position.x + cx + 0.5f, t.Position.y + cy + 0.5f, t.Position.z },
							glm::vec2{ 1.0f, 1.0f }, sub);
					}
				}
				continue;
			}

			auto& t = view.get<TransformComponent>(it.E);
			auto& s = view.get<SpriteRendererComponent>(it.E);

			// Lazy texture resolve (authored TexturePath; re-resolve on change).
			if (s.TexturePath != s.ResolvedPath)
			{
				s.ResolvedPath = s.TexturePath;
				s.Resolved = s.TexturePath.empty() ? nullptr
				                                   : AssetLibrary::GetTexture(s.TexturePath);
			}

			const float rotZ = glm::radians(t.Rotation.z);

			if (s.Resolved && s.Resolved->GetWidth() > 0)
			{
				// Shared U3 sizing rule; flips are negative scale (legacy convention).
				const glm::vec4 src = s.SourceRect;   // {u0,v0,u1,v1}, V top-left origin
				glm::vec2 size = SpriteRendererComponent::WorldSize(
					s, { t.Scale.x, t.Scale.y },
					(int)s.Resolved->GetWidth(), (int)s.Resolved->GetHeight());
				size.x *= (s.FlipX ? -1.0f : 1.0f);
				size.y *= (s.FlipY ? -1.0f : 1.0f);

				// SubTexture2D is bottom-left-origin UV; SourceRect is top-left.
				auto sub = CreateRef<SubTexture2D>(s.Resolved,
					glm::vec2{ src.x, 1.0f - src.w },   // uvMin (bottom-left)
					glm::vec2{ src.z, 1.0f - src.y });  // uvMax (top-right)
				Renderer2D::DrawRotatedQuad(t.Position, size, rotZ, sub, s.Color);
			}
			else if (s.ActiveMaterial)
			{
				const glm::vec2 size{ t.Scale.x * (s.FlipX ? -1.0f : 1.0f),
				                      t.Scale.y * (s.FlipY ? -1.0f : 1.0f) };
				Renderer2D::DrawRotatedQuad(t.Position, size, rotZ, s.ActiveMaterial);
			}
			else
			{
				const glm::vec2 size{ t.Scale.x * (s.FlipX ? -1.0f : 1.0f),
				                      t.Scale.y * (s.FlipY ? -1.0f : 1.0f) };
				Renderer2D::DrawRotatedQuad(t.Position, size, rotZ, s.Color);
			}
		}

		Renderer2D::PopRenderPass();

		// Restore the engine depth-write default (test was already ON).
		RenderCommand::SetDepthWrite(true);
	}

	void Scene::OnRender2DLights(const glm::mat4& viewProjection,
	                             uint32_t viewportWidth, uint32_t viewportHeight)
	{
		// Ambient from the environment (default white = no darkening).
		glm::vec3 ambient(1.0f);
		if (EnvironmentComponent* env = FindEnvironment())
			ambient = env->Ambient2D;

		// Collect the active 2D lights (raw TransformComponent XY, like the sprite pass).
		std::vector<Light2DRenderer::Light> lights;
		auto view = m_Registry.view<TransformComponent, Light2DComponent>();
		for (auto e : view)
		{
			const auto& lc = view.get<Light2DComponent>(e);
			if (!lc.Enabled)                // T12-style gate
				continue;
			if (!IsActiveInHierarchy(e))    // T13
				continue;
			const auto& t = view.get<TransformComponent>(e);
			lights.push_back({ { t.Position.x, t.Position.y }, lc.Radius, lc.Color, lc.Intensity, lc.Falloff });
		}

		// Compat gate: no lights + white ambient ⇒ the multiply is identity, so
		// skip the pass entirely and make NO GL calls (2D output byte-identical).
		if (lights.empty() && ambient == glm::vec3(1.0f))
			return;

		Light2DRenderer::Composite(lights, ambient, viewProjection, viewportWidth, viewportHeight);
	}

	EnvironmentComponent* Scene::FindEnvironment()
	{
		for (auto entity : m_Registry.view<EnvironmentComponent>())
			return &m_Registry.get<EnvironmentComponent>(entity);
		return nullptr;
	}

#ifdef COSMIC_2D_ONLY
	// W6 — the 2D twin of BuildRenderDesc. The 3D definition lives in Scene3D.cpp,
	// which the 2D configuration never compiles; this is the residue of it that a
	// 2D scene can actually fill. Everything it drops is a 3D gather with nothing
	// to gather: the four asset syncs (primitive meshes / world systems / voxel
	// volumes / navmeshes), the scene-light walk, terrain, water, particle
	// emitters, and the routed DrawOpaque submit hook. The clock advance and the
	// camera + time fields below are what SceneRenderer's surviving passes read, so
	// PlayerLayer, Starforge and the scene2d golden drive the 2D frame through the
	// SAME call they use on the 3D engine — no call-site fences anywhere.
	//
	// EcsScene stays null here for the same reason it does in the 3D twin.
	void Scene::BuildRenderDesc(const Camera& camera, float deltaTime, SceneRenderDesc& out)
	{
		m_WorldTime += deltaTime;

		out.SetCamera(camera);
		out.TimeSeconds = m_WorldTime;
		out.DeltaTime   = deltaTime;
	}
#endif   // COSMIC_2D_ONLY

} // Closes namespace Cosmic