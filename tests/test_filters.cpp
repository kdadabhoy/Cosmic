// test_filters.cpp — math/Filters.h (E12): LPF step response at tau, biquad
// -3 dB at cutoff, rate limiter exact clamping, moving average, washout.

#include <string>
#include <vector>
#include <cmath>

#include "doctest.h"
#include "math/Filters.h"

TEST_SUITE("Filters (E12)")
{
	TEST_CASE("LPF step response hits 63.2% at t = tau (within 1%)")
	{
		const float tau = 0.25f;
		Cosmic::LowPassFilter lpf(tau);
		lpf.Reset(0.0f);

		const float dt = 1e-4f;
		float t = 0.0f, y = 0.0f;
		while (t < tau - dt * 0.5f)
		{
			y = lpf.Update(1.0f, dt);
			t += dt;
		}

		CHECK(y == doctest::Approx(1.0f - std::exp(-1.0f)).epsilon(0.01)); // 0.6321
	}

	TEST_CASE("LPF first sample primes the state (no transient from zero)")
	{
		Cosmic::LowPassFilter lpf(1.0f);
		CHECK(lpf.Update(5.0f, 0.01f) == doctest::Approx(5.0f));
	}

	TEST_CASE("LPF cutoff-Hz constructor maps to tau = 1/(2*pi*fc)")
	{
		auto lpf = Cosmic::LowPassFilter::FromCutoffHz(10.0f);
		CHECK(lpf.GetTau() == doctest::Approx(1.0f / (Cosmic::kFilterTwoPi * 10.0f)));
	}

	TEST_CASE("Biquad LPF magnitude at cutoff is ~-3 dB")
	{
		const float fs = 1000.0f, fc = 50.0f;
		Cosmic::Biquad bq;
		bq.SetLowPass(fc, fs);
		bq.Reset(0.0f);

		// drive with a sine at fc, measure steady-state amplitude
		const int settle = 2000, measure = 2000;
		float peak = 0.0f;
		for (int i = 0; i < settle + measure; ++i)
		{
			const float x = std::sin(Cosmic::kFilterTwoPi * fc * static_cast<float>(i) / fs);
			const float y = bq.Update(x);
			if (i >= settle)
				peak = std::max(peak, std::abs(y));
		}

		const float db = 20.0f * std::log10(peak);
		CHECK(db == doctest::Approx(-3.01f).epsilon(0.05));
	}

	TEST_CASE("Biquad notch kills the center frequency, passes DC")
	{
		const float fs = 1000.0f, f0 = 60.0f;
		Cosmic::Biquad bq;
		bq.SetNotch(f0, fs);
		bq.Reset(0.0f);

		float peak = 0.0f;
		for (int i = 0; i < 4000; ++i)
		{
			const float x = std::sin(Cosmic::kFilterTwoPi * f0 * static_cast<float>(i) / fs);
			const float y = bq.Update(x);
			if (i >= 2000)
				peak = std::max(peak, std::abs(y));
		}
		CHECK(peak < 0.05f);   // > 26 dB rejection at the notch

		// DC passes: Reset to steady state at 1.0 and keep feeding 1.0
		bq.Reset(1.0f);
		float y = 0.0f;
		for (int i = 0; i < 100; ++i)
			y = bq.Update(1.0f);
		CHECK(y == doctest::Approx(1.0f).epsilon(0.01));
	}

	TEST_CASE("RateLimiter clamps the slew exactly")
	{
		Cosmic::RateLimiter rl(10.0f);   // 10 units/s
		rl.Reset(0.0f);

		// ask for a step of 100 with dt = 0.1 -> may move exactly 1.0
		CHECK(rl.Update(100.0f, 0.1f) == doctest::Approx(1.0f));
		CHECK(rl.Update(100.0f, 0.1f) == doctest::Approx(2.0f));

		// downward too
		CHECK(rl.Update(-100.0f, 0.25f) == doctest::Approx(-0.5f));

		// small target inside the allowed step is reached exactly
		rl.Reset(0.0f);
		CHECK(rl.Update(0.5f, 0.1f) == doctest::Approx(0.5f));
	}

	TEST_CASE("MovingAverage<4> averages the window, handles partial fill")
	{
		// Fresh state = empty window: the average grows over the first N samples.
		Cosmic::MovingAverage<4> ma;

		CHECK(ma.Update(4.0f) == doctest::Approx(4.0f));          // 1 sample
		CHECK(ma.Update(8.0f) == doctest::Approx(6.0f));          // 2 samples
		ma.Update(0.0f);
		CHECK(ma.Update(0.0f) == doctest::Approx(3.0f));          // 4 samples: (4+8+0+0)/4

		// window slides: 4 drops out
		CHECK(ma.Update(12.0f) == doctest::Approx((8.0f + 0.0f + 0.0f + 12.0f) / 4.0f));
	}

	TEST_CASE("MovingAverage<4> Reset(value) seeds the whole window")
	{
		// Reset(value) matches the other filters' contract: state re-seeded so
		// GetValue() == value, and the next samples REPLACE window slots (the
		// old count=0 path added on top of the phantom pre-filled sum).
		Cosmic::MovingAverage<4> ma;
		ma.Update(100.0f);                                        // dirty the state
		ma.Reset(2.0f);

		CHECK(ma.GetValue() == doctest::Approx(2.0f));
		CHECK(ma.Update(6.0f) == doctest::Approx((2.0f + 2.0f + 2.0f + 6.0f) / 4.0f));
		CHECK(ma.Update(6.0f) == doctest::Approx((2.0f + 2.0f + 6.0f + 6.0f) / 4.0f));
	}

	TEST_CASE("Washout passes transients and decays steady input to zero")
	{
		Cosmic::Washout w(0.2f);
		w.Reset(0.0f);

		const float dt = 0.001f;

		// step input: initial response jumps toward the step, then washes out
		float y = w.Update(1.0f, dt);
		CHECK(y > 0.9f);   // immediate transient passes

		for (int i = 0; i < 5000; ++i)   // 5 s >> tau
			y = w.Update(1.0f, dt);
		CHECK(std::abs(y) < 0.01f);      // steady state washed out
	}
}
