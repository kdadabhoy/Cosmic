// test_failsafe.cpp — energy accounting + failsafe thresholds (doc 04 §5:
// "energy-accounting failsafe thresholds ... gets its own tests").

#include "doctest.h"
#include "viperfc/Failsafe.h"

using namespace viperfc;

static FailsafeSupervisor::Inputs FlyingHover()
{
    FailsafeSupervisor::Inputs in;
    in.mode = FlightMode::Hover;
    in.blend = 0.0f;
    in.armed = true;
    in.airborne = true;
    in.vbat_V = 15.6f;    // 3.9 V/cell — healthy
    in.ibat_A = 15.0f;
    in.altAgl = 20.0f;
    in.distHome = 10.0f;
    in.gpsValid = true;
    in.sinceHeartbeat_s = 0.1f;
    return in;
}

TEST_CASE("hover budget: warn at 80%, forced exit at 100% — and it latches")
{
    FcParams p;
    p.hover_budget_s = 100.0f;   // shrink for the test
    FailsafeSupervisor fs;
    fs.Reset();

    const float dt = 0.1f;
    auto in = FlyingHover();

    bool warned = false;
    FailsafeSupervisor::Demand demand = FailsafeSupervisor::Demand::None;
    float tWarn = -1.0f, tHit = -1.0f;

    for (int i = 0; i < 2000; ++i)
    {
        demand = fs.Update(in, p, dt);
        if (fs.PendingAlert() == FcAlert::HoverBudgetWarn) { warned = true; tWarn = fs.HoverElapsed(); }
        if (demand == FailsafeSupervisor::Demand::ExitHover && tHit < 0.0f) tHit = fs.HoverElapsed();
    }

    CHECK(warned);
    CHECK(tWarn == doctest::Approx(80.0f).epsilon(0.02));
    CHECK(tHit  == doctest::Approx(100.0f).epsilon(0.02));

    // Cruise flight (blend=1) STOPS the clock but the demand stays latched.
    in.blend = 1.0f;
    const float elapsed = fs.HoverElapsed();
    demand = fs.Update(in, p, dt);
    CHECK(fs.HoverElapsed() == doctest::Approx(elapsed));
    CHECK(demand == FailsafeSupervisor::Demand::ExitHover);
}

TEST_CASE("energy accounting integrates V*I and trips the reserve RTL")
{
    FcParams p;
    p.batt_capacity_wh = 10.0f;    // tiny pack: usable 8.5 Wh, reserve 25% = 2.125
    FailsafeSupervisor fs;
    fs.Reset();

    auto in = FlyingHover();
    in.blend = 1.0f;               // don't trip the hover budget first
    in.vbat_V = 16.0f;
    in.ibat_A = 14.4f;             // 230.4 W -> 0.064 Wh/s

    // After 100 s: 6.4 Wh used, remaining usable 2.1 < 2.125 -> reserve RTL.
    FailsafeSupervisor::Demand demand = FailsafeSupervisor::Demand::None;
    for (int i = 0; i < 1000; ++i)
        demand = fs.Update(in, p, 0.1f);

    CHECK(fs.EnergyUsedWh() == doctest::Approx(6.4f).epsilon(0.01));
    CHECK(fs.Active(FcAlert::BatteryReserve));
    CHECK(demand == FailsafeSupervisor::Demand::Rtl);
}

TEST_CASE("battery voltage thresholds: low warns, critical forces LAND — after the qualify time")
{
    FcParams p;
    FailsafeSupervisor fs;
    fs.Reset();

    const float dt = 0.01f;
    const int qualifySteps = static_cast<int>(p.batt_v_qualify_s / dt) + 2;

    auto in = FlyingHover();
    in.vbat_V = 4.0f * p.batt_low_v_cell - 0.01f;    // just under warn
    FailsafeSupervisor::Demand demand = FailsafeSupervisor::Demand::None;
    for (int i = 0; i < qualifySteps; ++i)
        demand = fs.Update(in, p, dt);
    CHECK(fs.Active(FcAlert::BatteryLow));
    CHECK(demand == FailsafeSupervisor::Demand::None);   // warn only

    in.vbat_V = 4.0f * p.batt_crit_v_cell - 0.01f;
    for (int i = 0; i < qualifySteps; ++i)
        demand = fs.Update(in, p, dt);
    CHECK(fs.Active(FcAlert::BatteryCritical));
    CHECK(demand == FailsafeSupervisor::Demand::Land);

    // Land outranks everything and never de-escalates.
    in.sinceHeartbeat_s = 10.0f;   // link also lost
    demand = fs.Update(in, p, dt);
    CHECK(demand == FailsafeSupervisor::Demand::Land);
}

TEST_CASE("battery voltage: transient IR sag under load does NOT latch a land")
{
    // A gust fight or punch-out sags the pack below the critical threshold for
    // a second; the pack itself is healthy. The G1 sim gate caught the old
    // instantaneous check landing the aircraft mid-gust.
    FcParams p;
    FailsafeSupervisor fs;
    fs.Reset();

    const float dt = 1.0f / 240.0f;
    auto in = FlyingHover();

    for (int cycle = 0; cycle < 6; ++cycle)
    {
        // 1 s deep sag (below critical)...
        in.vbat_V = 4.0f * p.batt_crit_v_cell - 0.2f;
        for (int i = 0; i < 240; ++i)
            fs.Update(in, p, dt);
        // ...then 2 s recovered.
        in.vbat_V = 15.6f;
        for (int i = 0; i < 480; ++i)
            fs.Update(in, p, dt);
    }

    CHECK_FALSE(fs.Active(FcAlert::BatteryLow));
    CHECK_FALSE(fs.Active(FcAlert::BatteryCritical));
    CHECK(fs.Update(in, p, dt) == FailsafeSupervisor::Demand::None);
}

TEST_CASE("link loss triggers RTL and releases on link recovery")
{
    FcParams p;
    FailsafeSupervisor fs;
    fs.Reset();

    auto in = FlyingHover();
    in.sinceHeartbeat_s = p.link_timeout_s + 0.1f;
    auto demand = fs.Update(in, p, 0.01f);
    CHECK(demand == FailsafeSupervisor::Demand::Rtl);
    CHECK(fs.PendingAlert() == FcAlert::LinkLost);

    in.sinceHeartbeat_s = 0.0f;    // heartbeat back
    demand = fs.Update(in, p, 0.01f);
    CHECK(demand == FailsafeSupervisor::Demand::None);
}

TEST_CASE("geofence: 400 ft AGL and radius breaches demand RTL")
{
    FcParams p;
    FailsafeSupervisor fs;
    fs.Reset();

    auto in = FlyingHover();
    in.altAgl = p.geofence_agl_m + 1.0f;
    CHECK(fs.Update(in, p, 0.01f) == FailsafeSupervisor::Demand::Rtl);

    fs.Reset();
    in = FlyingHover();
    in.distHome = p.geofence_radius_m + 5.0f;
    CHECK(fs.Update(in, p, 0.01f) == FailsafeSupervisor::Demand::Rtl);
}
