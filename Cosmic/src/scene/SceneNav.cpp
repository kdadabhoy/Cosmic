// scene/SceneNav.cpp — the Scene <-> NavWorld bake pipeline (N2). See header.

#include "scene/SceneNav.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scene/Components3D.h"   // W4 — NavMeshComponent / NavAgentComponent
#include "physics/ScenePhysics.h"     // BuildColliderDesc — the collision-view enumeration
#include "physics/PhysicsTypes.h"     // BodyDesc / CollisionShapeDesc
#include "nav/NavWorld.h"
#include "voxel/VoxelVolume.h"
#include "voxel/BlockPalette.h"
#include "voxel/VoxelMesher.h"
#include "graphics/Mesh.h"            // MeshData / MeshVertex (voxel collision mesh)
#include "jobs/JobSystem.h"
#include "utils/FileSystem.h"
#include "core/Log.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Cosmic
{
    namespace
    {
        // --- triangle-soup emitters ------------------------------------------
        void AppendTri(std::vector<float>& v, std::vector<int>& t,
                       const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            const int base = int(v.size() / 3);
            for (const glm::vec3& p : { a, b, c }) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
            t.push_back(base); t.push_back(base + 1); t.push_back(base + 2);
        }

        // A box centered at the origin of `model`, half-extents `he`. Outward-CCW
        // winding (the +Y top face is walkable; sides/bottom are walls/floor to
        // Recast, which rasterizes every face regardless of facing).
        void AppendBox(std::vector<float>& v, std::vector<int>& t, const glm::mat4& model, const glm::vec3& he)
        {
            glm::vec3 c[8];
            c[0] = { -he.x, -he.y, -he.z }; c[1] = { he.x, -he.y, -he.z };
            c[2] = {  he.x,  he.y, -he.z }; c[3] = {-he.x,  he.y, -he.z };
            c[4] = { -he.x, -he.y,  he.z }; c[5] = { he.x, -he.y,  he.z };
            c[6] = {  he.x,  he.y,  he.z }; c[7] = {-he.x,  he.y,  he.z };
            for (glm::vec3& p : c) p = glm::vec3(model * glm::vec4(p, 1.0f));
            static const int idx[36] = {
                0,3,2, 0,2,1,   // -Z
                4,5,6, 4,6,7,   // +Z
                0,4,7, 0,7,3,   // -X
                1,2,6, 1,6,5,   // +X
                0,1,5, 0,5,4,   // -Y
                3,7,6, 3,6,2 }; // +Y (walkable top)
            for (int i = 0; i < 36; i += 3)
                AppendTri(v, t, c[idx[i]], c[idx[i + 1]], c[idx[i + 2]]);
        }

        void AppendMesh(std::vector<float>& v, std::vector<int>& t, const glm::mat4& model,
                        const glm::vec3& scale, const std::vector<glm::vec3>& mv, const std::vector<uint32_t>& mi)
        {
            for (size_t i = 0; i + 2 < mi.size(); i += 3)
            {
                const glm::vec3 a = glm::vec3(model * glm::vec4(mv[mi[i]]     * scale, 1.0f));
                const glm::vec3 b = glm::vec3(model * glm::vec4(mv[mi[i + 1]] * scale, 1.0f));
                const glm::vec3 c = glm::vec3(model * glm::vec4(mv[mi[i + 2]] * scale, 1.0f));
                AppendTri(v, t, a, b, c);
            }
        }

        // Terrain heightfield: vertices are already absolute world (the collider bakes
        // the terrain's world placement in; body pose is identity). Two tris per cell.
        void AppendHeightField(std::vector<float>& v, std::vector<int>& t, const CollisionShapeDesc& d)
        {
            const uint32_t n = d.HeightFieldSize;
            if (n < 2) return;
            auto W = [&](uint32_t i, uint32_t j) {
                return glm::vec3(d.HeightFieldOffset.x + i * d.HeightFieldCellSize,
                                 d.HeightSamples[size_t(j) * n + i],
                                 d.HeightFieldOffset.z + j * d.HeightFieldCellSize);
            };
            for (uint32_t j = 0; j + 1 < n; ++j)
                for (uint32_t i = 0; i + 1 < n; ++i)
                {
                    const glm::vec3 a = W(i, j), b = W(i + 1, j), c = W(i + 1, j + 1), e = W(i, j + 1);
                    AppendTri(v, t, a, c, b);   // up-facing
                    AppendTri(v, t, a, e, c);
                }
        }

        // Tessellate one enumerated collision body into world-space triangles.
        // Box/Mesh/HeightField are exact; curved/convex shapes (Sphere/Capsule/
        // ConvexHull) contribute their AABB — a conservative obstacle, and a v1
        // simplification (they are rarely the walkable floor).
        void TessellateBody(const BodyDesc& d, std::vector<float>& v, std::vector<int>& t)
        {
            const glm::mat4 bodyModel = glm::translate(glm::mat4(1.0f), d.Position) * glm::mat4_cast(d.Rotation);
            for (const CollisionShapeDesc& s : d.Shapes)
            {
                const glm::mat4 shapeModel = bodyModel
                    * glm::translate(glm::mat4(1.0f), s.Offset) * glm::mat4_cast(s.OffsetRotation);
                using Kind = CollisionShapeDesc::Kind;
                switch (s.Shape)
                {
                    case Kind::Box:
                        AppendBox(v, t, shapeModel, glm::abs(s.HalfExtents * s.Scale));
                        break;
                    case Kind::Sphere:
                        AppendBox(v, t, shapeModel, glm::vec3(std::abs(s.Radius * s.Scale.x)));
                        break;
                    case Kind::Capsule:
                    {
                        const float r  = std::abs(s.Radius * s.Scale.x);
                        const float hh = std::abs(s.HalfHeight * s.Scale.y);
                        AppendBox(v, t, shapeModel, glm::vec3(r, hh + r, r));
                        break;
                    }
                    case Kind::ConvexHull:
                    {
                        if (s.Vertices.empty()) break;
                        glm::vec3 mn(1e30f), mx(-1e30f);
                        for (const glm::vec3& p : s.Vertices) { glm::vec3 q = p * s.Scale; mn = glm::min(mn, q); mx = glm::max(mx, q); }
                        const glm::vec3 center = (mn + mx) * 0.5f;
                        AppendBox(v, t, shapeModel * glm::translate(glm::mat4(1.0f), center), glm::max((mx - mn) * 0.5f, glm::vec3(0.01f)));
                        break;
                    }
                    case Kind::Mesh:
                        AppendMesh(v, t, shapeModel, s.Scale, s.Vertices, s.Indices);
                        break;
                    case Kind::HeightField:
                        AppendHeightField(v, t, s);
                        break;
                }
            }
        }

        void AppendVoxels(Scene& scene, entt::entity e, std::vector<float>& v, std::vector<int>& t)
        {
            auto* vc = scene.GetRegistry().try_get<VoxelVolumeComponent>(e);
            if (!vc || !vc->Volume || !vc->Palette) return;
            const glm::vec3 origin = vc->Volume->GetOrigin();
            const float     vs     = vc->Volume->GetVoxelSize();
            std::vector<glm::ivec3> chunks;
            vc->Volume->ForEachChunk([&](const glm::ivec3& c) { chunks.push_back(c); });
            for (const glm::ivec3& c : chunks)
            {
                const MeshData md = VoxelMesher::BuildCollision(*vc->Volume, c, *vc->Palette);
                for (size_t i = 0; i + 2 < md.Indices.size(); i += 3)
                {
                    const glm::vec3 a = origin + md.Vertices[md.Indices[i]].Position     * vs;
                    const glm::vec3 b = origin + md.Vertices[md.Indices[i + 1]].Position * vs;
                    const glm::vec3 cc = origin + md.Vertices[md.Indices[i + 2]].Position * vs;
                    AppendTri(v, t, a, b, cc);
                }
            }
        }

        void HashBytes(std::size_t& h, const void* data, size_t bytes)
        {
            const unsigned char* p = static_cast<const unsigned char*>(data);
            for (size_t i = 0; i < bytes; ++i)   // FNV-1a
            {
                h ^= p[i];
                h *= 1099511628211ull;
            }
        }
        template<class T> void HashPod(std::size_t& h, const T& value) { HashBytes(h, &value, sizeof(T)); }
    }

    // ========================================================================
    // Geometry + recipe
    // ========================================================================
    void SceneNav::GatherGeometry(Scene& scene, entt::entity navEntity,
                                  std::vector<float>& outVerts, std::vector<int>& outTris)
    {
        auto& reg = scene.GetRegistry();
        const auto* nm = reg.try_get<NavMeshComponent>(navEntity);
        const NavSourceMode mode = nm ? nm->SourceMode : NavSourceMode::WholeScene;

        Entity navE(navEntity, &scene);
        auto inScope = [&](entt::entity e) -> bool
        {
            if (mode == NavSourceMode::WholeScene) return true;
            if (e == navEntity) return true;                         // the marker itself
            return scene.IsAncestor(navE, Entity(e, &scene));         // a descendant
        };

        // 1) Colliders (box/sphere/capsule/mesh + terrain heightfield) via the
        //    ScenePhysics collision-view enumeration — the honest physics source.
        for (auto e : reg.view<TransformComponent>())
        {
            if (!scene.IsActiveInHierarchy(e) || !inScope(e)) continue;
            const bool hasCollider = reg.any_of<BoxColliderComponent, SphereColliderComponent,
                CapsuleColliderComponent, MeshColliderComponent, TerrainColliderComponent>(e);
            if (!hasCollider) continue;
            BodyDesc d;
            if (ScenePhysics::BuildColliderDesc(scene, e, d))
                TessellateBody(d, outVerts, outTris);
        }

        // 2) Voxel volumes — per-chunk collision meshes (resident chunks only).
        for (auto e : reg.view<VoxelVolumeComponent>())
        {
            if (!scene.IsActiveInHierarchy(e) || !inScope(e)) continue;
            AppendVoxels(scene, e, outVerts, outTris);
        }
    }

    NavBuildDesc SceneNav::MakeBuildDesc(const NavMeshComponent& c)
    {
        NavBuildDesc d;
        d.CellSize   = c.CellSize;
        d.CellHeight = c.CellHeight;
        d.AgentRadius   = c.AgentRadius;
        d.AgentHeight   = c.AgentHeight;
        d.AgentMaxClimb = c.AgentMaxClimb;
        d.AgentMaxSlopeDeg = c.AgentMaxSlope;
        d.RegionMinSize   = c.RegionMinSize;
        d.RegionMergeSize = c.RegionMergeSize;
        d.EdgeMaxLen   = c.EdgeMaxLen;
        d.EdgeMaxError = c.EdgeMaxError;
        d.DetailSampleDist     = c.DetailSampleDist;
        d.DetailSampleMaxError = c.DetailSampleMaxError;
        d.VertsPerPoly = c.VertsPerPoly;
        d.TileSize     = c.TileSize;
        return d;
    }

    std::size_t SceneNav::Signature(const NavMeshComponent& c,
                                    const std::vector<float>& verts, const std::vector<int>& tris)
    {
        std::size_t h = 1469598103934665603ull;   // FNV-1a offset basis
        HashPod(h, c.CellSize);   HashPod(h, c.CellHeight);
        HashPod(h, c.AgentRadius); HashPod(h, c.AgentHeight); HashPod(h, c.AgentMaxClimb); HashPod(h, c.AgentMaxSlope);
        HashPod(h, c.RegionMinSize); HashPod(h, c.RegionMergeSize);
        HashPod(h, c.EdgeMaxLen); HashPod(h, c.EdgeMaxError);
        HashPod(h, c.DetailSampleDist); HashPod(h, c.DetailSampleMaxError);
        HashPod(h, c.VertsPerPoly); HashPod(h, c.TileSize);
        HashPod(h, c.SourceMode);
        if (!verts.empty()) HashBytes(h, verts.data(), verts.size() * sizeof(float));
        if (!tris.empty())  HashBytes(h, tris.data(),  tris.size()  * sizeof(int));
        if (h == 0) h = 1;   // never collide with the "never built" sentinel
        return h;
    }

    // ========================================================================
    // Bake
    // ========================================================================
    bool SceneNav::BakeSync(Scene& scene, entt::entity navEntity)
    {
        auto* nm = scene.GetRegistry().try_get<NavMeshComponent>(navEntity);
        if (!nm) return false;

        std::vector<float> verts; std::vector<int> tris;
        GatherGeometry(scene, navEntity, verts, tris);
        const std::size_t sig = Signature(*nm, verts, tris);

        Ref<NavWorld> nav = CreateRef<NavWorld>();
        std::string err;
        if (!nav->Build(MakeBuildDesc(*nm), NavGeometryInput{ verts, tris }, &err))
        {
            nm->Baking = false;
            return false;
        }
        nm->Nav = nav;
        nm->BuiltSignature = sig;
        nm->Baking = false;
        return true;
    }

    NavBakeJob SceneNav::BeginBake(Scene& scene, entt::entity navEntity)
    {
        NavBakeJob job;
        auto* nm = scene.GetRegistry().try_get<NavMeshComponent>(navEntity);
        if (!nm) return job;

        std::vector<float> verts; std::vector<int> tris;
        GatherGeometry(scene, navEntity, verts, tris);        // main thread (reads ECS)

        job.Signature = Signature(*nm, verts, tris);
        job.Result    = CreateRef<NavWorld>();
        job.Done      = CreateRef<std::atomic<bool>>(false);
        job.Valid     = true;
        nm->Baking    = true;

        const NavBuildDesc desc = MakeBuildDesc(*nm);
        auto result = job.Result;
        auto done   = job.Done;
        auto work = [desc, result, done, v = std::move(verts), t = std::move(tris)]() mutable
        {
            result->Build(desc, NavGeometryInput{ v, t });     // Recast is GL-free — safe on a worker
            done->store(true, std::memory_order_release);
        };

        JobSystem& js = JobSystem::Get();
        if (js.IsInitialized()) js.Submit(std::move(work));
        else                    work();                        // inline (headless / no pool)
        return job;
    }

    bool SceneNav::FinishBake(Scene& scene, entt::entity navEntity, NavBakeJob& job)
    {
        if (!job.Valid || !job.IsDone()) return false;
        if (auto* nm = scene.GetRegistry().try_get<NavMeshComponent>(navEntity))
        {
            if (job.Result && job.Result->IsBuilt())
            {
                nm->Nav = job.Result;
                nm->BuiltSignature = job.Signature;
            }
            nm->Baking = false;
        }
        job = {};   // consume
        return true;
    }

    // ========================================================================
    // `.cnav` sidecar
    // ========================================================================
    std::string SceneNav::SidecarPathFor(const NavMeshComponent& c, const std::string& scenePath)
    {
        if (!c.SidecarPath.empty()) return c.SidecarPath;
        if (scenePath.empty())      return {};
        // Derive `<scene-without-ext>.cnav` beside the scene (single-navmesh default;
        // author an explicit SidecarPath for multiple navmeshes in one scene).
        std::filesystem::path p = std::filesystem::u8path(scenePath);
        p.replace_extension(".cnav");
        return p.string();
    }

    bool SceneNav::SaveSidecar(const NavMeshComponent& c, const std::string& path)
    {
        if (!c.Nav || !c.Nav->IsBuilt() || path.empty()) return false;
        const NavMeshData data = c.Nav->Serialize();
        if (data.Empty()) return false;

        const std::string resolved = FileSystem::Resolve(path);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::u8path(resolved).parent_path(), ec);
        std::ofstream os(std::filesystem::u8path(resolved), std::ios::binary | std::ios::trunc);
        if (!os) { CS_CORE_ERROR("SceneNav::SaveSidecar — cannot open '{}'", resolved); return false; }
        os.write(reinterpret_cast<const char*>(data.Bytes.data()), (std::streamsize)data.Bytes.size());
        return static_cast<bool>(os);
    }

    bool SceneNav::LoadSidecar(NavMeshComponent& c, const std::string& path)
    {
        if (path.empty()) return false;
        const std::string resolved = FileSystem::Resolve(path);
        std::ifstream is(std::filesystem::u8path(resolved), std::ios::binary);
        if (!is) return false;
        NavMeshData data;
        data.Bytes.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
        if (data.Empty()) return false;

        Ref<NavWorld> nav = CreateRef<NavWorld>();
        if (!nav->Load(data)) return false;
        c.Nav = nav;
        return true;
    }

    // ========================================================================
    // SceneNavRuntime (N4) — play-session crowd binding
    // ========================================================================
    SceneNavRuntime::SceneNavRuntime(Scene& scene) : m_Scene(scene) {}
    SceneNavRuntime::~SceneNavRuntime() { Teardown(); }

    void SceneNavRuntime::BuildAgents()
    {
        auto& reg = m_Scene.GetRegistry();

        // Primary navmesh: the first NavMeshComponent with a built Nav (lazily load
        // its `.cnav` sidecar). v1 runs one active navmesh for the whole crowd.
        for (auto e : reg.view<NavMeshComponent>())
        {
            auto& nm = reg.get<NavMeshComponent>(e);
            if (!nm.Nav && !nm.SidecarPath.empty())
                SceneNav::LoadSidecar(nm, nm.SidecarPath);
            if (nm.Nav && nm.Nav->IsBuilt()) { m_Nav = nm.Nav.get(); break; }
        }
        if (!m_Nav) return;   // no navmesh — agents inert

        float maxRadius = 0.6f;
        for (auto e : reg.view<NavAgentComponent>())
            maxRadius = std::max(maxRadius, reg.get<NavAgentComponent>(e).Radius);
        m_Nav->CrowdInit(maxRadius, 256);

        // One agent per NavAgentComponent, in deterministic entt view order (the
        // crowd assigns ids in add order -> reproducible across runs).
        for (auto e : reg.view<NavAgentComponent, TransformComponent>())
        {
            if (!m_Scene.IsActiveInHierarchy(e)) continue;
            const auto& ac = reg.get<NavAgentComponent>(e);
            const glm::vec3 pos = glm::vec3(m_Scene.GetWorldTransform(Entity(e, &m_Scene))[3]);

            NavAgentParams p;
            p.Radius = ac.Radius; p.Height = ac.Height;
            p.MaxSpeed = ac.MaxSpeed; p.MaxAccel = ac.MaxAccel;

            const int id = m_Nav->AddAgent(pos, p);
            if (id >= 0)
            {
                AgentSlot slot;
                slot.Id = id;
                slot.StopDist = ac.StoppingDistance;
                m_Agents.emplace(e, slot);
            }
        }
    }

    void SceneNavRuntime::Step(float fixedDt)
    {
        if (!m_Nav || !m_Nav->CrowdReady()) return;
        m_Nav->UpdateCrowd(fixedDt);

        for (auto& [e, slot] : m_Agents)
        {
            WriteBackWorldPos(e, m_Nav->GetAgentPosition(slot.Id));

            if (slot.HasTarget && !slot.ArrivedLatched &&
                m_Nav->AgentDistanceToTarget(slot.Id) <= slot.StopDist)
            {
                slot.ArrivedLatched = true;
                slot.HasTarget      = false;
                m_Nav->ResetAgentTarget(slot.Id);
                m_Scene.Events().Emit("nav.arrived", Entity(e, &m_Scene));   // U2 signal
            }
        }
    }

    void SceneNavRuntime::Teardown()
    {
        if (m_Nav) m_Nav->CrowdShutdown();
        m_Agents.clear();
        m_Nav = nullptr;
    }

    bool SceneNavRuntime::HasAgent(entt::entity e) const { return m_Agents.count(e) != 0; }

    void SceneNavRuntime::SetTarget(entt::entity e, const glm::vec3& target)
    {
        auto it = m_Agents.find(e);
        if (it == m_Agents.end() || !m_Nav) return;
        m_Nav->SetAgentTarget(it->second.Id, target);
        it->second.HasTarget      = true;
        it->second.ArrivedLatched = false;
    }

    void SceneNavRuntime::Stop(entt::entity e)
    {
        auto it = m_Agents.find(e);
        if (it == m_Agents.end() || !m_Nav) return;
        m_Nav->ResetAgentTarget(it->second.Id);
        it->second.HasTarget      = false;
        it->second.ArrivedLatched = false;
    }

    bool SceneNavRuntime::HasArrived(entt::entity e) const
    {
        auto it = m_Agents.find(e);
        return it != m_Agents.end() && it->second.ArrivedLatched;
    }

    glm::vec3 SceneNavRuntime::AgentPosition(entt::entity e) const
    {
        auto it = m_Agents.find(e);
        return (it != m_Agents.end() && m_Nav) ? m_Nav->GetAgentPosition(it->second.Id) : glm::vec3(0.0f);
    }

    glm::vec3 SceneNavRuntime::AgentVelocity(entt::entity e) const
    {
        auto it = m_Agents.find(e);
        return (it != m_Agents.end() && m_Nav) ? m_Nav->GetAgentVelocity(it->second.Id) : glm::vec3(0.0f);
    }

    void SceneNavRuntime::WriteBackWorldPos(entt::entity e, const glm::vec3& worldPos)
    {
        auto& reg = m_Scene.GetRegistry();
        auto* tc = reg.try_get<TransformComponent>(e);
        if (!tc) return;

        // Parented agents: decompose the world position into the parent's local
        // frame (rotation/scale untouched — the crowd only moves position).
        if (const auto* rel = reg.try_get<RelationshipComponent>(e); rel && rel->Parent != UUID(0))
        {
            Entity parent = m_Scene.FindByUUID(rel->Parent);
            if (parent)
            {
                const glm::mat4 pw = m_Scene.GetWorldTransform(parent);
                tc->Position = glm::vec3(glm::inverse(pw) * glm::vec4(worldPos, 1.0f));
                return;
            }
        }
        tc->Position = worldPos;
    }
}
