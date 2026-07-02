// math/Spatial.h — quaternion & frame convention tests. Pure math, no engine state.

#include <doctest.h>

#include "math/Spatial.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Cosmic::Math;

static constexpr float kEps = 1e-4f;

static bool VecNear(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(eps)));
}

TEST_CASE("Euler ZYX <-> quaternion round-trips away from the gimbal poles")
{
    const glm::vec3 cases[] = {
        {  0.0f,   0.0f,   0.0f },
        { 10.0f,  20.0f,  30.0f },
        { -45.0f, 30.0f, -120.0f },
        { 179.0f, -60.0f,  5.0f },
    };

    for (const glm::vec3& eulerDeg : cases)
    {
        const glm::quat q    = QuatFromEulerZYX(eulerDeg);
        const glm::vec3 back = EulerZYXFromQuat(q);

        // Compare via quaternions to sidestep angle-wrapping equivalences.
        const glm::quat q2 = QuatFromEulerZYX(back);
        const float dot = glm::abs(glm::dot(q, q2)); // 1 == same rotation (sign-agnostic)
        CHECK(dot > 1.0f - 1e-5f);
    }
}

TEST_CASE("Pure yaw rotates North toward East (NED handedness)")
{
    // +90 deg yaw about Z (Down): the body X axis (North) must point East.
    const glm::quat q = QuatFromEulerZYX({ 0.0f, 0.0f, 90.0f });
    const glm::vec3 bodyX = q * glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK(VecNear(bodyX, glm::vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("IntegrateBodyRate accumulates a constant rate to the expected angle")
{
    // Spin about body Z at pi/2 rad/s for 1 s (small steps) => 90 deg yaw.
    glm::quat q(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::vec3 omega(0.0f, 0.0f, glm::half_pi<float>());

    const int steps = 1000;
    const float dt = 1.0f / (float)steps;
    for (int i = 0; i < steps; ++i)
        q = IntegrateBodyRate(q, omega, dt);

    const glm::vec3 euler = EulerZYXFromQuat(q);
    CHECK(euler.z == doctest::Approx(90.0f).epsilon(0.01));
    CHECK(glm::abs(euler.x) < 0.1f);
    CHECK(glm::abs(euler.y) < 0.1f);

    // Integration must preserve unit length.
    CHECK(glm::length(q) == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("NED <-> render frame mapping and round-trip")
{
    // Definition: render(x, y, z) = (E, -D, -N).
    CHECK(VecNear(NedToRender({ 1.0f, 0.0f, 0.0f }), glm::vec3(0.0f, 0.0f, -1.0f))); // North -> -Z
    CHECK(VecNear(NedToRender({ 0.0f, 1.0f, 0.0f }), glm::vec3(1.0f, 0.0f,  0.0f))); // East  -> +X
    CHECK(VecNear(NedToRender({ 0.0f, 0.0f, 1.0f }), glm::vec3(0.0f, -1.0f, 0.0f))); // Down  -> -Y

    const glm::vec3 v(3.2f, -7.5f, 42.0f);
    CHECK(VecNear(RenderToNed(NedToRender(v)), v));
    CHECK(VecNear(NedToRender(RenderToNed(v)), v));
}

TEST_CASE("NedQuatToRender maps a NED yaw onto render axes consistently")
{
    // 90 deg yaw in NED turns North->East. In render terms: -Z (North) must map to +X (East).
    const glm::quat qNed    = QuatFromEulerZYX({ 0.0f, 0.0f, 90.0f });
    const glm::quat qRender = NedQuatToRender(qNed);

    const glm::vec3 renderNorth(0.0f, 0.0f, -1.0f);
    const glm::vec3 rotated = qRender * renderNorth;
    CHECK(VecNear(rotated, glm::vec3(1.0f, 0.0f, 0.0f)));
}
