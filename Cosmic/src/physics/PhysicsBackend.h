#pragma once
// physics/PhysicsBackend.h
//
// ============================================================================
// Cosmic physics — the pluggable backend seam (Phase 29 / W3).
// ============================================================================
//
// PhysicsWorld is a DISPATCHER, not a base class: it holds one IPhysicsBackend
// and forwards. This is the RenderCommand -> RendererAPI idiom the renderer
// already uses (renderer/RendererAPI.h, renderer/RenderCommand.h), chosen so
// PhysicsWorld's public API does not change by one character — it is held BY
// VALUE at PlayerLayer.h and StarforgeApp.h, and making it abstract would force
// every one of those sites onto a unique_ptr + factory.
//
// IPhysicsBackend mirrors PhysicsWorld's public surface 1:1. Every parameter is
// PhysicsTypes.h / PhysicsBody.h vocabulary — glm + PODs. No Jolt, no GL, no
// entt: an implementation of this interface owes the engine nothing but these
// types, which is what makes "write your own physics for one app" real.
//
// WRITING YOUR OWN BACKEND
// ------------------------
//   // In your project's layer OnAttach — before any Play session starts:
//   Cosmic::PhysicsBackendRegistry::Register("my2d",
//       []{ return std::make_unique<My2DPhysics>(); });
//   Cosmic::PhysicsBackendRegistry::SetDefault("my2d");
//
// ...or leave the registry default alone and select per world with
// PhysicsSettings::Backend = "my2d". That is the whole integration: ScenePhysics
// keeps translating components -> BodyDesc -> CreateBody, so authored scenes,
// the inspector, serialization and scripts are unchanged. A backend that only
// cares about XY simply ignores Z. tests/test_physics_backend.cpp is a complete
// worked example.
//
// CONTRACTS a backend must honour
// -------------------------------
//   * Fixed step. Step() is called exactly once per accumulated fixed-dt, AFTER
//     scripts' OnFixedUpdate and BEFORE transform write-back and event dispatch
//     (ScenePhysics.h). Never integrate from a variable update.
//   * DrainContactEvents MOVES and CLEARS: events reported once, never twice.
//   * PhysicsSettings::ThreadCount == 0 means single-threaded / deterministic.
//     Honour it, or document that you ignore it.
//   * RayHit::EntityId round-trips BodyDesc::EntityId (the owning entity UUID).
//   * Out-parameters on a query that misses are left as the caller passed them;
//     the vector-filling queries are cleared by the dispatcher before the call.
// ============================================================================

