// Optimized Material Grouping System implemented 5/21/2026
// Scene::OnRender camera parameter + BeginScene/EndScene ownership added 5/26/2026
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "renderer/Renderer2D.h"
#include "renderer/Renderer3D.h"
#include "renderer/SceneRenderer.h"   // H2 — BuildRenderDesc fills a SceneRenderDesc
#include "assets/AssetLibrary.h"
#include "terrain/Terrain.h"
#include "water/Water.h"
#include "particles/ParticleSystem.h"
#include "scene/WorldSystemRecipes.h"
#include "voxel/VoxelVolume.h"        // Phase 18 — voxel chunk store
#include "voxel/BlockPalette.h"
#include "voxel/VoxelMesher.h"
#include "voxel/VoxelGenerator.h"
#include "voxel/VoxelRender.h"        // VoxelRenderData + atlas/recipe helpers
#include "physics/ScenePhysics.h"     // J4 — runtime body binding (Scene owns m_Physics)
#include "physics/PhysicsWorld.h"     // J4 — the play-session physics service
#include "camera/OrthographicCamera.h"
#include "camera/Camera.h"
#include "jobs/JobSystem.h"
#include "core/Log.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/matrix_decompose.hpp>

namespace Cosmic
{
	namespace
	{
		// Hash the parameters a PrimitiveMeshComponent's mesh is built from, so the
		// render-path sync can detect a change without any explicit dirty flag (E15).
		std::size_t PrimitiveSignature(const PrimitiveMeshComponent& p)
		{
			std::size_t h = std::hash<int>{}(static_cast<int>(p.ShapeType));
			auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
			mix(std::hash<float>{}(p.Size.x));
			mix(std::hash<float>{}(p.Size.y));
			mix(std::hash<float>{}(p.Size.z));
			mix(std::hash<float>{}(p.Radius));
			mix(std::hash<float>{}(p.Height));
			mix(std::hash<float>{}(p.TubeRadius));
			mix(std::hash<int>{}(p.Segments));
			mix(std::hash<int>{}(p.Rings));
			return h;
		}

		// Map a primitive spec to a freshly uploaded mesh (main-thread / GL — called
		// only from Scene::SyncPrimitiveMeshes inside the render pass).
		Ref<Mesh> BuildPrimitiveMesh(const PrimitiveMeshComponent& p)
		{
			const uint32_t seg   = static_cast<uint32_t>(p.Segments < 3 ? 3 : p.Segments);
			const uint32_t rings = static_cast<uint32_t>(p.Rings    < 3 ? 3 : p.Rings);
			using Shape = PrimitiveMeshComponent::Shape;
			switch (p.ShapeType)
			{
				case Shape::Box:      return Mesh::CreateBox(p.Size);
				case Shape::Sphere:   return Mesh::CreateUVSphere(p.Radius, rings, seg);
				case Shape::Plane:    return Mesh::CreatePlane(p.Size.x, p.Size.z);
				case Shape::Cylinder: return Mesh::CreateCylinder(p.Radius, p.Height, seg);
				case Shape::Cone:     return Mesh::CreateCone(p.Radius, p.Height, seg);
				case Shape::Torus:    return Mesh::CreateTorus(p.Radius, p.TubeRadius, seg, rings);
			}
			return nullptr;
		}

		// Gather the scene's light components into a SceneLightsDesc (S4.5). Shared by
		// OnRender3D (cheap path) and BuildRenderDesc (H2) so there is one truth for
		// "what lights a scene has": first DirectionalLight = sun; every PointLight is
		// pushed (SetLights is the single truncation point at kMaxPointLights).
		void GatherSceneLights(entt::registry& reg, Renderer3D::SceneLightsDesc& lights)
		{
			lights.Ambient = Renderer3D::GetAmbient();

			for (auto entity : reg.view<DirectionalLightComponent>())
			{
				const auto& dl = reg.get<DirectionalLightComponent>(entity);
				lights.SunDirection = dl.Direction;
				lights.SunColor     = dl.Color;
				lights.SunIntensity = dl.Intensity;
				break;   // first directional light wins as the sun
			}

			reg.view<TransformComponent, PointLightComponent>().each(
				[&](auto /*entity*/, const TransformComponent& t, const PointLightComponent& pl)
			{
				Renderer3D::PointLightDesc d;
				d.Position  = t.Position;
				d.Radius    = pl.Radius;
				d.Color     = pl.Color;
				d.Intensity = pl.Intensity;
				lights.Points.push_back(d);
			});
		}
	}

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

