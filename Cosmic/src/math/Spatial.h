#pragma once

// Spatial.h
// Last Modified: 7/1/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Spatial Math (quaternions & reference frames)
 * ============================================================================
 *
 * Header-only helpers for 3D attitude math and frame conventions. This is the
 * ONE authoritative place for the engine's coordinate conventions — every
 * simulation and the 3D renderer agree on these definitions:
 *
 * WORLD FRAME (simulation): NED — aviation standard, right-handed.
 *   +X = North, +Y = East, +Z = Down.  Gravity is +Z.
 *
 * RENDER FRAME (Renderer3D / cameras): right-handed, Y-up.
 *   +X = East, +Y = Up, +Z = South.
 *   Mapping: render(x, y, z) = (ned.e, -ned.d, -ned.n).
 *
 * EULER CONVENTION: ZYX intrinsic (yaw ψ about Z, then pitch θ about Y, then
 * roll φ about X) — the aerospace standard. Angles in these helpers are in
 * DEGREES at the API boundary (matching TransformComponent::Rotation); radians
 * internally.
 *
 * All functions are pure and allocation-free. glm::quat conventions:
 * constructor is (w, x, y, z); q1 * q2 applies q2 first.
 * ============================================================================
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

namespace Cosmic::Math
{
    // Standard gravity magnitude (m/s^2). Points +Z (Down) in the NED world frame.
    inline constexpr float GravityMss = 9.80665f;

    // =========================================================================
    // Euler <-> Quaternion (ZYX / yaw-pitch-roll, degrees)
    // =========================================================================

    /**
     * @brief Build an attitude quaternion from ZYX Euler angles in DEGREES.
     * @param eulerDeg (roll φ about X, pitch θ about Y, yaw ψ about Z).
     * Rotation applied in aerospace order: yaw, then pitch, then roll.
     */
    inline glm::quat QuatFromEulerZYX(const glm::vec3& eulerDeg)
    {
        const glm::vec3 r = glm::radians(eulerDeg);
        const glm::quat qYaw   = glm::angleAxis(r.z, glm::vec3(0.f, 0.f, 1.f));
        const glm::quat qPitch = glm::angleAxis(r.y, glm::vec3(0.f, 1.f, 0.f));
        const glm::quat qRoll  = glm::angleAxis(r.x, glm::vec3(1.f, 0.f, 0.f));
        return qYaw * qPitch * qRoll; // roll applied first (rightmost)
    }

    /**
     * @brief Extract ZYX Euler angles in DEGREES from a unit quaternion.
     * @return (roll φ, pitch θ, yaw ψ). Pitch is clamped to ±90° (gimbal poles).
     */
    inline glm::vec3 EulerZYXFromQuat(const glm::quat& q)
    {
        const float w = q.w, x = q.x, y = q.y, z = q.z;

        const float sinp = glm::clamp(2.0f * (w * y - z * x), -1.0f, 1.0f);

        const float roll  = std::atan2(2.0f * (w * x + y * z), 1.0f - 2.0f * (x * x + y * y));
        const float pitch = std::asin(sinp);
        const float yaw   = std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));

        return glm::degrees(glm::vec3(roll, pitch, yaw));
    }

    // =========================================================================
    // Attitude integration
    // =========================================================================

    /**
     * @brief Integrate a body-frame angular rate over one timestep.
     *
     * Standard quaternion kinematics: q̇ = ½ · q ⊗ (0, ω_body). First-order
     * (Euler) step, renormalized — accurate for the small dt of a fixed-step
     * simulation; wrap in RK4 at the call site for higher-order integration.
     *
     * @param q          Current attitude (body → world), unit quaternion.
     * @param omegaBody  Angular rate in the BODY frame, radians/second.
     * @param dt         Timestep in seconds.
     * @return           New unit attitude quaternion.
     */
    inline glm::quat IntegrateBodyRate(const glm::quat& q, const glm::vec3& omegaBody, float dt)
    {
        const glm::quat omegaQ(0.0f, omegaBody.x, omegaBody.y, omegaBody.z);
        const glm::quat qDot = (q * omegaQ) * 0.5f;
        return glm::normalize(q + qDot * dt);
    }

    // =========================================================================
    // NED <-> render-frame conversion
    // =========================================================================

    /** @brief Convert a NED-frame vector (N, E, D) to the render frame (E, -D, -N). */
    inline glm::vec3 NedToRender(const glm::vec3& ned)
    {
        return glm::vec3(ned.y, -ned.z, -ned.x);
    }

    /** @brief Convert a render-frame vector back to NED. Inverse of NedToRender. */
    inline glm::vec3 RenderToNed(const glm::vec3& render)
    {
        return glm::vec3(-render.z, render.x, -render.y);
    }

    /** @brief The NED→render basis change as a matrix (columns = images of N, E, D). */
    inline const glm::mat3& NedToRenderMatrix()
    {
        // glm::mat3 takes COLUMNS: N→(0,0,-1), E→(1,0,0), D→(0,-1,0). det = +1.
        static const glm::mat3 m(glm::vec3(0.f, 0.f, -1.f),
                                 glm::vec3(1.f, 0.f,  0.f),
                                 glm::vec3(0.f, -1.f, 0.f));
        return m;
    }

    /**
     * @brief Convert an attitude quaternion expressed over NED axes into the
     * render frame (change of basis: q_render = C ⊗ q_ned ⊗ C⁻¹).
     * Use to drive a rendered model's orientation from a simulation attitude.
     */
    inline glm::quat NedQuatToRender(const glm::quat& qNed)
    {
        static const glm::quat C = glm::quat_cast(NedToRenderMatrix());
        return C * qNed * glm::inverse(C);
    }
}
