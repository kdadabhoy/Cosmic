// scene/Scene3D.cpp — the 3D half of the Scene implementation (Phase 29 W5).
//
// scene/Scene.cpp keeps everything a 2D engine still needs: entity CRUD and the
// hierarchy, the update / fixed-update passes, the physics session (physics is
// dimension agnostic — plan doc 28 §4.1), the legacy 2D render path, sprite
// animation, the sprite pass and the 2D light composite. This file holds the parts
// that only mean something in a 3D world: the nav session, the render-path asset
// syncs (primitives / world systems / voxels / navmeshes), the world-FX pass, the
// direct 3D pass, the skeletal animators, the routed opaque submit and the
// ECS → SceneRenderDesc bridge.
//
// Cosmic/CMakeLists.txt drops this whole translation unit from a 2D build (step 3),
// and the #ifndef below is the same belt-and-braces fence Components3D.h and
// TypeRegistry3D.cpp carry: hand this file to the 2D configuration anyway and it
// compiles to nothing.
//
// Pure code motion (plan doc 28 §7.3). Every function below is the pre-split text
// verbatim — same order, same comments, same behaviour — so the 3D build is
// behaviour- and pixel-identical. test_render_desc is the net under BuildRenderDesc:
// it pins every SceneRenderDesc field this bridge fills, which is exactly what a
// move like this can silently drop.