	void Scene::SyncPrimitiveMeshes()
	{
		// Collect first: we get_or_emplace a sibling MeshRenderer below, and adding a
		// component is cleaner done outside a live view iteration.
		std::vector<entt::entity> prims;
		for (auto e : m_Registry.view<PrimitiveMeshComponent>())
			prims.push_back(e);

		for (auto e : prims)
		{
			auto&             prim = m_Registry.get<PrimitiveMeshComponent>(e);
			auto&             mr   = m_Registry.get_or_emplace<MeshRendererComponent>(e);
			const std::size_t sig  = PrimitiveSignature(prim);
			if (mr.MeshAsset && prim.BuiltSignature == sig)
				continue;
			if (Ref<Mesh> mesh = BuildPrimitiveMesh(prim))
			{
				mr.MeshAsset        = mesh;
				prim.BuiltSignature = sig;
			}
		}

		// Resolve imported/loaded mesh paths (E16): a scene loaded from disk stores
		// only MeshPath (a project:// asset); turn it into a live MeshAsset once via
		// the asset cache. Guarded so a missing file logs once, not every frame.
		auto meshView = m_Registry.view<MeshRendererComponent>();
		for (auto e : meshView)
		{
			auto& mr = meshView.get<MeshRendererComponent>(e);
			if (!mr.MeshAsset && !mr.MeshPath.empty() && !mr.MeshPathResolved)
			{
				mr.MeshPathResolved = true;   // one attempt regardless of outcome
				mr.MeshAsset = AssetLibrary::GetMesh(mr.MeshPath);
			}
			// Material asset path (E17) — same guarded, once-per-session resolution.
			if (!mr.MaterialAsset && !mr.MaterialPath.empty() && !mr.MaterialPathResolved)
			{
				mr.MaterialPathResolved = true;
				mr.MaterialAsset = AssetLibrary::GetMaterial(mr.MaterialPath);
			}
		}
	}

	void Scene::SyncWorldSystems()
	{
		// Terrain (E18): auto-build ONCE (asset null + never built) — the build is
		// expensive, so signature-change rebuilds are the editor's explicit,
		// JobSystem-offloaded job. UseRecipe gates the whole thing (compat: a
		// code-set TerrainAsset keeps UseRecipe false and is never touched).
		{
			auto view = m_Registry.view<TerrainComponent>();
			for (auto e : view)
			{
				auto& tc = view.get<TerrainComponent>(e);
				if (!tc.UseRecipe || tc.TerrainAsset || tc.BuiltSignature != 0)
					continue;
				TerrainSpecification spec = BuildTerrainSpec(tc);
				ResolveTerrainSpecAssets(tc, spec);          // main thread (VFS + GL textures)
				tc.TerrainAsset    = Terrain::Create(spec);  // CPU heightfield (GL-free)
				tc.BuiltSignature  = TerrainRecipeSignature(tc);
			}
		}

		// Water (E18): cheap (Water::Create is GL-free) — rebuild on null OR any
		// recipe change so Inspector/panel edits apply live.
		{
			auto view = m_Registry.view<WaterComponent>();
			for (auto e : view)
			{
				auto& wc = view.get<WaterComponent>(e);
				if (!wc.UseRecipe)
					continue;
				const std::size_t sig = WaterRecipeSignature(wc);
				if (wc.WaterAsset && wc.BuiltSignature == sig)
					continue;
				wc.WaterAsset     = Water::Create(BuildWaterSpec(wc));
				wc.BuiltSignature = sig;
			}
		}

		// Particles (E18): cheap (GPU pool is lazy) — rebuild on null OR any change.
		{
			auto view = m_Registry.view<ParticleEmitterComponent>();
			for (auto e : view)
			{
				auto& pc = view.get<ParticleEmitterComponent>(e);
				if (!pc.UseRecipe)
					continue;
				const std::size_t sig = EmitterRecipeSignature(pc);
				if (pc.Emitter && pc.BuiltSignature == sig)
					continue;
				ParticleEmitterSpec spec = BuildEmitterSpec(pc);
				if (!pc.TexturePath.empty())
					spec.Texture = AssetLibrary::GetTexture(pc.TexturePath);
				pc.Emitter        = ParticleEmitter::Create(spec);
				pc.BuiltSignature = sig;
			}
		}
	}

