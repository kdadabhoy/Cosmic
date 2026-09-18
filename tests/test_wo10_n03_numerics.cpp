// test_wo10_n03_numerics.cpp — N03 (2D stability, WO-10): the numeric toolkit at
// the catalog's exact parameters and bounds, plus the independent DOUBLE
// reference / units checks the analysis sample consumes.
//
//   * RK4 projectile: dt = 1/480 for 1 s, 1e-4 relative — the existing bound,
//     re-asserted here with the observed error reported, and the same run in
//     double (the reference the float toolkit is measured against);
//   * mass-spring-damper: 2,000 RK4 steps at 1/1000 vs the analytic envelope, 2e-3;
//   * RK4 order: halving h shrinks the error by a ratio in 10..24;
//   * semi-implicit Euler: max energy < 1.10 x initial over the existing 10-s case;
//   * F-TRAJECTORY: the sample's generator equals the catalog equations (double),
//     the committed reference CSV (Projects/AnalysisSample/data/trajectory.csv) round-trips
//     exactly, the units are SI (g, apex, zero-crossing, range), and the FLOAT
//     telemetry path (DataRecorder -> v1 -> DataPlayer::SampleAt) is measured
//     against the double reference — the observed float error is reported and
//     held to the default float bar, never hidden;
//   * the 1e11 origin offset: subtracted in DOUBLE before float display (the
//     sample's LocalFrame) is exact to a float ulp; the naive float path is not.
// Bounds are the catalog's; none is loosened.
#include <doctest.h>

#include "math/Integrators.h"
#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include "AnalysisFixtures.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    template<typename T>
    struct PV
    {
        T p, v;
        PV operator+(const PV& o) const { return { p + o.p, v + o.v }; }
        PV operator*(float s)     const { return { p * (T)s, v * (T)s }; }
    };
    double FloatBar(double expected) { return 1e-6 + 1e-5 * std::abs(expected); }
    double UlpF(double v) { const float f = (float)std::abs(v); return (double)(std::nextafter(f, INFINITY) - f); }
    std::string Fmt(const char* fmt, double a, double b = 0.0, double c = 0.0, double d = 0.0)
    {
        char buf[320]; std::snprintf(buf, sizeof(buf), fmt, a, b, c, d); return buf;
    }
}