#ifndef COSMIC_2D_ONLY

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/Components3D.h"       // W4 — the 3D component half this file is written against
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
#include "scene/SceneNav.h"           // N2/N4 — sidecar load + the play-session crowd
#include "camera/Camera.h"
#include "jobs/JobSystem.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

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
		void GatherSceneLights(Scene& scene, entt::registry& reg, Renderer3D::SceneLightsDesc& lights)
		{
			lights.Ambient = Renderer3D::GetAmbient();

			for (auto entity : reg.view<DirectionalLightComponent>())
			{
				const auto& dl = reg.get<DirectionalLightComponent>(entity);
				if (!dl.Enabled)                          // T12 — disabled light
					continue;
				if (!scene.IsActiveInHierarchy(entity))   // T13 — inactive subtree
					continue;
				lights.SunDirection = dl.Direction;
				lights.SunColor     = dl.Color;
				lights.SunIntensity = dl.Intensity;
				break;   // first ENABLED + ACTIVE directional light wins as the sun
			}

			reg.view<TransformComponent, PointLightComponent>().each(
				[&](auto entity, const TransformComponent& t, const PointLightComponent& pl)
			{
				if (!pl.Enabled)                          // T12
					return;
				if (!scene.IsActiveInHierarchy(entity))   // T13
					return;
				Renderer3D::PointLightDesc d;
				d.Position  = t.Position;
				d.Radius    = pl.Radius;
				d.Color     = pl.Color;
				d.Intensity = pl.Intensity;
				lights.Points.push_back(d);
			});
		}
	}

	// --- Nav session (N4) — mirrors the physics session ----------------------
	void Scene::OnNavStart()
	{
		// Compat gate: only spin up the runtime when there's something to do (an
		// agent to steer, or a navmesh worth binding for script queries).
		auto agents = m_Registry.view<NavAgentComponent>();
		auto meshes = m_Registry.view<NavMeshComponent>();
		if (agents.begin() == agents.end() && meshes.begin() == meshes.end())
			return;
		m_NavRuntime = std::make_unique<SceneNavRuntime>(*this);
		m_NavRuntime->BuildAgents();
	}

	void Scene::OnNavStep(float fixedDeltaTime)
	{
		if (m_NavRuntime)
			m_NavRuntime->Step(fixedDeltaTime);
	}

	void Scene::OnNavStop()
	{
		if (m_NavRuntime)
		{
			m_NavRuntime->Teardown();
			m_NavRuntime.reset();
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

			// M5 — resolve per-submesh material slots (guarded; the editor resets
			// the flag when a slot path changes). Empty paths resolve to null so a
			// slot falls back to the legacy material / colour at draw time.
			if (!mr.MaterialPaths.empty() && !mr.MaterialPathsResolved)
			{
				mr.MaterialPathsResolved = true;
				mr.MaterialAssets.clear();
				mr.MaterialAssets.reserve(mr.MaterialPaths.size());
				for (const std::string& p : mr.MaterialPaths)
					mr.MaterialAssets.push_back(p.empty() ? nullptr : AssetLibrary::GetMaterial(p));
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
				if (!wc.UseRecipe || !wc.Enabled || !IsActiveInHierarchy(e))   // T12/T13
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
				if (!pc.UseRecipe || !pc.Enabled || !IsActiveInHierarchy(e))   // T12/T13
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

	void Scene::SyncNavMeshes()
	{
		auto view = m_Registry.view<NavMeshComponent>();
		for (auto e : view)
		{
			auto& nm = view.get<NavMeshComponent>(e);
			if (nm.Nav || nm.SidecarPath.empty())
				continue;   // already loaded, or nothing on disk to load (bake produces it)
			SceneNav::LoadSidecar(nm, nm.SidecarPath);   // best-effort; a stale/missing sidecar just stays unbaked
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
				if (!wc.WaterAsset || !wc.Enabled)   // T12
					continue;
				if (!IsActiveInHierarchy(e))         // T13
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
				if (!pc.Emitter || !pc.Enabled)   // T12
					continue;
				if (!IsActiveInHierarchy(e))      // T13 — not ticked or drawn
					continue;
				pc.Emitter->SetTransform(WorldOf(e));
				pc.Emitter->Update(deltaTime, m_WorldTime);
				pc.Emitter->Render(view, sceneDepthID, invVP);
			}
		}

		Renderer3D::EndScene();
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

		// Navmeshes (Phase 26): lazily load `.cnav` sidecars. No-op without a
		// NavMeshComponent (compat gate).
		SyncNavMeshes();

		// --- Gather scene lights (S4.5) and upload before drawing. ---
		Renderer3D::SceneLightsDesc lights;
		GatherSceneLights(*this, m_Registry, lights);
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

	// A2 — the Animator driving `entity`: its own component, or the nearest
	// ancestor's (multi-mesh imports hang child MeshRenderers under one
	// animated parent). Null when none.
	AnimatorComponent* Scene::FindAnimatorFor(entt::entity entity)
	{
		entt::entity cur = entity;
		for (int depth = 0; depth < 64 && cur != entt::null; ++depth)
		{
			if (auto* an = m_Registry.try_get<AnimatorComponent>(cur))
				return an;
			auto* rel = m_Registry.try_get<RelationshipComponent>(cur);
			if (!rel || (uint64_t)rel->Parent == 0)
				break;
			Entity parent = FindByUUID(rel->Parent);
			cur = parent ? (entt::entity)parent : entt::null;
		}
		return nullptr;
	}

	void Scene::UpdateAnimators(float deltaTime)
	{
		// The skeleton an animator drives: its own entity's (or a descendant's)
		// skinned mesh — multi-mesh imports parent the pieces under the animator.
		auto findSkeleton = [this](entt::entity root) -> Ref<Skeleton>
		{
			std::vector<entt::entity> stack{ root };
			while (!stack.empty())
			{
				const entt::entity e = stack.back();
				stack.pop_back();
				if (auto* mr = m_Registry.try_get<MeshRendererComponent>(e))
					if (mr->MeshAsset && mr->MeshAsset->IsSkinned())
						return mr->MeshAsset->GetSkeleton();
				if (auto* rel = m_Registry.try_get<RelationshipComponent>(e))
					for (const UUID& c : rel->Children)
						if (Entity child = FindByUUID(c))
							stack.push_back((entt::entity)child);
			}
			return nullptr;
		};

		auto view = m_Registry.view<AnimatorComponent>();
		for (auto e : view)
		{
			auto& an = view.get<AnimatorComponent>(e);

			// Resolve the clip when the path changes (guarded, like MeshPath).
			if (an.ClipPath != an.ResolvedClipPath)
			{
				an.ResolvedClipPath = an.ClipPath;
				an.ClipRef = an.ClipPath.empty() ? nullptr
				                                 : AssetLibrary::GetAnimationClip(an.ClipPath);
				an.TimeSeconds = 0.0f;
			}

			if (!an.SkelRef)
				an.SkelRef = findSkeleton(e);   // meshes may resolve a frame later — retried

			if (!an.SkelRef || an.SkelRef->JointCount() == 0)
			{
				an.Palette.clear();
				an.JointModelMatrices.clear();   // M4 — no rig, no joints to socket to
				continue;
			}

			// Pose locals: a resolved clip samples the play head; a rig with no
			// clip holds its bind pose (so sockets still track it). The DRAW path
			// only ever consumes Palette, which stays empty without a clip — the
			// pre-M4 static bind-pose draw, byte-identical.
			if (an.ClipRef)
			{
				const float duration = an.ClipRef->Duration;
				if (an.Playing)
				{
					an.TimeSeconds += deltaTime * an.Speed;
					if (!an.Loop && duration > 0.0f)
						an.TimeSeconds = glm::clamp(an.TimeSeconds, 0.0f, duration);
					an.NormalizedTime = duration > 0.0f
						? an.ClipRef->ResolveTime(an.TimeSeconds, an.Loop) / duration : 0.0f;
				}
				else
				{
					// Paused: the (scrubbed) play head is authoritative.
					an.TimeSeconds = an.NormalizedTime * duration;
				}

				// M6 — resolve the crossfade target when it changes (guarded).
				if (an.NextClipPath != an.ResolvedNextClipPath)
				{
					an.ResolvedNextClipPath = an.NextClipPath;
					an.NextClipRef = an.NextClipPath.empty() ? nullptr
					                                         : AssetLibrary::GetAnimationClip(an.NextClipPath);
					an.NextTimeSeconds = 0.0f;
					an.FadeElapsed     = 0.0f;
				}

				if (an.NextClipRef && an.FadeDuration > 0.0f)
				{
					// Crossfade: advance the next head + the fade (while playing),
					// then pose-blend the two sampled poses at the fade weight.
					if (an.Playing)
					{
						an.NextTimeSeconds += deltaTime * an.Speed;
						an.FadeElapsed     += deltaTime;
					}
					const float w = glm::clamp(an.FadeElapsed / an.FadeDuration, 0.0f, 1.0f);
					if (w >= 1.0f)
					{
						// Fade complete — promote the next clip to current.
						an.ClipRef          = an.NextClipRef;
						an.ClipPath         = an.NextClipPath;
						an.ResolvedClipPath = an.NextClipPath;
						an.TimeSeconds      = an.NextTimeSeconds;
						an.NextClipRef      = nullptr;
						an.NextClipPath.clear();
						an.ResolvedNextClipPath.clear();
						an.FadeDuration = an.FadeElapsed = 0.0f;
						an.ClipRef->Sample(*an.SkelRef, an.TimeSeconds, an.Loop, an.ScratchLocals);
					}
					else
					{
						an.ClipRef->Sample(*an.SkelRef, an.TimeSeconds, an.Loop, an.ScratchLocals);
						an.NextClipRef->Sample(*an.SkelRef, an.NextTimeSeconds, an.Loop, an.ScratchLocalsB);
						AnimationClip::BlendLocals(an.ScratchLocals, an.ScratchLocalsB, w, an.ScratchLocals);
					}
				}
				else
				{
					an.ClipRef->Sample(*an.SkelRef, an.TimeSeconds, an.Loop, an.ScratchLocals);
				}

				an.SkelRef->ComputePalette(an.ScratchLocals, an.Palette);
			}
			else
			{
				an.Palette.clear();                             // no clip → static bind-pose draw (compat)
				an.SkelRef->GetBindLocals(an.ScratchLocals);    // expose the bind pose to sockets
			}

			// M4 — publish per-joint BAKED-space model frames for sockets + the
			// editor bone overlay: ImportCorrection · global (no inverse-bind), so
			// a child placed at JointModelMatrices[j] sits ON joint j.
			an.SkelRef->ComputeGlobals(an.ScratchLocals, an.ScratchGlobals);
			an.JointModelMatrices.resize(an.ScratchGlobals.size());
			for (size_t j = 0; j < an.ScratchGlobals.size(); ++j)
				an.JointModelMatrices[j] = an.SkelRef->ImportCorrection * an.ScratchGlobals[j];
		}
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
			if (!mr.Enabled)   // T12 — hidden in every pass (lit + shadow)
				return;
			if (!IsActiveInHierarchy(entity))   // T13 — inactive subtree
				return;
			if (depthOnly && !mr.CastShadows)
				return;

			const int entityID = (int)(uint32_t)entity;
			// World transform (E3): parent-world x local. Flat entities (no
			// RelationshipComponent) resolve to their local transform, so every
			// shipped flat scene renders identically.
			const glm::mat4 xform = WorldOf(entity);

			// A2 — a skinned mesh with a live Animator palette routes through
			// the skinned path (lit + shadow twins). Everything else — skinned
			// meshes with no/paused-out animator included — draws statically
			// (bind pose), which is the pre-A2 behavior exactly.
			if (mr.MeshAsset->IsSkinned() && mr.MaterialAsset)
			{
				if (AnimatorComponent* an = FindAnimatorFor(entity);
				    an && !an->Palette.empty() &&
				    an->Palette.size() == mr.MeshAsset->GetSkeleton()->JointCount())
				{
					ctx.DrawMeshSkinned(mr.MeshAsset, xform, mr.MaterialAsset,
					                    an->Palette.data(), (uint32_t)an->Palette.size(), entityID);
					return;
				}
			}

			// M5 — multi-material meshes: one lit draw per submesh with its slot
			// material (the queue still sorts/culls each). EMPTY MaterialAssets (or
			// a mesh with no submesh table) skips this entirely and the legacy
			// single-material path below runs — byte-identical. Skinned meshes stay
			// on the single-material skinned path (multi-material skinned deferred).
			if (!mr.MeshAsset->IsSkinned() && !mr.MaterialAssets.empty() &&
			    mr.MeshAsset->HasSubmeshes())
			{
				if (depthOnly)
				{
					// Shadow / coverage: one whole-mesh caster — depth ignores the
					// material split, so this matches a single-material caster.
					ctx.DrawMesh(mr.MeshAsset, xform, mr.Color, entityID);
				}
				else
				{
					for (const Submesh& sm : mr.MeshAsset->GetSubmeshes())
					{
						Ref<Material> slot = (sm.MaterialIndex < mr.MaterialAssets.size())
							? mr.MaterialAssets[sm.MaterialIndex] : nullptr;
						if (!slot) slot = mr.MaterialAsset;   // legacy fallback (may be null → colour)
						ctx.DrawMeshRange(mr.MeshAsset, xform, slot, mr.Color,
						                  sm.IndexOffset, sm.IndexCount, entityID);
					}
				}
				return;
			}

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
			if (!IsActiveInHierarchy(entity))   // T13
				return;
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
			if (!IsActiveInHierarchy(entity))   // T13
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

	// H2 — the ECS → SceneRenderDesc bridge that makes SceneRenderer the editor +
	// player render path. See the Scene.h contract.
	void Scene::BuildRenderDesc(const Camera& camera, float deltaTime, SceneRenderDesc& out)
	{
		// Same top-of-frame asset syncs OnRender3D runs, so a freshly loaded scene
		// (meshes stored by params/path, world systems by recipe) is render-ready.
		SyncPrimitiveMeshes();
		SyncWorldSystems();
		SyncVoxelVolumes(camera.GetPosition());
		SyncNavMeshes();

		m_WorldTime += deltaTime;

		out.SetCamera(camera);
		out.TimeSeconds = m_WorldTime;
		out.DeltaTime   = deltaTime;

		GatherSceneLights(*this, m_Registry, out.Lights);

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

#endif   // COSMIC_2D_ONLY
