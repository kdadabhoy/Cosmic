// physics/ScenePhysics.cpp — Scene <-> PhysicsWorld runtime binding (J4/J5). See header.

#include "physics/ScenePhysics.h"
#include "physics/PhysicsWorld.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#ifndef COSMIC_2D_ONLY
#include "scene/Components3D.h"   // W4 — MeshCollider/TerrainCollider + the terrain & voxel shape branches
#endif
#include "scripting/ScriptHost.h"
#ifndef COSMIC_2D_ONLY
#include "graphics/Mesh.h"           // mesh-collider geometry rebuild
#include "terrain/Terrain.h"
#include "voxel/VoxelVolume.h"       // V5 — per-chunk static mesh collision
#include "voxel/BlockPalette.h"
#include "voxel/VoxelMesher.h"
#include "voxel/VoxelRender.h"       // VoxelRenderData::CollisionDirty
#endif
#include "core/Log.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // Robust T/R/S extraction (avoids glm::decompose's historically conjugated
    // quaternion). Ignores mirror/negative scale (an edge case for colliders).
    // ------------------------------------------------------------------------
    static void DecomposeTRS(const glm::mat4& m, glm::vec3& t, glm::quat& r, glm::vec3& s)
    {
        t = glm::vec3(m[3]);
        s = glm::vec3(glm::length(glm::vec3(m[0])),
                      glm::length(glm::vec3(m[1])),
                      glm::length(glm::vec3(m[2])));
        glm::mat3 rot(
            glm::vec3(m[0]) / (s.x > 1e-8f ? s.x : 1.0f),
            glm::vec3(m[1]) / (s.y > 1e-8f ? s.y : 1.0f),
            glm::vec3(m[2]) / (s.z > 1e-8f ? s.z : 1.0f));
        r = glm::normalize(glm::quat_cast(rot));
    }

#ifndef COSMIC_2D_ONLY
    // Rebuild CPU geometry for a parametric primitive (the mesh itself keeps no CPU
    // copy after GPU upload, so a mesh collider on a primitive regenerates it here).
    static MeshData PrimitiveMeshData(const PrimitiveMeshComponent& p)
    {
        using Shape = PrimitiveMeshComponent::Shape;
        switch (p.ShapeType)
        {
            case Shape::Box:      return Mesh::BuildBox(p.Size);
            case Shape::Plane:    return Mesh::BuildPlane(p.Size.x, p.Size.z);
            case Shape::Cylinder: return Mesh::BuildCylinder(p.Radius, p.Height, uint32_t(p.Segments));
            case Shape::Cone:     return Mesh::BuildCone(p.Radius, p.Height, uint32_t(p.Segments));
            case Shape::Sphere:   return Mesh::BuildUVSphere(p.Radius, uint32_t(p.Rings), uint32_t(p.Segments));
            case Shape::Torus:    return Mesh::BuildTorus(p.Radius, p.TubeRadius, uint32_t(p.Segments), uint32_t(p.Rings));
        }
        return {};
    }
