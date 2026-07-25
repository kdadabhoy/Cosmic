// physics/backends/JoltBackend.cpp — the Jolt-backed IPhysicsBackend.
//
// This is the ONE translation unit that includes <Jolt/...>; Jolt is linked
// PRIVATE into Cosmic.dll, so no JPH:: type escapes past here. It was
// PhysicsWorld.cpp until Phase 29 W3 made PhysicsWorld a dispatcher: the body
// below is that file's Jolt half, moved verbatim, with `struct PhysicsWorld::Impl`
// renamed to `class JoltBackend final : public IPhysicsBackend` and its member
// functions given `override`. The maths, the ordering and the Jolt call sequence
// are unchanged — test_physics_determinism asserts bit-identical transforms
// across runs, and that is the gate this move was made under.
//
// Built only when COSMIC_WITH_JOLT is ON (the CMake source filter drops the whole
// file otherwise), on BOTH engine configurations: physics is dimension-agnostic,
// so the 2D engine keeps rigid bodies, box/sphere/capsule colliders and the
// character controller. The single 3D coupling — Renderer3D, used by DebugDraw —
// is fenced below.

#include "physics/PhysicsBackend.h"
#include "physics/backends/BuiltinBackends.h"
#include "core/Log.h"
#ifndef COSMIC_2D_ONLY
#include "renderer/Renderer3D.h"   // debug-draw line batch (J8)
#endif

