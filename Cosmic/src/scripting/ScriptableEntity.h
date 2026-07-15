#pragma once
// scripting/ScriptableEntity.h
//
// ============================================================================
// Cosmic scripting — the native C++ script base class (Phase 13 / E11).
// ============================================================================
//
// A script is a real C++ class deriving from ScriptableEntity, compiled into the
// project's game DLL and registered with CS_SCRIPT (see ModuleMacros.h). One
// entity carries a NativeScriptComponent naming the class; at Play the ScriptHost
// resolves the name -> factory, constructs an instance, injects the owning entity
// + scene, pushes the reflected field values saved with the scene, and drives the
// lifecycle callbacks below. Edit mode has no instances (scripts do not run in the
// editor) — the field values live in NativeScriptComponent until Play (§2.4).
//
// GL-free and header-only: user scripts include this (via <Cosmic.h>) and get the
// full engine API through GetScene()/GetEntity(). The lifecycle is:
//
//     OnCreate()   after every entity of the scene exists (all scripts constructed)
//     OnStart()    first Play frame, after every OnCreate has run
//     OnUpdate(ts) variable timestep
//     OnFixedUpdate(dt)  sim-grade fixed timestep
//     OnEvent(e)   input/application events forwarded while Play is live
//     OnDestroy()  on Stop, before the runtime scene is torn down
// ============================================================================

#include "core/Core.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/Components.h"            // TagComponent (SystemBuilder::WithTag), H9
#include "scene/FlowMachine.h"           // Q2 — Flow() variable proxy (FlowMachine/FlowValue)
#include "scripting/ModuleRegistry.h"    // SystemDescriptor (SystemBuilder), H9
#include "physics/ScenePhysics.h"        // J5/J6 — Physics()/Character() script proxies
#include "voxel/VoxelVolume.h"           // V4 — Voxels() script proxy
#include "voxel/BlockPalette.h"
#include "scene/SceneNav.h"              // N4 — Nav() script proxy (SceneNavRuntime)
#include "nav/NavWorld.h"                // N4 — Nav() queries (FindPath/Raycast/...)

#include <entt/entt.hpp>

#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <vector>
#include <string>
#include <functional>

namespace Cosmic
{
    class Event;

    // ------------------------------------------------------------------------
    // ITelemetrySink (E20) — a generic seam for script-emitted telemetry.
    //
    // A host (the Starforge editor's telemetry panel, or any sim harness) may
    // implement this and hand it to the ScriptHost; scripts then push named
    // scalar channels through ScriptableEntity::Telemetry().Push("name", value)
    // and the host routes them into its store. The engine stays name-agnostic —
    // there are no editor/Starforge types here, and the default (no sink) makes
    // Push a cheap no-op, so shipped apps are unaffected.
    // ------------------------------------------------------------------------
    class ITelemetrySink
    {
    public:
        virtual ~ITelemetrySink() = default;

        /** @brief Record one named scalar for `source` during the current step. */
        virtual void Push(entt::entity source, const char* channel, float value) = 0;
    };

    class COSMIC_API ScriptableEntity
    {
    public:
        virtual ~ScriptableEntity() = default;

        // ---- injected accessors (valid from OnCreate onward) ----------------

        /** @brief The owning entity handle. Components via GetComponent<T>(). */
        Entity GetEntity() const { return Entity(m_Handle, m_Scene); }

        /** @brief The scene this script lives in (spawn/destroy/find-by-uuid). */
        Scene& GetScene()  const { return *m_Scene; }

        template<typename T>
        T& GetComponent() { return GetEntity().GetComponent<T>(); }

        template<typename T>
        const T& GetComponent() const { return GetEntity().GetComponent<T>(); }

        template<typename T>
        bool HasComponent() const { return m_Scene && m_Scene->GetRegistry().all_of<T>(m_Handle); }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args) { return GetEntity().AddComponent<T>(std::forward<Args>(args)...); }

    protected:
        // ---- telemetry passthrough (E20) ------------------------------------
        // A thin handle bound to this script's entity. Push a named scalar to the
        // host's telemetry store: Telemetry().Push("thrust_N", value). No-op when
        // no sink is installed (the default outside a recording host).
        struct TelemetryProxy
        {
            ITelemetrySink* Sink = nullptr;
            entt::entity    Source = entt::null;
            void Push(const char* channel, float value) const
            {
                if (Sink) Sink->Push(Source, channel, value);
            }
        };
        TelemetryProxy Telemetry() const { return { m_TelemetrySink, m_Handle }; }