#endif // !COSMIC_2D_ONLY

    ScenePhysics::ScenePhysics(Scene& scene, PhysicsWorld& world)
        : m_Scene(scene), m_World(world) {}

    ScenePhysics::~ScenePhysics() { Teardown(); }

    // Collect the collider shapes on `e` into `out` (world scale baked in). Thin
    // member wrapper over the static enumeration (the play-session build path).
    bool ScenePhysics::BuildBodyDesc(entt::entity e, BodyDesc& out) const
    {
        return BuildColliderDesc(m_Scene, e, out);
    }

    // The scene's collision-view enumeration (edit-mode safe, static). Shared by the
    // play-session body build above and the N2 navmesh bake (SceneNav).
    bool ScenePhysics::BuildColliderDesc(Scene& scene, entt::entity e, BodyDesc& out)
    {
        auto& reg = scene.GetRegistry();

        glm::mat4 world = scene.GetWorldTransform(Entity(e, &scene));
        glm::vec3 t, s; glm::quat r;
        DecomposeTRS(world, t, r, s);
        out.Position = t;
        out.Rotation = r;

        // Motion + material from the RigidBody (implicit static if collider-only).
        const RigidBodyComponent* rb = reg.try_get<RigidBodyComponent>(e);
        bool wantsDynamic = false;
        if (rb)
        {
            out.Motion         = rb->Motion;
            out.Mass           = rb->Mass;
            out.Friction       = rb->Friction;
            out.Restitution    = rb->Restitution;
            out.LinearDamping  = rb->LinearDamping;
            out.AngularDamping = rb->AngularDamping;
            out.GravityFactor  = rb->GravityFactor;
            out.CCD            = rb->CCD;
            out.StartAsleep    = rb->StartAsleep;
            out.Category       = uint16_t(rb->CollisionCategory);
            out.CollidesWith   = uint16_t(rb->CollidesWith);
            wantsDynamic = rb->Motion == MotionType::Dynamic;
        }
        else
        {
            out.Motion = MotionType::Static;
        }

        bool anyTrigger = false;

        if (const auto* c = reg.try_get<BoxColliderComponent>(e); c && c->Enabled)   // T12
        {
            CollisionShapeDesc d;
            d.Shape = CollisionShapeDesc::Kind::Box;
            d.HalfExtents = c->HalfExtents;
            d.Offset = c->Offset;
            d.Scale = s;
            out.Shapes.push_back(std::move(d));
            anyTrigger |= c->IsTrigger;
        }
        if (const auto* c = reg.try_get<SphereColliderComponent>(e); c && c->Enabled)   // T12
        {
            CollisionShapeDesc d;
            d.Shape = CollisionShapeDesc::Kind::Sphere;
            d.Radius = c->Radius;
            d.Offset = c->Offset;
            d.Scale = s;
            out.Shapes.push_back(std::move(d));
            anyTrigger |= c->IsTrigger;
        }
        if (const auto* c = reg.try_get<CapsuleColliderComponent>(e); c && c->Enabled)   // T12
        {
            CollisionShapeDesc d;
            d.Shape = CollisionShapeDesc::Kind::Capsule;
            d.Radius = c->Radius;
            d.HalfHeight = c->HalfHeight;
            d.Offset = c->Offset;
            d.Scale = s;
            out.Shapes.push_back(std::move(d));
            anyTrigger |= c->IsTrigger;
        }
#ifndef COSMIC_2D_ONLY
        // MeshCollider and TerrainCollider are 3D-only (plan doc 28 §6.4); 2D keeps
        // the dimension-agnostic subset — RigidBody + Box/Sphere/Capsule + character.
        if (const auto* c = reg.try_get<MeshColliderComponent>(e); c && c->Enabled)   // T12
        {
            // Mesh geometry: rebuild from a sibling primitive; else fall back to the
            // renderer mesh's local AABB (imported-mesh triangle colliders wait on
            // CPU-side mesh retention — documented v1 limit).
            if (const auto* prim = reg.try_get<PrimitiveMeshComponent>(e))
            {
                MeshData md = PrimitiveMeshData(*prim);
                if (!md.Vertices.empty())
                {
                    if (!c->Convex && wantsDynamic)
                        CS_CORE_WARN("MeshCollider: a concave triangle mesh can't be dynamic (entity {0}); treat as static.", uint32_t(e));

                    CollisionShapeDesc d;
                    d.Shape = c->Convex ? CollisionShapeDesc::Kind::ConvexHull
                                        : CollisionShapeDesc::Kind::Mesh;
                    d.Scale = s;
                    d.Vertices.reserve(md.Vertices.size());
                    for (const auto& v : md.Vertices) d.Vertices.push_back(v.Position);
                    if (!c->Convex) d.Indices = md.Indices;
                    out.Shapes.push_back(std::move(d));
                }
            }
            else if (const auto* mr = reg.try_get<MeshRendererComponent>(e); mr && mr->MeshAsset)
            {
                const glm::vec3 mn = mr->MeshAsset->GetLocalMin();
                const glm::vec3 mx = mr->MeshAsset->GetLocalMax();
                CollisionShapeDesc d;
                d.Shape = CollisionShapeDesc::Kind::Box;
                d.HalfExtents = glm::max((mx - mn) * 0.5f, glm::vec3(0.01f));
                d.Offset = (mn + mx) * 0.5f;
                d.Scale = s;
                out.Shapes.push_back(std::move(d));
                CS_CORE_WARN("MeshCollider on an imported mesh (entity {0}) uses an AABB box in v1.", uint32_t(e));
            }
            anyTrigger |= c->IsTrigger;
        }
        if (reg.all_of<TerrainColliderComponent>(e))
        {
            if (const auto* tcomp = reg.try_get<TerrainComponent>(e); tcomp && tcomp->TerrainAsset)
            {
                const Terrain& terr = *tcomp->TerrainAsset;
                const TerrainSpecification& spec = terr.GetSpecification();
                const uint32_t n = spec.Resolution;         // vertices per side (32*2^k + 1, odd)
                // Jolt HeightFieldShape rounds its sample count UP to a multiple of the
                // block size (2), which would read past an odd n. n-1 is always even
                // (n = 32*2^k + 1), so we build the (n-1)^2 grid — dropping the far
                // +X/+Z edge row (a documented, harmless loss at the terrain rim).
                const uint32_t m = n - 1;
                const float spacing = spec.WorldSize / float(n - 1);   // vertex spacing

                CollisionShapeDesc d;
                d.Shape = CollisionShapeDesc::Kind::HeightField;
                d.HeightFieldSize = m;
                d.HeightSamples.resize(size_t(m) * m);
                for (uint32_t j = 0; j < m; ++j)
                    for (uint32_t i = 0; i < m; ++i)
                        d.HeightSamples[size_t(j) * m + i] =
                            spec.BaseHeight + terr.GetSample(i, j) * spec.HeightScale;

                const glm::vec2 corner = terr.GetWorldMinCorner();
                d.HeightFieldOffset = glm::vec3(corner.x, 0.0f, corner.y);
                d.HeightFieldCellSize = spacing;
                // Terrain is world geometry placed by its own spec — ignore the
                // entity transform for the shape (it is already world-space).
                out.Position = glm::vec3(0.0f);
                out.Rotation = glm::quat(1, 0, 0, 0);
                out.Motion = MotionType::Static;
                out.Shapes.push_back(std::move(d));
            }
            else
            {
                CS_CORE_WARN("TerrainCollider (entity {0}) has no built TerrainComponent — skipped.", uint32_t(e));
            }
        }
#endif // !COSMIC_2D_ONLY

        out.IsTrigger = anyTrigger;
        out.EntityId  = 0;
        if (const auto* id = reg.try_get<IDComponent>(e))
            out.EntityId = id->ID.Value();

        return !out.Shapes.empty();
    }

    void ScenePhysics::BuildBodies()
    {
        auto& reg = m_Scene.GetRegistry();

        // Character controllers (own capsule; no rigid body).
        for (auto e : reg.view<CharacterControllerComponent>())
        {
            if (!m_Scene.IsActiveInHierarchy(e))   // T13 — inactive: not baked
                continue;
            const auto& cc = reg.get<CharacterControllerComponent>(e);
            glm::mat4 world = m_Scene.GetWorldTransform(Entity(e, &m_Scene));
            glm::vec3 t, s; glm::quat r;
            DecomposeTRS(world, t, r, s);

            CharacterDesc cd;
            cd.Position    = t;
            cd.Height      = cc.Height;
            cd.Radius      = cc.Radius;
            cd.MaxSlopeDeg = cc.MaxSlopeDeg;
            cd.StepHeight  = cc.StepHeight;
            cd.Mass        = cc.Mass;
            if (const auto* id = reg.try_get<IDComponent>(e)) cd.EntityId = id->ID.Value();

            CharacterHandle h = m_World.CreateCharacter(cd);
            if (h.IsValid())
            {
                CharacterController ctrl(&m_World, h);
                ctrl.SetGravity(-9.81f);
                m_Characters.emplace(e, ctrl);
            }
        }

        // Rigid bodies + implicit-static colliders.
        for (auto e : reg.view<TransformComponent>())
        {
            if (reg.all_of<CharacterControllerComponent>(e))
                continue;   // handled above
            if (!m_Scene.IsActiveInHierarchy(e))   // T13 — inactive: not baked
                continue;
            // any_of is an OR, so the 3D half splits off without changing the 3D
            // result. Both 3D-only names go together: with the MeshCollider branch of
            // BuildColliderDesc fenced above, keeping MeshColliderComponent here would
            // pass the probe and then produce no shapes.
            const bool hasCollider =
                reg.any_of<BoxColliderComponent, SphereColliderComponent, CapsuleColliderComponent>(e)
#ifndef COSMIC_2D_ONLY
                || reg.any_of<MeshColliderComponent, TerrainColliderComponent>(e)
#endif
                ;
            const bool hasBody = reg.all_of<RigidBodyComponent>(e);
            if (!hasCollider && !hasBody)
                continue;

            BodyDesc desc;
            if (!BuildBodyDesc(e, desc))
            {
                if (hasBody)
                    CS_CORE_WARN("RigidBody on entity {0} has no collider — no body created.", uint32_t(e));
                continue;
            }
            PhysicsBody body = m_World.CreateBody(desc);
            if (body.IsValid())
                m_Bodies.emplace(e, body);
        }

#ifndef COSMIC_2D_ONLY
        // Static collision for any voxel volumes already resident at session start
        // (a loaded .cvox). Streamed/edited chunks come online via CollisionDirty.
        BuildVoxelBodies();
#endif
    }

    // --- Voxel collision (V5) — 3D-only (plan doc 28 §6.4) -------------------