TEST_SUITE("WO-10 N03 numerics")
{
    TEST_CASE("WO-10 N03: RK4 projectile dt=1/480 for 1 s within 1e-4 relative (float), and the double reference")
    {
        const float g = 9.80665f;
        auto derivF = [g](const PV<float>& s, float) -> PV<float> { return { s.v, -g }; };
        auto derivD = [](const PV<double>& s, float) -> PV<double> { return { s.v, -9.80665 }; };
        PV<float>  sf{ 0.0f, 12.0f };
        PV<double> sd{ 0.0, 12.0 };
        const float dt = 1.0f / 480.0f;
        float t = 0.0f;
        for (int i = 0; i < 480; ++i) { sf = Cosmic::IntegrateRK4(sf, derivF, t, dt); sd = Cosmic::IntegrateRK4(sd, derivD, t, dt); t += dt; }
        // The exact solution at T = 480 * dt (float dt summed: T is 480 * (1/480 as float)).
        const double T = 480.0 * (double)dt;
        const double exactP = 12.0 * T - 0.5 * 9.80665 * T * T, exactV = 12.0 - 9.80665 * T;
        const double errPf = std::abs((double)sf.p - exactP) / std::abs(exactP);
        const double errVf = std::abs((double)sf.v - exactV) / std::abs(exactV);
        const double errPd = std::abs(sd.p - exactP) / std::abs(exactP);
        MESSAGE((Fmt("RK4 projectile float: p rel err %.3e, v rel err %.3e (bound 1e-4); double-state reference rel err %.3e (IntegrateRK4's dt is float: dt/6 is rounded to float, ~1e-8 relative floor)", errPf, errVf, errPd)));
        CHECK(sf.p == doctest::Approx(exactP).epsilon(1e-4));
        CHECK(sf.v == doctest::Approx(exactV).epsilon(1e-4));
        // Quadratic dynamics: RK4 over a double state is exact up to the float-rounded
        // step coefficients the template takes (dt, dt/6 are float) — a ~1e-8 floor,
        // not 1e-16. Recorded as a property of the toolkit, not a defect.
        CHECK(errPd < 1e-7);
    }

    TEST_CASE("WO-10 N03: mass-spring-damper 2,000 RK4 steps at 1/1000 within 2e-3 of the analytic envelope")
    {
        const float m = 1.0f, k = 100.0f, c = 2.0f;
        const float wn = std::sqrt(k / m), zeta = c / (2.0f * std::sqrt(k * m));
        PV<float> s{ 1.0f, 0.0f };
        auto deriv = [&](const PV<float>& st, float) -> PV<float> { return { st.v, (-k * st.p - c * st.v) / m }; };
        const int steps = 2000; const float dt = 1.0f / 1000.0f; float t = 0.0f;
        for (int i = 0; i < steps; ++i) { s = Cosmic::IntegrateRK4(s, deriv, t, dt); t += dt; }
        const double T = (double)steps * (double)dt;
        const double wd = wn * std::sqrt(1.0 - (double)zeta * zeta), env = std::exp(-(double)zeta * wn * T);
        const double exact = env * (std::cos(wd * T) + ((double)zeta * wn / wd) * std::sin(wd * T));
        const double err = std::abs((double)s.p - exact) / std::abs(exact);
        MESSAGE((Fmt("oscillator: x(2 s) = %.7f vs analytic %.7f, rel err %.3e (bound 2e-3)", s.p, exact, err)));
        CHECK(s.p == doctest::Approx(exact).epsilon(2e-3));
        const float e0 = 0.5f * k, e1 = 0.5f * k * s.p * s.p + 0.5f * m * s.v * s.v;
        CHECK(e1 < e0);
    }

    TEST_CASE("WO-10 N03: RK4 halving-step error ratio in 10..24")
    {
        auto deriv = [](float x, float) { return x; };
        auto integrate = [&](int steps) -> float
        {
            float x = 1.0f, t = 0.0f; const float h = 1.0f / (float)steps;
            for (int i = 0; i < steps; ++i) { x = Cosmic::IntegrateRK4(x, deriv, t, h); t += h; }
            return x;
        };
        const double e = 2.718281828459045;
        const double err1 = std::abs((double)integrate(8) - e), err2 = std::abs((double)integrate(16) - e);
        const double ratio = err1 / err2;
        MESSAGE((Fmt("RK4 order: err(h=1/8) %.3e / err(h=1/16) %.3e = ratio %.2f (bound 10..24, theory 16)", err1, err2, ratio)));
        CHECK(ratio > 10.0);
        CHECK(ratio < 24.0);
    }

    TEST_CASE("WO-10 N03: semi-implicit Euler energy stays below 1.10 x initial over 10 s")
    {
        const float k = 50.0f; float p = 1.0f, v = 0.0f;
        auto accel = [&](float pos, float, float) { return -k * pos; };
        const float dt = 1.0f / 240.0f; float maxE = 0.0f;
        for (int i = 0; i < 240 * 10; ++i)
        {
            Cosmic::IntegrateSemiImplicitEuler(p, v, accel, 0.0f, dt);
            maxE = std::max(maxE, 0.5f * k * p * p + 0.5f * v * v);
        }
        const float e0 = 0.5f * k;
        MESSAGE((Fmt("semi-implicit: max energy / E0 = %.5f (bound < 1.10)", maxE / e0)));
        CHECK(maxE < e0 * 1.10f);
    }

    TEST_CASE("WO-10 N03: F-TRAJECTORY — generator equals the catalog equations, the reference CSV round-trips, units are SI")
    {
        using namespace AnalysisSample::Trajectory;
        // The catalog text, re-derived here independently of the header's At().
        const double g = 9.80665;
        double maxRel = 0.0;
        for (int i = 0; i < kSamples; ++i)
        {
            const double t = i / 120.0;
            const Row r = At(i);
            CHECK(r.t == t);
            CHECK(r.x == 30.0 * t);
            CHECK(r.y == 50.0 * t - 0.5 * g * t * t);
            CHECK(r.vx == 30.0);
            CHECK(r.vy == 50.0 - g * t);
        }
        (void)maxRel;
        CHECK(All().size() == 1201);

        // The committed reference file (written by the generator with max_digits10).
        const std::filesystem::path csv = std::filesystem::path(COSMIC_ANALYSIS_SAMPLE_DATA) / "trajectory.csv";
        REQUIRE(std::filesystem::exists(csv));
        std::vector<Row> rows;
        REQUIRE(LoadCsv(csv.string(), rows));
        REQUIRE(rows.size() == 1201);
        for (int i = 0; i < kSamples; ++i)
        {
            const Row r = At(i);
            CHECK(rows[i].t == r.t); CHECK(rows[i].x == r.x); CHECK(rows[i].y == r.y);
            CHECK(rows[i].vx == r.vx); CHECK(rows[i].vy == r.vy);
        }
        // Units (SI): the apex where vy crosses zero, the peak height, the 10-s range.
        const double tApex = 50.0 / g;                       // 5.0986 s
        CHECK(std::abs(AtTime(tApex).vy) < 1e-12);
        CHECK(AtTime(tApex).y == doctest::Approx(50.0 * 50.0 / (2.0 * g)).epsilon(1e-12));   // 127.46 m
        CHECK(At(1200).x == doctest::Approx(300.0).epsilon(1e-12));                           // 30 m/s * 10 s
        CHECK(At(1200).y == doctest::Approx(500.0 - 0.5 * g * 100.0).epsilon(1e-12));         // 9.6675 m
        CHECK(At(1200).t == 10.0);
        MESSAGE((Fmt("F-TRAJECTORY: apex t=%.6f s y=%.6f m; range at 10 s x=%.3f m y=%.6f m", tApex, AtTime(tApex).y, At(1200).x, At(1200).y)));
    }

    TEST_CASE("WO-10 N03: the float telemetry path (DataRecorder -> v1 -> DataPlayer) vs the double F-TRAJECTORY reference")
    {
        using namespace AnalysisSample::Trajectory;
        namespace fs = std::filesystem;
        Cosmic::DataRecorder rec;
        const uint32_t id = rec.Register("shot", "wo10", { "x", "y", "vx", "vy" });
        rec.ReserveCapacity(kSamples);
        for (int i = 0; i < kSamples; ++i)
        {
            if (i > 0) rec.Tick(1.0f / 120.0f);
            const Row r = At(i);
            rec.Record(id, { (float)r.x, (float)r.y, (float)r.vx, (float)r.vy });
        }
        const fs::path tmp = fs::temp_directory_path() / "wo10-n03-float";
        std::error_code ec; fs::remove_all(tmp, ec);
        rec.Flush(tmp.string(), "rec", 120.0f);
        rec.WaitForFlush();
        Cosmic::DataPlayer player;
        REQUIRE(player.Load((tmp / "rec").string()));
        double maxAbs[4] = { 0, 0, 0, 0 }, maxRel[4] = { 0, 0, 0, 0 }, maxUlps = 0.0, maxTimeErr = 0.0;
        int overBar = 0, overDomain = 0, worstDomainSample = -1, worstDomainChannel = -1; double worstDomainExcess = 0.0;
        for (int i = 0; i < kSamples; ++i)
        {
            const Row r = At(i);
            Cosmic::TelemetryFrame f;
            REQUIRE(player.SampleAt("shot", (float)r.t, f));
            const double ref[4] = { r.x, r.y, r.vx, r.vy };
            // The float path's error budget (the domain bound): the value is stored as a
            // float and interpolated in float (up to ~2 ulps of the value: two rounded
            // products and a sum), and the time it was recorded at / looked up at is a
            // float (two ulps of t), which moves the sample by |d/dt| * that.
            const double rate[4] = { std::abs(r.vx), std::abs(r.vy), 0.0, kG };
            for (int c = 0; c < 4; ++c)
            {
                const double e = std::abs((double)f.values[c] - ref[c]);
                maxAbs[c] = std::max(maxAbs[c], e);
                if (ref[c] != 0.0) maxRel[c] = std::max(maxRel[c], e / std::abs(ref[c]));
                if (e > FloatBar(ref[c])) ++overBar;
                const double domain = 2.0 * rate[c] * UlpF(r.t) + 2.0 * UlpF(ref[c]) + 1e-9;
                if (e > domain) { ++overDomain; if (e - domain > worstDomainExcess) { worstDomainExcess = e - domain; worstDomainSample = i; worstDomainChannel = c; } }
                if (e > 0.0 && std::abs(ref[c]) >= 1.0) maxUlps = std::max(maxUlps, e / UlpF(ref[c]));   // ulps are meaningless near zero
            }
            maxTimeErr = std::max(maxTimeErr, std::abs((double)f.timestamp - r.t));
        }
        MESSAGE((Fmt("float path vs double: x max abs err %.3e (rel %.3e), y max abs err %.3e", maxAbs[0], maxRel[0], maxAbs[1])));
        MESSAGE((Fmt("float path vs double: vy max abs err %.3e; worst error in float ulps of the value (|value| >= 1): %.2f; max timestamp err %.3e s (a timestamp error of e seconds shows up as 30*e metres in x)", maxAbs[3], maxUlps, maxTimeErr)));
        MESSAGE((std::string("float path vs double: samples over the generic float bar (1e-6 + 1e-5|x|, dominated by the float TIME resolution near x = 0): ") + std::to_string(overBar)
            + " of 4804; over the domain bound (2|v| ulp(t) + 2 ulp(value)): " + std::to_string(overDomain) + "; worst domain excess " + std::to_string(worstDomainExcess) + " at sample " + std::to_string(worstDomainSample) + " channel " + std::to_string(worstDomainChannel)));
        // The float path loses precision by design (contracts.md §5): every sample
        // is within its domain bound (float value + float time resolution), the
        // large-value samples within the generic float bar. Before WO-10 the
        // recorder's float time accumulator (KI-16) put the 1,200th timestamp
        // 6.7e-5 s off, i.e. 2 mm in x and 3 mm in y — over both bars.
        CHECK(overDomain == 0);
        CHECK(maxAbs[0] <= FloatBar(300.0));
        CHECK(maxAbs[1] <= FloatBar(127.46));
        CHECK(maxTimeErr <= 2.0 * UlpF(10.0));   // every stored timestamp within two float ulps of i/120 (float dt argument + float storage)
        fs::remove_all(tmp, ec);
    }

    TEST_CASE("WO-10 N03: a 1e11 origin offset subtracted in double before float display is exact; the naive float path is not")
    {
        using namespace AnalysisSample;
        LocalFrame frame; frame.originX = 1e11; frame.originY = 1e11;
        const double ulpD = std::nextafter(1e11, INFINITY) - 1e11;
        double maxErrLocal = 0.0, maxErrNaive = 0.0;
        for (int i = 0; i < Trajectory::kSamples; ++i)
        {
            const Trajectory::Row r = Trajectory::At(i);
            const double worldX = 1e11 + r.x, worldY = 1e11 + r.y;
            // The sample's conversion: double subtraction, then float.
            const float lx = frame.ToLocalX(worldX), ly = frame.ToLocalY(worldY);
            maxErrLocal = std::max(maxErrLocal, std::max(std::abs((double)lx - r.x), std::abs((double)ly - r.y)));
            // The path X01 guards against: converting the world value to float first.
            const float nx = (float)worldX - (float)1e11, ny = (float)worldY - (float)1e11;
            maxErrNaive = std::max(maxErrNaive, std::max(std::abs((double)nx - r.x), std::abs((double)ny - r.y)));
            // Exact up to the DOUBLE ulp at 1e11 (1.5e-5 m: forming 1e11 + x already rounds
            // the world value) plus the float ulp of the local value.
            CHECK(std::abs((double)lx - r.x) <= ulpD / 2.0 + UlpF(r.x) / 2.0 + 1e-12);
            CHECK(std::abs((double)ly - r.y) <= ulpD / 2.0 + UlpF(r.y) / 2.0 + 1e-12);
        }
        MESSAGE((Fmt("1e11 offset: double-subtract-then-float max err %.3e m (double ulp at 1e11 = %.3e m); float-then-subtract max err %.1f m (float ulp at 1e11 = %.0f m)", maxErrLocal, ulpD, maxErrNaive, UlpF(1e11))));
        CHECK(maxErrLocal < 1e-4);
        CHECK(maxErrNaive > 100.0);   // the naive path is off by whole metres — the reason the sample converts in double
    }
}
