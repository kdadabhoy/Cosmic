// test_integrators.cpp — math/Integrators.h (E11): RK4 vs analytic solutions,
// semi-implicit Euler energy behavior, O(h^4) order check, FixedSubstepper.

#include <string>
#include <vector>
#include <cmath>

#include "doctest.h"
#include "math/Integrators.h"

#include <glm/glm.hpp>

namespace
{
	// 1D projectile state for RK4 (pos, vel packed in a vec2: x=pos, y=vel)
	struct PV
	{
		float p, v;
		PV operator+(const PV& o) const { return { p + o.p, v + o.v }; }
		PV operator*(float s)     const { return { p * s, v * s }; }
	};
}

TEST_SUITE("Integrators (E11)")
{
	TEST_CASE("RK4 projectile matches the exact solution")
	{
		// dv/dt = -g, dp/dt = v  ->  p(t) = p0 + v0 t - g t^2 / 2
		const float g = 9.80665f;
		PV s{ 0.0f, 12.0f };
		auto deriv = [g](const PV& st, float) -> PV { return { st.v, -g }; };

		const float dt = 1.0f / 480.0f;
		float t = 0.0f;
		for (int i = 0; i < 480; ++i)  // 1 second
		{
			s = Cosmic::IntegrateRK4(s, deriv, t, dt);
			t += dt;
		}

		const float exactP = 12.0f * 1.0f - 0.5f * g * 1.0f;
		const float exactV = 12.0f - g * 1.0f;
		// The dynamics are quadratic — RK4 is exact up to float rounding.
		CHECK(s.p == doctest::Approx(exactP).epsilon(1e-4));
		CHECK(s.v == doctest::Approx(exactV).epsilon(1e-4));
	}

	TEST_CASE("RK4 mass-spring-damper decays like the analytic envelope")
	{
		// m x'' + c x' + k x = 0, underdamped: x(t) = e^(-zeta wn t) * ...
		const float m = 1.0f, k = 100.0f, c = 2.0f;
		const float wn = std::sqrt(k / m);          // 10 rad/s
		const float zeta = c / (2.0f * std::sqrt(k * m)); // 0.1

		PV s{ 1.0f, 0.0f };
		auto deriv = [&](const PV& st, float) -> PV { return { st.v, (-k * st.p - c * st.v) / m }; };

		// The system is autonomous, so only the STEP COUNT sets the final time
		// — take it as exact (steps * dt) and evaluate the analytic solution
		// there, avoiding a float 2.0/dt truncation off-by-one.
		const int   steps = 2000;
		const float dt = 1.0f / 1000.0f;
		float t = 0.0f;
		for (int i = 0; i < steps; ++i)
		{
			s = Cosmic::IntegrateRK4(s, deriv, t, dt);
			t += dt;
		}

		const float T = static_cast<float>(steps) * dt;
		const float wd = wn * std::sqrt(1.0f - zeta * zeta);
		const float env = std::exp(-zeta * wn * T);
		const float exact = env * (std::cos(wd * T) + (zeta * wn / wd) * std::sin(wd * T));
		CHECK(s.p == doctest::Approx(exact).epsilon(2e-3));

		// energy must have decayed (damping), not grown
		const float e0 = 0.5f * k * 1.0f;
		const float e1 = 0.5f * k * s.p * s.p + 0.5f * m * s.v * s.v;
		CHECK(e1 < e0);
	}

	TEST_CASE("RK4 error scales ~O(h^4): halving h shrinks error ~16x")
	{
		// dx/dt = x  ->  x(1) = e. Nonlinear enough in h to expose the order.
		auto deriv = [](float x, float) { return x; };

		auto integrate = [&](int steps) -> float
		{
			float x = 1.0f, t = 0.0f;
			const float h = 1.0f / static_cast<float>(steps);
			for (int i = 0; i < steps; ++i) { x = Cosmic::IntegrateRK4(x, deriv, t, h); t += h; }
			return x;
		};

		// use double for the error math; float precision floors ~1e-7
		const double e = 2.718281828459045;
		const double err1 = std::abs(static_cast<double>(integrate(8))  - e);
		const double err2 = std::abs(static_cast<double>(integrate(16)) - e);

		const double ratio = err1 / err2;
		// theoretical 16; allow slack for float arithmetic
		CHECK(ratio > 10.0);
		CHECK(ratio < 24.0);
	}

	TEST_CASE("semi-implicit Euler stays energy-bounded on an undamped oscillator")
	{
		const float k = 50.0f;
		float p = 1.0f, v = 0.0f;
		auto accel = [&](float pos, float, float) { return -k * pos; };

		const float dt = 1.0f / 240.0f;
		float maxE = 0.0f;
		for (int i = 0; i < 240 * 10; ++i)  // 10 seconds
		{
			Cosmic::IntegrateSemiImplicitEuler(p, v, accel, 0.0f, dt);
			const float e = 0.5f * k * p * p + 0.5f * v * v;
			maxE = std::max(maxE, e);
		}

		// symplectic: energy oscillates near E0 but does not diverge
		const float e0 = 0.5f * k;
		CHECK(maxE < e0 * 1.10f);
	}

	TEST_CASE("FixedSubstepper runs N equal substeps and carries residual")
	{
		Cosmic::FixedSubstepper sub;

		int calls = 0;
		float total = 0.0f;
		sub.Run(1.0f / 60.0f, 8, [&](float h)
		{
			++calls;
			total += h;
			CHECK(h == doctest::Approx(1.0f / 480.0f));
		});
		CHECK(calls == 8);
		CHECK(total == doctest::Approx(1.0f / 60.0f).epsilon(1e-4));

		// residual carry: two runs of 0.75 * h_frame at 1 substep -> 1 step
		// after the first run, 1 more once the residual tops up... verify no
		// time is lost across calls.
		Cosmic::FixedSubstepper sub2;
		float integrated = 0.0f;
		for (int i = 0; i < 100; ++i)
			sub2.Run(0.0100f, 4, [&](float h) { integrated += h; });
		CHECK(integrated + sub2.GetResidual() == doctest::Approx(1.0f).epsilon(1e-3));

		sub2.Reset();
		CHECK(sub2.GetResidual() == 0.0f);
	}
}
