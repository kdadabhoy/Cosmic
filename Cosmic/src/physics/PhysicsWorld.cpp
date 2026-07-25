// physics/PhysicsWorld.cpp — the physics dispatcher. See PhysicsWorld.h.
//
// Every method here is a one-line forward to the IPhysicsBackend resolved at
// Init. This file contains no simulation code and includes no <Jolt/...>: that
// all lives in physics/backends/JoltBackend.cpp since Phase 29 W3. The shape is
// deliberately the one renderer/RenderCommand.h already uses in front of
// RendererAPI.
//
// A null m_Backend (before Init, after Shutdown) is a valid state and every
// forward tolerates it, matching the old "return early when !initialized"
// behaviour exactly. The two vector-filling queries and DrainContactEvents clear
// their out-parameter HERE, so the "always cleared" contract holds for every
// backend, not just the ones that remember to do it.

#include "physics/PhysicsWorld.h"
#include "physics/PhysicsBackend.h"
#include "core/Log.h"

#include <string>
#include <utility>

namespace Cosmic
{
    PhysicsWorld::PhysicsWorld() = default;
    PhysicsWorld::~PhysicsWorld() { Shutdown(); }

    // ---- lifecycle ----------------------------------------------------------
    void PhysicsWorld::Init(const PhysicsSettings& settings)
    {
        // An Init on a live world restarts it (the editor's play/stop cycle).
        Shutdown();

        RegisterBuiltinPhysicsBackends();

        const std::string& name = settings.Backend.empty() ? PhysicsBackendRegistry::Default()
                                                           : settings.Backend;

        std::unique_ptr<IPhysicsBackend> backend = PhysicsBackendRegistry::Create(name);
        if (!backend)
        {
            CS_CORE_ERROR("PhysicsWorld: unknown physics backend \"{0}\" — falling back to \"null\". "
                          "Register it with PhysicsBackendRegistry::Register before starting a session.", name);
            backend = PhysicsBackendRegistry::Create("null");
            if (!backend)   // cannot happen: RegisterBuiltinPhysicsBackends always registers "null"
            {
                CS_CORE_ERROR("PhysicsWorld: the \"null\" backend is missing — physics is disabled.");
                return;
            }
        }

        m_Backend = std::move(backend);
        m_Backend->Init(settings);
    }

    void PhysicsWorld::Shutdown()
    {
        if (!m_Backend) return;
        m_Backend->Shutdown();
        m_Backend.reset();
    }

    bool PhysicsWorld::IsInitialized() const { return m_Backend && m_Backend->IsInitialized(); }

    void PhysicsWorld::Step(float fixedDt) { if (m_Backend) m_Backend->Step(fixedDt); }

    // ---- bodies -------------------------------------------------------------
    PhysicsBody PhysicsWorld::CreateBody(const BodyDesc& desc)
    { return m_Backend ? m_Backend->CreateBody(desc) : PhysicsBody{}; }

    void PhysicsWorld::DestroyBody(PhysicsBody body)
    { if (m_Backend) m_Backend->DestroyBody(body); }

    void PhysicsWorld::SetBodyTransform(PhysicsBody body, const glm::vec3& p, const glm::quat& r)
    { if (m_Backend) m_Backend->SetBodyTransform(body, p, r); }

    void PhysicsWorld::GetBodyTransform(PhysicsBody body, glm::vec3& outP, glm::quat& outR) const
    { if (m_Backend) m_Backend->GetBodyTransform(body, outP, outR); }

    void PhysicsWorld::MoveKinematic(PhysicsBody body, const glm::vec3& p, const glm::quat& r, float dt)
    { if (m_Backend) m_Backend->MoveKinematic(body, p, r, dt); }

    void PhysicsWorld::SetLinearVelocity(PhysicsBody body, const glm::vec3& v)
    { if (m_Backend) m_Backend->SetLinearVelocity(body, v); }

    glm::vec3 PhysicsWorld::GetLinearVelocity(PhysicsBody body) const
    { return m_Backend ? m_Backend->GetLinearVelocity(body) : glm::vec3(0.0f); }

    void PhysicsWorld::SetAngularVelocity(PhysicsBody body, const glm::vec3& w)
    { if (m_Backend) m_Backend->SetAngularVelocity(body, w); }

