// physics/PhysicsWorld.cpp — Jolt-backed rigid-body / query / character service.
// See PhysicsWorld.h. This is the ONE translation unit (plus PhysicsDebug.cpp and
// CharacterController.h helpers) that includes <Jolt/...>; Jolt is linked PRIVATE
// into Cosmic.dll, so no JPH:: type escapes past here.

#include "physics/PhysicsWorld.h"
#include "core/Log.h"
#include "renderer/Renderer3D.h"   // debug-draw line batch (J8)

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
    // Impl — all Jolt state.
    // ========================================================================
    struct PhysicsWorld::Impl
    {
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

        // Contact listener (defined below; needs Impl). Heap-owned so it can be
        // constructed after Impl exists and reset before the system tears down.
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

    // ------------------------------------------------------------------------
    // Contact listener: fine mask veto + enter/exit refcounting.
    // ------------------------------------------------------------------------
    namespace
    {
        class ContactListenerImpl final : public JPH::ContactListener
        {
        public:
            explicit ContactListenerImpl(PhysicsWorld::Impl* w) : m(w) {}

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
            PhysicsWorld::Impl* m;
        };

        // BodyFilter that keeps only bodies whose fine Category bits intersect the
        // query mask (0xFFFF => everything) and are not the ignored entity. Used by
        // RayCast/SphereCast/Overlap.
        class MaskBodyFilter final : public JPH::BodyFilter
        {
        public:
            MaskBodyFilter(const PhysicsWorld::Impl* w, uint16_t mask, uint64_t ignore)
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
            const PhysicsWorld::Impl* m;
            uint16_t m_Mask;
            uint64_t m_Ignore;
        };
    }

    void PhysicsWorld::Impl::CleanUp()
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

    // ========================================================================
    // PhysicsWorld
    // ========================================================================
    PhysicsWorld::PhysicsWorld() : m_Impl(std::make_unique<Impl>()) {}
    PhysicsWorld::~PhysicsWorld() { Shutdown(); }

    bool PhysicsWorld::IsInitialized() const { return m_Impl && m_Impl->initialized; }

    void PhysicsWorld::Init(const PhysicsSettings& settings)
    {
        EnsureJoltGlobalInit();
        if (m_Impl->initialized)
            Shutdown();

        m_Impl->settings = settings;

        // The temp allocator is a per-Update linear arena; it must cover the worst
        // case for the configured max pairs/constraints (not the live body count).
        // Scale it off the settings with a comfortable floor.
        const uint32_t tempSize = std::max<uint32_t>(
            48u * 1024 * 1024,
            settings.MaxBodies * 2048u + settings.MaxBodyPairs * 256u + settings.MaxContactConstraints * 512u);
        m_Impl->tempAlloc = new JPH::TempAllocatorImpl(tempSize);

        int threads;
        if (settings.ThreadCount < 0)
            threads = std::max(0, std::min<int>(int(std::thread::hardware_concurrency()) - 1, 4));
        else
            threads = settings.ThreadCount;

        if (threads == 0)
            m_Impl->jobSystem = new JPH::JobSystemSingleThreaded(JPH::cMaxPhysicsJobs);
        else
            m_Impl->jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

        m_Impl->system = std::make_unique<JPH::PhysicsSystem>();
        m_Impl->system->Init(settings.MaxBodies, /*numBodyMutexes*/ 0,
                            settings.MaxBodyPairs, settings.MaxContactConstraints,
                            m_Impl->bpLayers, m_Impl->objVsBp, m_Impl->objPair);
        m_Impl->system->SetGravity(ToJph(settings.Gravity));

        m_Impl->contactListener = std::make_unique<ContactListenerImpl>(m_Impl.get());
        m_Impl->system->SetContactListener(m_Impl->contactListener.get());

        m_Impl->initialized     = true;
        m_Impl->broadPhaseDirty = false;
    }

    void PhysicsWorld::Shutdown()
    {
        if (m_Impl) m_Impl->CleanUp();
    }

    void PhysicsWorld::Step(float fixedDt)
    {
        if (!m_Impl->initialized || fixedDt <= 0.0f) return;

        if (m_Impl->broadPhaseDirty)
        {
            m_Impl->system->OptimizeBroadPhase();
            m_Impl->broadPhaseDirty = false;
        }

        // Jolt is tuned for <= 1/60 s per collision step; subdivide larger dt.
        const int collisionSteps = std::max(1, int(std::ceil(fixedDt * 60.0f - 1e-3f)));
        m_Impl->system->Update(fixedDt, collisionSteps, m_Impl->tempAlloc, m_Impl->jobSystem);
    }

    // ---- shape building -----------------------------------------------------
    namespace
    {
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
    }

    PhysicsBody PhysicsWorld::CreateBody(const BodyDesc& desc)
    {
        if (!m_Impl->initialized) return {};

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

        JPH::BodyInterface& bi = m_Impl->system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(bcs);
        if (!body)
        {
            CS_CORE_ERROR("PhysicsWorld: out of bodies (MaxBodies={0}) — entity {1} not created.",
                          m_Impl->settings.MaxBodies, desc.EntityId);
            return {};
        }

        const JPH::EActivation act =
            (emt == JPH::EMotionType::Static || desc.StartAsleep) ? JPH::EActivation::DontActivate
                                                                  : JPH::EActivation::Activate;
        bi.AddBody(body->GetID(), act);

        const uint32_t key = body->GetID().GetIndexAndSequenceNumber();
        m_Impl->meta[key] = { desc.EntityId, desc.Category, desc.CollidesWith, desc.IsTrigger };
        m_Impl->broadPhaseDirty = true;
        return PhysicsBody{ key };
    }

    void PhysicsWorld::DestroyBody(PhysicsBody body)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        JPH::BodyID id(body.Id);
        JPH::BodyInterface& bi = m_Impl->system->GetBodyInterface();
        if (bi.IsAdded(id)) bi.RemoveBody(id);
        bi.DestroyBody(id);
        m_Impl->meta.erase(body.Id);
        m_Impl->broadPhaseDirty = true;
    }

    void PhysicsWorld::SetBodyTransform(PhysicsBody body, const glm::vec3& p, const glm::quat& r)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().SetPositionAndRotation(
            JPH::BodyID(body.Id), ToJphR(p), ToJph(r), JPH::EActivation::Activate);
    }

    void PhysicsWorld::GetBodyTransform(PhysicsBody body, glm::vec3& outP, glm::quat& outR) const
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        JPH::RVec3 p; JPH::Quat r;
        m_Impl->system->GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body.Id), p, r);
        outP = ToGlmR(p);
        outR = ToGlm(r);
    }

    void PhysicsWorld::MoveKinematic(PhysicsBody body, const glm::vec3& p, const glm::quat& r, float dt)
    {
        if (!m_Impl->initialized || !body.IsValid() || dt <= 0.0f) return;
        m_Impl->system->GetBodyInterface().MoveKinematic(JPH::BodyID(body.Id), ToJphR(p), ToJph(r), dt);
    }

    void PhysicsWorld::SetLinearVelocity(PhysicsBody body, const glm::vec3& v)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().SetLinearVelocity(JPH::BodyID(body.Id), ToJph(v));
    }
    glm::vec3 PhysicsWorld::GetLinearVelocity(PhysicsBody body) const
    {
        if (!m_Impl->initialized || !body.IsValid()) return glm::vec3(0.0f);
        return ToGlm(m_Impl->system->GetBodyInterface().GetLinearVelocity(JPH::BodyID(body.Id)));
    }
    void PhysicsWorld::SetAngularVelocity(PhysicsBody body, const glm::vec3& w)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().SetAngularVelocity(JPH::BodyID(body.Id), ToJph(w));
    }
    glm::vec3 PhysicsWorld::GetAngularVelocity(PhysicsBody body) const
    {
        if (!m_Impl->initialized || !body.IsValid()) return glm::vec3(0.0f);
        return ToGlm(m_Impl->system->GetBodyInterface().GetAngularVelocity(JPH::BodyID(body.Id)));
    }
    void PhysicsWorld::AddForce(PhysicsBody body, const glm::vec3& f)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().AddForce(JPH::BodyID(body.Id), ToJph(f));
    }
    void PhysicsWorld::AddImpulse(PhysicsBody body, const glm::vec3& imp)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().AddImpulse(JPH::BodyID(body.Id), ToJph(imp));
    }
    void PhysicsWorld::AddTorque(PhysicsBody body, const glm::vec3& t)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().AddTorque(JPH::BodyID(body.Id), ToJph(t));
    }
    bool PhysicsWorld::IsActive(PhysicsBody body) const
    {
        if (!m_Impl->initialized || !body.IsValid()) return false;
        return m_Impl->system->GetBodyInterface().IsActive(JPH::BodyID(body.Id));
    }
    void PhysicsWorld::Activate(PhysicsBody body)
    {
        if (!m_Impl->initialized || !body.IsValid()) return;
        m_Impl->system->GetBodyInterface().ActivateBody(JPH::BodyID(body.Id));
    }

    // ---- queries ------------------------------------------------------------
    std::optional<RayHit> PhysicsWorld::RayCast(const glm::vec3& origin, const glm::vec3& dir,
                                                float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        if (!m_Impl->initialized || maxDistance <= 0.0f) return std::nullopt;
        glm::vec3 d = dir;
        if (glm::length(d) < 1e-8f) return std::nullopt;
        d = glm::normalize(d);

        JPH::RRayCast ray{ ToJphR(origin), ToJph(d * maxDistance) };
        JPH::RayCastResult result;

        JPH::BroadPhaseLayerFilter bpFilter;
        JPH::ObjectLayerFilter     objFilter;
        MaskBodyFilter             bodyFilter(m_Impl.get(), layerMask, ignoreEntity);

        if (!m_Impl->system->GetNarrowPhaseQuery().CastRay(ray, result, bpFilter, objFilter, bodyFilter))
            return std::nullopt;

        RayHit hit;
        hit.Hit = true;
        hit.Distance = result.mFraction * maxDistance;
        const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
        hit.Point = ToGlmR(point);
        if (const auto* mm = m_Impl->FindMeta(result.mBodyID)) hit.EntityId = mm->entity;

        JPH::BodyLockRead lock(m_Impl->system->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded())
            hit.Normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
        return hit;
    }

    std::optional<RayHit> PhysicsWorld::SphereCast(const glm::vec3& origin, const glm::vec3& dir,
                                                   float radius, float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        if (!m_Impl->initialized || maxDistance <= 0.0f || radius <= 0.0f) return std::nullopt;
        glm::vec3 d = dir;
        if (glm::length(d) < 1e-8f) return std::nullopt;
        d = glm::normalize(d);

        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();
        JPH::RShapeCast shapeCast(&sphere, JPH::Vec3::sReplicate(1.0f),
                                  JPH::RMat44::sTranslation(ToJphR(origin)), ToJph(d * maxDistance));

        JPH::ShapeCastSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

        JPH::BroadPhaseLayerFilter bpFilter;
        JPH::ObjectLayerFilter     objFilter;
        MaskBodyFilter             bodyFilter(m_Impl.get(), layerMask, ignoreEntity);

        m_Impl->system->GetNarrowPhaseQuery().CastShape(shapeCast, settings, ToJphR(origin),
                                                       collector, bpFilter, objFilter, bodyFilter);
        if (!collector.HadHit()) return std::nullopt;

        RayHit hit;
        hit.Hit = true;
        hit.Distance = collector.mHit.mFraction * maxDistance;
        const JPH::RVec3 point = shapeCast.mCenterOfMassStart.GetTranslation()
                               + collector.mHit.mFraction * shapeCast.mDirection;
        hit.Point  = ToGlmR(point);
        hit.Normal = ToGlm(-collector.mHit.mPenetrationAxis.Normalized());
        if (const auto* mm = m_Impl->FindMeta(collector.mHit.mBodyID2)) hit.EntityId = mm->entity;
        return hit;
    }

    void PhysicsWorld::OverlapSphere(const glm::vec3& center, float radius,
                                     std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        out.clear();
        if (!m_Impl->initialized || radius <= 0.0f) return;

        JPH::SphereShape sphere(radius);
        sphere.SetEmbedded();
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;

        JPH::BroadPhaseLayerFilter bpFilter;
        JPH::ObjectLayerFilter     objFilter;
        MaskBodyFilter             bodyFilter(m_Impl.get(), layerMask, ignoreEntity);
        JPH::ShapeFilter           shapeFilter;

        m_Impl->system->GetNarrowPhaseQuery().CollideShape(
            &sphere, JPH::Vec3::sReplicate(1.0f), JPH::RMat44::sTranslation(ToJphR(center)),
            settings, ToJphR(center), collector, bpFilter, objFilter, bodyFilter, shapeFilter);

        std::unordered_set<uint64_t> seen;
        for (const auto& h : collector.mHits)
            if (const auto* mm = m_Impl->FindMeta(h.mBodyID2))
                if (seen.insert(mm->entity).second)
                    out.push_back(mm->entity);
    }

    void PhysicsWorld::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rot,
                                  std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        out.clear();
        if (!m_Impl->initialized) return;

        JPH::BoxShape box(ToJph(glm::max(halfExtents, glm::vec3(0.005f))));
        box.SetEmbedded();
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;

        JPH::BroadPhaseLayerFilter bpFilter;
        JPH::ObjectLayerFilter     objFilter;
        MaskBodyFilter             bodyFilter(m_Impl.get(), layerMask, ignoreEntity);
        JPH::ShapeFilter           shapeFilter;

        JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJph(rot), ToJphR(center));
        m_Impl->system->GetNarrowPhaseQuery().CollideShape(
            &box, JPH::Vec3::sReplicate(1.0f), transform,
            settings, ToJphR(center), collector, bpFilter, objFilter, bodyFilter, shapeFilter);

        std::unordered_set<uint64_t> seen;
        for (const auto& h : collector.mHits)
            if (const auto* mm = m_Impl->FindMeta(h.mBodyID2))
                if (seen.insert(mm->entity).second)
                    out.push_back(mm->entity);
    }

    // ---- characters (J6) ----------------------------------------------------
    CharacterHandle PhysicsWorld::CreateCharacter(const CharacterDesc& desc)
    {
        if (!m_Impl->initialized) return {};

        const float radius = std::max(0.05f, desc.Radius);
        const float cyl = std::max(0.01f, desc.Height * 0.5f - radius);   // cylinder half-height

        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mMaxSlopeAngle = glm::radians(desc.MaxSlopeDeg);
        settings->mMass = desc.Mass;
        // Capsule centered on the body origin, shifted up so the handle position is
        // the character's feet.
        JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(cyl, radius);
        settings->mShape = JPH::RotatedTranslatedShapeSettings(
            JPH::Vec3(0, cyl + radius, 0), JPH::Quat::sIdentity(), capsule).Create().Get();
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);   // accept contacts below feet

        JPH::Ref<JPH::CharacterVirtual> ch = new JPH::CharacterVirtual(
            settings, ToJphR(desc.Position), JPH::Quat::sIdentity(), desc.EntityId, m_Impl->system.get());

        uint32_t slot;
        if (!m_Impl->freeCharSlots.empty())
        {
            slot = m_Impl->freeCharSlots.back();
            m_Impl->freeCharSlots.pop_back();
            m_Impl->characters[slot] = { ch, desc.StepHeight, desc.EntityId, true };
        }
        else
        {
            slot = uint32_t(m_Impl->characters.size());
            m_Impl->characters.push_back({ ch, desc.StepHeight, desc.EntityId, true });
        }
        return CharacterHandle{ slot };
    }

    void PhysicsWorld::DestroyCharacter(CharacterHandle ch)
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return;
        auto& e = m_Impl->characters[ch.Id];
        if (!e.alive) return;
        e.ch = nullptr;
        e.alive = false;
        m_Impl->freeCharSlots.push_back(ch.Id);
    }

    void PhysicsWorld::UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt)
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size() || dt <= 0.0f) return;
        auto& e = m_Impl->characters[ch.Id];
        if (!e.alive || !e.ch) return;

        e.ch->SetLinearVelocity(ToJph(desiredVelocity));

        JPH::CharacterVirtual::ExtendedUpdateSettings us;
        us.mWalkStairsStepUp = JPH::Vec3(0, e.stepHeight, 0);

        e.ch->ExtendedUpdate(dt, m_Impl->system->GetGravity(), us,
            m_Impl->system->GetDefaultBroadPhaseLayerFilter(PhysicsObjectLayer::Character),
            m_Impl->system->GetDefaultLayerFilter(PhysicsObjectLayer::Character),
            JPH::BodyFilter{}, JPH::ShapeFilter{}, *m_Impl->tempAlloc);
    }

    void PhysicsWorld::GetCharacterTransform(CharacterHandle ch, glm::vec3& outP, glm::quat& outR) const
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return;
        const auto& e = m_Impl->characters[ch.Id];
        if (!e.alive || !e.ch) return;
        outP = ToGlmR(e.ch->GetPosition());
        outR = ToGlm(e.ch->GetRotation());
    }

    void PhysicsWorld::SetCharacterPosition(CharacterHandle ch, const glm::vec3& p)
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return;
        auto& e = m_Impl->characters[ch.Id];
        if (e.alive && e.ch) e.ch->SetPosition(ToJphR(p));
    }

    bool PhysicsWorld::IsCharacterGrounded(CharacterHandle ch) const
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return false;
        const auto& e = m_Impl->characters[ch.Id];
        if (!e.alive || !e.ch) return false;
        return e.ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    }

    glm::vec3 PhysicsWorld::GetCharacterGroundNormal(CharacterHandle ch) const
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return glm::vec3(0, 1, 0);
        const auto& e = m_Impl->characters[ch.Id];
        if (!e.alive || !e.ch) return glm::vec3(0, 1, 0);
        return ToGlm(e.ch->GetGroundNormal());
    }

    glm::vec3 PhysicsWorld::GetCharacterVelocity(CharacterHandle ch) const
    {
        if (!m_Impl->initialized || !ch.IsValid() || ch.Id >= m_Impl->characters.size()) return glm::vec3(0.0f);
        const auto& e = m_Impl->characters[ch.Id];
        if (!e.alive || !e.ch) return glm::vec3(0.0f);
        return ToGlm(e.ch->GetLinearVelocity());
    }

    // ---- events -------------------------------------------------------------
    void PhysicsWorld::DrainContactEvents(std::vector<ContactEvent>& out)
    {
        out.clear();
        if (!m_Impl->initialized) return;
        std::lock_guard<std::mutex> lk(m_Impl->eventMutex);
        out.swap(m_Impl->eventQueue);
    }

    PhysicsStats PhysicsWorld::GetStatistics() const
    {
        PhysicsStats s;
        if (!m_Impl->initialized) return s;
        s.BodyCount    = m_Impl->system->GetNumBodies();
        s.ActiveBodies = m_Impl->system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
        return s;
    }

    // ---- debug draw (J8) ----------------------------------------------------
    // Live Jolt state to the Renderer3D line batch. Jolt's DrawBodies is compiled
    // only under JPH_DEBUG_RENDERER (Debug config), so this is a no-op in Release —
    // exactly the desired "engine builds clean, ships lean" behaviour. Colours come
    // from Jolt (SleepColor: sleeping bodies read grey/blue); no GL is touched here,
    // only the batched line verbs (the real GL lives in Renderer3D/platform).
    void PhysicsWorld::DebugDraw() const
    {
        if (!m_Impl->initialized) return;
#ifdef JPH_DEBUG_RENDERER
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
        m_Impl->system->DrawBodies(ds, &r);

        // Character capsules (they are not bodies in the system).
        for (const auto& e : m_Impl->characters)
            if (e.alive && e.ch)
                e.ch->GetShape()->Draw(&r, e.ch->GetCenterOfMassTransform(), JPH::Vec3::sReplicate(1.0f),
                                       JPH::Color::sYellow, false, true);
#endif
    }
}