#ifndef COSMIC_2D_ONLY

    PhysicsBody ScenePhysics::MakeVoxelChunkBody(entt::entity e, const glm::ivec3& chunk)
    {
        auto& reg = m_Scene.GetRegistry();
        auto* vc = reg.try_get<VoxelVolumeComponent>(e);
        if (!vc || !vc->Volume || !vc->Palette)
            return {};

        const MeshData md = VoxelMesher::BuildCollision(*vc->Volume, chunk, *vc->Palette);
        if (md.Indices.empty())
            return {};

        // Bake the volume's world placement into the vertices so the static body sits
        // at the origin (voxel volumes are static world geometry, like terrain).
        const glm::vec3 origin = vc->Volume->GetOrigin();
        const float     vs     = vc->Volume->GetVoxelSize();

        CollisionShapeDesc shape;
        shape.Shape = CollisionShapeDesc::Kind::Mesh;
        shape.Vertices.reserve(md.Vertices.size());
        for (const MeshVertex& v : md.Vertices)
            shape.Vertices.push_back(origin + v.Position * vs);
        shape.Indices = md.Indices;

        BodyDesc desc;
        desc.Motion   = MotionType::Static;
        desc.Position = glm::vec3(0.0f);
        desc.Rotation = glm::quat(1, 0, 0, 0);
        desc.Friction = 0.8f;
        if (const auto* id = reg.try_get<IDComponent>(e))
            desc.EntityId = id->ID.Value();
        desc.Shapes.push_back(std::move(shape));
        return m_World.CreateBody(desc);
    }

    void ScenePhysics::BuildVoxelBodies()
    {
        auto& reg = m_Scene.GetRegistry();
        for (auto e : reg.view<VoxelVolumeComponent>())
        {
            auto& vc = reg.get<VoxelVolumeComponent>(e);
            if (!vc.Volume || !vc.Palette)
                continue;

            ChunkBodyMap& bodies = m_VoxelBodies[e];
            std::vector<glm::ivec3> chunks;
            vc.Volume->ForEachChunk([&](const glm::ivec3& c) { chunks.push_back(c); });
            for (const glm::ivec3& c : chunks)
            {
                PhysicsBody b = MakeVoxelChunkBody(e, c);
                if (b.IsValid())
                    bodies[c] = b;
            }
            if (vc.Render)
                vc.Render->CollisionDirty.clear();   // just built these
        }
    }

    void ScenePhysics::RebuildDirtyVoxelChunks()
    {
        auto& reg = m_Scene.GetRegistry();
        for (auto e : reg.view<VoxelVolumeComponent>())
        {
            auto& vc = reg.get<VoxelVolumeComponent>(e);
            if (!vc.Volume || !vc.Palette || !vc.Render || vc.Render->CollisionDirty.empty())
                continue;

            ChunkBodyMap& bodies = m_VoxelBodies[e];
            std::vector<glm::ivec3> dirty(vc.Render->CollisionDirty.begin(), vc.Render->CollisionDirty.end());
            vc.Render->CollisionDirty.clear();

            constexpr size_t kBudget = 8;   // chunk bodies rebuilt per fixed step
            size_t done = 0;
            for (const glm::ivec3& c : dirty)
            {
                if (done++ >= kBudget) { vc.Render->CollisionDirty.insert(c); continue; }
                if (auto it = bodies.find(c); it != bodies.end())
                {
                    m_World.DestroyBody(it->second);
                    bodies.erase(it);
                }
                PhysicsBody b = MakeVoxelChunkBody(e, c);
                if (b.IsValid())
                    bodies[c] = b;
            }
        }
    }