        // ---- physics passthrough (J5) ---------------------------------------
        // A thin handle to this entity's rigid body + the scene's PhysicsWorld. Every
        // call is optional-safe: a no-op / empty result when no physics session is
        // active or this entity has no body (so scripts work in edit-only harnesses).
        struct PhysicsProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            PhysicsWorld* World() const
            {
                ScenePhysics* sp = S ? S->GetPhysics() : nullptr;
                return sp ? &sp->World() : nullptr;
            }
            PhysicsBody Body() const
            {
                ScenePhysics* sp = S ? S->GetPhysics() : nullptr;
                return sp ? sp->GetBody(Handle) : PhysicsBody{};
            }
            uint64_t SelfId() const
            {
                if (!S) return 0;
                if (auto* id = S->GetRegistry().try_get<IDComponent>(Handle)) return id->ID.Value();
                return 0;
            }

            void AddForce(const glm::vec3& f)   const { if (auto* w = World()) w->AddForce(Body(), f); }
            void AddImpulse(const glm::vec3& i)  const { if (auto* w = World()) w->AddImpulse(Body(), i); }
            void AddTorque(const glm::vec3& t)   const { if (auto* w = World()) w->AddTorque(Body(), t); }
            void SetVelocity(const glm::vec3& v) const { if (auto* w = World()) w->SetLinearVelocity(Body(), v); }
            glm::vec3 GetVelocity() const { auto* w = World(); return w ? w->GetLinearVelocity(Body()) : glm::vec3(0.0f); }

