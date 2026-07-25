// physics/backends/NullBackend.cpp — the no-op IPhysicsBackend.
//
// Creates no bodies, steps nothing, and returns empty queries. Always built and
// always registered, which buys three things:
//
//   1. COSMIC_WITH_JOLT=OFF is a valid engine configuration — Create() always has
//      something to return, and PhysicsWorld::Init's unknown-name fallback always
//      has somewhere to fall back to.
//   2. A scene with physics authored into it still runs (bodies are invalid,
//      transforms are simply never written back) instead of crashing.
//   3. It is the minimal reference implementation: every method here is what a
//      backend must at least provide. tests/test_physics_backend.cpp is the
//      fuller worked example.
//
// CreateBody returns the INVALID handle, which is the honest answer — there is no
// body — and is what keeps a null world coherent end to end: ScenePhysics records
// nothing, never asks for a transform it cannot get, and leaves authored poses
// exactly as the scene file had them. GetStatistics agrees, reporting zero bodies.

#include "physics/PhysicsBackend.h"
#include "physics/backends/BuiltinBackends.h"

#include <memory>

namespace Cosmic
{
    namespace
    {
        class NullBackend final : public IPhysicsBackend
        {
        public:
            const char* Name() const override { return "null"; }

            // ---- lifecycle --------------------------------------------------
            void Init(const PhysicsSettings&) override { m_Init = true; }
            void Shutdown() override { m_Init = false; }
            bool IsInitialized() const override { return m_Init; }
            void Step(float) override {}

            // ---- bodies -----------------------------------------------------
            PhysicsBody CreateBody(const BodyDesc&) override { return {}; }
            void DestroyBody(PhysicsBody) override {}
            void SetBodyTransform(PhysicsBody, const glm::vec3&, const glm::quat&) override {}
            void GetBodyTransform(PhysicsBody, glm::vec3&, glm::quat&) const override {}   // out-params untouched
            void MoveKinematic(PhysicsBody, const glm::vec3&, const glm::quat&, float) override {}

            void      SetLinearVelocity(PhysicsBody, const glm::vec3&) override {}
            glm::vec3 GetLinearVelocity(PhysicsBody) const override { return glm::vec3(0.0f); }
            void      SetAngularVelocity(PhysicsBody, const glm::vec3&) override {}
            glm::vec3 GetAngularVelocity(PhysicsBody) const override { return glm::vec3(0.0f); }

            void AddForce(PhysicsBody, const glm::vec3&) override {}
            void AddImpulse(PhysicsBody, const glm::vec3&) override {}
            void AddTorque(PhysicsBody, const glm::vec3&) override {}

            bool IsActive(PhysicsBody) const override { return false; }
            void Activate(PhysicsBody) override {}

            // ---- queries ----------------------------------------------------
            std::optional<RayHit> RayCast(const glm::vec3&, const glm::vec3&, float,
                                          uint16_t, uint64_t) const override { return std::nullopt; }
            std::optional<RayHit> SphereCast(const glm::vec3&, const glm::vec3&, float, float,
                                             uint16_t, uint64_t) const override { return std::nullopt; }
            void OverlapSphere(const glm::vec3&, float, std::vector<uint64_t>& out,
                               uint16_t, uint64_t) const override { out.clear(); }
            void OverlapBox(const glm::vec3&, const glm::vec3&, const glm::quat&,
                            std::vector<uint64_t>& out, uint16_t, uint64_t) const override { out.clear(); }

            // ---- characters -------------------------------------------------
            CharacterHandle CreateCharacter(const CharacterDesc&) override { return {}; }
            void DestroyCharacter(CharacterHandle) override {}
            void UpdateCharacter(CharacterHandle, const glm::vec3&, float) override {}
            void GetCharacterTransform(CharacterHandle, glm::vec3&, glm::quat&) const override {}
            void SetCharacterPosition(CharacterHandle, const glm::vec3&) override {}
            bool IsCharacterGrounded(CharacterHandle) const override { return false; }
            glm::vec3 GetCharacterGroundNormal(CharacterHandle) const override { return glm::vec3(0, 1, 0); }
            glm::vec3 GetCharacterVelocity(CharacterHandle) const override { return glm::vec3(0.0f); }

            // ---- events / introspection / debug ------------------------------
            void DrainContactEvents(std::vector<ContactEvent>& out) override { out.clear(); }
            PhysicsStats GetStatistics() const override { return {}; }
            void DebugDraw() const override {}

        private:
            bool m_Init = false;
        };
    }

    void RegisterNullPhysicsBackend()
    {
        PhysicsBackendRegistry::Register("null", [] { return std::unique_ptr<IPhysicsBackend>(new NullBackend()); });
    }
}