// --- Jolt ---------------------------------------------------------------------
#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyManager.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Geometry/Plane.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
    // ========================================================================
    // glm <-> Jolt conversions (single precision; RVec3 aliases Vec3 here).
    // ========================================================================
    static inline JPH::Vec3 ToJph(const glm::vec3& v)   { return JPH::Vec3(v.x, v.y, v.z); }
    static inline JPH::RVec3 ToJphR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
    static inline JPH::Quat ToJph(const glm::quat& q)   { return JPH::Quat(q.x, q.y, q.z, q.w); }
    static inline glm::vec3 ToGlm(JPH::Vec3Arg v)       { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    static inline glm::vec3 ToGlmR(JPH::RVec3Arg v)     { return glm::vec3(float(v.GetX()), float(v.GetY()), float(v.GetZ())); }
    static inline glm::quat ToGlm(JPH::QuatArg q)       { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

    // ========================================================================
    // Process-wide Jolt init (allocator/factory/type registration). Done once and
    // intentionally never torn down — RegisterTypes must outlive every PhysicsWorld
    // the process might create (editor play/stop cycles), and re-registering would
    // assert. Same "leak on purpose" pattern as the reflection registry.
    // ========================================================================
    static void CosmicJoltTrace(const char* fmt, ...)
    {
        char buf[1024];
        va_list args; va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        CS_CORE_TRACE("[Jolt] {0}", buf);
    }

#ifdef JPH_ENABLE_ASSERTS
    static bool CosmicJoltAssert(const char* expr, const char* msg, const char* file, JPH::uint line)
    {
        CS_CORE_ERROR("[Jolt] Assert {0}:{1}: ({2}) {3}", file, line, expr, msg ? msg : "");
        return false;   // don't trigger a breakpoint; the log is the record
    }
#endif

    static void EnsureJoltGlobalInit()
    {
        static const bool s_Once = []
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = &CosmicJoltTrace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &CosmicJoltAssert;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
            return true;
        }();
        (void)s_Once;
    }

    // ========================================================================
    // Layer configuration — coarse broadphase categories (the fine 16-bit mask is
    // applied on top in the contact validate + query filters).
    // ========================================================================
    namespace
    {
        namespace BP
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING{ 0 };
            static constexpr JPH::BroadPhaseLayer MOVING{ 1 };
            static constexpr JPH::uint NUM = 2;
        }

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return BP::NUM; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override
            {
                return (l == PhysicsObjectLayer::Static) ? BP::NON_MOVING : BP::MOVING;
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "cs_layer"; }
#endif
        };

        class ObjVsBpFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override
            {
                // Static + (static) triggers only test against the moving tree.
                if (obj == PhysicsObjectLayer::Static || obj == PhysicsObjectLayer::Trigger)
                    return bp == BP::MOVING;
                return true;
            }
        };

        class ObjPairFilterImpl final : public JPH::ObjectLayerPairFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                auto nonMoving = [](JPH::ObjectLayer l)
                { return l == PhysicsObjectLayer::Static || l == PhysicsObjectLayer::Trigger; };
                if (nonMoving(a) && nonMoving(b)) return false;   // two non-movers never pair
                return true;
            }
        };

        // 64-bit key for an unordered pair of bodies (contact-pair refcounting).
        static inline uint64_t PairKey(JPH::BodyID a, JPH::BodyID b)
        {
            uint32_t x = a.GetIndexAndSequenceNumber();
            uint32_t y = b.GetIndexAndSequenceNumber();
            if (x > y) std::swap(x, y);
            return (uint64_t(x) << 32) | uint64_t(y);
        }
    }

    // ========================================================================
    // JoltBackend — all Jolt state. This was `struct PhysicsWorld::Impl`; the
    // member functions already had exactly the right signatures, so becoming an
    // IPhysicsBackend was a rename plus `override`.
    // ========================================================================
    namespace
    {
        class JoltBackend final : public IPhysicsBackend
        {
        public:
            ~JoltBackend() override { CleanUp(); }

            const char* Name() const override { return "jolt"; }

            // ---- lifecycle --------------------------------------------------
            void Init(const PhysicsSettings& in) override;
            void Shutdown() override { CleanUp(); }
            bool IsInitialized() const override { return initialized; }
            void Step(float fixedDt) override;

            // ---- bodies -----------------------------------------------------
            PhysicsBody CreateBody(const BodyDesc& desc) override;
            void        DestroyBody(PhysicsBody body) override;
            void SetBodyTransform(PhysicsBody body, const glm::vec3& p, const glm::quat& r) override;
            void GetBodyTransform(PhysicsBody body, glm::vec3& outP, glm::quat& outR) const override;
            void MoveKinematic(PhysicsBody body, const glm::vec3& p, const glm::quat& r, float dt) override;

            void      SetLinearVelocity(PhysicsBody body, const glm::vec3& v) override;
            glm::vec3 GetLinearVelocity(PhysicsBody body) const override;
            void      SetAngularVelocity(PhysicsBody body, const glm::vec3& w) override;
            glm::vec3 GetAngularVelocity(PhysicsBody body) const override;

            void AddForce(PhysicsBody body, const glm::vec3& f) override;
            void AddImpulse(PhysicsBody body, const glm::vec3& imp) override;
            void AddTorque(PhysicsBody body, const glm::vec3& t) override;

            bool IsActive(PhysicsBody body) const override;
            void Activate(PhysicsBody body) override;

            // ---- queries ----------------------------------------------------
            std::optional<RayHit> RayCast(const glm::vec3& origin, const glm::vec3& dir, float maxDistance,
                                          uint16_t layerMask, uint64_t ignoreEntity) const override;
            std::optional<RayHit> SphereCast(const glm::vec3& origin, const glm::vec3& dir, float radius,
                                             float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const override;
            void OverlapSphere(const glm::vec3& center, float radius, std::vector<uint64_t>& out,
                               uint16_t layerMask, uint64_t ignoreEntity) const override;
            void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rot,
                            std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const override;

            // ---- characters (J6) --------------------------------------------
            CharacterHandle CreateCharacter(const CharacterDesc& desc) override;
            void            DestroyCharacter(CharacterHandle ch) override;
            void      UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt) override;
            void      GetCharacterTransform(CharacterHandle ch, glm::vec3& outP, glm::quat& outR) const override;
            void      SetCharacterPosition(CharacterHandle ch, const glm::vec3& p) override;
            bool      IsCharacterGrounded(CharacterHandle ch) const override;
            glm::vec3 GetCharacterGroundNormal(CharacterHandle ch) const override;
            glm::vec3 GetCharacterVelocity(CharacterHandle ch) const override;

            // ---- events / introspection / debug ------------------------------
            void         DrainContactEvents(std::vector<ContactEvent>& out) override;
            PhysicsStats GetStatistics() const override;
            void         DebugDraw() const override;

        public:
            PhysicsSettings settings;

            BPLayerInterfaceImpl bpLayers;
            ObjVsBpFilterImpl     objVsBp;
            ObjPairFilterImpl     objPair;

            // Heap-owned + recreated on each Init so the editor can play/stop repeatedly
            // (PhysicsSystem::Init isn't meant to run twice on one instance).
            std::unique_ptr<JPH::PhysicsSystem> system;
            JPH::TempAllocatorImpl* tempAlloc = nullptr;
            JPH::JobSystem*         jobSystem = nullptr;

            bool initialized     = false;
            bool broadPhaseDirty = false;

            struct BodyMeta { uint64_t entity; uint16_t category; uint16_t collidesWith; bool isSensor; };
            std::unordered_map<uint32_t, BodyMeta> meta;     // keyed by BodyID packed value

            // Contact events (worker threads push; main thread drains).
            std::mutex                        eventMutex;
            std::vector<ContactEvent>         eventQueue;
            std::unordered_map<uint64_t, int> pairRefs;      // active sub-shape contacts per body pair

            // Characters (J6). Index into this vector is the CharacterHandle id.
            struct CharEntry
            {
                JPH::Ref<JPH::CharacterVirtual> ch;
                float    stepHeight = 0.35f;
                uint64_t entity = 0;
                bool     alive = false;
            };
            std::vector<CharEntry>   characters;
            std::vector<uint32_t>    freeCharSlots;

            // Contact listener (defined below; needs JoltBackend). Heap-owned so it can
            // be constructed after the backend exists and reset before the system tears
            // down.
            std::unique_ptr<JPH::ContactListener> contactListener;

            const BodyMeta* FindMeta(JPH::BodyID id) const
            {
                auto it = meta.find(id.GetIndexAndSequenceNumber());
                return it == meta.end() ? nullptr : &it->second;
            }

            void QueueEnter(JPH::BodyID id1, JPH::BodyID id2)
            {
                const BodyMeta* m1 = FindMeta(id1);
                const BodyMeta* m2 = FindMeta(id2);
                if (!m1 || !m2) return;

                ContactEvent ev;
                const bool sensor = m1->isSensor || m2->isSensor;
                if (sensor)
                {
                    ev.Kind = ContactKind::TriggerEnter;
                    if (m1->isSensor) { ev.EntityA = m1->entity; ev.EntityB = m2->entity; }
                    else              { ev.EntityA = m2->entity; ev.EntityB = m1->entity; }
                }
                else
                {
                    ev.Kind = ContactKind::CollisionEnter;
                    ev.EntityA = m1->entity; ev.EntityB = m2->entity;
                }
                eventQueue.push_back(ev);
            }

            void QueueExit(JPH::BodyID id1, JPH::BodyID id2)
            {
                const BodyMeta* m1 = FindMeta(id1);
                const BodyMeta* m2 = FindMeta(id2);
                if (!m1 || !m2) return;

                ContactEvent ev;
                const bool sensor = m1->isSensor || m2->isSensor;
                if (sensor)
                {
                    ev.Kind = ContactKind::TriggerExit;
                    if (m1->isSensor) { ev.EntityA = m1->entity; ev.EntityB = m2->entity; }
                    else              { ev.EntityA = m2->entity; ev.EntityB = m1->entity; }
                }
                else
                {
                    ev.Kind = ContactKind::CollisionExit;
                    ev.EntityA = m1->entity; ev.EntityB = m2->entity;
                }
                eventQueue.push_back(ev);
            }

            void CleanUp();
        };
    }

    // ------------------------------------------------------------------------
    // Contact listener: fine mask veto + enter/exit refcounting.
    // ------------------------------------------------------------------------
    namespace
    {
        class ContactListenerImpl final : public JPH::ContactListener
        {
        public:
            explicit ContactListenerImpl(JoltBackend* w) : m(w) {}

            JPH::ValidateResult OnContactValidate(const JPH::Body& b1, const JPH::Body& b2,
                                                  JPH::RVec3Arg, const JPH::CollideShapeResult&) override
            {
                const auto* m1 = m->FindMeta(b1.GetID());
                const auto* m2 = m->FindMeta(b2.GetID());
                if (m1 && m2)
                {
                    const bool ok = (m1->category & m2->collidesWith) && (m2->category & m1->collidesWith);
                    if (!ok)
                        return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                }
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                                const JPH::ContactManifold&, JPH::ContactSettings&) override
            {
                std::lock_guard<std::mutex> lk(m->eventMutex);
                const uint64_t key = PairKey(b1.GetID(), b2.GetID());
                int& n = m->pairRefs[key];
                if (n++ == 0)
                    m->QueueEnter(b1.GetID(), b2.GetID());
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
            {
                std::lock_guard<std::mutex> lk(m->eventMutex);
                const uint64_t key = PairKey(pair.GetBody1ID(), pair.GetBody2ID());
                auto it = m->pairRefs.find(key);
                if (it == m->pairRefs.end()) return;
                if (--it->second <= 0)
                {
                    m->pairRefs.erase(it);
                    m->QueueExit(pair.GetBody1ID(), pair.GetBody2ID());
                }
            }

        private:
            JoltBackend* m;
        };

        // BodyFilter that keeps only bodies whose fine Category bits intersect the
        // query mask (0xFFFF => everything) and are not the ignored entity. Used by
        // RayCast/SphereCast/Overlap.
        class MaskBodyFilter final : public JPH::BodyFilter
        {
        public:
            MaskBodyFilter(const JoltBackend* w, uint16_t mask, uint64_t ignore)
                : m(w), m_Mask(mask), m_Ignore(ignore) {}
            bool ShouldCollide(const JPH::BodyID& id) const override
            {
                const auto* meta = m->FindMeta(id);
                if (!meta) return true;
                if (m_Ignore && meta->entity == m_Ignore) return false;
                return m_Mask == 0xFFFF || (meta->category & m_Mask) != 0;
            }
            bool ShouldCollideLocked(const JPH::Body&) const override { return true; }
        private:
            const JoltBackend* m;
            uint16_t m_Mask;
            uint64_t m_Ignore;
        };

        void JoltBackend::CleanUp()
        {
            if (!initialized) return;

            // Characters first (their Refs point at the system).
            characters.clear();
            freeCharSlots.clear();

            // Remove + destroy every remaining body so the system doesn't assert a leak.
            if (system)
            {
                JPH::BodyInterface& bi = system->GetBodyInterface();
                for (auto& [key, unused] : meta)
                {
                    (void)unused;
                    JPH::BodyID id(key);
                    if (bi.IsAdded(id))
                        bi.RemoveBody(id);
                    bi.DestroyBody(id);
                }
                system->SetContactListener(nullptr);
            }
            meta.clear();

            {
                std::lock_guard<std::mutex> lk(eventMutex);
                eventQueue.clear();
                pairRefs.clear();
            }

            contactListener.reset();
            system.reset();   // destroy the PhysicsSystem (now body-free)

            delete jobSystem; jobSystem = nullptr;
            delete tempAlloc; tempAlloc = nullptr;
            initialized = false;
        }

        // ====================================================================
        // Lifecycle
        // ====================================================================
        void JoltBackend::Init(const PhysicsSettings& in)
        {
            EnsureJoltGlobalInit();
            if (initialized)
                CleanUp();

            settings = in;

            // The temp allocator is a per-Update linear arena; it must cover the worst
            // case for the configured max pairs/constraints (not the live body count).
            // Scale it off the settings with a comfortable floor.
            const uint32_t tempSize = std::max<uint32_t>(
                48u * 1024 * 1024,
                in.MaxBodies * 2048u + in.MaxBodyPairs * 256u + in.MaxContactConstraints * 512u);
            tempAlloc = new JPH::TempAllocatorImpl(tempSize);

            int threads;
            if (in.ThreadCount < 0)
                threads = std::max(0, std::min<int>(int(std::thread::hardware_concurrency()) - 1, 4));
            else
                threads = in.ThreadCount;

            if (threads == 0)
                jobSystem = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
            else
                jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

            system = std::make_unique<JPH::PhysicsSystem>();
            system->Init(in.MaxBodies, /*numBodyMutexes*/ 0,
                         in.MaxBodyPairs, in.MaxContactConstraints,
                         bpLayers, objVsBp, objPair);
            system->SetGravity(ToJph(in.Gravity));

            contactListener = std::make_unique<ContactListenerImpl>(this);
            system->SetContactListener(contactListener.get());

            initialized     = true;
            broadPhaseDirty = false;
        }

        void JoltBackend::Step(float fixedDt)
        {
            if (!initialized || fixedDt <= 0.0f) return;

            if (broadPhaseDirty)
            {
                system->OptimizeBroadPhase();
                broadPhaseDirty = false;
            }

            // Jolt is tuned for <= 1/60 s per collision step; subdivide larger dt.
            const int collisionSteps = std::max(1, int(std::ceil(fixedDt * 60.0f - 1e-3f)));
            system->Update(fixedDt, collisionSteps, tempAlloc, jobSystem);
        }

        // ---- shape building -------------------------------------------------
        // Builds one primitive (scale baked in, NO offset/rotation). Returns null on
        // an invalid shape (logged once by the caller).
        JPH::Ref<JPH::Shape> BuildPrimitive(const CollisionShapeDesc& d)
        {
            using Kind = CollisionShapeDesc::Kind;
            const glm::vec3 s = d.Scale;
            const bool uniform = std::abs(s.x - s.y) < 1e-4f && std::abs(s.x - s.z) < 1e-4f;

            JPH::ShapeSettings::ShapeResult res;
            switch (d.Shape)
            {
                case Kind::Box:
                {
                    glm::vec3 he = glm::abs(d.HalfExtents * s);
                    he = glm::max(he, glm::vec3(0.005f));            // Jolt min convex radius safety
                    res = JPH::BoxShapeSettings(ToJph(he)).Create();
                    break;
                }
                case Kind::Sphere:
                {
                    if (!uniform)
                        CS_CORE_WARN("PhysicsWorld: non-uniform scale on a sphere collider — using X scale.");
                    res = JPH::SphereShapeSettings(std::max(0.005f, d.Radius * s.x)).Create();
                    break;
                }
                case Kind::Capsule:
                {
                    if (!uniform)
                        CS_CORE_WARN("PhysicsWorld: non-uniform scale on a capsule collider — using X/Y scale.");
                    const float r  = std::max(0.005f, d.Radius * s.x);
                    const float hh = std::max(0.005f, d.HalfHeight * s.y);
                    res = JPH::CapsuleShapeSettings(hh, r).Create();
                    break;
                }
                case Kind::ConvexHull:
                {
                    JPH::Array<JPH::Vec3> pts;
                    pts.reserve(d.Vertices.size());
                    for (const glm::vec3& v : d.Vertices) pts.push_back(ToJph(v * s));
                    if (pts.size() < 4) return nullptr;
                    res = JPH::ConvexHullShapeSettings(pts).Create();
                    break;
                }
                case Kind::Mesh:
                {
                    JPH::VertexList verts;
                    verts.reserve(d.Vertices.size());
                    for (const glm::vec3& v : d.Vertices)
                    {
                        glm::vec3 p = v * s;
                        verts.push_back(JPH::Float3(p.x, p.y, p.z));
                    }
                    JPH::IndexedTriangleList tris;
                    for (size_t i = 0; i + 2 < d.Indices.size(); i += 3)
                        tris.push_back(JPH::IndexedTriangle(d.Indices[i], d.Indices[i + 1], d.Indices[i + 2], 0));
                    if (tris.empty()) return nullptr;
                    res = JPH::MeshShapeSettings(verts, tris).Create();
                    break;
                }
                case Kind::HeightField:
                {
                    if (d.HeightFieldSize < 2) return nullptr;
                    const JPH::Vec3 offset = ToJph(d.HeightFieldOffset);
                    const JPH::Vec3 scale(d.HeightFieldCellSize, 1.0f, d.HeightFieldCellSize);
                    res = JPH::HeightFieldShapeSettings(d.HeightSamples.data(), offset, scale, d.HeightFieldSize).Create();
                    break;
                }
            }

            if (!res.IsValid())
            {
                CS_CORE_WARN("PhysicsWorld: collider shape build failed: {0}", res.GetError());
                return nullptr;
            }
            return res.Get();
        }

        // Builds the final body shape from all sub-shapes (offset/rotation applied;
        // a compound when there is more than one).
        JPH::Ref<JPH::Shape> BuildBodyShape(const std::vector<CollisionShapeDesc>& shapes)
        {
            if (shapes.empty()) return nullptr;

            if (shapes.size() == 1)
            {
                const CollisionShapeDesc& d = shapes[0];
                JPH::Ref<JPH::Shape> raw = BuildPrimitive(d);
                if (!raw) return nullptr;
                const bool noOffset = glm::length(d.Offset) < 1e-5f;
                const bool noRot = std::abs(d.OffsetRotation.w - 1.0f) < 1e-6f
                                && std::abs(d.OffsetRotation.x) < 1e-6f
                                && std::abs(d.OffsetRotation.y) < 1e-6f
                                && std::abs(d.OffsetRotation.z) < 1e-6f;
                if (noOffset && noRot) return raw;
                return JPH::RotatedTranslatedShapeSettings(ToJph(d.Offset), ToJph(d.OffsetRotation), raw).Create().Get();
            }

            JPH::StaticCompoundShapeSettings compound;
            int added = 0;
            for (const CollisionShapeDesc& d : shapes)
            {
                JPH::Ref<JPH::Shape> raw = BuildPrimitive(d);
                if (!raw) continue;
                compound.AddShape(ToJph(d.Offset), ToJph(d.OffsetRotation), raw);
                ++added;
            }
            if (added == 0) return nullptr;
            JPH::ShapeSettings::ShapeResult res = compound.Create();
            if (!res.IsValid())
            {
                CS_CORE_WARN("PhysicsWorld: compound shape build failed: {0}", res.GetError());
                return nullptr;
            }
            return res.Get();
        }

        // ====================================================================
        // Bodies
        // ====================================================================
        PhysicsBody JoltBackend::CreateBody(const BodyDesc& desc)
        {
            if (!initialized) return {};

            JPH::Ref<JPH::Shape> shape = BuildBodyShape(desc.Shapes);
            if (!shape)
            {
                CS_CORE_WARN("PhysicsWorld: body for entity {0} has no valid collider — skipped.", desc.EntityId);
                return {};
            }

            JPH::EMotionType emt =
                desc.Motion == MotionType::Static    ? JPH::EMotionType::Static :
                desc.Motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic :
                                                       JPH::EMotionType::Dynamic;

            JPH::ObjectLayer layer =
                desc.IsTrigger                    ? JPH::ObjectLayer(PhysicsObjectLayer::Trigger) :
                (desc.Motion == MotionType::Static ? JPH::ObjectLayer(PhysicsObjectLayer::Static)
                                                   : JPH::ObjectLayer(PhysicsObjectLayer::Dynamic));

            JPH::BodyCreationSettings bcs(shape, ToJphR(desc.Position), ToJph(desc.Rotation), emt, layer);
            bcs.mUserData       = desc.EntityId;
            bcs.mFriction       = desc.Friction;
            bcs.mRestitution    = desc.Restitution;
            bcs.mLinearDamping  = desc.LinearDamping;
            bcs.mAngularDamping = desc.AngularDamping;
            bcs.mGravityFactor  = desc.GravityFactor;
            bcs.mIsSensor       = desc.IsTrigger;
            bcs.mAllowSleeping  = true;
            bcs.mMotionQuality  = desc.CCD ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
            if (emt == JPH::EMotionType::Dynamic)
            {
                bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                bcs.mMassPropertiesOverride.mMass = std::max(0.001f, desc.Mass);
            }

            JPH::BodyInterface& bi = system->GetBodyInterface();
            JPH::Body* body = bi.CreateBody(bcs);
            if (!body)
            {
                CS_CORE_ERROR("PhysicsWorld: out of bodies (MaxBodies={0}) — entity {1} not created.",
                              settings.MaxBodies, desc.EntityId);
                return {};
            }

            const JPH::EActivation act =
                (emt == JPH::EMotionType::Static || desc.StartAsleep) ? JPH::EActivation::DontActivate
                                                                      : JPH::EActivation::Activate;
            bi.AddBody(body->GetID(), act);

            const uint32_t key = body->GetID().GetIndexAndSequenceNumber();
            meta[key] = { desc.EntityId, desc.Category, desc.CollidesWith, desc.IsTrigger };
            broadPhaseDirty = true;
            return PhysicsBody{ key };
        }

        void JoltBackend::DestroyBody(PhysicsBody body)
        {
            if (!initialized || !body.IsValid()) return;
            JPH::BodyID id(body.Id);
            JPH::BodyInterface& bi = system->GetBodyInterface();
            if (bi.IsAdded(id)) bi.RemoveBody(id);
            bi.DestroyBody(id);
            meta.erase(body.Id);
            broadPhaseDirty = true;
        }

        void JoltBackend::SetBodyTransform(PhysicsBody body, const glm::vec3& p, const glm::quat& r)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().SetPositionAndRotation(
                JPH::BodyID(body.Id), ToJphR(p), ToJph(r), JPH::EActivation::Activate);
        }

        void JoltBackend::GetBodyTransform(PhysicsBody body, glm::vec3& outP, glm::quat& outR) const
        {
            if (!initialized || !body.IsValid()) return;
            JPH::RVec3 p; JPH::Quat r;
            system->GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body.Id), p, r);
            outP = ToGlmR(p);
            outR = ToGlm(r);
        }

        void JoltBackend::MoveKinematic(PhysicsBody body, const glm::vec3& p, const glm::quat& r, float dt)
        {
            if (!initialized || !body.IsValid() || dt <= 0.0f) return;
            system->GetBodyInterface().MoveKinematic(JPH::BodyID(body.Id), ToJphR(p), ToJph(r), dt);
        }

        void JoltBackend::SetLinearVelocity(PhysicsBody body, const glm::vec3& v)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().SetLinearVelocity(JPH::BodyID(body.Id), ToJph(v));
        }
        glm::vec3 JoltBackend::GetLinearVelocity(PhysicsBody body) const
        {
            if (!initialized || !body.IsValid()) return glm::vec3(0.0f);
            return ToGlm(system->GetBodyInterface().GetLinearVelocity(JPH::BodyID(body.Id)));
        }
        void JoltBackend::SetAngularVelocity(PhysicsBody body, const glm::vec3& w)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().SetAngularVelocity(JPH::BodyID(body.Id), ToJph(w));
        }
        glm::vec3 JoltBackend::GetAngularVelocity(PhysicsBody body) const
        {
            if (!initialized || !body.IsValid()) return glm::vec3(0.0f);
            return ToGlm(system->GetBodyInterface().GetAngularVelocity(JPH::BodyID(body.Id)));
        }
        void JoltBackend::AddForce(PhysicsBody body, const glm::vec3& f)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().AddForce(JPH::BodyID(body.Id), ToJph(f));
        }
        void JoltBackend::AddImpulse(PhysicsBody body, const glm::vec3& imp)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().AddImpulse(JPH::BodyID(body.Id), ToJph(imp));
        }
        void JoltBackend::AddTorque(PhysicsBody body, const glm::vec3& t)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().AddTorque(JPH::BodyID(body.Id), ToJph(t));
        }
        bool JoltBackend::IsActive(PhysicsBody body) const
        {
            if (!initialized || !body.IsValid()) return false;
            return system->GetBodyInterface().IsActive(JPH::BodyID(body.Id));
        }
        void JoltBackend::Activate(PhysicsBody body)
        {
            if (!initialized || !body.IsValid()) return;
            system->GetBodyInterface().ActivateBody(JPH::BodyID(body.Id));
        }

        // ====================================================================
        // Queries
        // ====================================================================
        std::optional<RayHit> JoltBackend::RayCast(const glm::vec3& origin, const glm::vec3& dir,
                                                   float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
        {
            if (!initialized || maxDistance <= 0.0f) return std::nullopt;
            glm::vec3 d = dir;
            if (glm::length(d) < 1e-8f) return std::nullopt;
            d = glm::normalize(d);

            JPH::RRayCast ray{ ToJphR(origin), ToJph(d * maxDistance) };
            JPH::RayCastResult result;

            JPH::BroadPhaseLayerFilter bpFilter;
            JPH::ObjectLayerFilter     objFilter;
            MaskBodyFilter             bodyFilter(this, layerMask, ignoreEntity);

            if (!system->GetNarrowPhaseQuery().CastRay(ray, result, bpFilter, objFilter, bodyFilter))
                return std::nullopt;

            RayHit hit;
            hit.Hit = true;
            hit.Distance = result.mFraction * maxDistance;
            const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
            hit.Point = ToGlmR(point);
            if (const auto* mm = FindMeta(result.mBodyID)) hit.EntityId = mm->entity;

            JPH::BodyLockRead lock(system->GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
                hit.Normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
            return hit;
        }

        std::optional<RayHit> JoltBackend::SphereCast(const glm::vec3& origin, const glm::vec3& dir,
                                                      float radius, float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
        {
            if (!initialized || maxDistance <= 0.0f || radius <= 0.0f) return std::nullopt;
            glm::vec3 d = dir;
            if (glm::length(d) < 1e-8f) return std::nullopt;
            d = glm::normalize(d);

            JPH::SphereShape sphere(radius);
            sphere.SetEmbedded();
            JPH::RShapeCast shapeCast(&sphere, JPH::Vec3::sReplicate(1.0f),
                                      JPH::RMat44::sTranslation(ToJphR(origin)), ToJph(d * maxDistance));

            JPH::ShapeCastSettings settings2;
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

            JPH::BroadPhaseLayerFilter bpFilter;
            JPH::ObjectLayerFilter     objFilter;
            MaskBodyFilter             bodyFilter(this, layerMask, ignoreEntity);

            system->GetNarrowPhaseQuery().CastShape(shapeCast, settings2, ToJphR(origin),
                                                    collector, bpFilter, objFilter, bodyFilter);
            if (!collector.HadHit()) return std::nullopt;

            RayHit hit;
            hit.Hit = true;
            hit.Distance = collector.mHit.mFraction * maxDistance;
            const JPH::RVec3 point = shapeCast.mCenterOfMassStart.GetTranslation()
                                   + collector.mHit.mFraction * shapeCast.mDirection;
            hit.Point  = ToGlmR(point);
            hit.Normal = ToGlm(-collector.mHit.mPenetrationAxis.Normalized());
            if (const auto* mm = FindMeta(collector.mHit.mBodyID2)) hit.EntityId = mm->entity;
            return hit;
        }

        void JoltBackend::OverlapSphere(const glm::vec3& center, float radius,
                                        std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const
        {
            out.clear();
            if (!initialized || radius <= 0.0f) return;

            JPH::SphereShape sphere(radius);
            sphere.SetEmbedded();
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            JPH::CollideShapeSettings settings2;

            JPH::BroadPhaseLayerFilter bpFilter;
            JPH::ObjectLayerFilter     objFilter;
            MaskBodyFilter             bodyFilter(this, layerMask, ignoreEntity);
            JPH::ShapeFilter           shapeFilter;

            system->GetNarrowPhaseQuery().CollideShape(
                &sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJphR(center)),
                settings2, ToJphR(center), collector, bpFilter, objFilter, bodyFilter, shapeFilter);

            std::unordered_set<uint64_t> seen;
            for (const auto& h : collector.mHits)
                if (const auto* mm = FindMeta(h.mBodyID2))
                    if (seen.insert(mm->entity).second)
                        out.push_back(mm->entity);
        }

        void JoltBackend::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rot,
                                     std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const
        {
            out.clear();
            if (!initialized) return;

            JPH::BoxShape box(ToJph(glm::max(halfExtents, glm::vec3(0.005f))));
            box.SetEmbedded();
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            JPH::CollideShapeSettings settings2;

            JPH::BroadPhaseLayerFilter bpFilter;
            JPH::ObjectLayerFilter     objFilter;
            MaskBodyFilter             bodyFilter(this, layerMask, ignoreEntity);
            JPH::ShapeFilter           shapeFilter;

            JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJph(rot), ToJphR(center));
            system->GetNarrowPhaseQuery().CollideShape(
                &box, JPH::Vec3::sReplicate(1.0f), transform,
                settings2, ToJphR(center), collector, bpFilter, objFilter, bodyFilter, shapeFilter);

            std::unordered_set<uint64_t> seen;
            for (const auto& h : collector.mHits)
                if (const auto* mm = FindMeta(h.mBodyID2))
                    if (seen.insert(mm->entity).second)
                        out.push_back(mm->entity);
        }

        // ====================================================================
        // Characters (J6)
        // ====================================================================
        CharacterHandle JoltBackend::CreateCharacter(const CharacterDesc& desc)
        {
            if (!initialized) return {};

            const float radius = std::max(0.05f, desc.Radius);
            const float cyl = std::max(0.01f, desc.Height * 0.5f - radius);   // cylinder half-height

            JPH::Ref<JPH::CharacterVirtualSettings> chSettings = new JPH::CharacterVirtualSettings();
            chSettings->mMaxSlopeAngle = glm::radians(desc.MaxSlopeDeg);
            chSettings->mMass = desc.Mass;
            // Capsule centered on the body origin, shifted up so the handle position is
            // the character's feet.
            JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(cyl, radius);
            chSettings->mShape = JPH::RotatedTranslatedShapeSettings(
                JPH::Vec3(0, cyl + radius, 0), JPH::Quat::sIdentity(), capsule).Create().Get();
            chSettings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);   // accept contacts below feet

            JPH::Ref<JPH::CharacterVirtual> ch = new JPH::CharacterVirtual(
                chSettings, ToJphR(desc.Position), JPH::Quat::sIdentity(), desc.EntityId, system.get());

            uint32_t slot;
            if (!freeCharSlots.empty())
            {
                slot = freeCharSlots.back();
                freeCharSlots.pop_back();
                characters[slot] = { ch, desc.StepHeight, desc.EntityId, true };
            }
            else
            {
                slot = uint32_t(characters.size());
                characters.push_back({ ch, desc.StepHeight, desc.EntityId, true });
            }
            return CharacterHandle{ slot };
        }

        void JoltBackend::DestroyCharacter(CharacterHandle ch)
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return;
            auto& e = characters[ch.Id];
            if (!e.alive) return;
            e.ch = nullptr;
            e.alive = false;
            freeCharSlots.push_back(ch.Id);
        }

        void JoltBackend::UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt)
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size() || dt <= 0.0f) return;
            auto& e = characters[ch.Id];
            if (!e.alive || !e.ch) return;

            e.ch->SetLinearVelocity(ToJph(desiredVelocity));

            JPH::CharacterVirtual::ExtendedUpdateSettings us;
            us.mWalkStairsStepUp = JPH::Vec3(0, e.stepHeight, 0);

            e.ch->ExtendedUpdate(dt, system->GetGravity(), us,
                system->GetDefaultBroadPhaseLayerFilter(PhysicsObjectLayer::Character),
                system->GetDefaultLayerFilter(PhysicsObjectLayer::Character),
                JPH::BodyFilter{}, JPH::ShapeFilter{}, *tempAlloc);
        }

        void JoltBackend::GetCharacterTransform(CharacterHandle ch, glm::vec3& outP, glm::quat& outR) const
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return;
            const auto& e = characters[ch.Id];
            if (!e.alive || !e.ch) return;
            outP = ToGlmR(e.ch->GetPosition());
            outR = ToGlm(e.ch->GetRotation());
        }

        void JoltBackend::SetCharacterPosition(CharacterHandle ch, const glm::vec3& p)
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return;
            auto& e = characters[ch.Id];
            if (e.alive && e.ch) e.ch->SetPosition(ToJphR(p));
        }

        bool JoltBackend::IsCharacterGrounded(CharacterHandle ch) const
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return false;
            const auto& e = characters[ch.Id];
            if (!e.alive || !e.ch) return false;
            return e.ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
        }

        glm::vec3 JoltBackend::GetCharacterGroundNormal(CharacterHandle ch) const
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return glm::vec3(0, 1, 0);
            const auto& e = characters[ch.Id];
            if (!e.alive || !e.ch) return glm::vec3(0, 1, 0);
            return ToGlm(e.ch->GetGroundNormal());
        }

        glm::vec3 JoltBackend::GetCharacterVelocity(CharacterHandle ch) const
        {
            if (!initialized || !ch.IsValid() || ch.Id >= characters.size()) return glm::vec3(0.0f);
            const auto& e = characters[ch.Id];
            if (!e.alive || !e.ch) return glm::vec3(0.0f);
            return ToGlm(e.ch->GetLinearVelocity());
        }

        // ====================================================================
        // Events / statistics
        // ====================================================================
        void JoltBackend::DrainContactEvents(std::vector<ContactEvent>& out)
        {
            out.clear();
            if (!initialized) return;
            std::lock_guard<std::mutex> lk(eventMutex);
            out.swap(eventQueue);
        }

        PhysicsStats JoltBackend::GetStatistics() const
        {
            PhysicsStats s;
            if (!initialized) return s;
            s.BodyCount    = system->GetNumBodies();
            s.ActiveBodies = system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
            return s;
        }

        // ---- debug draw (J8) ------------------------------------------------
        // Live Jolt state to the Renderer3D line batch. Jolt's DrawBodies is compiled
        // only under JPH_DEBUG_RENDERER (Debug config), so this is a no-op in Release —
        // exactly the desired "engine builds clean, ships lean" behaviour. Colours come
        // from Jolt (SleepColor: sleeping bodies read grey/blue); no GL is touched here,
        // only the batched line verbs (the real GL lives in Renderer3D/platform).
        //
        // The 2D engine has no Renderer3D, so the body is fenced out there as well and
        // DebugDraw becomes a no-op; §6.4 pairs that with a Renderer2D collider overlay
        // in ViewportController.
        void JoltBackend::DebugDraw() const
        {
            if (!initialized) return;
#if defined(JPH_DEBUG_RENDERER) && !defined(COSMIC_2D_ONLY)
            // Forward Jolt's debug geometry to the engine's batched line verbs.
            class LineRenderer final : public JPH::DebugRendererSimple
            {
            public:
                void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override
                {
                    Renderer3D::DrawLine(ToGlmR(from), ToGlmR(to),
                        glm::vec4(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f));
                }
                void DrawTriangle(JPH::RVec3Arg v1, JPH::RVec3Arg v2, JPH::RVec3Arg v3,
                                  JPH::ColorArg color, ECastShadow) override
                {
                    const glm::vec4 c(color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, 1.0f);
                    Renderer3D::DrawLine(ToGlmR(v1), ToGlmR(v2), c);
                    Renderer3D::DrawLine(ToGlmR(v2), ToGlmR(v3), c);
                    Renderer3D::DrawLine(ToGlmR(v3), ToGlmR(v1), c);
                }
                void DrawText3D(JPH::RVec3Arg, const JPH::string_view&, JPH::ColorArg, float) override {}
            };

            LineRenderer r;
            JPH::BodyManager::DrawSettings ds;
            ds.mDrawShape          = true;
            ds.mDrawShapeWireframe = true;
            ds.mDrawShapeColor     = JPH::BodyManager::EShapeColor::SleepColor;   // sleeping bodies read grey
            system->DrawBodies(ds, &r);

            // Character capsules (they are not bodies in the system).
            for (const auto& e : characters)
                if (e.alive && e.ch)
                    e.ch->GetShape()->Draw(&r, e.ch->GetCenterOfMassTransform(), JPH::Vec3::sReplicate(1.0f),
                                           JPH::Color::sYellow, false, true);
#endif
        }
    }

    // ========================================================================
    // Registration (BuiltinBackends.h) — explicit, called from
    // RegisterBuiltinPhysicsBackends, never a file-scope registrar object.
    // ========================================================================
    void RegisterJoltPhysicsBackend()
    {
        PhysicsBackendRegistry::Register("jolt", [] { return std::unique_ptr<IPhysicsBackend>(new JoltBackend()); });
    }
}