#endif // !COSMIC_2D_ONLY

    void ScenePhysics::Step(float fixedDt)
    {
        auto& reg = m_Scene.GetRegistry();

#ifndef COSMIC_2D_ONLY
        // 0) Rebuild collision for any voxel chunks edited/streamed since last step,
        //    so characters + dynamics walk on the current geometry this step.
        RebuildDirtyVoxelChunks();
#endif

        // 1) Push kinematic targets from the (possibly script-moved) transforms.
        for (auto& [e, body] : m_Bodies)
        {
            const auto* rb = reg.try_get<RigidBodyComponent>(e);
            if (!rb || rb->Motion != MotionType::Kinematic) continue;
            glm::mat4 world = m_Scene.GetWorldTransform(Entity(e, &m_Scene));
            glm::vec3 t, s; glm::quat r;
            DecomposeTRS(world, t, r, s);
            m_World.MoveKinematic(body, t, r, fixedDt);
        }

        // 2) Advance the simulation exactly one fixed step.
        m_World.Step(fixedDt);

        // 3) Write dynamic body transforms back into the ECS.
        for (auto& [e, body] : m_Bodies)
        {
            const auto* rb = reg.try_get<RigidBodyComponent>(e);
            if (!rb || rb->Motion != MotionType::Dynamic) continue;
            // Seeded, not left indeterminate: GetBodyTransform leaves its out-params
            // untouched when it has nothing to report, which a pluggable backend (W3)
            // may legitimately do. Jolt always writes both for a live body, so with
            // the default backend these are dead stores and behaviour is unchanged.
            glm::vec3 p(0.0f); glm::quat q(1, 0, 0, 0);
            m_World.GetBodyTransform(body, p, q);
            WriteBackWorldPose(e, p, q);
        }

        // 4) Advance character controllers (after Step, per Jolt's recommendation).
        for (auto& [e, ctrl] : m_Characters)
        {
            ctrl.Tick(fixedDt);
            WriteBackWorldPose(e, ctrl.GetPosition(), glm::quat(1, 0, 0, 0));
        }
    }

    void ScenePhysics::WriteBackWorldPose(entt::entity e, const glm::vec3& worldPos, const glm::quat& worldRot)
    {
        auto& reg = m_Scene.GetRegistry();
        auto* tc = reg.try_get<TransformComponent>(e);
        if (!tc) return;

        // Parented entities: decompose the world pose into the parent's local frame
        // so hierarchy stays consistent. Scale is left untouched (physics never
        // changes it). Only position + quaternion are written.
        if (const auto* rel = reg.try_get<RelationshipComponent>(e); rel && rel->Parent != UUID(0))
        {
            Entity parent = m_Scene.FindByUUID(rel->Parent);
            if (parent)
            {
                if (!m_WarnedMovingParent)
                {
                    if (reg.any_of<RigidBodyComponent, CharacterControllerComponent>((entt::entity)parent))
                    {
                        const auto* prb = reg.try_get<RigidBodyComponent>((entt::entity)parent);
                        if ((prb && prb->Motion != MotionType::Static) || reg.all_of<CharacterControllerComponent>((entt::entity)parent))
                        {
                            CS_CORE_WARN("Physics: a dynamic body parented under a MOVING parent is unsupported in v1 (entity {0}).", uint32_t(e));
                            m_WarnedMovingParent = true;
                        }
                    }
                }
                glm::mat4 parentWorld = m_Scene.GetWorldTransform(parent);
                glm::mat4 bodyWorld = glm::translate(glm::mat4(1.0f), worldPos) * glm::mat4_cast(worldRot);
                glm::mat4 local = glm::inverse(parentWorld) * bodyWorld;
                glm::vec3 lt, ls; glm::quat lr;
                DecomposeTRS(local, lt, lr, ls);
                tc->Position = lt;
                tc->RotationQuat = lr;
                tc->UseQuatRotation = true;
                return;
            }
        }

        tc->Position = worldPos;
        tc->RotationQuat = worldRot;
        tc->UseQuatRotation = true;
    }

    void ScenePhysics::DispatchEvents(ScriptHost& scripts)
    {
        m_World.DrainContactEvents(m_EventScratch);
        for (const ContactEvent& ev : m_EventScratch)
        {
            Entity a = m_Scene.FindByUUID(UUID(ev.EntityA));
            Entity b = m_Scene.FindByUUID(UUID(ev.EntityB));
            if (!a || !b) continue;

            const entt::entity ha = (entt::entity)a;
            const entt::entity hb = (entt::entity)b;
            switch (ev.Kind)
            {
                case ContactKind::CollisionEnter:
                    scripts.DispatchCollisionEnter(ha, b);
                    scripts.DispatchCollisionEnter(hb, a);
                    break;
                case ContactKind::CollisionExit:
                    scripts.DispatchCollisionExit(ha, b);
                    scripts.DispatchCollisionExit(hb, a);
                    break;
                case ContactKind::TriggerEnter:
                    scripts.DispatchTriggerEnter(ha, b);   // a = sensor
                    scripts.DispatchTriggerEnter(hb, a);
                    break;
                case ContactKind::TriggerExit:
                    scripts.DispatchTriggerExit(ha, b);
                    scripts.DispatchTriggerExit(hb, a);
                    break;
            }
        }
    }

    void ScenePhysics::Teardown()
    {
        for (auto& [e, body] : m_Bodies)
            m_World.DestroyBody(body);
        m_Bodies.clear();

        for (auto& [e, ctrl] : m_Characters)
            if (ctrl.IsValid())
                m_World.DestroyCharacter(ctrl.GetHandle());
        m_Characters.clear();

#ifndef COSMIC_2D_ONLY
        for (auto& [e, chunks] : m_VoxelBodies)
            for (auto& [c, body] : chunks)
                m_World.DestroyBody(body);
        m_VoxelBodies.clear();
#endif
    }

    PhysicsBody ScenePhysics::GetBody(entt::entity e) const
    {
        auto it = m_Bodies.find(e);
        return it == m_Bodies.end() ? PhysicsBody{} : it->second;
    }

    CharacterController* ScenePhysics::GetCharacter(entt::entity e)
    {
        auto it = m_Characters.find(e);
        return it == m_Characters.end() ? nullptr : &it->second;
    }
}