	void Scene::SyncVoxelVolumes(const glm::vec3& cameraPos)
	{
		auto view = m_Registry.view<VoxelVolumeComponent>();
		for (auto e : view)
		{
			auto& vc = view.get<VoxelVolumeComponent>(e);

			// Palette (lazy): from .cpal, else the default block set.
			if (!vc.Palette)
			{
				if (!vc.PalettePath.empty())
					vc.Palette = BlockPalette::Load(vc.PalettePath);
				if (!vc.Palette)
					vc.Palette = BlockPalette::CreateDefault();
			}

			// Volume (lazy) + placement at the entity's world transform.
			if (!vc.Volume)
			{
				vc.Volume = VoxelVolume::Create();
				if (!vc.VolumePath.empty())
					vc.Volume->Load(vc.VolumePath);   // best-effort; empty on miss
			}
			vc.Volume->SetVoxelSize(vc.VoxelSize);
			vc.Volume->SetOrigin(glm::vec3(WorldOf(e)[3]));

			// Runtime render data + procedural atlas (rebuilt when the palette changes).
			if (!vc.Render)
				vc.Render = std::make_shared<VoxelRenderData>();
			VoxelRenderData& rd = *vc.Render;
			rd.Mode = vc.Greedy ? VoxelMeshMode::Greedy : VoxelMeshMode::Culled;

			const std::size_t palVer = VoxelPaletteVersion(*vc.Palette);
			if (!rd.AtlasMaterial || rd.AtlasPaletteVersion != palVer)
			{
				BuildVoxelAtlas(rd, *vc.Palette);
				rd.AtlasPaletteVersion = palVer;
			}

			// Streaming generation (V6): fill ungenerated chunks near the camera,
			// nearest-first, a small budget per call so the main thread never hitches.
			VoxelGeneratorRecipe recipe = BuildVoxelRecipe(vc);
			if (recipe.Enabled)
			{
				const std::size_t genSig = VoxelGenerator::Signature(recipe);
				if (vc.BuiltGenSignature != genSig)
				{
					rd.Generated.clear();          // recipe changed -> re-terrain untouched chunks
					vc.BuiltGenSignature = genSig;
				}

				const glm::ivec3 camChunk = VoxelVolume::ChunkCoord(vc.Volume->WorldToVoxel(cameraPos));
				const int R = std::max(1, vc.ViewRadius);
				constexpr int kGenBudget = 2;
				int generated = 0;

				static const glm::ivec3 kN[6] =
					{ {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };

				for (int r = 0; r <= R && generated < kGenBudget; ++r)
				{
					for (int dz = -r; dz <= r && generated < kGenBudget; ++dz)
					for (int dy = -r; dy <= r && generated < kGenBudget; ++dy)
					for (int dx = -r; dx <= r && generated < kGenBudget; ++dx)
					{
						if (std::max(std::max(std::abs(dx), std::abs(dy)), std::abs(dz)) != r)
							continue;   // ring shell only
						const glm::ivec3 cc = camChunk + glm::ivec3(dx, dy, dz);
						if (rd.Generated.count(cc) || vc.Volume->HasChunk(cc))
							continue;
						VoxelGenerator::GenerateChunk(*vc.Volume, cc, recipe);
						rd.Generated.insert(cc);
						for (const glm::ivec3& n : kN)
							if (vc.Volume->HasChunk(cc + n))
								vc.Volume->MarkChunkDirty(cc + n);
						++generated;
					}
				}
			}

			// Re-mesh dirty chunks: JobSystem workers build MeshData, the main thread
			// uploads a bounded budget per call (leftovers requeue as still-dirty).
			std::vector<glm::ivec3> dirty;
			vc.Volume->TakeDirtyChunks(dirty);
			if (dirty.empty())
				continue;

			constexpr size_t kMeshBudget = 24;
			if (dirty.size() > kMeshBudget)
			{
				for (size_t i = kMeshBudget; i < dirty.size(); ++i)
					vc.Volume->MarkChunkDirty(dirty[i]);
				dirty.resize(kMeshBudget);
			}

			VoxelVolume&        vol  = *vc.Volume;
			const BlockPalette& pal  = *vc.Palette;
			const VoxelMeshMode mode = rd.Mode;

			std::vector<MeshData> builtData(dirty.size());
			JobSystem& js = JobSystem::Get();
			const bool async = js.IsInitialized();
			for (size_t i = 0; i < dirty.size(); ++i)
			{
				auto work = [&vol, &pal, mode, &builtData, &dirty, i]()
				{
					builtData[i] = VoxelMesher::BuildChunk(vol, dirty[i], pal, mode);
				};
				if (async) js.Submit(work);
				else       work();
			}
			if (async) js.WaitIdle();

			for (size_t i = 0; i < dirty.size(); ++i)
			{
				if (builtData[i].Vertices.empty())
					rd.ChunkMeshes.erase(dirty[i]);
				else
					rd.ChunkMeshes[dirty[i]] = Mesh::Create(builtData[i]);
				rd.CollisionDirty.insert(dirty[i]);   // physics rebuilds these (V5)
			}
		}
	}

	void Scene::OnRenderWorldFX(const Camera& camera,
	                            uint32_t sceneColorID, uint32_t sceneDepthID,
	                            uint32_t viewportWidth, uint32_t viewportHeight,
	                            float deltaTime)
	{
		m_WorldTime += deltaTime;

		// The first terrain (if any) acts as the shore-attenuation source for water.
		Ref<Terrain> shore;
		{
			auto tView = m_Registry.view<TerrainComponent>();
			for (auto e : tView)
				if (Ref<Terrain> t = tView.get<TerrainComponent>(e).TerrainAsset) { shore = t; break; }
		}

		const glm::mat4 viewProj = camera.GetViewProjectionMatrix();
		const glm::mat4 view     = camera.GetViewMatrix();
		const glm::mat4 invVP    = glm::inverse(viewProj);

		// Water + particles draw immediately (not queued); wrap in a scene so the
		// camera UBO is live (their vertex stages read CameraBlock).
		Renderer3D::BeginScene(camera);

		{
			auto view3 = m_Registry.view<WaterComponent>();
			for (auto e : view3)
			{
				auto& wc = view3.get<WaterComponent>(e);
				if (!wc.WaterAsset)
					continue;
				wc.WaterAsset->SetShoreTerrain(shore);
				wc.WaterAsset->Render(camera.GetPosition(), m_WorldTime, viewProj,
				                      sceneColorID, sceneDepthID,
				                      viewportWidth, viewportHeight, (int)(uint32_t)e);
			}
		}

		{
			auto pview = m_Registry.view<TransformComponent, ParticleEmitterComponent>();
			for (auto e : pview)
			{
				auto& pc = pview.get<ParticleEmitterComponent>(e);
				if (!pc.Emitter)
					continue;
				pc.Emitter->SetTransform(WorldOf(e));
				pc.Emitter->Update(deltaTime, m_WorldTime);
				pc.Emitter->Render(view, sceneDepthID, invVP);
			}
		}

		Renderer3D::EndScene();
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

	void Scene::OnRender3D(const Camera& camera)
	{
		// The scene owns the full 3D pass. Callers must NOT wrap this in their own
		// BeginScene/EndScene. Sorting + frustum culling happen inside Renderer3D's
		// queue (S12.1/S12.2) — this method only decides WHAT to submit.

		// Parametric primitives (E15): (re)build any mesh whose parameters changed
		// or that is still null after a load, before the draw loop consumes it.
		SyncPrimitiveMeshes();

		// World-system recipes (E18): (re)build terrain/water/particle assets from
		// their authoring recipes. No-op for entities without a recipe (UseRecipe
		// false) — so shipped apps that never attach these components are unaffected.
		SyncWorldSystems();

		// Voxel volumes (Phase 18): stream + re-mesh dirty chunks. No-op without a
		// VoxelVolumeComponent (compat gate).
		SyncVoxelVolumes(camera.GetPosition());

		// --- Gather scene lights (S4.5) and upload before drawing. ---
		Renderer3D::SceneLightsDesc lights;
		GatherSceneLights(m_Registry, lights);
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

		// Meshes + LOD groups through the shared submit path (a Main-pass context
		// into the live scene) — identical draws to the pre-H2 inline loops.
		SceneDrawContext ctx;
		ctx.Pass           = ScenePass::Main;
		ctx.ViewProjection = camera.GetViewProjectionMatrix();
		ctx.EyePosition    = camera.GetPosition();
		ctx.CameraPosition = camera.GetPosition();
		SubmitOpaqueMeshes(ctx);

		Renderer3D::EndScene();
	}

	// H2 — routed opaque submit shared by OnRender3D and BuildRenderDesc's DrawOpaque.
	void Scene::SubmitOpaqueMeshes(const SceneDrawContext& ctx)
	{
		const bool depthOnly = ctx.IsDepthOnly();   // shadow / coverage passes

		auto view = m_Registry.view<TransformComponent, MeshRendererComponent>();
		view.each([&](auto entity, const TransformComponent& /*transform*/, const MeshRendererComponent& mr)
		{
			if (!mr.MeshAsset)
				return;
			if (depthOnly && !mr.CastShadows)
				return;

			const int entityID = (int)(uint32_t)entity;
			// World transform (E3): parent-world x local. Flat entities (no
			// RelationshipComponent) resolve to their local transform, so every
			// shipped flat scene renders identically.
			const glm::mat4 xform = WorldOf(entity);
			if (mr.MaterialAsset)
				ctx.DrawMesh(mr.MeshAsset, xform, mr.MaterialAsset, entityID);
			else
				ctx.DrawMesh(mr.MeshAsset, xform, mr.Color, entityID);
		});

		// LOD groups (S12.4): one level per entity, picked by the REAL camera
		// distance so a caster (depth pass) matches its lit level exactly.
		auto lodView = m_Registry.view<TransformComponent, LODGroupComponent>();
		lodView.each([&](auto entity, const TransformComponent& transform, const LODGroupComponent& lod)
		{
			if (depthOnly && !lod.CastShadows)
				return;
			const float dist = glm::distance(ctx.CameraPosition, transform.Position);
			const int level = LODGroupComponent::SelectLevel(lod.Levels, dist);
			if (level < 0 || !lod.Levels[level].MeshAsset)
				return;

			const int entityID = (int)(uint32_t)entity;
			const glm::mat4 xform = WorldOf(entity);
			if (lod.MaterialAsset)
				ctx.DrawMesh(lod.Levels[level].MeshAsset, xform, lod.MaterialAsset, entityID);
			else
				ctx.DrawMesh(lod.Levels[level].MeshAsset, xform, lod.Color, entityID);
		});

		// Voxel volumes (Phase 18): draw each uploaded chunk mesh with the shared
		// atlas material. Chunk positions are absolute voxel coords, so one
		// translate(Origin)*scale(VoxelSize) transform places the whole volume; the
		// S12 queue frustum-culls each chunk mesh by its own AABB for free.
		auto voxView = m_Registry.view<VoxelVolumeComponent>();
		voxView.each([&](auto entity, VoxelVolumeComponent& vc)
		{
			if (!vc.Volume || !vc.Render || !vc.Render->AtlasMaterial)
				return;
			const glm::mat4 xform =
				glm::translate(glm::mat4(1.0f), vc.Volume->GetOrigin()) *
				glm::scale(glm::mat4(1.0f), glm::vec3(vc.Volume->GetVoxelSize()));
			const int entityID = (int)(uint32_t)entity;
			for (const auto& kv : vc.Render->ChunkMeshes)
				if (kv.second)
					ctx.DrawMesh(kv.second, xform, vc.Render->AtlasMaterial, entityID);
		});
	}

	EnvironmentComponent* Scene::FindEnvironment()
	{
		for (auto entity : m_Registry.view<EnvironmentComponent>())
			return &m_Registry.get<EnvironmentComponent>(entity);
		return nullptr;
	}

	// H2 — the ECS → SceneRenderDesc bridge that makes SceneRenderer the editor +
	// player render path. See the Scene.h contract.
	void Scene::BuildRenderDesc(const Camera& camera, float deltaTime, SceneRenderDesc& out)
	{
		// Same top-of-frame asset syncs OnRender3D runs, so a freshly loaded scene
		// (meshes stored by params/path, world systems by recipe) is render-ready.
		SyncPrimitiveMeshes();
		SyncWorldSystems();
		SyncVoxelVolumes(camera.GetPosition());

		m_WorldTime += deltaTime;

		out.SetCamera(camera);
		out.TimeSeconds = m_WorldTime;
		out.DeltaTime   = deltaTime;

		GatherSceneLights(m_Registry, out.Lights);

		// Terrain: the first built asset drives the Reflection/Main/shadow passes and
		// doubles as the shore-attenuation source for the water bodies below.
		Ref<Terrain> shore;
		for (auto e : m_Registry.view<TerrainComponent>())
		{
			if (Ref<Terrain> t = m_Registry.get<TerrainComponent>(e).TerrainAsset)
			{
				out.TerrainSystem = t.get();
				shore             = t;
				break;
			}
		}

		// Water bodies — PrimaryReflectionWater = the one nearest the camera (the only
		// surface that gets a real planar reflection; the rest use IBL fallback).
		{
			const glm::vec3 camPos = camera.GetPosition();
			float bestDist = std::numeric_limits<float>::max();
			for (auto e : m_Registry.view<WaterComponent>())
			{
				auto& wc = m_Registry.get<WaterComponent>(e);
				if (!wc.WaterAsset)
					continue;
				wc.WaterAsset->SetShoreTerrain(shore);
				const glm::vec3 surf{ wc.Center.x, wc.SurfaceHeight, wc.Center.y };
				const float d = glm::distance(camPos, surf);
				if (d < bestDist)
				{
					bestDist = d;
					out.PrimaryReflectionWater = (int)out.WaterBodies.size();
				}
				out.WaterBodies.push_back(wc.WaterAsset.get());
			}
			if (out.WaterBodies.empty())
				out.PrimaryReflectionWater = -1;
		}

		// Particle emitters — advanced here (SceneRenderer only DRAWS them) and placed
		// at their entity's world transform.
		for (auto e : m_Registry.view<TransformComponent, ParticleEmitterComponent>())
		{
			auto& pc = m_Registry.get<ParticleEmitterComponent>(e);
			if (!pc.Emitter)
				continue;
			pc.Emitter->SetTransform(WorldOf(e));
			pc.Emitter->Update(deltaTime, m_WorldTime);
			out.Emitters.push_back(pc.Emitter.get());
		}

		// Opaque geometry is submitted per pass (main/reflection/shadow) through the
		// routed context — so meshes reflect + cast. EcsScene stays null (leaving it
		// set would double-draw against DrawOpaque + re-draw terrain in the opaque pass).
		out.DrawOpaque = [this](const SceneDrawContext& c) { SubmitOpaqueMeshes(c); };
	}
} // Closes namespace Cosmic