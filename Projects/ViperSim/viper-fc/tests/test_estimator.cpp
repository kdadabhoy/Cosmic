// test_estimator.cpp — complementary-filter convergence on canned data
// (doc 04 §5: "estimator convergence on canned data").

#include "doctest.h"
#include "viperfc/Estimator.h"

using namespace viperfc;

// Canned sensors for a vehicle at rest with the given attitude.
static SensorFrame RestFrame(const Quat& att, const Vec3& pos)
{
    SensorFrame f;
    f.gyro_rads = { 0, 0, 0 };
    f.accel_mss = RotateInv(att, { 0, 0, -kGravity });   // specific force = -g
    f.mag_uT    = RotateInv(att, { 22.0f, 0.0f, 42.0f }); // north-ish field, no declination
    f.gps.valid = true;
    f.gps.posNed = pos;
    f.gps.velNed = { 0, 0, 0 };
    f.baro_pa = 101325.0f + pos.z * kRhoAir * kGravity;   // deeper = more pressure
    f.vbat_V = 16.0f;
    return f;
}

TEST_CASE("estimator converges to a canned attitude from a wrong initial guess")
{
    FcParams p;
    Estimator est;
    est.Reset();   // level guess

    // On the ground (baro reference = home) with a tilted, yawed attitude.
    const Quat truth = FromEulerZYX(Rad(15.0f), Rad(-10.0f), Rad(40.0f));
    const Vec3 pos{ 5.0f, -3.0f, 0.0f };
    const SensorFrame f = RestFrame(truth, pos);

    const float dt = 1.0f / 240.0f;
    for (int i = 0; i < 240 * 15; ++i)
        est.Update(f, p, dt);

    const Vec3 attErr = AttitudeErrorBody(est.Get().att, truth);
    CHECK(Deg(Norm(attErr)) < 3.0f);

    CHECK(est.Get().posNed.x == doctest::Approx(pos.x).epsilon(0.05));
    CHECK(est.Get().posNed.y == doctest::Approx(pos.y).epsilon(0.05));
    CHECK(std::fabs(est.Get().altAgl) < 0.5f);
    CHECK(Norm(est.Get().velNed) < 0.3f);
}

TEST_CASE("gyro-only rotation tracks, then accel correction removes drift")
{
    FcParams p;
    Estimator est;
    est.Reset();

    // Rotate about body x at 30 deg/s for 2 s with consistent accel.
    const float dt = 1.0f / 240.0f;
    Quat truth{};
    for (int i = 0; i < 480; ++i)
    {
        truth = IntegrateBodyRate(truth, { Rad(30.0f), 0, 0 }, dt);
        SensorFrame f = RestFrame(truth, {});
        f.gyro_rads = { Rad(30.0f), 0, 0 };
        est.Update(f, p, dt);
    }

    const Vec3 err = AttitudeErrorBody(est.Get().att, truth);
    CHECK(Deg(Norm(err)) < 3.0f);
}

TEST_CASE("pitot below ~5 m/s is flagged unreliable (it gates transition logic)")
{
    FcParams p;
    Estimator est;
    est.Reset();

    SensorFrame f = RestFrame({}, {});
    f.airspeed_pa = 0.5f * kRhoAir * 3.0f * 3.0f;   // 3 m/s
    est.Update(f, p, 1.0f / 240.0f);
    CHECK_FALSE(est.Get().airspeedValid);

    f.airspeed_pa = 0.5f * kRhoAir * 15.0f * 15.0f; // 15 m/s
    est.Update(f, p, 1.0f / 240.0f);
    CHECK(est.Get().airspeed == doctest::Approx(15.0f).epsilon(0.01));
    CHECK(est.Get().airspeedValid);
}
