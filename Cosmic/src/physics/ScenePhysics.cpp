// physics/ScenePhysics.cpp — Scene <-> PhysicsWorld runtime binding (J4/J5). See header.

#include "physics/ScenePhysics.h"
#include "physics/PhysicsWorld.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scripting/ScriptHost.h"
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
    // play-session body build above and any edit-mode consumer (History: the N2
    // navmesh bake, SceneNav, gathered triangles through it on the 3D engine).
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

    }

    void ScenePhysics::Step(float fixedDt)
    {
        auto& reg = m_Scene.GetRegistry();

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