#include "core/Core.h"
#include "physics/PhysicsBody.h"
#include "physics/PhysicsTypes.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Cosmic
{
    // ------------------------------------------------------------------------
    // IPhysicsBackend — one simulation implementation. Mirrors PhysicsWorld's
    // public surface 1:1; the defaulted arguments live on PhysicsWorld, so every
    // parameter here is explicit.
    // ------------------------------------------------------------------------
    class COSMIC_API IPhysicsBackend
    {
    public:
        virtual ~IPhysicsBackend() = default;

        /** @brief Registry name this backend was created under ("jolt", "null", ...). */
        virtual const char* Name() const = 0;

        // ---- lifecycle ------------------------------------------------------
        virtual void Init(const PhysicsSettings& settings) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsInitialized() const = 0;
        virtual void Step(float fixedDt) = 0;

        // ---- bodies ---------------------------------------------------------
        virtual PhysicsBody CreateBody(const BodyDesc& desc) = 0;
        virtual void        DestroyBody(PhysicsBody body) = 0;
        virtual void SetBodyTransform(PhysicsBody body, const glm::vec3& position, const glm::quat& rotation) = 0;
        virtual void GetBodyTransform(PhysicsBody body, glm::vec3& outPosition, glm::quat& outRotation) const = 0;
        virtual void MoveKinematic(PhysicsBody body, const glm::vec3& position, const glm::quat& rotation, float dt) = 0;

        virtual void      SetLinearVelocity(PhysicsBody body, const glm::vec3& v) = 0;
        virtual glm::vec3 GetLinearVelocity(PhysicsBody body) const = 0;
        virtual void      SetAngularVelocity(PhysicsBody body, const glm::vec3& w) = 0;
        virtual glm::vec3 GetAngularVelocity(PhysicsBody body) const = 0;

        virtual void AddForce(PhysicsBody body, const glm::vec3& force) = 0;
        virtual void AddImpulse(PhysicsBody body, const glm::vec3& impulse) = 0;
        virtual void AddTorque(PhysicsBody body, const glm::vec3& torque) = 0;

        virtual bool IsActive(PhysicsBody body) const = 0;
        virtual void Activate(PhysicsBody body) = 0;

        // ---- queries --------------------------------------------------------
        virtual std::optional<RayHit> RayCast(const glm::vec3& origin, const glm::vec3& direction,
                                              float maxDistance, uint16_t layerMask,
                                              uint64_t ignoreEntity) const = 0;
        virtual std::optional<RayHit> SphereCast(const glm::vec3& origin, const glm::vec3& direction,
                                                 float radius, float maxDistance, uint16_t layerMask,
                                                 uint64_t ignoreEntity) const = 0;
        virtual void OverlapSphere(const glm::vec3& center, float radius,
                                   std::vector<uint64_t>& outEntities, uint16_t layerMask,
                                   uint64_t ignoreEntity) const = 0;
        virtual void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation,
                                std::vector<uint64_t>& outEntities, uint16_t layerMask,
                                uint64_t ignoreEntity) const = 0;

        // ---- character controllers ------------------------------------------
        virtual CharacterHandle CreateCharacter(const CharacterDesc& desc) = 0;
        virtual void            DestroyCharacter(CharacterHandle ch) = 0;
        virtual void      UpdateCharacter(CharacterHandle ch, const glm::vec3& desiredVelocity, float dt) = 0;
        virtual void      GetCharacterTransform(CharacterHandle ch, glm::vec3& outPosition, glm::quat& outRotation) const = 0;
        virtual void      SetCharacterPosition(CharacterHandle ch, const glm::vec3& position) = 0;
        virtual bool      IsCharacterGrounded(CharacterHandle ch) const = 0;
        virtual glm::vec3 GetCharacterGroundNormal(CharacterHandle ch) const = 0;
        virtual glm::vec3 GetCharacterVelocity(CharacterHandle ch) const = 0;

        // ---- events ---------------------------------------------------------
        virtual void DrainContactEvents(std::vector<ContactEvent>& out) = 0;

        // ---- introspection --------------------------------------------------
        virtual PhysicsStats GetStatistics() const = 0;

        // ---- debug draw -----------------------------------------------------
        /** @brief Emit live wireframes to the engine's batched line verbs. A
         *  backend with nothing to draw leaves this empty. */
        virtual void DebugDraw() const = 0;
    };

    // ------------------------------------------------------------------------
    // PhysicsBackendRegistry — name -> factory, plus the process-wide default.
    //
    // The map is a FUNCTION-LOCAL static inside PhysicsBackend.cpp (a Meyers
    // singleton), so there is no static-initialization-order question across the
    // DLL boundary. Built-in registration is likewise an explicit call
    // (RegisterBuiltinPhysicsBackends, from PhysicsWorld::Init), not a
    // file-scope registrar object.
    //
    // Not thread-safe by design: registration happens at layer-attach time on
    // the main thread, long before any Play session steps.
    // ------------------------------------------------------------------------
    class COSMIC_API PhysicsBackendRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<IPhysicsBackend>()>;

        /** @brief Register (or replace) the factory for `name`. */
        static void Register(std::string name, Factory factory);
        static bool Has(const std::string& name);
        /** @brief Every registered name, sorted — the editor/CLI listing. */
        static std::vector<std::string> Names();

        /** @brief The app-level override: which backend an empty
         *  PhysicsSettings::Backend resolves to. Defaults to "jolt" when the
         *  engine was built with COSMIC_WITH_JOLT, otherwise "null". */
        static void SetDefault(const std::string& name);
        static const std::string& Default();

        /** @brief Instantiate `name`, or nullptr when it is not registered.
         *  The caller (PhysicsWorld::Init) owns the fallback policy. */
        static std::unique_ptr<IPhysicsBackend> Create(const std::string& name);
    };

    /** @brief Register the engine's own backends: "null" always, "jolt" when
     *  built with COSMIC_WITH_JOLT. Idempotent, and called by PhysicsWorld::Init
     *  before it resolves a name — an app never has to call it. It does NOT
     *  touch the default, so a SetDefault made at layer-attach time survives. */
    COSMIC_API void RegisterBuiltinPhysicsBackends();
}
