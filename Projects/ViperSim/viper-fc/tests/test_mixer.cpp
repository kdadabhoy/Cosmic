// test_mixer.cpp — control allocation + saturation policy (doc 04 §5).

#include "doctest.h"
#include "viperfc/Mixer.h"

using namespace viperfc;

static FcParams P() { return FcParams{}; }

TEST_CASE("mixer: pure collective drives both motors equally, servos centered")
{
    ActuatorFrame out;
    MixTailsitter({ Vec3{ 0, 0, 0 }, 0.5f }, P(), out);
    CHECK(out.motor[0] == doctest::Approx(0.5f));
    CHECK(out.motor[1] == doctest::Approx(0.5f));
    CHECK(out.servo[0] == doctest::Approx(0.0f));
    CHECK(out.servo[1] == doctest::Approx(0.0f));
}

TEST_CASE("mixer: axis routing matches the tailsitter allocation table")
{
    ActuatorFrame out;

    // torque.z (yaw about belly axis) -> differential MOTORS, +z = left faster.
    MixTailsitter({ Vec3{ 0, 0, 0.5f }, 0.5f }, P(), out);
    CHECK(out.motor[1] > out.motor[0]);
    CHECK(out.servo[0] == doctest::Approx(0.0f));

    // torque.y (pitch) -> SYMMETRIC elevon.
    MixTailsitter({ Vec3{ 0, 0.4f, 0 }, 0.5f }, P(), out);
    CHECK(out.servo[0] == doctest::Approx(out.servo[1]));
    CHECK(out.servo[0] > 0.0f);

    // torque.x (roll about nose) -> DIFFERENTIAL elevon.
    MixTailsitter({ Vec3{ 0.4f, 0, 0 }, 0.5f }, P(), out);
    CHECK(out.servo[0] == doctest::Approx(-out.servo[1]));
}

TEST_CASE("mixer saturation: differential survives collective clipping")
{
    ActuatorFrame out;

    // Full collective + yaw demand: pair shifts down so the difference lives.
    MixTailsitter({ Vec3{ 0, 0, 0.6f }, 1.0f }, P(), out);
    CHECK(out.motor[1] <= 1.0f);
    CHECK(out.motor[0] <= 1.0f);
    CHECK(out.motor[1] - out.motor[0] == doctest::Approx(2.0f * 0.6f * P().mix_yaw_gain).epsilon(0.05));

    // Zero collective + yaw demand: pair shifts up off the idle floor.
    MixTailsitter({ Vec3{ 0, 0, 0.6f }, 0.0f }, P(), out);
    CHECK(out.motor[0] >= P().motor_idle);
    CHECK(out.motor[1] > out.motor[0]);

    // Servo commands always clamp to [-1, 1].
    MixTailsitter({ Vec3{ 5.0f, 5.0f, 0 }, 0.5f }, P(), out);
    CHECK(out.servo[0] <= 1.0f);
    CHECK(out.servo[1] >= -1.0f);
}