    glm::vec3 PhysicsWorld::GetAngularVelocity(PhysicsBody body) const
    { return m_Backend ? m_Backend->GetAngularVelocity(body) : glm::vec3(0.0f); }

    void PhysicsWorld::AddForce(PhysicsBody body, const glm::vec3& f)
    { if (m_Backend) m_Backend->AddForce(body, f); }

    void PhysicsWorld::AddImpulse(PhysicsBody body, const glm::vec3& imp)
    { if (m_Backend) m_Backend->AddImpulse(body, imp); }

    void PhysicsWorld::AddTorque(PhysicsBody body, const glm::vec3& t)
    { if (m_Backend) m_Backend->AddTorque(body, t); }

    bool PhysicsWorld::IsActive(PhysicsBody body) const
    { return m_Backend ? m_Backend->IsActive(body) : false; }

    void PhysicsWorld::Activate(PhysicsBody body)
    { if (m_Backend) m_Backend->Activate(body); }

    // ---- queries ------------------------------------------------------------
    std::optional<RayHit> PhysicsWorld::RayCast(const glm::vec3& origin, const glm::vec3& dir,
                                                float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        return m_Backend ? m_Backend->RayCast(origin, dir, maxDistance, layerMask, ignoreEntity)
                         : std::nullopt;
    }

    std::optional<RayHit> PhysicsWorld::SphereCast(const glm::vec3& origin, const glm::vec3& dir, float radius,
                                                   float maxDistance, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        return m_Backend ? m_Backend->SphereCast(origin, dir, radius, maxDistance, layerMask, ignoreEntity)
                         : std::nullopt;
    }

    void PhysicsWorld::OverlapSphere(const glm::vec3& center, float radius, std::vector<uint64_t>& out,
                                     uint16_t layerMask, uint64_t ignoreEntity) const
    {
        out.clear();
        if (m_Backend) m_Backend->OverlapSphere(center, radius, out, layerMask, ignoreEntity);
    }

    void PhysicsWorld::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rot,
                                  std::vector<uint64_t>& out, uint16_t layerMask, uint64_t ignoreEntity) const
    {
        out.clear();
        if (m_Backend) m_Backend->OverlapBox(center, halfExtents, rot, out, layerMask, ignoreEntity);
    }

    // ---- characters (J6) ----------------------------------------------------
    CharacterHandle PhysicsWorld::CreateCharacter(const CharacterDesc& desc)
    { return m_Backend ? m_Backend->CreateCharacter(desc) : CharacterHandle{}; }

    void PhysicsWorld::DestroyCharacter(CharacterHandle ch)
    { if (m_Backend) m_Backend->DestroyCharacter(ch); }

    void PhysicsWorld::UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt)
    { if (m_Backend) m_Backend->UpdateCharacter(ch, desiredVelocity, dt); }

    void PhysicsWorld::GetCharacterTransform(CharacterHandle ch, glm::vec3& outP, glm::quat& outR) const
    { if (m_Backend) m_Backend->GetCharacterTransform(ch, outP, outR); }

    void PhysicsWorld::SetCharacterPosition(CharacterHandle ch, const glm::vec3& p)
    { if (m_Backend) m_Backend->SetCharacterPosition(ch, p); }

    bool PhysicsWorld::IsCharacterGrounded(CharacterHandle ch) const
    { return m_Backend ? m_Backend->IsCharacterGrounded(ch) : false; }

    glm::vec3 PhysicsWorld::GetCharacterGroundNormal(CharacterHandle ch) const
    { return m_Backend ? m_Backend->GetCharacterGroundNormal(ch) : glm::vec3(0, 1, 0); }

    glm::vec3 PhysicsWorld::GetCharacterVelocity(CharacterHandle ch) const
    { return m_Backend ? m_Backend->GetCharacterVelocity(ch) : glm::vec3(0.0f); }

    // ---- events -------------------------------------------------------------
    void PhysicsWorld::DrainContactEvents(std::vector<ContactEvent>& out)
    {
        out.clear();
        if (m_Backend) m_Backend->DrainContactEvents(out);
    }

    // ---- introspection ------------------------------------------------------
    PhysicsStats PhysicsWorld::GetStatistics() const
    { return m_Backend ? m_Backend->GetStatistics() : PhysicsStats{}; }

    // ---- debug draw (J8) ----------------------------------------------------
    void PhysicsWorld::DebugDraw() const
    { if (m_Backend) m_Backend->DebugDraw(); }
}