            std::optional<RayHit> RayCast(const glm::vec3& origin, const glm::vec3& dir,
                                          float maxDist, uint16_t mask = 0xFFFF) const
            {
                auto* w = World();
                return w ? w->RayCast(origin, dir, maxDist, mask, SelfId()) : std::nullopt;
            }
            std::vector<Entity> OverlapSphere(const glm::vec3& center, float radius, uint16_t mask = 0xFFFF) const
            {
                std::vector<Entity> out;
                auto* w = World();
                if (!w) return out;
                std::vector<uint64_t> ids;
                w->OverlapSphere(center, radius, ids, mask, SelfId());
                for (uint64_t id : ids)
                    if (Entity e = S->FindByUUID(UUID(id))) out.push_back(e);
                return out;
            }
            // Convenience: a short down-ray from this entity's world origin (ignoring
            // self). Pass a distance that clears the collider's bottom. For a proper
            // walker use Character().IsGrounded() instead.
            bool IsGrounded(float maxDist = 1.1f) const
            {
                auto* w = World();
                if (!w) return false;
                const glm::vec3 pos = glm::vec3(S->GetWorldTransform(Entity(Handle, S))[3]);
                return w->RayCast(pos, glm::vec3(0, -1, 0), maxDist, 0xFFFF, SelfId()).has_value();
            }
        };
        PhysicsProxy Physics() const { return { m_Scene, m_Handle }; }

        // ---- character controller passthrough (J6) --------------------------
        struct CharacterProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            CharacterController* Ctrl() const
            {
                ScenePhysics* sp = S ? S->GetPhysics() : nullptr;
                return sp ? sp->GetCharacter(Handle) : nullptr;
            }
            void Move(const glm::vec3& horizontalVelocity) const { if (auto* c = Ctrl()) c->Move(horizontalVelocity); }
            void Jump(float speed)  const { if (auto* c = Ctrl()) c->Jump(speed); }
            bool IsGrounded()       const { auto* c = Ctrl(); return c && c->IsGrounded(); }
            glm::vec3 GetGroundNormal() const { auto* c = Ctrl(); return c ? c->GetGroundNormal() : glm::vec3(0, 1, 0); }
            glm::vec3 GetVelocity()     const { auto* c = Ctrl(); return c ? c->GetVelocity() : glm::vec3(0.0f); }
        };
        CharacterProxy Character() const { return { m_Scene, m_Handle }; }

        // ---- signal passthrough (U2) ----------------------------------------
        // The scene EventBus (U1/U5) reached from a script: Signals().Emit("name")
        // broadcasts a signal (UI + flow + other scripts see it), Signals().Connect
        // subscribes a lambda to one named signal (unsubscribe with the returned
        // handle). Prefer overriding OnSignal for a catch-all. No-op with no scene.
        struct SignalProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            void Emit(const std::string& signal) const
            {
                if (S) S->Events().Emit(signal, Entity(Handle, S));
            }
            EventBus::Handle Connect(const std::string& signal, EventBus::SignalHandler fn) const
            {
                return S ? S->Events().Connect(signal, std::move(fn)) : 0;
            }
            void Disconnect(EventBus::Handle h) const { if (S) S->Events().Disconnect(h); }
        };
        SignalProxy Signals() const { return { m_Scene, m_Handle }; }

        // ---- flow-variable passthrough (Q2) ---------------------------------
        // Read/write the blackboard of the FlowMachine currently driving this
        // scene (the flow points its top scene here). No-ops / defaults when no
        // flow is running. Typed helpers wrap GetVar/SetVar for script ergonomics.
        struct FlowProxy
        {
            Scene* S = nullptr;
            FlowMachine* Machine() const { return S ? S->ActiveFlow() : nullptr; }

            FlowValue GetVar(const std::string& name) const
            {
                FlowMachine* m = Machine();
                return m ? m->GetVar(name) : FlowValue::MakeBool(false);
            }
            void SetVar(const std::string& name, const FlowValue& v) const
            {
                if (FlowMachine* m = Machine()) m->SetVar(name, v);
            }

            double GetNumber(const std::string& n) const { return GetVar(n).Number; }
            void   SetNumber(const std::string& n, double v) const { SetVar(n, FlowValue::MakeNumber(v)); }
            void   AddNumber(const std::string& n, double d) const { SetNumber(n, GetNumber(n) + d); }
            bool   GetBool(const std::string& n) const { return GetVar(n).Bool; }
            void   SetBool(const std::string& n, bool v) const { SetVar(n, FlowValue::MakeBool(v)); }
            std::string GetString(const std::string& n) const { return GetVar(n).String; }
            void   SetString(const std::string& n, const std::string& v) const { SetVar(n, FlowValue::MakeString(v)); }
        };
        FlowProxy Flow() const { return { m_Scene }; }

        // ---- voxel passthrough (V4) -----------------------------------------
        // A thin handle to a voxel world: this entity's VoxelVolumeComponent if it
        // has one, else the scene's first. Get/Set are in WORLD VOXEL coordinates;
        // RayCast/Break/Place take a WORLD ray (the "dig a tunnel / place a block"
        // script path). Edits mark chunks dirty — the render + collision rebuild
        // picks them up next frame. All calls are safe no-ops before the volume is
        // initialized (the first Scene::SyncVoxelVolumes) or with no voxel entity.
        struct VoxelProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            VoxelVolumeComponent* Comp() const
            {
                if (!S) return nullptr;
                if (auto* c = S->GetRegistry().try_get<VoxelVolumeComponent>(Handle)) return c;
                for (auto e : S->GetRegistry().view<VoxelVolumeComponent>())
                    return &S->GetRegistry().get<VoxelVolumeComponent>(e);
                return nullptr;
            }
            VoxelVolume*  Volume()  const { auto* c = Comp(); return c ? c->Volume.get()  : nullptr; }
            BlockPalette* Palette() const { auto* c = Comp(); return c ? c->Palette.get() : nullptr; }

            uint16_t Get(int x, int y, int z) const { auto* v = Volume(); return v ? v->Get(x, y, z) : 0; }
            void     Set(int x, int y, int z, uint16_t block) const { if (auto* v = Volume()) v->Set(x, y, z, block); }
            void     Set(const glm::ivec3& c, uint16_t block) const { Set(c.x, c.y, c.z, block); }

            glm::ivec3 WorldToVoxel(const glm::vec3& p) const { auto* v = Volume(); return v ? v->WorldToVoxel(p) : glm::ivec3(0); }

            VoxelRayHit RayCast(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const
            {
                VoxelVolume* v = Volume(); BlockPalette* p = Palette();
                if (!v || !p) return {};
                return v->RayCast(origin, dir, maxDist, *p);
            }
            /** @brief Break (set to Air) the first solid voxel a world ray hits. */
            bool Break(const glm::vec3& origin, const glm::vec3& dir, float maxDist) const
            {
                const VoxelRayHit h = RayCast(origin, dir, maxDist);
                if (!h.Hit) return false;
                Set(h.Voxel, 0);
                return true;
            }
            /** @brief Place `block` into the empty cell in front of the face a world
             *  ray hits (the standard voxel place-on-face). */
            bool Place(const glm::vec3& origin, const glm::vec3& dir, float maxDist, uint16_t block) const
            {
                const VoxelRayHit h = RayCast(origin, dir, maxDist);
                if (!h.Hit) return false;
                Set(h.Place, block);
                return true;
            }
        };
        VoxelProxy Voxels() const { return { m_Scene, m_Handle }; }

        // ---- animator passthrough (M6) --------------------------------------
        // Script control of THIS entity's AnimatorComponent: a timed CROSSFADE to
        // another clip (idle↔walk↔run), a hard Play, pause/resume, and a fade
        // query. No-ops when the entity has no Animator. The full controller graph
        // stays parked — this is the minimal playable-character tier.
        struct AnimatorProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            AnimatorComponent* Comp() const
            {
                return S ? S->GetRegistry().try_get<AnimatorComponent>(Handle) : nullptr;
            }

            /** @brief Timed blend to `clipPath` ("file#clip") over `seconds` (<=0 =
             *  hard switch). Re-targets an in-flight fade. */
            void CrossfadeTo(const std::string& clipPath, float seconds) const
            {
                if (auto* an = Comp()) an->CrossfadeTo(clipPath, seconds);
            }
            /** @brief Switch to `clipPath` immediately (no fade). */
            void Play(const std::string& clipPath) const { CrossfadeTo(clipPath, 0.0f); }
            void SetPlaying(bool playing) const { if (auto* an = Comp()) an->Playing = playing; }
            /** @brief True while a crossfade is pending or in flight. */
            bool IsCrossfading() const { auto* an = Comp(); return an && !an->NextClipPath.empty(); }
            std::string CurrentClip() const { auto* an = Comp(); return an ? an->ClipPath : std::string(); }
        };
        AnimatorProxy Animator() const { return { m_Scene, m_Handle }; }

        // ---- navigation passthrough (N4) ------------------------------------
        // Steer this entity's NavAgent over the scene's baked navmesh, and run
        // navmesh queries usable by any script (agent or not — e.g. a SystemScript
        // computing patrol waypoints). All calls are safe no-ops / empty results
        // when no play session or navmesh is active. Arrival fires as the
        // `nav.arrived` signal on the scene EventBus (catch it in OnSignal).
        struct NavProxy
        {
            Scene*       S = nullptr;
            entt::entity Handle = entt::null;

            SceneNavRuntime* Runtime() const { return S ? S->GetNav() : nullptr; }
            NavWorld*        World()   const { auto* r = Runtime(); return r ? r->Nav() : nullptr; }

            // Agent control (this entity's NavAgent).
            void SetTarget(const glm::vec3& worldPos) const { if (auto* r = Runtime()) r->SetTarget(Handle, worldPos); }
            void Stop()          const { if (auto* r = Runtime()) r->Stop(Handle); }
            bool HasAgent()      const { auto* r = Runtime(); return r && r->HasAgent(Handle); }
            bool HasArrived()    const { auto* r = Runtime(); return r && r->HasArrived(Handle); }
            glm::vec3 Position() const { auto* r = Runtime(); return r ? r->AgentPosition(Handle) : glm::vec3(0.0f); }
            glm::vec3 Velocity() const { auto* r = Runtime(); return r ? r->AgentVelocity(Handle) : glm::vec3(0.0f); }

            // Navmesh queries (const — no agent required).
            NavPath   FindPath(const glm::vec3& a, const glm::vec3& b) const { auto* w = World(); return w ? w->FindPath(a, b) : NavPath{}; }
            NavRayHit Raycast(const glm::vec3& a, const glm::vec3& b)   const { auto* w = World(); return w ? w->Raycast(a, b) : NavRayHit{}; }
            std::optional<glm::vec3> NearestPoint(const glm::vec3& p)   const { auto* w = World(); return w ? w->NearestPoint(p) : std::nullopt; }
            /** @brief Deterministic random navmesh point within `radius` of `center`;
             *  `rngState` is a caller-owned xorshift seed (reproducible). */
            std::optional<glm::vec3> RandomPointAround(const glm::vec3& center, float radius, uint32_t& rngState) const
            {
                auto* w = World(); return w ? w->RandomPointAround(center, radius, rngState) : std::nullopt;
            }
        };
        NavProxy Nav() const { return { m_Scene, m_Handle }; }


        // Override the ones you need — all default to no-ops.
        virtual void OnCreate() {}
        virtual void OnStart() {}
        virtual void OnUpdate(float ts) { (void)ts; }
        virtual void OnFixedUpdate(float fixedDt) { (void)fixedDt; }
        virtual void OnEvent(Event& e) { (void)e; }
        virtual void OnDestroy() {}

        // Signal callback (U2) — a catch-all fired for EVERY signal emitted on the
        // scene bus (buttons, flow, other scripts, this script). Filter by name.
        // Same-frame, main-thread. Default no-op.
        virtual void OnSignal(const std::string& signal, Entity source) { (void)signal; (void)source; }

        // Physics contact callbacks (J5) — fired on the main thread after the fixed
        // physics step. `other` is the counterpart entity. Default no-ops.
        virtual void OnCollisionEnter(Entity other) { (void)other; }
        virtual void OnCollisionExit(Entity other)  { (void)other; }
        virtual void OnTriggerEnter(Entity other)   { (void)other; }
        virtual void OnTriggerExit(Entity other)    { (void)other; }

    private:
        friend class ScriptHost;   // injects m_Scene/m_Handle/m_TelemetrySink + drives callbacks
        entt::entity    m_Handle{ entt::null };
        Scene*          m_Scene = nullptr;
        ITelemetrySink* m_TelemetrySink = nullptr;   // null unless a host installs one
    };

    // ========================================================================
    // SystemScript (H9) — logic bound to a *class* of entities.
    //
    // Where ScriptableEntity is one-instance-per-entity, a SystemScript is
    // one-instance-per-scene whose OnUpdateAll gets the WHOLE matching entity set
    // each tick (the "one physics script drives every airplane" pattern). Register
    // with CS_SYSTEM(T).Requires<Components...>().WithTag("optional") — membership is
    // that query, rebuilt per tick from the live scene (entities may spawn/die).
    // Reflected fields (CS_FIELD) serialize/inspect exactly like a script's. Held by
    // a SystemScriptComponent on any entity; instantiated + ticked by the ScriptHost
    // (systems tick BEFORE per-entity scripts — deterministic).
    // ========================================================================
    class COSMIC_API SystemScript
    {
    public:
        virtual ~SystemScript() = default;

        /** @brief The scene this system runs in (spawn/destroy/find-by-uuid). */
        Scene& GetScene() const { return *m_Scene; }

    protected:
        virtual void OnCreate() {}
        virtual void OnStart() {}
        // Called ONCE per tick with the matching entity set (not per entity). The span
        // is scratch owned by the ScriptHost — valid only for the call; copy handles
        // you keep. Iteration order is the entt view order (not user-sortable in v1).
        virtual void OnUpdateAll(std::span<Entity> entities, float ts) { (void)entities; (void)ts; }
        virtual void OnFixedUpdateAll(std::span<Entity> entities, float fixedDt) { (void)entities; (void)fixedDt; }
        virtual void OnDestroy() {}

    private:
        friend class ScriptHost;   // injects m_Scene + drives callbacks
        Scene* m_Scene = nullptr;
    };

    // ------------------------------------------------------------------------
    // SystemBuilder<T> — the CS_SYSTEM chain: declares membership + reflected
    // fields on a SystemDescriptor. Requires<>() sets the component filter,
    // WithTag() adds an exact TagComponent match, Order() sequences systems, and
    // Field() (via CS_FIELD) attaches reflected overrides. Defined here (not in
    // ModuleRegistry.h) so the membership query can see Scene/Entity/TagComponent;
    // ModuleRegistry forward-declares it and only instantiates AddSystem<T> where
    // the full definition is visible (a module .cpp via <Cosmic.h>).
    // ------------------------------------------------------------------------
    template<typename T>
    class SystemBuilder
    {
    public:
        explicit SystemBuilder(SystemDescriptor* desc) : m_Desc(desc) {}

        template<typename... Comps>
        SystemBuilder& Requires()
        {
            m_Base = [](Scene& s, std::vector<entt::entity>& out)
            {
                for (auto e : s.GetRegistry().view<Comps...>())
                    out.push_back(e);
            };
            Rebuild();
            return *this;
        }

        SystemBuilder& WithTag(const std::string& tag) { m_Tag = tag; Rebuild(); return *this; }
        SystemBuilder& Order(int order) { if (m_Desc) m_Desc->Order = order; return *this; }

        // CS_FIELD entry point — reflected field on the descriptor's field list;
        // returns a ClassBuilder so hint calls + subsequent CS_FIELDs chain.
        template<typename M>
        Reflect::ClassBuilder<T> Field(const char* name, M T::* member)
        {
            Reflect::ClassBuilder<T> cb(&m_Desc->Fields);
            cb.Field(name, member);
            return cb;
        }

    private:
        void Rebuild()
        {
            if (!m_Desc)
                return;
            auto base = m_Base;
            std::string tag = m_Tag;
            m_Desc->Collect = [base, tag](Scene& s, std::vector<entt::entity>& out)
            {
                if (!base)
                    return;
                std::vector<entt::entity> tmp;
                base(s, tmp);
                auto& reg = s.GetRegistry();
                for (entt::entity e : tmp)
                {
                    if (!tag.empty())
                    {
                        auto* tc = reg.try_get<TagComponent>(e);
                        if (!tc || tc->Tag != tag)
                            continue;
                    }
                    out.push_back(e);
                }
            };
        }

        SystemDescriptor* m_Desc = nullptr;
        std::function<void(Scene&, std::vector<entt::entity>&)> m_Base;
        std::string m_Tag;
    };
}
