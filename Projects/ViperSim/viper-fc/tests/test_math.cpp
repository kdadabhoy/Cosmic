// test_math.cpp — quaternion integration + frame conventions (doc 04 §5).

#include "doctest.h"
#include "viperfc/Math.h"

using namespace viperfc;

TEST_CASE("quaternion body-rate integration matches the analytic rotation")
{
    // Spin about body Z at 90 deg/s for 1 s in small steps -> 90 deg yaw.
    Quat q{};
    const Vec3 omega{ 0, 0, Rad(90.0f) };
    const float dt = 1.0f / 480.0f;
    for (int i = 0; i < 480; ++i)
        q = IntegrateBodyRate(q, omega, dt);

    float r, p, y;
    ToEulerZYX(q, r, p, y);
    CHECK(Deg(y) == doctest::Approx(90.0f).epsilon(0.01));
    CHECK(Deg(r) == doctest::Approx(0.0f).epsilon(0.01));
    CHECK(Deg(p) == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("attitude error is a body-frame shortest-path rotation vector")
{
    const Quat q  = FromEulerZYX(0, 0, 0);
    const Quat qd = FromEulerZYX(Rad(20.0f), 0, 0);
    const Vec3 e = AttitudeErrorBody(q, qd);
    CHECK(Deg(e.x) == doctest::Approx(20.0f).epsilon(0.001));
    CHECK(e.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(e.z == doctest::Approx(0.0f).epsilon(1e-4));

    // Double-cover: -q represents the same attitude -> zero error.
    const Quat qneg{ -q.w, -q.x, -q.y, -q.z };
    const Vec3 e2 = AttitudeErrorBody(qneg, q);
    CHECK(Norm(e2) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("hover attitude: Euler pitch-90 equals the basis construction")
{
    // FromEulerZYX(0, 90deg, psi): nose (body x) up, belly (body z) at heading.
    const float psi = Rad(35.0f);
    const Quat q = FromEulerZYX(0.0f, kPi * 0.5f, psi);

    const Vec3 nose = Rotate(q, { 1, 0, 0 });
    CHECK(nose.z == doctest::Approx(-1.0f).epsilon(1e-4));   // NED -z = up

    const Vec3 belly = Rotate(q, { 0, 0, 1 });
    CHECK(belly.x == doctest::Approx(std::cos(psi)).epsilon(1e-4));
    CHECK(belly.y == doctest::Approx(std::sin(psi)).epsilon(1e-4));

    // FromBasis reproduces the same attitude.
    const Quat qb = FromBasis({ 0, 0, -1 },
                              Cross(Vec3{ std::cos(psi), std::sin(psi), 0 }, Vec3{ 0, 0, -1 }),
                              { std::cos(psi), std::sin(psi), 0 });
    const Vec3 err = AttitudeErrorBody(q, qb);
    CHECK(Norm(err) == doctest::Approx(0.0f).epsilon(1e-3));
}
